// Win32 compatibility layer (macOS): file handles, attributes, directories and well-known folders.
// Paths arrive as UTF-32 wide strings; backslashes are accepted as separators.
#include "platform/mac/mac_handles.h"
#include "platform/mac/mac_internal.h"

#include <copyfile.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace macwin {
void SetLastErrorFromErrno(int e); // win32_compat_core.cpp
}

namespace {

struct FileObject : macwin::HandleObject {
    FileObject(int f, std::string p, bool del) : HandleObject(macwin::HandleKind::File), fd(f), path(std::move(p)), deleteOnClose(del) {}
    ~FileObject() override {
        if (fd >= 0) close(fd);
        if (deleteOnClose && !path.empty()) unlink(path.c_str());
    }
    int fd;
    std::string path;
    bool deleteOnClose;
};

std::shared_ptr<FileObject> FileFromHandle(HANDLE h) { return macwin::LookupHandleAs<FileObject>(h, macwin::HandleKind::File); }

std::string P(LPCWSTR path) { return macplat::NativePathFromWide(path); }

FILETIME TimespecToFileTime(const struct timespec& ts) {
    const ULONGLONG v = 116444736000000000ULL + static_cast<ULONGLONG>(ts.tv_sec) * 10000000ULL + static_cast<ULONGLONG>(ts.tv_nsec) / 100ULL;
    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(v & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(v >> 32);
    return ft;
}

struct timespec FileTimeToTimespec(const FILETIME& ft) {
    const ULONGLONG v = (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    const ULONGLONG since = v > 116444736000000000ULL ? v - 116444736000000000ULL : 0;
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(since / 10000000ULL);
    ts.tv_nsec = static_cast<long>((since % 10000000ULL) * 100ULL);
    return ts;
}

DWORD AttributesFromStat(const struct stat& st, const std::string& path) {
    DWORD attrs = 0;
    if (S_ISDIR(st.st_mode)) attrs |= FILE_ATTRIBUTE_DIRECTORY;
    if (access(path.c_str(), W_OK) != 0) attrs |= FILE_ATTRIBUTE_READONLY;
    const size_t slash = path.find_last_of('/');
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    if (!base.empty() && base[0] == '.') attrs |= FILE_ATTRIBUTE_HIDDEN;
    return attrs ? attrs : FILE_ATTRIBUTE_NORMAL;
}

void CopyToWideBuffer(const std::wstring& s, LPWSTR buffer, DWORD size) {
    if (!buffer || size == 0) return;
    const size_t n = std::min<size_t>(s.size(), size - 1);
    wmemcpy(buffer, s.data(), n);
    buffer[n] = 0;
}

std::wstring EnsureTrailingSlash(std::wstring s) {
    if (s.empty() || s.back() != L'/') s.push_back(L'/');
    return s;
}

std::string HomeDirectory() {
    const char* home = getenv("HOME");
    return home ? home : "/tmp";
}

} // namespace

HANDLE CreateFileA(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES attrs, DWORD disposition, DWORD flags, HANDLE templ) {
    if (!path) return INVALID_HANDLE_VALUE;
    return CreateFileW(macplat::Utf8ToWide(path).c_str(), access, share, attrs, disposition, flags, templ);
}

HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD, LPSECURITY_ATTRIBUTES, DWORD disposition, DWORD flags, HANDLE) {
    if (!path) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    const std::string native = P(path);
    int oflags = O_CLOEXEC;
    const bool wantRead = (access & (GENERIC_READ | GENERIC_ALL | FILE_READ_DATA)) != 0;
    const bool wantWrite = (access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA)) != 0;
    if (wantRead && wantWrite) oflags |= O_RDWR;
    else if (wantWrite) oflags |= O_WRONLY;
    else oflags |= O_RDONLY;
    if ((access & FILE_APPEND_DATA) && !(access & (GENERIC_WRITE | FILE_WRITE_DATA))) oflags |= O_APPEND;

    struct stat st {};
    const bool exists = stat(native.c_str(), &st) == 0;
    switch (disposition) {
    case CREATE_NEW: oflags |= O_CREAT | O_EXCL; break;
    case CREATE_ALWAYS: oflags |= O_CREAT | O_TRUNC; break;
    case OPEN_EXISTING: break;
    case OPEN_ALWAYS: oflags |= O_CREAT; break;
    case TRUNCATE_EXISTING: oflags |= O_TRUNC; break;
    default: break;
    }
    if (exists && S_ISDIR(st.st_mode)) {
        // Directory handles (FILE_FLAG_BACKUP_SEMANTICS) are only used for timestamps.
        oflags = O_RDONLY | O_CLOEXEC;
    }
    const int fd = open(native.c_str(), oflags, 0644);
    if (fd < 0) {
        const int e = errno;
        if (e == EEXIST) SetLastError(ERROR_FILE_EXISTS);
        else macwin::SetLastErrorFromErrno(e);
        return INVALID_HANDLE_VALUE;
    }
    SetLastError((exists && (disposition == CREATE_ALWAYS || disposition == OPEN_ALWAYS)) ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
    return macwin::RegisterHandle(std::make_shared<FileObject>(fd, native, (flags & FILE_FLAG_DELETE_ON_CLOSE) != 0));
}

BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD toRead, LPDWORD read, LPOVERLAPPED) {
    auto f = FileFromHandle(file);
    if (read) *read = 0;
    if (!f) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    size_t total = 0;
    while (total < toRead) {
        const ssize_t n = ::read(f->fd, static_cast<char*>(buffer) + total, toRead - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            macwin::SetLastErrorFromErrno(errno);
            if (read) *read = static_cast<DWORD>(total);
            return FALSE;
        }
        if (n == 0) break;
        total += static_cast<size_t>(n);
    }
    if (read) *read = static_cast<DWORD>(total);
    return TRUE;
}

BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD toWrite, LPDWORD written, LPOVERLAPPED) {
    auto f = FileFromHandle(file);
    if (written) *written = 0;
    if (!f) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    size_t total = 0;
    while (total < toWrite) {
        const ssize_t n = ::write(f->fd, static_cast<const char*>(buffer) + total, toWrite - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            macwin::SetLastErrorFromErrno(errno);
            if (written) *written = static_cast<DWORD>(total);
            return FALSE;
        }
        total += static_cast<size_t>(n);
    }
    if (written) *written = static_cast<DWORD>(total);
    return TRUE;
}

BOOL FlushFileBuffers(HANDLE file) {
    auto f = FileFromHandle(file);
    if (!f) return FALSE;
    // F_FULLFSYNC is the macOS equivalent of a durable flush.
    if (fcntl(f->fd, F_FULLFSYNC) == 0) return TRUE;
    return fsync(f->fd) == 0;
}

BOOL GetFileSizeEx(HANDLE file, PLARGE_INTEGER size) {
    auto f = FileFromHandle(file);
    struct stat st {};
    if (!f || !size || fstat(f->fd, &st) != 0) return FALSE;
    size->QuadPart = st.st_size;
    return TRUE;
}

DWORD GetFileSize(HANDLE file, LPDWORD high) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) return INVALID_FILE_SIZE;
    if (high) *high = static_cast<DWORD>(size.QuadPart >> 32);
    return static_cast<DWORD>(size.QuadPart & 0xFFFFFFFF);
}

BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER newPointer, DWORD method) {
    auto f = FileFromHandle(file);
    if (!f) return FALSE;
    const int whence = method == FILE_BEGIN ? SEEK_SET : (method == FILE_CURRENT ? SEEK_CUR : SEEK_END);
    const off_t pos = lseek(f->fd, static_cast<off_t>(distance.QuadPart), whence);
    if (pos < 0) {
        macwin::SetLastErrorFromErrno(errno);
        return FALSE;
    }
    if (newPointer) newPointer->QuadPart = pos;
    return TRUE;
}

DWORD SetFilePointer(HANDLE file, LONG distance, PLONG distanceHigh, DWORD method) {
    LARGE_INTEGER d{};
    d.QuadPart = distanceHigh ? ((static_cast<LONGLONG>(*distanceHigh) << 32) | static_cast<DWORD>(distance)) : distance;
    LARGE_INTEGER np{};
    if (!SetFilePointerEx(file, d, &np, method)) return INVALID_SET_FILE_POINTER;
    if (distanceHigh) *distanceHigh = static_cast<LONG>(np.QuadPart >> 32);
    return static_cast<DWORD>(np.QuadPart & 0xFFFFFFFF);
}

BOOL SetEndOfFile(HANDLE file) {
    auto f = FileFromHandle(file);
    if (!f) return FALSE;
    const off_t pos = lseek(f->fd, 0, SEEK_CUR);
    return pos >= 0 && ftruncate(f->fd, pos) == 0;
}

BOOL GetFileTime(HANDLE file, LPFILETIME creation, LPFILETIME accessTime, LPFILETIME write) {
    auto f = FileFromHandle(file);
    struct stat st {};
    if (!f || fstat(f->fd, &st) != 0) return FALSE;
    if (creation) *creation = TimespecToFileTime(st.st_birthtimespec);
    if (accessTime) *accessTime = TimespecToFileTime(st.st_atimespec);
    if (write) *write = TimespecToFileTime(st.st_mtimespec);
    return TRUE;
}

BOOL SetFileTime(HANDLE file, const FILETIME*, const FILETIME* accessTime, const FILETIME* write) {
    auto f = FileFromHandle(file);
    if (!f) return FALSE;
    struct timespec times[2];
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_nsec = UTIME_OMIT;
    if (accessTime) times[0] = FileTimeToTimespec(*accessTime);
    if (write) times[1] = FileTimeToTimespec(*write);
    return futimens(f->fd, times) == 0;
}

DWORD GetFileAttributesW(LPCWSTR path) {
    if (!path) return INVALID_FILE_ATTRIBUTES;
    const std::string native = P(path);
    struct stat st {};
    if (stat(native.c_str(), &st) != 0) {
        macwin::SetLastErrorFromErrno(errno);
        return INVALID_FILE_ATTRIBUTES;
    }
    return AttributesFromStat(st, native);
}

DWORD GetFileAttributesA(LPCSTR path) { return path ? GetFileAttributesW(macplat::Utf8ToWide(path).c_str()) : INVALID_FILE_ATTRIBUTES; }

BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS, LPVOID infoPtr) {
    if (!path || !infoPtr) return FALSE;
    const std::string native = P(path);
    struct stat st {};
    if (stat(native.c_str(), &st) != 0) {
        macwin::SetLastErrorFromErrno(errno);
        return FALSE;
    }
    auto* info = static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(infoPtr);
    info->dwFileAttributes = AttributesFromStat(st, native);
    info->ftCreationTime = TimespecToFileTime(st.st_birthtimespec);
    info->ftLastAccessTime = TimespecToFileTime(st.st_atimespec);
    info->ftLastWriteTime = TimespecToFileTime(st.st_mtimespec);
    info->nFileSizeHigh = static_cast<DWORD>(static_cast<uint64_t>(st.st_size) >> 32);
    info->nFileSizeLow = static_cast<DWORD>(static_cast<uint64_t>(st.st_size) & 0xFFFFFFFF);
    return TRUE;
}

BOOL SetFileAttributesW(LPCWSTR path, DWORD attributes) {
    if (!path) return FALSE;
    const std::string native = P(path);
    struct stat st {};
    if (stat(native.c_str(), &st) != 0) return FALSE;
    mode_t mode = st.st_mode & 07777;
    if (attributes & FILE_ATTRIBUTE_READONLY) mode &= ~static_cast<mode_t>(0222);
    else mode |= S_IWUSR;
    return chmod(native.c_str(), mode) == 0;
}

BOOL DeleteFileW(LPCWSTR path) {
    if (!path) return FALSE;
    if (unlink(P(path).c_str()) != 0) {
        macwin::SetLastErrorFromErrno(errno);
        return FALSE;
    }
    return TRUE;
}

BOOL DeleteFileA(LPCSTR path) { return path ? DeleteFileW(macplat::Utf8ToWide(path).c_str()) : FALSE; }

BOOL MoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    if (!from || !to) return FALSE;
    const std::string src = P(from), dst = P(to);
    struct stat st {};
    if (!(flags & MOVEFILE_REPLACE_EXISTING) && stat(dst.c_str(), &st) == 0) {
        SetLastError(ERROR_ALREADY_EXISTS);
        return FALSE;
    }
    if (rename(src.c_str(), dst.c_str()) == 0) return TRUE;
    if (errno == EXDEV && (flags & MOVEFILE_COPY_ALLOWED)) {
        if (copyfile(src.c_str(), dst.c_str(), nullptr, COPYFILE_ALL) == 0 && unlink(src.c_str()) == 0) return TRUE;
    }
    macwin::SetLastErrorFromErrno(errno);
    return FALSE;
}

BOOL MoveFileW(LPCWSTR from, LPCWSTR to) { return MoveFileExW(from, to, MOVEFILE_COPY_ALLOWED); }

BOOL ReplaceFileW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD, LPVOID, LPVOID) {
    if (!replaced || !replacement) return FALSE;
    const std::string target = P(replaced), source = P(replacement);
    if (backup) {
        const std::string b = P(backup);
        if (copyfile(target.c_str(), b.c_str(), nullptr, COPYFILE_ALL) != 0) {
            macwin::SetLastErrorFromErrno(errno);
            return FALSE;
        }
    }
    if (rename(source.c_str(), target.c_str()) != 0) {
        macwin::SetLastErrorFromErrno(errno);
        return FALSE;
    }
    return TRUE;
}

BOOL CopyFileW(LPCWSTR from, LPCWSTR to, BOOL failIfExists) {
    if (!from || !to) return FALSE;
    const std::string src = P(from), dst = P(to);
    struct stat st {};
    if (failIfExists && stat(dst.c_str(), &st) == 0) {
        SetLastError(ERROR_FILE_EXISTS);
        return FALSE;
    }
    if (copyfile(src.c_str(), dst.c_str(), nullptr, COPYFILE_ALL) != 0) {
        macwin::SetLastErrorFromErrno(errno);
        return FALSE;
    }
    return TRUE;
}

BOOL CreateDirectoryW(LPCWSTR path, LPSECURITY_ATTRIBUTES) {
    if (!path) return FALSE;
    if (mkdir(P(path).c_str(), 0755) != 0) {
        macwin::SetLastErrorFromErrno(errno);
        return FALSE;
    }
    return TRUE;
}

BOOL CreateDirectoryA(LPCSTR path, LPSECURITY_ATTRIBUTES attrs) {
    return path ? CreateDirectoryW(macplat::Utf8ToWide(path).c_str(), attrs) : FALSE;
}

BOOL RemoveDirectoryW(LPCWSTR path) {
    if (!path) return FALSE;
    if (rmdir(P(path).c_str()) != 0) {
        macwin::SetLastErrorFromErrno(errno);
        return FALSE;
    }
    return TRUE;
}

DWORD GetCurrentDirectoryW(DWORD size, LPWSTR buffer) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return 0;
    const std::wstring w = macplat::Utf8ToWide(cwd);
    if (!buffer || size <= w.size()) return static_cast<DWORD>(w.size() + 1);
    CopyToWideBuffer(w, buffer, size);
    return static_cast<DWORD>(w.size());
}

DWORD GetCurrentDirectoryA(DWORD size, LPSTR buffer) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return 0;
    const size_t len = strlen(cwd);
    if (!buffer || size <= len) return static_cast<DWORD>(len + 1);
    memcpy(buffer, cwd, len + 1);
    return static_cast<DWORD>(len);
}

BOOL SetCurrentDirectoryW(LPCWSTR path) { return path && chdir(P(path).c_str()) == 0; }

DWORD GetTempPathW(DWORD size, LPWSTR buffer) {
    const char* tmp = getenv("TMPDIR");
    const std::wstring w = EnsureTrailingSlash(macplat::Utf8ToWide(tmp && *tmp ? tmp : "/tmp"));
    if (!buffer || size <= w.size()) return static_cast<DWORD>(w.size() + 1);
    CopyToWideBuffer(w, buffer, size);
    return static_cast<DWORD>(w.size());
}

DWORD GetFullPathNameW(LPCWSTR path, DWORD size, LPWSTR buffer, LPWSTR* filePart) {
    if (!path) return 0;
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(P(path)), ec).lexically_normal();
    const std::wstring w = macplat::Utf8ToWide(abs.string().c_str());
    if (!buffer || size <= w.size()) return static_cast<DWORD>(w.size() + 1);
    CopyToWideBuffer(w, buffer, size);
    if (filePart) {
        wchar_t* slash = wcsrchr(buffer, L'/');
        *filePart = slash ? slash + 1 : buffer;
    }
    return static_cast<DWORD>(w.size());
}

// Windows code looks for system fonts under %WINDIR%\Fonts; /System/Library/Fonts is the macOS equivalent.
UINT GetWindowsDirectoryW(LPWSTR buffer, UINT size) {
    const std::wstring w = L"/System/Library";
    if (!buffer || size <= w.size()) return static_cast<UINT>(w.size() + 1);
    CopyToWideBuffer(w, buffer, size);
    return static_cast<UINT>(w.size());
}

BOOL PathIsRelativeW(LPCWSTR path) {
    if (!path || !*path) return TRUE;
    if (path[0] == L'/' || path[0] == L'\\' || path[0] == L'~') return FALSE;
    // Accept Windows drive-qualified paths as absolute so configs from Windows behave predictably.
    if (iswalpha(path[0]) && path[1] == L':') return FALSE;
    return TRUE;
}

BOOL PathFileExistsW(LPCWSTR path) { return path && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }
BOOL PathFileExistsA(LPCSTR path) { return path && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

LPWSTR PathFindFileNameW(LPCWSTR path) {
    if (!path) return nullptr;
    const wchar_t* last = path;
    for (const wchar_t* p = path; *p; ++p)
        if ((*p == L'/' || *p == L'\\') && p[1]) last = p + 1;
    return const_cast<LPWSTR>(last);
}

LPWSTR PathFindExtensionW(LPCWSTR path) {
    if (!path) return nullptr;
    const wchar_t* name = PathFindFileNameW(path);
    const wchar_t* dot = wcsrchr(name, L'.');
    return const_cast<LPWSTR>(dot ? dot : name + wcslen(name));
}

BOOL PathRemoveFileSpecW(LPWSTR path) {
    if (!path) return FALSE;
    wchar_t* last = nullptr;
    for (wchar_t* p = path; *p; ++p)
        if (*p == L'/' || *p == L'\\') last = p;
    if (!last) return FALSE;
    *(last == path ? last + 1 : last) = 0;
    return TRUE;
}

HRESULT SHGetFolderPathW(HWND, int folder, HANDLE, DWORD, LPWSTR path) {
    if (!path) return E_INVALIDARG;
    std::string dir;
    switch (folder & ~CSIDL_FLAG_CREATE) {
    case CSIDL_PROFILE: dir = HomeDirectory(); break;
    case CSIDL_APPDATA:
    case CSIDL_LOCAL_APPDATA: dir = HomeDirectory() + "/Library/Application Support"; break;
    case CSIDL_DESKTOPDIRECTORY: dir = HomeDirectory() + "/Desktop"; break;
    default: return E_FAIL;
    }
    CopyToWideBuffer(macplat::Utf8ToWide(dir.c_str()), path, MAX_PATH);
    return S_OK;
}

int SHCreateDirectoryExW(HWND, LPCWSTR path, const SECURITY_ATTRIBUTES*) {
    if (!path) return ERROR_INVALID_PARAMETER;
    const std::filesystem::path p(P(path));
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec)) {
        SetLastError(ERROR_ALREADY_EXISTS);
        return ERROR_ALREADY_EXISTS;
    }
    if (!std::filesystem::create_directories(p, ec) || ec) {
        const DWORD err = ec.value() == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_PATH_NOT_FOUND;
        SetLastError(err);
        return static_cast<int>(err);
    }
    SetLastError(ERROR_SUCCESS);
    return ERROR_SUCCESS;
}

const KNOWNFOLDERID FOLDERID_Profile = { 0x5E6C858F, 0x0E22, 0x4760, { 0x9A, 0xFE, 0xEA, 0x33, 0x17, 0xB6, 0x71, 0x73 } };
const KNOWNFOLDERID FOLDERID_RoamingAppData = { 0x3EB685DB, 0x65F9, 0x4CF6, { 0xA0, 0x3A, 0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D } };
const KNOWNFOLDERID FOLDERID_LocalAppData = { 0xF1B32785, 0x6FBA, 0x4FCF, { 0x9D, 0x55, 0x7B, 0x8E, 0x7F, 0x15, 0x70, 0x91 } };
const KNOWNFOLDERID FOLDERID_Desktop = { 0xB4BFCC3A, 0xDB2C, 0x424C, { 0xB0, 0x29, 0x7F, 0xE9, 0x9A, 0x87, 0xC6, 0x41 } };
const KNOWNFOLDERID FOLDERID_Downloads = { 0x374DE290, 0x123F, 0x4565, { 0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B } };
const KNOWNFOLDERID FOLDERID_Pictures = { 0x33E28130, 0x4E1E, 0x4676, { 0x83, 0x5A, 0x98, 0x39, 0x5C, 0x3B, 0xC3, 0xBB } };
const KNOWNFOLDERID FOLDERID_Documents = { 0xFDD39AD0, 0x238F, 0x46AF, { 0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7 } };

HRESULT SHGetKnownFolderPath(REFKNOWNFOLDERID id, DWORD, HANDLE, PWSTR* path) {
    if (!path) return E_INVALIDARG;
    *path = nullptr;
    std::string dir;
    if (&id == &FOLDERID_Profile || memcmp(&id, &FOLDERID_Profile, sizeof(GUID)) == 0) dir = HomeDirectory();
    else if (memcmp(&id, &FOLDERID_RoamingAppData, sizeof(GUID)) == 0 || memcmp(&id, &FOLDERID_LocalAppData, sizeof(GUID)) == 0)
        dir = HomeDirectory() + "/Library/Application Support";
    else if (memcmp(&id, &FOLDERID_Desktop, sizeof(GUID)) == 0) dir = HomeDirectory() + "/Desktop";
    else if (memcmp(&id, &FOLDERID_Downloads, sizeof(GUID)) == 0) dir = HomeDirectory() + "/Downloads";
    else if (memcmp(&id, &FOLDERID_Pictures, sizeof(GUID)) == 0) dir = HomeDirectory() + "/Pictures";
    else if (memcmp(&id, &FOLDERID_Documents, sizeof(GUID)) == 0) dir = HomeDirectory() + "/Documents";
    else return E_FAIL;
    const std::wstring w = macplat::Utf8ToWide(dir.c_str());
    auto* mem = static_cast<wchar_t*>(malloc((w.size() + 1) * sizeof(wchar_t)));
    if (!mem) return E_OUTOFMEMORY;
    wmemcpy(mem, w.c_str(), w.size() + 1);
    *path = mem;
    return S_OK;
}

void CoTaskMemFree(LPVOID mem) { free(mem); }

// Named shared memory is only used by the (unsupported) OBS virtual camera.
HANDLE CreateFileMappingW(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
}
HANDLE OpenFileMappingW(DWORD, BOOL, LPCWSTR) {
    SetLastError(ERROR_FILE_NOT_FOUND);
    return nullptr;
}
LPVOID MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, SIZE_T) { return nullptr; }
BOOL UnmapViewOfFile(LPCVOID) { return FALSE; }
