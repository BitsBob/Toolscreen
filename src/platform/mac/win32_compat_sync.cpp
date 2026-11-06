// Win32 compatibility layer (macOS): kernel objects (events, waitable timers, mutexes, threads),
// waits, thread message queues, the game window's message queue, window timers, low-level keyboard
// hook emulation and SendInput.
#include "platform/mac/mac_handles.h"
#include "platform/mac/mac_internal.h"
#include "platform/mac/mac_keymap.h"
#include "platform/mac/mac_platform_bridge.h"

#include <libproc.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace macwin {

// ---------------------------------------------------------------------------------------------
// Handle registry
// ---------------------------------------------------------------------------------------------
namespace {
std::mutex g_handleMutex;
std::unordered_map<uintptr_t, std::shared_ptr<HandleObject>> g_handles;
std::atomic<uintptr_t> g_nextHandle{ 0x1000 };
} // namespace

HANDLE RegisterHandle(std::shared_ptr<HandleObject> object) {
    const uintptr_t value = g_nextHandle.fetch_add(4);
    std::lock_guard<std::mutex> lock(g_handleMutex);
    g_handles.emplace(value, std::move(object));
    return reinterpret_cast<HANDLE>(value);
}

std::shared_ptr<HandleObject> LookupHandle(HANDLE handle) {
    std::lock_guard<std::mutex> lock(g_handleMutex);
    auto it = g_handles.find(reinterpret_cast<uintptr_t>(handle));
    return it == g_handles.end() ? nullptr : it->second;
}

static bool ReleaseHandle(HANDLE handle) {
    std::shared_ptr<HandleObject> released;
    {
        std::lock_guard<std::mutex> lock(g_handleMutex);
        auto it = g_handles.find(reinterpret_cast<uintptr_t>(handle));
        if (it == g_handles.end()) return false;
        released = std::move(it->second);
        g_handles.erase(it);
    }
    return true; // `released` is destroyed outside the lock
}

} // namespace macwin

using macwin::HandleKind;
using macwin::HandleObject;

namespace {

using Clock = std::chrono::steady_clock;

struct EventObject : HandleObject {
    EventObject(bool manual, bool initial) : HandleObject(HandleKind::Event), manualReset(manual), signaled(initial) {}
    std::mutex mutex;
    std::condition_variable cv;
    bool manualReset;
    bool signaled;
};

struct TimerObject : HandleObject {
    explicit TimerObject(bool manual) : HandleObject(HandleKind::Timer), manualReset(manual) {}
    std::mutex mutex;
    std::condition_variable cv;
    bool manualReset;
    bool armed = false;
    bool signaled = false;
    Clock::time_point deadline{};
    std::chrono::milliseconds period{ 0 };
};

struct MutexObject : HandleObject {
    MutexObject() : HandleObject(HandleKind::Mutex) {}
    std::recursive_timed_mutex mutex;
};

struct ThreadObject : HandleObject {
    ThreadObject() : HandleObject(HandleKind::Thread) {}
    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;
    DWORD exitCode = STILL_ACTIVE;
    DWORD threadId = 0;
};

struct ProcessObject : HandleObject {
    explicit ProcessObject(pid_t p) : HandleObject(HandleKind::Process), pid(p) {}
    pid_t pid;
};

// Sleeps until `deadline` with sub-millisecond accuracy (the FPS limiter depends on it).
void PreciseSleepUntil(Clock::time_point deadline) {
    using namespace std::chrono;
    for (;;) {
        const auto now = Clock::now();
        if (now >= deadline) return;
        const auto remaining = deadline - now;
        if (remaining > microseconds(1500)) {
            std::this_thread::sleep_for(remaining - microseconds(1000));
        } else {
            std::this_thread::yield();
        }
    }
}

DWORD WaitOnEvent(EventObject& e, DWORD timeoutMs) {
    std::unique_lock<std::mutex> lock(e.mutex);
    const auto ready = [&] { return e.signaled; };
    if (timeoutMs == INFINITE) {
        e.cv.wait(lock, ready);
    } else if (!e.cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready)) {
        return WAIT_TIMEOUT;
    }
    if (!e.manualReset) e.signaled = false;
    return WAIT_OBJECT_0;
}

DWORD WaitOnTimer(TimerObject& t, DWORD timeoutMs) {
    const auto start = Clock::now();
    const auto limit = timeoutMs == INFINITE ? Clock::time_point::max() : start + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lock(t.mutex);
    for (;;) {
        if (t.signaled) {
            if (!t.manualReset) t.signaled = false;
            return WAIT_OBJECT_0;
        }
        if (!t.armed) {
            if (limit == Clock::time_point::max()) {
                t.cv.wait(lock);
            } else if (t.cv.wait_until(lock, limit) == std::cv_status::timeout) {
                return WAIT_TIMEOUT;
            }
            continue;
        }
        const auto deadline = t.deadline;
        if (deadline > limit) {
            lock.unlock();
            PreciseSleepUntil(limit);
            return WAIT_TIMEOUT;
        }
        lock.unlock();
        PreciseSleepUntil(deadline);
        lock.lock();
        if (t.armed && t.deadline == deadline) {
            if (t.period.count() > 0) {
                t.deadline += t.period;
            } else {
                t.armed = false;
            }
            if (t.manualReset) t.signaled = true;
            return WAIT_OBJECT_0;
        }
    }
}

DWORD WaitOnThread(ThreadObject& t, DWORD timeoutMs) {
    std::unique_lock<std::mutex> lock(t.mutex);
    const auto ready = [&] { return t.finished; };
    if (timeoutMs == INFINITE) {
        t.cv.wait(lock, ready);
        return WAIT_OBJECT_0;
    }
    return t.cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), ready) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}

bool TryAcquireSignal(const std::shared_ptr<HandleObject>& object) {
    switch (object->kind) {
    case HandleKind::Event: {
        auto& e = static_cast<EventObject&>(*object);
        std::lock_guard<std::mutex> lock(e.mutex);
        if (!e.signaled) return false;
        if (!e.manualReset) e.signaled = false;
        return true;
    }
    case HandleKind::Timer: {
        auto& t = static_cast<TimerObject&>(*object);
        std::lock_guard<std::mutex> lock(t.mutex);
        if (t.signaled) {
            if (!t.manualReset) t.signaled = false;
            return true;
        }
        if (t.armed && Clock::now() >= t.deadline) {
            if (t.period.count() > 0) t.deadline += t.period;
            else t.armed = false;
            if (t.manualReset) t.signaled = true;
            return true;
        }
        return false;
    }
    case HandleKind::Thread: {
        auto& t = static_cast<ThreadObject&>(*object);
        std::lock_guard<std::mutex> lock(t.mutex);
        return t.finished;
    }
    case HandleKind::Mutex: return static_cast<MutexObject&>(*object).mutex.try_lock();
    case HandleKind::Process: {
        const pid_t pid = static_cast<ProcessObject&>(*object).pid;
        return kill(pid, 0) != 0 && errno == ESRCH;
    }
    default: return false;
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Kernel objects
// ---------------------------------------------------------------------------------------------
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES, BOOL manualReset, BOOL initialState, LPCWSTR) {
    return macwin::RegisterHandle(std::make_shared<EventObject>(manualReset != FALSE, initialState != FALSE));
}
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES attrs, BOOL manualReset, BOOL initialState, LPCSTR) {
    return CreateEventW(attrs, manualReset, initialState, nullptr);
}

BOOL SetEvent(HANDLE event) {
    auto e = macwin::LookupHandleAs<EventObject>(event, HandleKind::Event);
    if (!e) return FALSE;
    {
        std::lock_guard<std::mutex> lock(e->mutex);
        e->signaled = true;
    }
    if (e->manualReset) e->cv.notify_all();
    else e->cv.notify_one();
    return TRUE;
}

BOOL ResetEvent(HANDLE event) {
    auto e = macwin::LookupHandleAs<EventObject>(event, HandleKind::Event);
    if (!e) return FALSE;
    std::lock_guard<std::mutex> lock(e->mutex);
    e->signaled = false;
    return TRUE;
}

HANDLE CreateWaitableTimerW(LPSECURITY_ATTRIBUTES, BOOL manualReset, LPCWSTR) {
    return macwin::RegisterHandle(std::make_shared<TimerObject>(manualReset != FALSE));
}

HANDLE CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES attrs, LPCWSTR name, DWORD flags, DWORD) {
    return CreateWaitableTimerW(attrs, (flags & CREATE_WAITABLE_TIMER_MANUAL_RESET) != 0, name);
}

BOOL SetWaitableTimer(HANDLE timer, const LARGE_INTEGER* dueTime, LONG period, void*, LPVOID, BOOL) {
    auto t = macwin::LookupHandleAs<TimerObject>(timer, HandleKind::Timer);
    if (!t || !dueTime) return FALSE;
    Clock::time_point deadline;
    if (dueTime->QuadPart < 0) {
        deadline = Clock::now() + std::chrono::nanoseconds((-dueTime->QuadPart) * 100);
    } else {
        FILETIME nowFt{};
        GetSystemTimeAsFileTime(&nowFt);
        const LONGLONG now = (static_cast<LONGLONG>(nowFt.dwHighDateTime) << 32) | nowFt.dwLowDateTime;
        const LONGLONG delta = std::max<LONGLONG>(0, dueTime->QuadPart - now);
        deadline = Clock::now() + std::chrono::nanoseconds(delta * 100);
    }
    {
        std::lock_guard<std::mutex> lock(t->mutex);
        t->armed = true;
        t->signaled = false;
        t->deadline = deadline;
        t->period = std::chrono::milliseconds(std::max<LONG>(0, period));
    }
    t->cv.notify_all();
    return TRUE;
}

BOOL CancelWaitableTimer(HANDLE timer) {
    auto t = macwin::LookupHandleAs<TimerObject>(timer, HandleKind::Timer);
    if (!t) return FALSE;
    std::lock_guard<std::mutex> lock(t->mutex);
    t->armed = false;
    return TRUE;
}

HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES, BOOL initialOwner, LPCWSTR) {
    auto m = std::make_shared<MutexObject>();
    if (initialOwner) m->mutex.lock();
    return macwin::RegisterHandle(m);
}
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES attrs, BOOL initialOwner, LPCSTR) { return CreateMutexW(attrs, initialOwner, nullptr); }

BOOL ReleaseMutex(HANDLE mutex) {
    auto m = macwin::LookupHandleAs<MutexObject>(mutex, HandleKind::Mutex);
    if (!m) return FALSE;
    m->mutex.unlock();
    return TRUE;
}

DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds) {
    std::shared_ptr<HandleObject> object = macwin::LookupHandle(handle);
    if (!object) {
        SetLastError(ERROR_INVALID_HANDLE);
        return WAIT_FAILED;
    }
    switch (object->kind) {
    case HandleKind::Event: return WaitOnEvent(static_cast<EventObject&>(*object), milliseconds);
    case HandleKind::Timer: return WaitOnTimer(static_cast<TimerObject&>(*object), milliseconds);
    case HandleKind::Thread: return WaitOnThread(static_cast<ThreadObject&>(*object), milliseconds);
    case HandleKind::Mutex: {
        auto& m = static_cast<MutexObject&>(*object);
        if (milliseconds == INFINITE) {
            m.mutex.lock();
            return WAIT_OBJECT_0;
        }
        return m.mutex.try_lock_for(std::chrono::milliseconds(milliseconds)) ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
    }
    default: break;
    }
    // Generic polling path (processes).
    const auto limit = milliseconds == INFINITE ? Clock::time_point::max() : Clock::now() + std::chrono::milliseconds(milliseconds);
    while (!TryAcquireSignal(object)) {
        if (Clock::now() >= limit) return WAIT_TIMEOUT;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return WAIT_OBJECT_0;
}

DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL waitAll, DWORD milliseconds) {
    if (!handles || count == 0) return WAIT_FAILED;
    if (count == 1) return WaitForSingleObject(handles[0], milliseconds);
    std::vector<std::shared_ptr<HandleObject>> objects;
    for (DWORD i = 0; i < count; ++i) {
        auto object = macwin::LookupHandle(handles[i]);
        if (!object) return WAIT_FAILED;
        objects.push_back(std::move(object));
    }
    const auto limit = milliseconds == INFINITE ? Clock::time_point::max() : Clock::now() + std::chrono::milliseconds(milliseconds);
    std::vector<bool> done(count, false);
    for (;;) {
        for (DWORD i = 0; i < count; ++i) {
            if (done[i]) continue;
            if (TryAcquireSignal(objects[i])) {
                if (!waitAll) return WAIT_OBJECT_0 + i;
                done[i] = true;
            }
        }
        if (waitAll && std::all_of(done.begin(), done.end(), [](bool b) { return b; })) return WAIT_OBJECT_0;
        if (Clock::now() >= limit) return WAIT_TIMEOUT;
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

HANDLE CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE start, LPVOID param, DWORD, LPDWORD threadId) {
    if (!start) return nullptr;
    auto object = std::make_shared<ThreadObject>();
    std::promise<DWORD> idPromise;
    std::future<DWORD> idFuture = idPromise.get_future();
    std::thread([object, start, param, p = std::move(idPromise)]() mutable {
        p.set_value(GetCurrentThreadId());
        const DWORD code = start(param);
        {
            std::lock_guard<std::mutex> lock(object->mutex);
            object->finished = true;
            object->exitCode = code;
        }
        object->cv.notify_all();
    }).detach();
    object->threadId = idFuture.get();
    if (threadId) *threadId = object->threadId;
    return macwin::RegisterHandle(object);
}

BOOL GetExitCodeThread(HANDLE thread, LPDWORD exitCode) {
    auto t = macwin::LookupHandleAs<ThreadObject>(thread, HandleKind::Thread);
    if (!t || !exitCode) return FALSE;
    std::lock_guard<std::mutex> lock(t->mutex);
    *exitCode = t->exitCode;
    return TRUE;
}

HANDLE OpenProcess(DWORD, BOOL, DWORD pid) {
    if (kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return macwin::RegisterHandle(std::make_shared<ProcessObject>(static_cast<pid_t>(pid)));
}

static pid_t PidForProcessHandle(HANDLE process) {
    if (process == GetCurrentProcess()) return getpid();
    auto p = macwin::LookupHandleAs<ProcessObject>(process, HandleKind::Process);
    return p ? p->pid : -1;
}

BOOL QueryFullProcessImageNameA(HANDLE process, DWORD, LPSTR name, PDWORD size) {
    const pid_t pid = PidForProcessHandle(process);
    if (pid < 0 || !name || !size) return FALSE;
    char* path = MacBridge_CopyProcessPath(pid);
    if (!path) return FALSE;
    const size_t len = strlen(path);
    const bool fits = len < *size;
    if (fits) {
        memcpy(name, path, len + 1);
        *size = static_cast<DWORD>(len);
    }
    free(path);
    return fits;
}

BOOL QueryFullProcessImageNameW(HANDLE process, DWORD flags, LPWSTR name, PDWORD size) {
    char buffer[PROC_PIDPATHINFO_MAXSIZE];
    DWORD n = sizeof(buffer);
    if (!QueryFullProcessImageNameA(process, flags, buffer, &n) || !name || !size) return FALSE;
    const std::wstring w = macplat::Utf8ToWide(buffer);
    if (w.size() >= *size) return FALSE;
    wmemcpy(name, w.c_str(), w.size() + 1);
    *size = static_cast<DWORD>(w.size());
    return TRUE;
}

BOOL CloseHandle(HANDLE handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE || handle == GetCurrentProcess() || handle == GetCurrentThread()) return TRUE;
    if (!macwin::ReleaseHandle(handle)) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return TRUE;
}

BOOL DuplicateHandle(HANDLE, HANDLE src, HANDLE, LPHANDLE dst, DWORD, BOOL, DWORD) {
    if (!dst) return FALSE;
    auto object = macwin::LookupHandle(src);
    if (!object) return FALSE;
    *dst = macwin::RegisterHandle(object);
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Thread message queues
// ---------------------------------------------------------------------------------------------
namespace {

struct ThreadQueue {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<MSG> messages;
};

std::mutex g_threadQueuesMutex;
std::unordered_map<DWORD, std::shared_ptr<ThreadQueue>> g_threadQueues;

std::shared_ptr<ThreadQueue> QueueForThread(DWORD threadId, bool create) {
    std::lock_guard<std::mutex> lock(g_threadQueuesMutex);
    auto it = g_threadQueues.find(threadId);
    if (it != g_threadQueues.end()) return it->second;
    if (!create) return nullptr;
    auto q = std::make_shared<ThreadQueue>();
    g_threadQueues.emplace(threadId, q);
    return q;
}

bool MessageMatches(const MSG& m, HWND hwnd, UINT minMsg, UINT maxMsg) {
    if (hwnd && m.hwnd != hwnd) return false;
    if (minMsg == 0 && maxMsg == 0) return true;
    return m.message >= minMsg && m.message <= maxMsg;
}

void FillMessageTimeAndPos(MSG& m) {
    m.time = GetTickCount();
    POINT pt{};
    GetCursorPos(&pt);
    m.pt = pt;
}

} // namespace

BOOL PostThreadMessageW(DWORD threadId, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto q = QueueForThread(threadId, false);
    if (!q) {
        SetLastError(1444 /* ERROR_INVALID_THREAD_ID */);
        return FALSE;
    }
    MSG m{};
    m.message = msg;
    m.wParam = wParam;
    m.lParam = lParam;
    FillMessageTimeAndPos(m);
    {
        std::lock_guard<std::mutex> lock(q->mutex);
        q->messages.push_back(m);
    }
    q->cv.notify_one();
    return TRUE;
}

BOOL GetMessageW(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax) {
    if (!msg) return -1;
    auto q = QueueForThread(GetCurrentThreadId(), true);
    std::unique_lock<std::mutex> lock(q->mutex);
    for (;;) {
        for (auto it = q->messages.begin(); it != q->messages.end(); ++it) {
            if (MessageMatches(*it, hwnd, filterMin, filterMax)) {
                *msg = *it;
                q->messages.erase(it);
                return msg->message == WM_QUIT ? FALSE : TRUE;
            }
        }
        q->cv.wait(lock);
    }
}

BOOL PeekMessageW(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax, UINT remove) {
    auto q = QueueForThread(GetCurrentThreadId(), true);
    std::lock_guard<std::mutex> lock(q->mutex);
    for (auto it = q->messages.begin(); it != q->messages.end(); ++it) {
        if (MessageMatches(*it, hwnd, filterMin, filterMax)) {
            if (msg) *msg = *it;
            if (remove & PM_REMOVE) q->messages.erase(it);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL TranslateMessage(const MSG*) { return FALSE; }

LRESULT DispatchMessageW(const MSG* msg) {
    if (!msg) return 0;
    if (msg->message == WM_TIMER && msg->lParam) {
        reinterpret_cast<TIMERPROC>(msg->lParam)(msg->hwnd, WM_TIMER, msg->wParam, msg->time);
        return 0;
    }
    if (macwin::IsGameHwnd(msg->hwnd)) return macwin::DispatchToGameWindow(msg->message, msg->wParam, msg->lParam, 0, msg->time);
    return 0;
}

void PostQuitMessage(int exitCode) { PostThreadMessageW(GetCurrentThreadId(), WM_QUIT, static_cast<WPARAM>(exitCode), 0); }

// ---------------------------------------------------------------------------------------------
// Game window procedure chain and message queue
// ---------------------------------------------------------------------------------------------
namespace {

struct PostedMessage {
    UINT msg;
    WPARAM wParam;
    LPARAM lParam;
    ULONG_PTR extraInfo;
    DWORD time;
    std::shared_ptr<std::promise<LRESULT>> reply; // set for cross-thread SendMessage
};

std::atomic<WNDPROC> g_baseWindowProc{ nullptr };
std::atomic<WNDPROC> g_currentWindowProc{ nullptr };
std::mutex g_windowQueueMutex;
std::deque<PostedMessage> g_windowQueue;

struct MessageContext {
    ULONG_PTR extraInfo = 0;
    DWORD time = 0;
};
thread_local MessageContext t_messageContext;

} // namespace

namespace macwin {

void SetBaseWindowProc(WNDPROC proc) {
    g_baseWindowProc.store(proc);
    WNDPROC expected = nullptr;
    g_currentWindowProc.compare_exchange_strong(expected, proc);
}

WNDPROC CurrentWindowProc() {
    WNDPROC proc = g_currentWindowProc.load();
    return proc ? proc : g_baseWindowProc.load();
}

WNDPROC ReplaceWindowProc(WNDPROC proc) {
    WNDPROC previous = g_currentWindowProc.exchange(proc);
    return previous ? previous : g_baseWindowProc.load();
}

LRESULT DispatchToGameWindow(UINT msg, WPARAM wParam, LPARAM lParam, ULONG_PTR extraInfo, DWORD time) {
    const HWND hwnd = GameHwnd();
    WNDPROC proc = CurrentWindowProc();
    if (!hwnd || !proc) return 0;
    const MessageContext saved = t_messageContext;
    t_messageContext.extraInfo = extraInfo;
    t_messageContext.time = time ? time : GetTickCount();
    const LRESULT result = proc(hwnd, msg, wParam, lParam);
    t_messageContext = saved;
    return result;
}

bool PostGameWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, ULONG_PTR extraInfo) {
    if (!GameHwnd()) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }
    bool wasEmpty = false;
    {
        std::lock_guard<std::mutex> lock(g_windowQueueMutex);
        wasEmpty = g_windowQueue.empty();
        g_windowQueue.push_back(PostedMessage{ msg, wParam, lParam, extraInfo, GetTickCount(), nullptr });
    }
    if (wasEmpty && !MacBridge_IsMainThread()) MacBridge_PostEmptyEvent();
    return true;
}

void PumpGameWindowMessages() {
    std::deque<PostedMessage> pending;
    {
        std::lock_guard<std::mutex> lock(g_windowQueueMutex);
        pending.swap(g_windowQueue);
    }
    for (PostedMessage& m : pending) {
        LRESULT result = 0;
        if (m.msg == WM_TIMER && m.lParam) {
            reinterpret_cast<TIMERPROC>(m.lParam)(GameHwnd(), WM_TIMER, m.wParam, m.time);
        } else {
            result = DispatchToGameWindow(m.msg, m.wParam, m.lParam, m.extraInfo, m.time);
        }
        if (m.reply) m.reply->set_value(result);
    }
}

DWORD CurrentMessageTime() { return t_messageContext.time ? t_messageContext.time : GetTickCount(); }
ULONG_PTR CurrentMessageExtraInfo() { return t_messageContext.extraInfo; }

} // namespace macwin

LRESULT CallWindowProcW(WNDPROC proc, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    return proc ? proc(hwnd, msg, wParam, lParam) : 0;
}

LRESULT DefWindowProcW(HWND, UINT msg, WPARAM, LPARAM) {
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    case WM_NCHITTEST: return HTCLIENT;
    default: return 0;
    }
}

BOOL PostMessageW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!hwnd) return PostThreadMessageW(GetCurrentThreadId(), msg, wParam, lParam);
    if (macwin::IsGameHwnd(hwnd)) return macwin::PostGameWindowMessage(msg, wParam, lParam) ? TRUE : FALSE;
    SetLastError(ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
}

static LRESULT SendToGameWindow(UINT msg, WPARAM wParam, LPARAM lParam, DWORD timeoutMs, bool* timedOut) {
    if (timedOut) *timedOut = false;
    if (GetCurrentThreadId() == macwin::GameWindowThreadId()) return macwin::DispatchToGameWindow(msg, wParam, lParam);
    auto reply = std::make_shared<std::promise<LRESULT>>();
    std::future<LRESULT> future = reply->get_future();
    {
        std::lock_guard<std::mutex> lock(g_windowQueueMutex);
        g_windowQueue.push_back(PostedMessage{ msg, wParam, lParam, 0, GetTickCount(), reply });
    }
    MacBridge_PostEmptyEvent();
    if (timeoutMs == INFINITE) return future.get();
    if (future.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready) {
        if (timedOut) *timedOut = true;
        return 0;
    }
    return future.get();
}

LRESULT SendMessageW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!macwin::IsGameHwnd(hwnd)) return 0;
    return SendToGameWindow(msg, wParam, lParam, INFINITE, nullptr);
}

LRESULT SendMessageTimeoutW(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT, UINT timeout, PDWORD_PTR result) {
    if (!macwin::IsGameHwnd(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    bool timedOut = false;
    const LRESULT r = SendToGameWindow(msg, wParam, lParam, timeout, &timedOut);
    if (timedOut) {
        SetLastError(ERROR_TIMEOUT);
        return 0;
    }
    if (result) *result = static_cast<DWORD_PTR>(r);
    return 1;
}

LPARAM GetMessageExtraInfo() { return static_cast<LPARAM>(macwin::CurrentMessageExtraInfo()); }

UINT RegisterWindowMessageW(LPCWSTR name) {
    static std::mutex mutex;
    static std::map<std::wstring, UINT> ids;
    static UINT next = 0xC100;
    if (!name) return 0;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = ids.find(name);
    if (it != ids.end()) return it->second;
    const UINT id = next++;
    ids.emplace(name, id);
    return id;
}

UINT RegisterWindowMessageA(LPCSTR name) { return name ? RegisterWindowMessageW(macplat::Utf8ToWide(name).c_str()) : 0; }

// ---------------------------------------------------------------------------------------------
// Window timers
// ---------------------------------------------------------------------------------------------
namespace {

struct WindowTimer {
    HWND hwnd;
    DWORD threadId; // for thread timers (hwnd == NULL)
    UINT_PTR id;
    std::chrono::milliseconds interval;
    Clock::time_point next;
    TIMERPROC proc;
};

std::mutex g_timerMutex;
std::condition_variable g_timerCv;
std::vector<WindowTimer> g_timers;
std::once_flag g_timerThreadOnce;
UINT_PTR g_nextThreadTimerId = 0x7F00;

void TimerThreadMain() {
    std::unique_lock<std::mutex> lock(g_timerMutex);
    for (;;) {
        if (g_timers.empty()) {
            g_timerCv.wait(lock);
            continue;
        }
        auto soonest = std::min_element(g_timers.begin(), g_timers.end(),
                                        [](const WindowTimer& a, const WindowTimer& b) { return a.next < b.next; });
        const auto when = soonest->next;
        if (g_timerCv.wait_until(lock, when) == std::cv_status::no_timeout) continue; // timers changed
        const auto now = Clock::now();
        for (WindowTimer& t : g_timers) {
            if (t.next > now) continue;
            t.next = now + t.interval;
            const LPARAM procParam = reinterpret_cast<LPARAM>(t.proc);
            if (t.hwnd) {
                macwin::PostGameWindowMessage(WM_TIMER, t.id, procParam);
            } else {
                PostThreadMessageW(t.threadId, WM_TIMER, t.id, procParam);
            }
        }
    }
}

} // namespace

UINT_PTR SetTimer(HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc) {
    std::call_once(g_timerThreadOnce, [] { std::thread(TimerThreadMain).detach(); });
    if (elapse < USER_TIMER_MINIMUM) elapse = USER_TIMER_MINIMUM;
    std::lock_guard<std::mutex> lock(g_timerMutex);
    if (!hwnd) id = g_nextThreadTimerId++;
    for (WindowTimer& t : g_timers) {
        if (t.hwnd == hwnd && t.id == id && hwnd) {
            t.interval = std::chrono::milliseconds(elapse);
            t.next = Clock::now() + t.interval;
            t.proc = proc;
            g_timerCv.notify_all();
            return id;
        }
    }
    g_timers.push_back(WindowTimer{ hwnd, GetCurrentThreadId(), id, std::chrono::milliseconds(elapse),
                                    Clock::now() + std::chrono::milliseconds(elapse), proc });
    g_timerCv.notify_all();
    return id ? id : 1;
}

BOOL KillTimer(HWND hwnd, UINT_PTR id) {
    std::lock_guard<std::mutex> lock(g_timerMutex);
    const size_t before = g_timers.size();
    g_timers.erase(std::remove_if(g_timers.begin(), g_timers.end(), [&](const WindowTimer& t) { return t.hwnd == hwnd && t.id == id; }),
                   g_timers.end());
    g_timerCv.notify_all();
    return g_timers.size() != before;
}

// ---------------------------------------------------------------------------------------------
// Physical input state
// ---------------------------------------------------------------------------------------------
namespace {
std::array<std::atomic<uint8_t>, 256> g_physicalKeys{};
}

namespace macwin {

void SetPhysicalKeyDown(uint16_t vk, bool down) {
    if (vk < 256) g_physicalKeys[vk].store(down ? 1 : 0, std::memory_order_relaxed);
}

bool IsPhysicalKeyDown(uint16_t vk) {
    switch (vk) {
    case VK_SHIFT: return IsPhysicalKeyDown(VK_LSHIFT) || IsPhysicalKeyDown(VK_RSHIFT) || g_physicalKeys[VK_SHIFT].load();
    case VK_CONTROL: return IsPhysicalKeyDown(VK_LCONTROL) || IsPhysicalKeyDown(VK_RCONTROL) || g_physicalKeys[VK_CONTROL].load();
    case VK_MENU: return IsPhysicalKeyDown(VK_LMENU) || IsPhysicalKeyDown(VK_RMENU) || g_physicalKeys[VK_MENU].load();
    default: return vk < 256 && g_physicalKeys[vk].load(std::memory_order_relaxed) != 0;
    }
}

void ResetPhysicalInputState() {
    for (auto& k : g_physicalKeys) k.store(0, std::memory_order_relaxed);
}

} // namespace macwin

// ---------------------------------------------------------------------------------------------
// WH_KEYBOARD_LL emulation
// ---------------------------------------------------------------------------------------------
namespace {
struct HookEntry {
    HHOOK handle;
    HOOKPROC proc;
};
std::mutex g_hookMutex;
std::vector<HookEntry> g_lowLevelKeyboardHooks;
std::atomic<uintptr_t> g_nextHookHandle{ 0x5100 };
} // namespace

HHOOK SetWindowsHookExW(int idHook, HOOKPROC proc, HINSTANCE, DWORD) {
    if (idHook != WH_KEYBOARD_LL || !proc) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return nullptr;
    }
    const HHOOK handle = reinterpret_cast<HHOOK>(g_nextHookHandle.fetch_add(4));
    std::lock_guard<std::mutex> lock(g_hookMutex);
    g_lowLevelKeyboardHooks.insert(g_lowLevelKeyboardHooks.begin(), HookEntry{ handle, proc });
    return handle;
}

BOOL UnhookWindowsHookEx(HHOOK hook) {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    const size_t before = g_lowLevelKeyboardHooks.size();
    g_lowLevelKeyboardHooks.erase(std::remove_if(g_lowLevelKeyboardHooks.begin(), g_lowLevelKeyboardHooks.end(),
                                                 [&](const HookEntry& e) { return e.handle == hook; }),
                                  g_lowLevelKeyboardHooks.end());
    return g_lowLevelKeyboardHooks.size() != before;
}

LRESULT CallNextHookEx(HHOOK, int, WPARAM, LPARAM) { return 0; }

namespace macwin {

bool HasLowLevelKeyboardHooks() {
    std::lock_guard<std::mutex> lock(g_hookMutex);
    return !g_lowLevelKeyboardHooks.empty();
}

bool RunLowLevelKeyboardHooks(WPARAM message, const KBDLLHOOKSTRUCT& info) {
    std::vector<HookEntry> hooks;
    {
        std::lock_guard<std::mutex> lock(g_hookMutex);
        hooks = g_lowLevelKeyboardHooks;
    }
    KBDLLHOOKSTRUCT copy = info;
    for (const HookEntry& h : hooks) {
        if (h.proc(HC_ACTION, message, reinterpret_cast<LPARAM>(&copy)) != 0) return true;
    }
    return false;
}

} // namespace macwin

// ---------------------------------------------------------------------------------------------
// Raw input records
// ---------------------------------------------------------------------------------------------
namespace {
constexpr size_t kRawRing = 64;
thread_local std::array<RAWINPUT, kRawRing> t_rawRing{};
thread_local size_t t_rawRingNext = 0;
} // namespace

namespace macwin {
HRAWINPUT MakeRawMouseInput(LONG dx, LONG dy, USHORT buttonFlags, USHORT buttonData) {
    RAWINPUT& r = t_rawRing[t_rawRingNext++ % kRawRing];
    memset(&r, 0, sizeof(r));
    r.header.dwType = RIM_TYPEMOUSE;
    r.header.dwSize = sizeof(RAWINPUT);
    r.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
    r.data.mouse.lLastX = dx;
    r.data.mouse.lLastY = dy;
    r.data.mouse.usButtonFlags = buttonFlags;
    r.data.mouse.usButtonData = buttonData;
    return reinterpret_cast<HRAWINPUT>(&r);
}
} // namespace macwin

UINT GetRawInputData(HRAWINPUT rawInput, UINT command, LPVOID data, PUINT size, UINT headerSize) {
    if (!rawInput || !size || headerSize != sizeof(RAWINPUTHEADER)) return static_cast<UINT>(-1);
    const RAWINPUT* r = reinterpret_cast<const RAWINPUT*>(rawInput);
    const UINT needed = command == RID_HEADER ? sizeof(RAWINPUTHEADER) : sizeof(RAWINPUT);
    if (!data) {
        *size = needed;
        return 0;
    }
    if (*size < needed) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return static_cast<UINT>(-1);
    }
    memcpy(data, r, needed);
    return needed;
}

BOOL RegisterRawInputDevices(const RAWINPUTDEVICE*, UINT, UINT) { return TRUE; }

// ---------------------------------------------------------------------------------------------
// SendInput: injected events reach the game window like Windows' input queue would deliver them.
// ---------------------------------------------------------------------------------------------
namespace {
std::atomic<bool> g_injectedAltDown{ false };

LPARAM BuildKeyLParam(uint16_t scan, bool up, bool altDown) {
    LPARAM lp = 1;
    lp |= static_cast<LPARAM>(scan & 0xFF) << 16;
    if (scan & mackeys::kExtendedFlag) lp |= static_cast<LPARAM>(1) << 24;
    if (altDown) lp |= static_cast<LPARAM>(1) << 29;
    if (up) lp |= (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);
    return lp;
}

void PostClientMouseMessage(UINT msg, WPARAM wParam, ULONG_PTR extra) {
    POINT pt{};
    GetCursorPos(&pt);
    ScreenToClient(macwin::GameHwnd(), &pt);
    macwin::PostGameWindowMessage(msg, wParam, MAKELPARAM(pt.x, pt.y), extra);
}

WPARAM CurrentMouseKeyFlags() {
    WPARAM flags = 0;
    if (macwin::IsPhysicalKeyDown(VK_LBUTTON)) flags |= MK_LBUTTON;
    if (macwin::IsPhysicalKeyDown(VK_RBUTTON)) flags |= MK_RBUTTON;
    if (macwin::IsPhysicalKeyDown(VK_MBUTTON)) flags |= MK_MBUTTON;
    if (macwin::IsPhysicalKeyDown(VK_XBUTTON1)) flags |= MK_XBUTTON1;
    if (macwin::IsPhysicalKeyDown(VK_XBUTTON2)) flags |= MK_XBUTTON2;
    if (macwin::IsPhysicalKeyDown(VK_SHIFT)) flags |= MK_SHIFT;
    if (macwin::IsPhysicalKeyDown(VK_CONTROL)) flags |= MK_CONTROL;
    return flags;
}

void InjectKeyboard(const KEYBDINPUT& ki) {
    const bool up = (ki.dwFlags & KEYEVENTF_KEYUP) != 0;
    if (ki.dwFlags & KEYEVENTF_UNICODE) {
        if (!up) macwin::PostGameWindowMessage(WM_CHAR, ki.wScan, 1, ki.dwExtraInfo);
        return;
    }
    const mackeys::KeyEntry* entry = nullptr;
    uint16_t scan = 0;
    if (ki.dwFlags & KEYEVENTF_SCANCODE) {
        scan = static_cast<uint16_t>(ki.wScan & 0xFF);
        if (ki.dwFlags & KEYEVENTF_EXTENDEDKEY) scan |= mackeys::kExtendedFlag;
        entry = mackeys::FindByScan(scan);
    } else {
        entry = mackeys::FindByVk(ki.wVk);
        if (entry) scan = entry->scan;
        if ((ki.dwFlags & KEYEVENTF_EXTENDEDKEY) && entry) {
            if (const mackeys::KeyEntry* ext = mackeys::FindByScan(static_cast<uint16_t>(entry->scan | mackeys::kExtendedFlag))) {
                if (ext->vk == entry->vk) {
                    entry = ext;
                    scan = ext->scan;
                }
            }
        }
    }
    const uint16_t vk = entry ? entry->vk : ki.wVk;
    const uint16_t sidedVk = entry ? entry->sidedVk : ki.wVk;
    if (vk == VK_MENU) g_injectedAltDown.store(!up);
    const bool altDown = g_injectedAltDown.load() || macwin::IsPhysicalKeyDown(VK_MENU);
    const bool sys = altDown || vk == VK_F10;
    const UINT msg = up ? (sys ? WM_SYSKEYUP : WM_KEYUP) : (sys ? WM_SYSKEYDOWN : WM_KEYDOWN);

    KBDLLHOOKSTRUCT info{};
    info.vkCode = sidedVk;
    info.scanCode = scan & 0xFF;
    info.flags = LLKHF_INJECTED | ((scan & mackeys::kExtendedFlag) ? LLKHF_EXTENDED : 0) | (up ? LLKHF_UP : 0) |
                 (altDown ? LLKHF_ALTDOWN : 0);
    info.time = GetTickCount();
    info.dwExtraInfo = ki.dwExtraInfo;
    if (macwin::RunLowLevelKeyboardHooks(msg, info)) return;

    // Injected input updates the async key state, as on Windows.
    macwin::SetPhysicalKeyDown(sidedVk, !up);
    macwin::SetPhysicalKeyDown(vk, !up);
    macwin::PostGameWindowMessage(msg, vk, BuildKeyLParam(scan, up, altDown), ki.dwExtraInfo);

    // TranslateMessage equivalent for injected key downs.
    if (!up && entry && entry->macKeyCode >= 0 && !macwin::IsPhysicalKeyDown(VK_CONTROL) && !macwin::IsPhysicalKeyDown(VK_LWIN) &&
        !macwin::IsPhysicalKeyDown(VK_RWIN)) {
        uint32_t chars[4];
        const int n = MacBridge_TranslateKeyCode(static_cast<uint16_t>(entry->macKeyCode), macwin::IsPhysicalKeyDown(VK_SHIFT),
                                                 altDown, chars, 4);
        for (int i = 0; i < n; ++i) {
            if (chars[i] < 0x20 || chars[i] == 0x7F || (chars[i] >= 0xF700 && chars[i] <= 0xF8FF)) continue;
            macwin::PostGameWindowMessage(sys ? WM_SYSCHAR : WM_CHAR, chars[i], BuildKeyLParam(scan, false, altDown), ki.dwExtraInfo);
        }
    }
}

void InjectMouse(const MOUSEINPUT& mi) {
    const ULONG_PTR extra = mi.dwExtraInfo;
    struct ButtonMap {
        DWORD flag;
        UINT msg;
        int vk;
        bool down;
    };
    static const ButtonMap kButtons[] = {
        { MOUSEEVENTF_LEFTDOWN, WM_LBUTTONDOWN, VK_LBUTTON, true },   { MOUSEEVENTF_LEFTUP, WM_LBUTTONUP, VK_LBUTTON, false },
        { MOUSEEVENTF_RIGHTDOWN, WM_RBUTTONDOWN, VK_RBUTTON, true },  { MOUSEEVENTF_RIGHTUP, WM_RBUTTONUP, VK_RBUTTON, false },
        { MOUSEEVENTF_MIDDLEDOWN, WM_MBUTTONDOWN, VK_MBUTTON, true }, { MOUSEEVENTF_MIDDLEUP, WM_MBUTTONUP, VK_MBUTTON, false },
    };
    for (const ButtonMap& b : kButtons) {
        if (!(mi.dwFlags & b.flag)) continue;
        macwin::SetPhysicalKeyDown(static_cast<uint16_t>(b.vk), b.down);
        PostClientMouseMessage(b.msg, CurrentMouseKeyFlags(), extra);
    }
    if (mi.dwFlags & (MOUSEEVENTF_XDOWN | MOUSEEVENTF_XUP)) {
        const bool down = (mi.dwFlags & MOUSEEVENTF_XDOWN) != 0;
        const WORD which = (mi.mouseData & XBUTTON2) ? XBUTTON2 : XBUTTON1;
        macwin::SetPhysicalKeyDown(which == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1, down);
        PostClientMouseMessage(down ? WM_XBUTTONDOWN : WM_XBUTTONUP, MAKEWPARAM(CurrentMouseKeyFlags(), which), extra);
    }
    if (mi.dwFlags & MOUSEEVENTF_WHEEL) {
        POINT pt{};
        GetCursorPos(&pt);
        macwin::PostGameWindowMessage(WM_MOUSEWHEEL, MAKEWPARAM(CurrentMouseKeyFlags(), static_cast<short>(mi.mouseData)),
                                      MAKELPARAM(pt.x, pt.y), extra);
    }
    if (mi.dwFlags & MOUSEEVENTF_HWHEEL) {
        POINT pt{};
        GetCursorPos(&pt);
        macwin::PostGameWindowMessage(WM_MOUSEHWHEEL, MAKEWPARAM(CurrentMouseKeyFlags(), static_cast<short>(mi.mouseData)),
                                      MAKELPARAM(pt.x, pt.y), extra);
    }
    if ((mi.dwFlags & MOUSEEVENTF_MOVE) && !(mi.dwFlags & MOUSEEVENTF_ABSOLUTE)) {
        POINT pt{};
        GetCursorPos(&pt);
        SetCursorPos(pt.x + mi.dx, pt.y + mi.dy);
    }
}

} // namespace

UINT SendInput(UINT count, LPINPUT inputs, int size) {
    if (!inputs || size != static_cast<int>(sizeof(INPUT))) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (!macwin::GameHwnd()) return 0;
    for (UINT i = 0; i < count; ++i) {
        if (inputs[i].type == INPUT_KEYBOARD) InjectKeyboard(inputs[i].ki);
        else if (inputs[i].type == INPUT_MOUSE) InjectMouse(inputs[i].mi);
    }
    return count;
}
