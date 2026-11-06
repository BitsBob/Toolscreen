// Win32 compatibility layer (macOS): errors, time, processes, modules, environment, text
// conversion, embedded resources, symbolisation and other process-level services.
#include "platform/mac/mac_internal.h"
#include "platform/mac/mac_platform_bridge.h"

#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <libproc.h>
#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

extern char** environ;

// ---------------------------------------------------------------------------------------------
// UTF-8 <-> UTF-32 (wchar_t is 32-bit on macOS)
// ---------------------------------------------------------------------------------------------
namespace {

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decodes UTF-8; invalid sequences become U+FFFD. `invalid` reports whether any were seen.
std::wstring DecodeUtf8(const char* s, size_t len, bool* invalid = nullptr) {
    std::wstring out;
    out.reserve(len);
    size_t i = 0;
    bool bad = false;
    while (i < len) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        size_t extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            extra = 3;
        } else {
            out.push_back(0xFFFD);
            bad = true;
            ++i;
            continue;
        }
        bool ok = true;
        for (size_t k = 1; k <= extra; ++k) {
            if (i + k >= len || (static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        if (!ok) {
            out.push_back(0xFFFD);
            bad = true;
            ++i;
            continue;
        }
        out.push_back(static_cast<wchar_t>(cp));
        i += extra + 1;
    }
    if (invalid) *invalid = bad;
    return out;
}

std::string EncodeUtf8(const wchar_t* s, size_t len) {
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        uint32_t cp = static_cast<uint32_t>(s[i]);
        // Tolerate UTF-16 surrogate pairs that came through 16-bit WPARAMs.
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len) {
            const uint32_t low = static_cast<uint32_t>(s[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        AppendUtf8(out, cp);
    }
    return out;
}

thread_local DWORD t_lastError = 0;

DWORD ErrnoToWin32(int e) {
    switch (e) {
    case 0: return ERROR_SUCCESS;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EROFS: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_ALREADY_EXISTS;
    case ENOTEMPTY: return ERROR_DIR_NOT_EMPTY;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    case EBADF: return ERROR_INVALID_HANDLE;
    case EINVAL: return ERROR_INVALID_PARAMETER;
    case EBUSY: return ERROR_SHARING_VIOLATION;
    case ETIMEDOUT: return ERROR_TIMEOUT;
    default: return 0x20000000u | static_cast<DWORD>(e); // customer bit: raw errno
    }
}

uint64_t MonotonicNanoseconds() {
    static mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t info{};
        mach_timebase_info(&info);
        return info;
    }();
    const uint64_t t = mach_absolute_time();
    return static_cast<uint64_t>((static_cast<__uint128_t>(t) * timebase.numer) / timebase.denom);
}

constexpr uint64_t kUnixEpochAsFileTime = 116444736000000000ULL;

ULONGLONG ToULL(const FILETIME& ft) { return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; }
FILETIME FromULL(ULONGLONG v) {
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(v & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(v >> 32);
    return ft;
}

void TmToSystemTime(const struct tm& tm, int millis, LPSYSTEMTIME out) {
    out->wYear = static_cast<WORD>(tm.tm_year + 1900);
    out->wMonth = static_cast<WORD>(tm.tm_mon + 1);
    out->wDayOfWeek = static_cast<WORD>(tm.tm_wday);
    out->wDay = static_cast<WORD>(tm.tm_mday);
    out->wHour = static_cast<WORD>(tm.tm_hour);
    out->wMinute = static_cast<WORD>(tm.tm_min);
    out->wSecond = static_cast<WORD>(tm.tm_sec);
    out->wMilliseconds = static_cast<WORD>(millis);
}

} // namespace

namespace macplat {
std::wstring Utf8ToWide(const char* s, size_t len) {
    if (!s) return {};
    if (len == static_cast<size_t>(-1)) len = strlen(s);
    return DecodeUtf8(s, len);
}
std::string WideToUtf8(const wchar_t* s, size_t len) {
    if (!s) return {};
    if (len == static_cast<size_t>(-1)) len = wcslen(s);
    return EncodeUtf8(s, len);
}
std::string NativePathFromNarrow(const char* path) {
    std::string out = path ? path : "";
    std::replace(out.begin(), out.end(), '\\', '/');
    // "~/..." is expanded for convenience (configs written by hand).
    if (!out.empty() && out[0] == '~') {
        const char* home = getenv("HOME");
        if (home && (out.size() == 1 || out[1] == '/')) out = std::string(home) + out.substr(1);
    }
    return out;
}
std::string NativePathFromWide(const wchar_t* path) {
    const std::string utf8 = WideToUtf8(path);
    return NativePathFromNarrow(utf8.c_str());
}
} // namespace macplat

// ---------------------------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------------------------
DWORD GetLastError() { return t_lastError; }
void SetLastError(DWORD error) { t_lastError = error; }
namespace macwin {
void SetLastErrorFromErrno(int e) { t_lastError = ErrnoToWin32(e); }
} // namespace macwin

DWORD FormatMessageA(DWORD flags, LPCVOID, DWORD messageId, DWORD, LPSTR buffer, DWORD size, va_list*) {
    std::string text;
    if ((messageId & 0x20000000u) != 0) {
        text = strerror(static_cast<int>(messageId & 0xFFFF));
    } else {
        switch (messageId) {
        case ERROR_SUCCESS: text = "The operation completed successfully."; break;
        case ERROR_FILE_NOT_FOUND: text = "The system cannot find the file specified."; break;
        case ERROR_PATH_NOT_FOUND: text = "The system cannot find the path specified."; break;
        case ERROR_ACCESS_DENIED: text = "Access is denied."; break;
        case ERROR_ALREADY_EXISTS: text = "Cannot create a file when that file already exists."; break;
        case ERROR_NOT_SUPPORTED: text = "The request is not supported."; break;
        default: text = "Error " + std::to_string(messageId) + "."; break;
        }
    }
    text += "\r\n";
    if (flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
        char* mem = static_cast<char*>(malloc(text.size() + 1));
        if (!mem) return 0;
        memcpy(mem, text.c_str(), text.size() + 1);
        *reinterpret_cast<char**>(buffer) = mem;
        return static_cast<DWORD>(text.size());
    }
    if (!buffer || size == 0) return 0;
    const size_t n = std::min<size_t>(text.size(), size - 1);
    memcpy(buffer, text.data(), n);
    buffer[n] = 0;
    return static_cast<DWORD>(n);
}

HLOCAL LocalFree(HLOCAL mem) {
    free(mem);
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------------------------
void Sleep(DWORD milliseconds) {
    if (milliseconds == 0) {
        sched_yield();
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

DWORD SleepEx(DWORD milliseconds, BOOL) {
    Sleep(milliseconds);
    return 0;
}

ULONGLONG GetTickCount64() { return MonotonicNanoseconds() / 1000000ULL; }
DWORD GetTickCount() { return static_cast<DWORD>(GetTickCount64()); }

BOOL QueryPerformanceCounter(LARGE_INTEGER* counter) {
    if (!counter) return FALSE;
    counter->QuadPart = static_cast<LONGLONG>(MonotonicNanoseconds());
    return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency) {
    if (!frequency) return FALSE;
    frequency->QuadPart = 1000000000LL;
    return TRUE;
}

void GetSystemTimeAsFileTime(LPFILETIME out) {
    if (!out) return;
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    const ULONGLONG v = kUnixEpochAsFileTime + static_cast<ULONGLONG>(ts.tv_sec) * 10000000ULL + static_cast<ULONGLONG>(ts.tv_nsec) / 100ULL;
    *out = FromULL(v);
}

void GetSystemTime(LPSYSTEMTIME out) {
    if (!out) return;
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm {};
    gmtime_r(&ts.tv_sec, &tm);
    TmToSystemTime(tm, static_cast<int>(ts.tv_nsec / 1000000), out);
}

void GetLocalTime(LPSYSTEMTIME out) {
    if (!out) return;
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm {};
    localtime_r(&ts.tv_sec, &tm);
    TmToSystemTime(tm, static_cast<int>(ts.tv_nsec / 1000000), out);
}

BOOL FileTimeToLocalFileTime(const FILETIME* in, LPFILETIME out) {
    if (!in || !out) return FALSE;
    const ULONGLONG v = ToULL(*in);
    const time_t secs = static_cast<time_t>((v - kUnixEpochAsFileTime) / 10000000ULL);
    struct tm tm {};
    localtime_r(&secs, &tm);
    const LONGLONG offset = static_cast<LONGLONG>(tm.tm_gmtoff) * 10000000LL;
    *out = FromULL(static_cast<ULONGLONG>(static_cast<LONGLONG>(v) + offset));
    return TRUE;
}

BOOL FileTimeToSystemTime(const FILETIME* in, LPSYSTEMTIME out) {
    if (!in || !out) return FALSE;
    const ULONGLONG v = ToULL(*in);
    if (v < kUnixEpochAsFileTime) return FALSE;
    const ULONGLONG since = v - kUnixEpochAsFileTime;
    const time_t secs = static_cast<time_t>(since / 10000000ULL);
    struct tm tm {};
    gmtime_r(&secs, &tm);
    TmToSystemTime(tm, static_cast<int>((since % 10000000ULL) / 10000ULL), out);
    return TRUE;
}

BOOL SystemTimeToFileTime(const SYSTEMTIME* in, LPFILETIME out) {
    if (!in || !out) return FALSE;
    struct tm tm {};
    tm.tm_year = in->wYear - 1900;
    tm.tm_mon = in->wMonth - 1;
    tm.tm_mday = in->wDay;
    tm.tm_hour = in->wHour;
    tm.tm_min = in->wMinute;
    tm.tm_sec = in->wSecond;
    const time_t secs = timegm(&tm);
    *out = FromULL(kUnixEpochAsFileTime + static_cast<ULONGLONG>(secs) * 10000000ULL + static_cast<ULONGLONG>(in->wMilliseconds) * 10000ULL);
    return TRUE;
}

LONG CompareFileTime(const FILETIME* a, const FILETIME* b) {
    const ULONGLONG x = ToULL(*a), y = ToULL(*b);
    return x < y ? -1 : (x > y ? 1 : 0);
}

DWORD GetMessageTime() { return macwin::CurrentMessageTime(); }
UINT timeBeginPeriod(UINT) { return 0; }
UINT timeEndPeriod(UINT) { return 0; }

// ---------------------------------------------------------------------------------------------
// Processes / threads
// ---------------------------------------------------------------------------------------------
DWORD GetCurrentProcessId() { return static_cast<DWORD>(getpid()); }

DWORD GetCurrentThreadId() {
    thread_local DWORD id = [] {
        uint64_t tid = 0;
        pthread_threadid_np(nullptr, &tid);
        DWORD v = static_cast<DWORD>(tid ^ (tid >> 32));
        return v ? v : 1u;
    }();
    return id;
}

HANDLE GetCurrentProcess() { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1)); }
HANDLE GetCurrentThread() { return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2)); }

BOOL SetThreadPriority(HANDLE thread, int priority) {
    if (thread != GetCurrentThread()) return TRUE;
    qos_class_t qos = QOS_CLASS_DEFAULT;
    if (priority >= THREAD_PRIORITY_TIME_CRITICAL) qos = QOS_CLASS_USER_INTERACTIVE;
    else if (priority >= THREAD_PRIORITY_ABOVE_NORMAL) qos = QOS_CLASS_USER_INITIATED;
    else if (priority <= THREAD_PRIORITY_IDLE) qos = QOS_CLASS_BACKGROUND;
    else if (priority < THREAD_PRIORITY_NORMAL) qos = QOS_CLASS_UTILITY;
    pthread_set_qos_class_self_np(qos, 0);
    return TRUE;
}

int GetThreadPriority(HANDLE) { return THREAD_PRIORITY_NORMAL; }
BOOL SwitchToThread() {
    sched_yield();
    return TRUE;
}

BOOL GetProcessTimes(HANDLE, LPFILETIME creation, LPFILETIME exitTime, LPFILETIME kernel, LPFILETIME user) {
    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    auto toFt = [](const struct timeval& tv) {
        return FromULL(static_cast<ULONGLONG>(tv.tv_sec) * 10000000ULL + static_cast<ULONGLONG>(tv.tv_usec) * 10ULL);
    };
    if (kernel) *kernel = toFt(usage.ru_stime);
    if (user) *user = toFt(usage.ru_utime);
    if (exitTime) *exitTime = FromULL(0);
    if (creation) {
        struct proc_bsdinfo info {};
        if (proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) == sizeof(info)) {
            *creation = FromULL(kUnixEpochAsFileTime + info.pbi_start_tvsec * 10000000ULL + info.pbi_start_tvusec * 10ULL);
        } else {
            *creation = FromULL(0);
        }
    }
    return TRUE;
}

[[noreturn]] void ExitProcess(UINT exitCode) { exit(static_cast<int>(exitCode)); }

BOOL TerminateProcess(HANDLE process, UINT exitCode) {
    if (process == GetCurrentProcess()) _exit(static_cast<int>(exitCode));
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

// ---------------------------------------------------------------------------------------------
// Modules. An HMODULE is the address of the image's mach_header (the image base), like Windows.
// ---------------------------------------------------------------------------------------------
namespace {

std::string BaseNameLower(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return base;
}

std::string StripExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// Windows DLL names the shared code asks for, mapped onto their macOS counterparts.
std::string TranslateWindowsModuleName(const std::string& lowerName) {
    const std::string stem = StripExtension(lowerName);
    if (stem == "opengl32") return "opengl";
    if (stem == "glfw" || stem == "glfw3") return "libglfw";
    if (stem == "lwjgl") return "liblwjgl";
    if (stem == "toolscreen") return "libtoolscreen";
    return stem;
}

const mach_header* FindImageByName(const std::string& requestedLower) {
    const std::string wanted = TranslateWindowsModuleName(requestedLower);
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        const char* name = _dyld_get_image_name(i);
        if (!name) continue;
        const std::string base = BaseNameLower(name);
        if (base == requestedLower || StripExtension(base) == wanted || base == wanted) return _dyld_get_image_header(i);
    }
    return nullptr;
}

std::string ImagePathForHeader(const mach_header* header) {
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i) {
        if (_dyld_get_image_header(i) == header) {
            const char* name = _dyld_get_image_name(i);
            return name ? name : "";
        }
    }
    return {};
}

} // namespace

namespace macwin {
std::string ModulePathForHandle(HMODULE module) {
    if (!module) {
        char buffer[PATH_MAX];
        uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size) == 0) return buffer;
        return {};
    }
    return ImagePathForHeader(reinterpret_cast<const mach_header*>(module));
}

HMODULE ModuleHandleForAddress(const void* address) {
    Dl_info info{};
    if (!address || !dladdr(address, &info) || !info.dli_fbase) return nullptr;
    return reinterpret_cast<HMODULE>(info.dli_fbase);
}
} // namespace macwin

DWORD GetModuleFileNameW(HMODULE module, LPWSTR buffer, DWORD size) {
    const std::wstring path = macplat::Utf8ToWide(macwin::ModulePathForHandle(module).c_str());
    if (!buffer || size == 0) return 0;
    if (path.empty()) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        buffer[0] = 0;
        return 0;
    }
    const size_t n = std::min<size_t>(path.size(), size - 1);
    wmemcpy(buffer, path.data(), n);
    buffer[n] = 0;
    if (path.size() >= size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return size;
    }
    return static_cast<DWORD>(n);
}

DWORD GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size) {
    const std::string path = macwin::ModulePathForHandle(module);
    if (!buffer || size == 0) return 0;
    const size_t n = std::min<size_t>(path.size(), size - 1);
    memcpy(buffer, path.data(), n);
    buffer[n] = 0;
    return static_cast<DWORD>(n);
}

HMODULE GetModuleHandleA(LPCSTR name) {
    if (!name) return reinterpret_cast<HMODULE>(const_cast<mach_header*>(_dyld_get_image_header(0)));
    std::string lower = BaseNameLower(name);
    return reinterpret_cast<HMODULE>(const_cast<mach_header*>(FindImageByName(lower)));
}

HMODULE GetModuleHandleW(LPCWSTR name) {
    if (!name) return GetModuleHandleA(nullptr);
    return GetModuleHandleA(macplat::WideToUtf8(name).c_str());
}

BOOL GetModuleHandleExA(DWORD flags, LPCSTR name, HMODULE* out) {
    if (!out) return FALSE;
    if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) {
        *out = macwin::ModuleHandleForAddress(name);
    } else {
        *out = GetModuleHandleA(name);
    }
    if (!*out) SetLastError(ERROR_MOD_NOT_FOUND);
    return *out != nullptr;
}

BOOL GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE* out) {
    if (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) return GetModuleHandleExA(flags, reinterpret_cast<LPCSTR>(name), out);
    if (!out) return FALSE;
    *out = GetModuleHandleW(name);
    if (!*out) SetLastError(ERROR_MOD_NOT_FOUND);
    return *out != nullptr;
}

HMODULE LoadLibraryA(LPCSTR path) {
    if (!path) return nullptr;
    const std::string native = macplat::NativePathFromNarrow(path);
    void* handle = dlopen(native.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    // Resolve the image base of what was just loaded.
    const uint32_t count = _dyld_image_count();
    char resolved[PATH_MAX];
    const bool haveResolved = realpath(native.c_str(), resolved) != nullptr;
    for (uint32_t i = count; i-- > 0;) {
        const char* name = _dyld_get_image_name(i);
        if (!name) continue;
        char imageResolved[PATH_MAX];
        if (haveResolved && realpath(name, imageResolved) && strcmp(imageResolved, resolved) == 0) {
            return reinterpret_cast<HMODULE>(const_cast<mach_header*>(_dyld_get_image_header(i)));
        }
    }
    return GetModuleHandleA(native.c_str());
}

HMODULE LoadLibraryW(LPCWSTR path) { return path ? LoadLibraryA(macplat::WideToUtf8(path).c_str()) : nullptr; }
HMODULE LoadLibraryExW(LPCWSTR path, HANDLE, DWORD) { return LoadLibraryW(path); }
HMODULE LoadLibraryExA(LPCSTR path, HANDLE, DWORD) { return LoadLibraryA(path); }

BOOL FreeLibrary(HMODULE module) {
    const std::string path = macwin::ModulePathForHandle(module);
    if (path.empty()) return FALSE;
    void* handle = dlopen(path.c_str(), RTLD_NOLOAD | RTLD_LAZY);
    if (!handle) return FALSE;
    dlclose(handle); // the NOLOAD reference
    dlclose(handle); // the LoadLibrary reference
    return TRUE;
}

FARPROC GetProcAddress(HMODULE module, LPCSTR name) {
    if (!name) return nullptr;
    if (!module) return dlsym(RTLD_DEFAULT, name);
    const std::string path = macwin::ModulePathForHandle(module);
    if (path.empty()) return nullptr;
    void* handle = dlopen(path.c_str(), RTLD_NOLOAD | RTLD_LAZY);
    if (!handle) return nullptr;
    void* sym = dlsym(handle, name);
    dlclose(handle);
    if (!sym) SetLastError(ERROR_PROC_NOT_FOUND);
    return sym;
}

BOOL DisableThreadLibraryCalls(HMODULE) { return TRUE; }

extern "C" PVOID RtlPcToFileHeader(PVOID pcValue, PVOID* baseOfImage) {
    Dl_info info{};
    void* base = (pcValue && dladdr(pcValue, &info)) ? info.dli_fbase : nullptr;
    if (baseOfImage) *baseOfImage = base;
    return base;
}

SIZE_T VirtualQuery(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T length) {
    if (!info || length < sizeof(MEMORY_BASIC_INFORMATION)) return 0;
    mach_vm_address_t regionAddress = reinterpret_cast<mach_vm_address_t>(address);
    mach_vm_size_t regionSize = 0;
    vm_region_basic_info_data_64_t basic{};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t objectName = MACH_PORT_NULL;
    const kern_return_t kr = mach_vm_region(mach_task_self(), &regionAddress, &regionSize, VM_REGION_BASIC_INFO_64,
                                            reinterpret_cast<vm_region_info_t>(&basic), &count, &objectName);
    memset(info, 0, sizeof(*info));
    if (kr != KERN_SUCCESS || regionAddress > reinterpret_cast<mach_vm_address_t>(address)) {
        info->BaseAddress = const_cast<void*>(address);
        info->State = MEM_FREE;
        info->Protect = PAGE_NOACCESS;
        return sizeof(*info);
    }
    info->BaseAddress = reinterpret_cast<void*>(regionAddress);
    info->AllocationBase = info->BaseAddress;
    info->RegionSize = static_cast<SIZE_T>(regionSize);
    info->State = MEM_COMMIT;
    const vm_prot_t p = basic.protection;
    if ((p & VM_PROT_EXECUTE) && (p & VM_PROT_WRITE)) info->Protect = PAGE_EXECUTE_READWRITE;
    else if (p & VM_PROT_EXECUTE) info->Protect = (p & VM_PROT_READ) ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    else if (p & VM_PROT_WRITE) info->Protect = PAGE_READWRITE;
    else if (p & VM_PROT_READ) info->Protect = PAGE_READONLY;
    else info->Protect = PAGE_NOACCESS;
    info->AllocationProtect = info->Protect;
    Dl_info dl{};
    info->Type = (dladdr(address, &dl) && dl.dli_fbase) ? MEM_IMAGE : MEM_PRIVATE;
    return sizeof(*info);
}

// ---------------------------------------------------------------------------------------------
// Command line / environment / debugging
// ---------------------------------------------------------------------------------------------
LPWSTR GetCommandLineW() {
    static std::wstring commandLine = [] {
        char* raw = MacBridge_CopyProcessCommandLine();
        std::wstring w = macplat::Utf8ToWide(raw ? raw : "");
        free(raw);
        return w;
    }();
    return commandLine.data();
}

LPSTR GetCommandLineA() {
    static std::string commandLine = macplat::WideToUtf8(GetCommandLineW());
    return commandLine.data();
}

DWORD GetEnvironmentVariableA(LPCSTR name, LPSTR buffer, DWORD size) {
    const char* value = name ? getenv(name) : nullptr;
    if (!value) {
        SetLastError(203 /* ERROR_ENVVAR_NOT_FOUND */);
        return 0;
    }
    const size_t len = strlen(value);
    if (!buffer || size <= len) return static_cast<DWORD>(len + 1);
    memcpy(buffer, value, len + 1);
    return static_cast<DWORD>(len);
}

DWORD GetEnvironmentVariableW(LPCWSTR name, LPWSTR buffer, DWORD size) {
    const char* value = name ? getenv(macplat::WideToUtf8(name).c_str()) : nullptr;
    if (!value) {
        SetLastError(203 /* ERROR_ENVVAR_NOT_FOUND */);
        return 0;
    }
    const std::wstring w = macplat::Utf8ToWide(value);
    if (!buffer || size <= w.size()) return static_cast<DWORD>(w.size() + 1);
    wmemcpy(buffer, w.c_str(), w.size() + 1);
    return static_cast<DWORD>(w.size());
}

BOOL SetEnvironmentVariableA(LPCSTR name, LPCSTR value) {
    if (!name) return FALSE;
    return (value ? setenv(name, value, 1) : unsetenv(name)) == 0;
}

BOOL SetEnvironmentVariableW(LPCWSTR name, LPCWSTR value) {
    if (!name) return FALSE;
    const std::string n = macplat::WideToUtf8(name);
    if (!value) return unsetenv(n.c_str()) == 0;
    return setenv(n.c_str(), macplat::WideToUtf8(value).c_str(), 1) == 0;
}

DWORD ExpandEnvironmentStringsW(LPCWSTR src, LPWSTR dst, DWORD size) {
    std::wstring out;
    const std::wstring in = src ? src : L"";
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'%') {
            const size_t end = in.find(L'%', i + 1);
            if (end != std::wstring::npos) {
                const std::string var = macplat::WideToUtf8(in.substr(i + 1, end - i - 1).c_str());
                const char* value = getenv(var.c_str());
                if (!value && (var == "USERPROFILE" || var == "APPDATA" || var == "LOCALAPPDATA")) value = getenv("HOME");
                if (value) {
                    out += macplat::Utf8ToWide(value);
                    i = end;
                    continue;
                }
            }
        }
        out.push_back(in[i]);
    }
    if (dst && size > out.size()) wmemcpy(dst, out.c_str(), out.size() + 1);
    return static_cast<DWORD>(out.size() + 1);
}

int getenv_s(size_t* requiredSize, char* buffer, size_t size, const char* name) {
    const char* value = name ? getenv(name) : nullptr;
    if (!value) {
        if (requiredSize) *requiredSize = 0;
        if (buffer && size) buffer[0] = 0;
        return 0;
    }
    const size_t len = strlen(value) + 1;
    if (requiredSize) *requiredSize = len;
    if (!buffer) return 0;
    if (size < len) return ERANGE;
    memcpy(buffer, value, len);
    return 0;
}

int _wgetenv_s(size_t* requiredSize, wchar_t* buffer, size_t size, const wchar_t* name) {
    const char* value = name ? getenv(macplat::WideToUtf8(name).c_str()) : nullptr;
    if (!value) {
        if (requiredSize) *requiredSize = 0;
        if (buffer && size) buffer[0] = 0;
        return 0;
    }
    const std::wstring w = macplat::Utf8ToWide(value);
    if (requiredSize) *requiredSize = w.size() + 1;
    if (!buffer) return 0;
    if (size < w.size() + 1) return ERANGE;
    wmemcpy(buffer, w.c_str(), w.size() + 1);
    return 0;
}

int _dupenv_s(char** buffer, size_t* size, const char* name) {
    if (!buffer) return EINVAL;
    const char* value = name ? getenv(name) : nullptr;
    *buffer = value ? strdup(value) : nullptr;
    if (size) *size = value ? strlen(value) + 1 : 0;
    return 0;
}

int fopen_s(FILE** out, const char* path, const char* mode) {
    if (!out) return EINVAL;
    *out = fopen(macplat::NativePathFromNarrow(path).c_str(), mode);
    return *out ? 0 : errno;
}

FILE* _wfopen(const wchar_t* path, const wchar_t* mode) {
    if (!path || !mode) return nullptr;
    return fopen(macplat::NativePathFromWide(path).c_str(), macplat::WideToUtf8(mode).c_str());
}

int _wfopen_s(FILE** out, const wchar_t* path, const wchar_t* mode) {
    if (!out) return EINVAL;
    *out = _wfopen(path, mode);
    return *out ? 0 : errno;
}

void OutputDebugStringA(LPCSTR text) {
    if (text) fputs(text, stderr);
}
void OutputDebugStringW(LPCWSTR text) {
    if (text) fputs(macplat::WideToUtf8(text).c_str(), stderr);
}

USHORT CaptureStackBackTrace(ULONG framesToSkip, ULONG framesToCapture, PVOID* backTrace, PULONG hash) {
    if (!backTrace || framesToCapture == 0) return 0;
    void* frames[128];
    const int total = backtrace(frames, 128);
    int start = static_cast<int>(framesToSkip) + 1; // skip this function
    int n = 0;
    for (int i = start; i < total && n < static_cast<int>(framesToCapture); ++i) backTrace[n++] = frames[i];
    if (hash) {
        ULONG h = 0;
        for (int i = 0; i < n; ++i) h = h * 31 + static_cast<ULONG>(reinterpret_cast<uintptr_t>(backTrace[i]));
        *hash = h;
    }
    return static_cast<USHORT>(n);
}

LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter) {
    // Hardware faults belong to the JVM's signal handlers on macOS; installing our own would break
    // HotSpot's implicit null checks and safepoints. The filter is recorded but never invoked.
    static std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> s_filter{ nullptr };
    return s_filter.exchange(filter);
}

PVOID AddVectoredExceptionHandler(ULONG, PVECTORED_EXCEPTION_HANDLER handler) { return reinterpret_cast<PVOID>(handler); }
ULONG RemoveVectoredExceptionHandler(PVOID) { return 1; }

// DbgHelp: symbol names through dladdr (demangled).
BOOL SymInitialize(HANDLE, PCSTR, BOOL) { return TRUE; }
BOOL SymCleanup(HANDLE) { return TRUE; }
DWORD SymGetOptions() { return 0; }
DWORD SymSetOptions(DWORD options) { return options; }

BOOL SymFromAddr(HANDLE, DWORD64 address, DWORD64* displacement, PSYMBOL_INFO symbol) {
    Dl_info info{};
    if (!symbol || !dladdr(reinterpret_cast<void*>(address), &info) || !info.dli_sname) return FALSE;
    int status = 0;
    char* demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
    const char* name = (status == 0 && demangled) ? demangled : info.dli_sname;
    std::string text = name;
    if (info.dli_fname) {
        const char* base = strrchr(info.dli_fname, '/');
        text = std::string(base ? base + 1 : info.dli_fname) + "!" + text;
    }
    free(demangled);
    const size_t maxLen = symbol->MaxNameLen ? symbol->MaxNameLen : 1;
    const size_t n = std::min(text.size(), maxLen - 1);
    memcpy(symbol->Name, text.data(), n);
    symbol->Name[n] = 0;
    symbol->NameLen = static_cast<ULONG>(n);
    symbol->Address = reinterpret_cast<ULONGLONG>(info.dli_saddr);
    if (displacement) *displacement = address - reinterpret_cast<DWORD64>(info.dli_saddr);
    return TRUE;
}

BOOL SymGetLineFromAddr64(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64) { return FALSE; }

// ---------------------------------------------------------------------------------------------
// Text conversion
// ---------------------------------------------------------------------------------------------
int MultiByteToWideChar(UINT, DWORD flags, LPCSTR src, int srcLen, LPWSTR dst, int dstLen) {
    if (!src) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    const bool includeTerminator = srcLen < 0;
    const size_t len = includeTerminator ? strlen(src) : static_cast<size_t>(srcLen);
    bool invalid = false;
    std::wstring w = DecodeUtf8(src, len, &invalid);
    if (invalid && (flags & MB_ERR_INVALID_CHARS)) {
        SetLastError(1113 /* ERROR_NO_UNICODE_TRANSLATION */);
        return 0;
    }
    if (includeTerminator) w.push_back(0);
    if (dstLen == 0) return static_cast<int>(w.size());
    if (!dst || static_cast<size_t>(dstLen) < w.size()) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    wmemcpy(dst, w.data(), w.size());
    return static_cast<int>(w.size());
}

int WideCharToMultiByte(UINT, DWORD, LPCWSTR src, int srcLen, LPSTR dst, int dstLen, LPCSTR, LPBOOL usedDefault) {
    if (!src) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (usedDefault) *usedDefault = FALSE;
    const bool includeTerminator = srcLen < 0;
    const size_t len = includeTerminator ? wcslen(src) : static_cast<size_t>(srcLen);
    std::string s = EncodeUtf8(src, len);
    if (includeTerminator) s.push_back(0);
    if (dstLen == 0) return static_cast<int>(s.size());
    if (!dst || static_cast<size_t>(dstLen) < s.size()) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(dst, s.data(), s.size());
    return static_cast<int>(s.size());
}

// ---------------------------------------------------------------------------------------------
// Embedded resources
// ---------------------------------------------------------------------------------------------
extern "C" {
struct ToolscreenEmbeddedResource {
    int id;
    const unsigned char* data;
    size_t size;
};
extern const struct ToolscreenEmbeddedResource g_toolscreenEmbeddedResources[];
}

HRSRC FindResourceW(HMODULE, LPCWSTR name, LPCWSTR type) {
    if (!IS_INTRESOURCE(name) || (type && (!IS_INTRESOURCE(type) || type != RT_RCDATA))) {
        SetLastError(ERROR_RESOURCE_TYPE_NOT_FOUND);
        return nullptr;
    }
    const int id = static_cast<int>(reinterpret_cast<uintptr_t>(name));
    for (const ToolscreenEmbeddedResource* r = g_toolscreenEmbeddedResources; r->data; ++r) {
        if (r->id == id) return reinterpret_cast<HRSRC>(const_cast<ToolscreenEmbeddedResource*>(r));
    }
    SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
    return nullptr;
}

HGLOBAL LoadResource(HMODULE, HRSRC resource) { return resource; }

DWORD SizeofResource(HMODULE, HRSRC resource) {
    return resource ? static_cast<DWORD>(reinterpret_cast<const ToolscreenEmbeddedResource*>(resource)->size) : 0;
}

LPVOID LockResource(HGLOBAL data) {
    return data ? const_cast<unsigned char*>(reinterpret_cast<const ToolscreenEmbeddedResource*>(data)->data) : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Registry and version resources (not present on macOS)
// ---------------------------------------------------------------------------------------------
LSTATUS RegOpenKeyExW(HKEY, LPCWSTR, DWORD, DWORD, PHKEY result) {
    if (result) *result = nullptr;
    return ERROR_FILE_NOT_FOUND;
}
LSTATUS RegOpenKeyExA(HKEY, LPCSTR, DWORD, DWORD, PHKEY result) {
    if (result) *result = nullptr;
    return ERROR_FILE_NOT_FOUND;
}
LSTATUS RegQueryValueExW(HKEY, LPCWSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return ERROR_FILE_NOT_FOUND; }
LSTATUS RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return ERROR_FILE_NOT_FOUND; }
LSTATUS RegEnumValueW(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return ERROR_NO_MORE_ITEMS; }
LSTATUS RegCloseKey(HKEY) { return ERROR_SUCCESS; }

DWORD GetFileVersionInfoSizeW(LPCWSTR, LPDWORD handle) {
    if (handle) *handle = 0;
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}
BOOL GetFileVersionInfoW(LPCWSTR, DWORD, DWORD, LPVOID) { return FALSE; }
BOOL VerQueryValueW(LPCVOID, LPCWSTR, LPVOID* buffer, PUINT size) {
    if (buffer) *buffer = nullptr;
    if (size) *size = 0;
    return FALSE;
}
