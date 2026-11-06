// Win32 compatibility layer (macOS): the game window model, geometry/monitors, cursor and keyboard
// queries, dialogs, clipboard, shell integration, GDI stubs and WGL queries.
#include "platform/mac/mac_internal.h"
#include "platform/mac/mac_keymap.h"
#include "platform/mac/mac_platform_bridge.h"

#include <OpenGL/OpenGL.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------
// Game window model
// ---------------------------------------------------------------------------------------------
namespace {
std::atomic<void*> g_gameGlfwWindow{ nullptr };
std::atomic<void*> g_gameNSWindow{ nullptr };
std::atomic<DWORD> g_gameWindowThread{ 0 };
std::atomic<int> g_fbWidth{ 0 };
std::atomic<int> g_fbHeight{ 0 };
std::mutex g_propMutex;
std::map<std::wstring, HANDLE> g_windowProps;
std::atomic<LONG_PTR> g_userData{ 0 };
std::atomic<LONG> g_windowStyle{ static_cast<LONG>(WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN) };
std::atomic<LONG> g_windowExStyle{ static_cast<LONG>(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE) };
std::atomic<HWND> g_captureWindow{ nullptr };
} // namespace

namespace macwin {

void SetGameWindow(void* glfwWindow, void* nsWindow, uint32_t ownerThreadId) {
    g_gameGlfwWindow.store(glfwWindow);
    g_gameNSWindow.store(nsWindow);
    g_gameWindowThread.store(ownerThreadId);
}

void ClearGameWindow(void* glfwWindow) {
    void* expected = glfwWindow;
    if (g_gameGlfwWindow.compare_exchange_strong(expected, nullptr)) {
        g_gameNSWindow.store(nullptr);
        if (MacBridge_IsMainThread()) MacBridge_RefreshWindowCache(nullptr);
    }
}

HWND GameHwnd() { return static_cast<HWND>(g_gameGlfwWindow.load()); }
void* GameNSWindow() { return g_gameNSWindow.load(); }
DWORD GameWindowThreadId() { return g_gameWindowThread.load(); }

HWND ForeignHwnd(uint32_t windowId) { return reinterpret_cast<HWND>((static_cast<uintptr_t>(windowId) << 1) | 1u); }
bool IsForeignHwnd(HWND hwnd) { return (reinterpret_cast<uintptr_t>(hwnd) & 1u) != 0; }
uint32_t ForeignWindowId(HWND hwnd) { return IsForeignHwnd(hwnd) ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hwnd) >> 1) : 0; }
HWND DesktopHwnd() { return reinterpret_cast<HWND>(static_cast<uintptr_t>(0x10)); }

void SetFramebufferSize(int width, int height) {
    g_fbWidth.store(width);
    g_fbHeight.store(height);
}

bool GetFramebufferSize(int& width, int& height) {
    width = g_fbWidth.load();
    height = g_fbHeight.load();
    return width > 0 && height > 0;
}

void RefreshWindowGeometry() {
    if (!MacBridge_IsMainThread()) return;
    MacBridge_RefreshWindowCache(GameNSWindow());
}

} // namespace macwin

using macwin::GameHwnd;
using macwin::IsGameHwnd;

namespace {

bool ForeignInfo(HWND hwnd, MacForeignWindowInfo& info) {
    const uint32_t id = macwin::ForeignWindowId(hwnd);
    return id != 0 && MacBridge_GetWindowInfo(id, &info);
}

RECT ToRect(const MacRect& r) { return RECT{ r.left, r.top, r.right, r.bottom }; }

bool GameClientOriginInScreen(POINT& origin) {
    MacRect r{};
    if (!MacBridge_GetCachedClientRectInScreen(&r)) return false;
    origin.x = r.left;
    origin.y = r.top;
    return true;
}

} // namespace

BOOL IsWindow(HWND hwnd) {
    if (!hwnd) return FALSE;
    if (IsGameHwnd(hwnd) || hwnd == macwin::DesktopHwnd()) return TRUE;
    MacForeignWindowInfo info{};
    return ForeignInfo(hwnd, info);
}

BOOL IsWindowVisible(HWND hwnd) {
    if (IsGameHwnd(hwnd)) return MacBridge_IsCachedWindowVisible();
    MacForeignWindowInfo info{};
    return ForeignInfo(hwnd, info) && info.onScreen;
}

BOOL IsIconic(HWND hwnd) { return IsGameHwnd(hwnd) && MacBridge_IsCachedWindowMinimized(); }
BOOL IsZoomed(HWND hwnd) { return IsGameHwnd(hwnd) && MacBridge_IsCachedWindowZoomed(); }
BOOL IsWindowEnabled(HWND hwnd) { return IsWindow(hwnd); }

HWND GetForegroundWindow() { return MacBridge_IsCachedWindowForeground() ? GameHwnd() : nullptr; }
HWND GetActiveWindow() { return GetForegroundWindow(); }
HWND GetFocus() { return GetForegroundWindow(); }

BOOL SetForegroundWindow(HWND hwnd) {
    if (!IsGameHwnd(hwnd)) return FALSE;
    MacBridge_ActivateGameWindow();
    return TRUE;
}

HWND SetFocus(HWND hwnd) {
    const HWND previous = GetFocus();
    SetForegroundWindow(hwnd);
    return previous;
}

HWND GetDesktopWindow() { return macwin::DesktopHwnd(); }
HWND GetAncestor(HWND hwnd, UINT) { return hwnd; }
HWND GetParent(HWND) { return nullptr; }
HWND GetWindow(HWND, UINT) { return nullptr; }
HWND FindWindowW(LPCWSTR, LPCWSTR) { return nullptr; }

BOOL EnumWindows(WNDENUMPROC proc, LPARAM lParam) {
    if (!proc) return FALSE;
    if (const HWND game = GameHwnd()) {
        if (!proc(game, lParam)) return TRUE;
    }
    std::vector<MacForeignWindowInfo> windows(512);
    const int count = MacBridge_ListWindows(windows.data(), static_cast<int>(windows.size()));
    const uint32_t gameNumber = MacBridge_GetCachedWindowNumber();
    for (int i = 0; i < count; ++i) {
        if (windows[i].windowId == gameNumber) continue;
        if (!proc(macwin::ForeignHwnd(windows[i].windowId), lParam)) break;
    }
    return TRUE;
}

DWORD GetWindowThreadProcessId(HWND hwnd, LPDWORD processId) {
    if (IsGameHwnd(hwnd)) {
        if (processId) *processId = GetCurrentProcessId();
        const DWORD tid = macwin::GameWindowThreadId();
        return tid ? tid : 1;
    }
    MacForeignWindowInfo info{};
    if (ForeignInfo(hwnd, info)) {
        if (processId) *processId = static_cast<DWORD>(info.ownerPid);
        return 1;
    }
    if (processId) *processId = 0;
    return 0;
}

static std::wstring WindowTitle(HWND hwnd) {
    if (IsGameHwnd(hwnd)) return L"Minecraft";
    MacForeignWindowInfo info{};
    if (!ForeignInfo(hwnd, info)) return {};
    std::string title = info.title;
    if (title.empty()) title = info.ownerName;
    return macplat::Utf8ToWide(title.c_str());
}

int GetWindowTextW(HWND hwnd, LPWSTR buffer, int size) {
    if (!buffer || size <= 0) return 0;
    const std::wstring title = WindowTitle(hwnd);
    const size_t n = std::min<size_t>(title.size(), static_cast<size_t>(size - 1));
    wmemcpy(buffer, title.data(), n);
    buffer[n] = 0;
    return static_cast<int>(n);
}

int GetWindowTextA(HWND hwnd, LPSTR buffer, int size) {
    if (!buffer || size <= 0) return 0;
    const std::string title = macplat::WideToUtf8(WindowTitle(hwnd).c_str());
    const size_t n = std::min<size_t>(title.size(), static_cast<size_t>(size - 1));
    memcpy(buffer, title.data(), n);
    buffer[n] = 0;
    return static_cast<int>(n);
}

int GetWindowTextLengthW(HWND hwnd) { return static_cast<int>(WindowTitle(hwnd).size()); }

// Minecraft's GLFW window answers to GLFW's Win32 class name so window-matching code works.
static std::wstring WindowClass(HWND hwnd) {
    if (IsGameHwnd(hwnd)) return L"GLFW30";
    MacForeignWindowInfo info{};
    if (ForeignInfo(hwnd, info)) return macplat::Utf8ToWide(info.ownerName);
    return {};
}

int GetClassNameW(HWND hwnd, LPWSTR buffer, int size) {
    if (!buffer || size <= 0) return 0;
    const std::wstring cls = WindowClass(hwnd);
    const size_t n = std::min<size_t>(cls.size(), static_cast<size_t>(size - 1));
    wmemcpy(buffer, cls.data(), n);
    buffer[n] = 0;
    return static_cast<int>(n);
}

int GetClassNameA(HWND hwnd, LPSTR buffer, int size) {
    if (!buffer || size <= 0) return 0;
    const std::string cls = macplat::WideToUtf8(WindowClass(hwnd).c_str());
    const size_t n = std::min<size_t>(cls.size(), static_cast<size_t>(size - 1));
    memcpy(buffer, cls.data(), n);
    buffer[n] = 0;
    return static_cast<int>(n);
}

HANDLE GetPropW(HWND hwnd, LPCWSTR name) {
    if (!IsGameHwnd(hwnd) || !name) return nullptr;
    std::lock_guard<std::mutex> lock(g_propMutex);
    auto it = g_windowProps.find(name);
    return it == g_windowProps.end() ? nullptr : it->second;
}

BOOL SetPropW(HWND hwnd, LPCWSTR name, HANDLE data) {
    if (!IsGameHwnd(hwnd) || !name) return FALSE;
    std::lock_guard<std::mutex> lock(g_propMutex);
    g_windowProps[name] = data;
    return TRUE;
}

HANDLE RemovePropW(HWND hwnd, LPCWSTR name) {
    if (!IsGameHwnd(hwnd) || !name) return nullptr;
    std::lock_guard<std::mutex> lock(g_propMutex);
    auto it = g_windowProps.find(name);
    if (it == g_windowProps.end()) return nullptr;
    HANDLE value = it->second;
    g_windowProps.erase(it);
    return value;
}

LONG_PTR SetWindowLongPtrW(HWND hwnd, int index, LONG_PTR value) {
    if (!IsGameHwnd(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return 0;
    }
    switch (index) {
    case GWLP_WNDPROC: return reinterpret_cast<LONG_PTR>(macwin::ReplaceWindowProc(reinterpret_cast<WNDPROC>(value)));
    case GWLP_USERDATA: return g_userData.exchange(value);
    case GWL_STYLE: return g_windowStyle.exchange(static_cast<LONG>(value));
    case GWL_EXSTYLE: return g_windowExStyle.exchange(static_cast<LONG>(value));
    default: return 0;
    }
}

LONG_PTR GetWindowLongPtrW(HWND hwnd, int index) {
    if (!IsGameHwnd(hwnd)) return 0;
    switch (index) {
    case GWLP_WNDPROC: return reinterpret_cast<LONG_PTR>(macwin::CurrentWindowProc());
    case GWLP_USERDATA: return g_userData.load();
    case GWL_STYLE: return g_windowStyle.load();
    case GWL_EXSTYLE: return g_windowExStyle.load();
    case GWLP_HINSTANCE: return reinterpret_cast<LONG_PTR>(GetModuleHandleW(nullptr));
    default: return 0;
    }
}

LONG GetWindowLongW(HWND hwnd, int index) { return static_cast<LONG>(GetWindowLongPtrW(hwnd, index)); }
LONG SetWindowLongW(HWND hwnd, int index, LONG value) { return static_cast<LONG>(SetWindowLongPtrW(hwnd, index, value)); }
ULONG_PTR GetClassLongPtrW(HWND, int) { return 0; }

BOOL GetClientRect(HWND hwnd, LPRECT rect) {
    if (!rect) return FALSE;
    if (IsGameHwnd(hwnd)) {
        int w = 0, h = 0;
        if (!macwin::GetFramebufferSize(w, h)) {
            MacRect r{};
            if (!MacBridge_GetCachedClientRectInScreen(&r)) return FALSE;
            w = r.right - r.left;
            h = r.bottom - r.top;
        }
        *rect = RECT{ 0, 0, w, h };
        return TRUE;
    }
    if (hwnd == macwin::DesktopHwnd()) {
        MacRect frame{};
        if (!MacBridge_GetMonitorRect(0, &frame, nullptr)) return FALSE;
        *rect = RECT{ 0, 0, frame.right - frame.left, frame.bottom - frame.top };
        return TRUE;
    }
    MacForeignWindowInfo info{};
    if (!ForeignInfo(hwnd, info)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    *rect = RECT{ 0, 0, info.frame.right - info.frame.left, info.frame.bottom - info.frame.top };
    return TRUE;
}

BOOL GetWindowRect(HWND hwnd, LPRECT rect) {
    if (!rect) return FALSE;
    if (IsGameHwnd(hwnd)) {
        MacRect r{};
        if (!MacBridge_GetCachedWindowRectInScreen(&r)) return FALSE;
        *rect = ToRect(r);
        return TRUE;
    }
    if (hwnd == macwin::DesktopHwnd()) {
        MacRect frame{};
        if (!MacBridge_GetMonitorRect(0, &frame, nullptr)) return FALSE;
        *rect = ToRect(frame);
        return TRUE;
    }
    MacForeignWindowInfo info{};
    if (!ForeignInfo(hwnd, info)) return FALSE;
    *rect = ToRect(info.frame);
    return TRUE;
}

BOOL ClientToScreen(HWND hwnd, LPPOINT point) {
    if (!point) return FALSE;
    POINT origin{};
    if (IsGameHwnd(hwnd)) {
        if (!GameClientOriginInScreen(origin)) return FALSE;
    } else {
        MacForeignWindowInfo info{};
        if (!ForeignInfo(hwnd, info)) return FALSE;
        origin = POINT{ info.frame.left, info.frame.top };
    }
    point->x += origin.x;
    point->y += origin.y;
    return TRUE;
}

BOOL ScreenToClient(HWND hwnd, LPPOINT point) {
    if (!point) return FALSE;
    POINT origin{};
    if (IsGameHwnd(hwnd)) {
        if (!GameClientOriginInScreen(origin)) return FALSE;
    } else {
        MacForeignWindowInfo info{};
        if (!ForeignInfo(hwnd, info)) return FALSE;
        origin = POINT{ info.frame.left, info.frame.top };
    }
    point->x -= origin.x;
    point->y -= origin.y;
    return TRUE;
}

int MapWindowPoints(HWND from, HWND to, LPPOINT points, UINT count) {
    for (UINT i = 0; i < count; ++i) {
        if (from) ClientToScreen(from, &points[i]);
        if (to) ScreenToClient(to, &points[i]);
    }
    return 0;
}

BOOL AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD) { return TRUE; }

// The macOS port does not reposition Minecraft's window: modes are applied by resizing the game's
// framebuffer inside an unchanged window (exactly like fullscreen on Windows).
BOOL SetWindowPos(HWND hwnd, HWND, int, int, int, int, UINT) { return IsWindow(hwnd); }
BOOL MoveWindow(HWND hwnd, int, int, int, int, BOOL) { return IsWindow(hwnd); }
BOOL ShowWindow(HWND hwnd, int) { return IsWindowVisible(hwnd); }

BOOL GetWindowPlacement(HWND hwnd, WINDOWPLACEMENT* placement) {
    if (!placement || !IsGameHwnd(hwnd)) return FALSE;
    RECT r{};
    GetWindowRect(hwnd, &r);
    placement->flags = 0;
    placement->showCmd = IsIconic(hwnd) ? SW_SHOWMINIMIZED : (IsZoomed(hwnd) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    placement->rcNormalPosition = r;
    return TRUE;
}

BOOL SetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT*) { return IsGameHwnd(hwnd); }
BOOL InvalidateRect(HWND, const RECT*, BOOL) { return TRUE; }
BOOL UpdateWindow(HWND) { return TRUE; }
BOOL RedrawWindow(HWND, const RECT*, HRGN, UINT) { return TRUE; }
BOOL DestroyWindow(HWND) { return FALSE; }
HWND WindowFromDC(HDC dc) { return reinterpret_cast<HWND>(dc); }

HWND WindowFromPoint(POINT point) {
    const HWND game = GameHwnd();
    POINT p = point;
    RECT client{};
    if (game && ScreenToClient(game, &p) && GetClientRect(game, &client) && p.x >= 0 && p.y >= 0 && p.x < client.right &&
        p.y < client.bottom) {
        return game;
    }
    return nullptr;
}

ATOM RegisterClassExW(const WNDCLASSEXW*) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}
BOOL UnregisterClassW(LPCWSTR, HINSTANCE) { return FALSE; }
HWND CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Monitors / metrics / system parameters
// ---------------------------------------------------------------------------------------------
static HMONITOR MonitorHandle(int index) { return index < 0 ? nullptr : reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(index + 1)); }
static int MonitorIndex(HMONITOR monitor) { return monitor ? static_cast<int>(reinterpret_cast<uintptr_t>(monitor)) - 1 : -1; }

HMONITOR MonitorFromWindow(HWND hwnd, DWORD flags) {
    if (IsGameHwnd(hwnd)) return MonitorHandle(MacBridge_GetMonitorIndexForCachedWindow());
    RECT r{};
    if (GetWindowRect(hwnd, &r)) return MonitorFromRect(&r, flags);
    return flags == MONITOR_DEFAULTTONULL ? nullptr : MonitorHandle(0);
}

HMONITOR MonitorFromPoint(POINT point, DWORD flags) {
    const int index = MacBridge_GetMonitorIndexForPoint(point.x, point.y);
    if (index >= 0) return MonitorHandle(index);
    return flags == MONITOR_DEFAULTTONULL ? nullptr : MonitorHandle(0);
}

HMONITOR MonitorFromRect(LPCRECT rect, DWORD flags) {
    if (!rect) return MonitorHandle(0);
    POINT center{ (rect->left + rect->right) / 2, (rect->top + rect->bottom) / 2 };
    return MonitorFromPoint(center, flags);
}

BOOL GetMonitorInfoW(HMONITOR monitor, LPMONITORINFO info) {
    if (!info) return FALSE;
    MacRect frame{}, work{};
    const int index = MonitorIndex(monitor);
    if (!MacBridge_GetMonitorRect(index, &frame, &work)) return FALSE;
    info->rcMonitor = ToRect(frame);
    info->rcWork = ToRect(work);
    info->dwFlags = index == 0 ? MONITORINFOF_PRIMARY : 0;
    if (info->cbSize >= sizeof(MONITORINFOEXW)) {
        auto* ex = reinterpret_cast<MONITORINFOEXW*>(info);
        swprintf(ex->szDevice, 32, L"\\\\.\\DISPLAY%d", index + 1);
    }
    return TRUE;
}

int GetSystemMetrics(int index) {
    MacRect primary{};
    MacBridge_GetMonitorRect(0, &primary, nullptr);
    switch (index) {
    case SM_CXSCREEN: return primary.right - primary.left;
    case SM_CYSCREEN: return primary.bottom - primary.top;
    case SM_CMONITORS: return std::max(1, MacBridge_GetMonitorCount());
    case SM_XVIRTUALSCREEN:
    case SM_YVIRTUALSCREEN:
    case SM_CXVIRTUALSCREEN:
    case SM_CYVIRTUALSCREEN: {
        RECT u{ 0, 0, 0, 0 };
        const int n = MacBridge_GetMonitorCount();
        for (int i = 0; i < n; ++i) {
            MacRect f{};
            if (!MacBridge_GetMonitorRect(i, &f, nullptr)) continue;
            if (i == 0) u = ToRect(f);
            u.left = std::min(u.left, f.left);
            u.top = std::min(u.top, f.top);
            u.right = std::max(u.right, f.right);
            u.bottom = std::max(u.bottom, f.bottom);
        }
        if (index == SM_XVIRTUALSCREEN) return u.left;
        if (index == SM_YVIRTUALSCREEN) return u.top;
        if (index == SM_CXVIRTUALSCREEN) return u.right - u.left;
        return u.bottom - u.top;
    }
    case SM_CXCURSOR:
    case SM_CYCURSOR: return static_cast<int>(32 * MacBridge_GetCachedBackingScale());
    case SM_CXDOUBLECLK:
    case SM_CYDOUBLECLK: return 4;
    case SM_SWAPBUTTON: return 0;
    default: return 0;
    }
}

int GetSystemMetricsForDpi(int index, UINT) { return GetSystemMetrics(index); }
UINT GetDpiForWindow(HWND) { return static_cast<UINT>(USER_DEFAULT_SCREEN_DPI * MacBridge_GetCachedBackingScale()); }
UINT GetDpiForSystem() { return GetDpiForWindow(nullptr); }
BOOL SetProcessDPIAware() { return TRUE; }

BOOL SystemParametersInfoW(UINT action, UINT, PVOID data, UINT) {
    switch (action) {
    case SPI_GETMOUSESPEED:
        // Toolscreen never changes the macOS pointer speed; report Windows' neutral speed.
        if (data) *static_cast<int*>(data) = 10;
        return TRUE;
    case SPI_SETMOUSESPEED: return TRUE;
    case SPI_GETKEYBOARDDELAY: {
        int delayMs = 250, repeatMs = 33;
        MacBridge_GetKeyRepeatTimings(&delayMs, &repeatMs);
        if (data) *static_cast<int*>(data) = std::clamp((delayMs - 250 + 125) / 250, 0, 3);
        return TRUE;
    }
    case SPI_GETKEYBOARDSPEED: {
        int delayMs = 250, repeatMs = 33;
        MacBridge_GetKeyRepeatTimings(&delayMs, &repeatMs);
        // Windows maps 0..31 to ~2.5..30 repeats per second.
        const double hz = repeatMs > 0 ? 1000.0 / repeatMs : 30.0;
        if (data) *static_cast<DWORD*>(data) = static_cast<DWORD>(std::clamp((hz - 2.5) / (27.5 / 31.0), 0.0, 31.0));
        return TRUE;
    }
    case SPI_SETKEYBOARDDELAY:
    case SPI_SETKEYBOARDSPEED:
    case SPI_SETFILTERKEYS: return TRUE; // system keyboard settings are left untouched on macOS
    case SPI_GETFILTERKEYS:
        if (data) {
            auto* fk = static_cast<FILTERKEYS*>(data);
            const UINT size = fk->cbSize;
            memset(fk, 0, sizeof(FILTERKEYS));
            fk->cbSize = size;
        }
        return TRUE;
    case SPI_GETWORKAREA: {
        MacRect work{};
        if (!MacBridge_GetMonitorRect(0, nullptr, &work) || !data) return FALSE;
        *static_cast<RECT*>(data) = ToRect(work);
        return TRUE;
    }
    default: return FALSE;
    }
}

// ---------------------------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------------------------
namespace {
std::atomic<HCURSOR> g_currentCursor{ nullptr };
std::mutex g_clipMutex;
bool g_clipActive = false;
RECT g_clipRect{};
} // namespace

BOOL GetCursorPos(LPPOINT point) {
    if (!point) return FALSE;
    int32_t x = 0, y = 0;
    if (!MacBridge_GetCursorPosScreenPixels(&x, &y)) return FALSE;
    point->x = x;
    point->y = y;
    return TRUE;
}

BOOL SetCursorPos(int x, int y) { return MacBridge_SetCursorPosScreenPixels(x, y); }

BOOL ClipCursor(const RECT* rect) {
    // GLFW's disabled-cursor mode already confines the pointer; the rectangle is only recorded.
    std::lock_guard<std::mutex> lock(g_clipMutex);
    g_clipActive = rect != nullptr;
    if (rect) g_clipRect = *rect;
    return TRUE;
}

BOOL GetClipCursor(LPRECT rect) {
    if (!rect) return FALSE;
    std::lock_guard<std::mutex> lock(g_clipMutex);
    if (g_clipActive) {
        *rect = g_clipRect;
    } else {
        rect->left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        rect->top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        rect->right = rect->left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        rect->bottom = rect->top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }
    return TRUE;
}

HCURSOR SetCursor(HCURSOR cursor) {
    const HCURSOR previous = g_currentCursor.exchange(cursor);
    if (cursor) MacBridge_SetCursor(cursor);
    return previous;
}

HCURSOR GetCursor() { return g_currentCursor.load(); }
int ShowCursor(BOOL show) { return MacBridge_ShowCursor(show ? 1 : 0); }

BOOL GetCursorInfo(PCURSORINFO info) {
    if (!info) return FALSE;
    info->flags = MacBridge_IsCursorVisible() ? CURSOR_SHOWING : 0;
    info->hCursor = g_currentCursor.load();
    GetCursorPos(&info->ptScreenPos);
    return TRUE;
}

HCURSOR LoadCursorW(HINSTANCE, LPCWSTR name) {
    const int id = IS_INTRESOURCE(name) ? static_cast<int>(reinterpret_cast<uintptr_t>(name)) : 32512;
    return static_cast<HCURSOR>(MacBridge_GetSystemCursor(id));
}

HANDLE LoadImageW(HINSTANCE instance, LPCWSTR name, UINT type, int, int, UINT flags) {
    if (type == IMAGE_CURSOR && !(flags & LR_LOADFROMFILE) && IS_INTRESOURCE(name)) return LoadCursorW(instance, name);
    // Windows cursor files (.cur/.ani) are not decoded on macOS.
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
}

HANDLE CopyImage(HANDLE image, UINT, int, int, UINT) { return image; }
BOOL GetIconInfo(HICON, PICONINFO) { return FALSE; }
BOOL GetIconInfoExW(HICON, PICONINFOEXW) { return FALSE; }
BOOL DestroyCursor(HCURSOR) { return TRUE; }
BOOL DestroyIcon(HICON) { return TRUE; }

HWND SetCapture(HWND hwnd) { return g_captureWindow.exchange(hwnd); }
BOOL ReleaseCapture() {
    g_captureWindow.store(nullptr);
    return TRUE;
}
HWND GetCapture() { return g_captureWindow.load(); }
BOOL TrackMouseEvent(LPTRACKMOUSEEVENT) { return TRUE; }
UINT GetDoubleClickTime() { return static_cast<UINT>(MacBridge_GetDoubleClickIntervalMs()); }

// ---------------------------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------------------------
namespace {

bool IsToggleKeyOn(int vk) {
    if (vk == VK_CAPITAL) return MacBridge_IsCapsLockOn() != 0;
    return false;
}

bool IsKeyDownForQuery(int vk) {
    if (vk < 0 || vk > 255) return false;
    if (macwin::IsPhysicalKeyDown(static_cast<uint16_t>(vk))) return true;
    // Fall back to the window server's view for keys pressed while Minecraft was not focused.
    if (!MacBridge_IsCachedWindowForeground()) {
        const mackeys::KeyEntry* entry = mackeys::FindByVk(static_cast<uint16_t>(vk));
        if (entry && entry->macKeyCode >= 0 && vk != VK_LBUTTON && vk != VK_RBUTTON && vk != VK_MBUTTON) {
            if (MacBridge_IsMacKeyDown(static_cast<uint16_t>(entry->macKeyCode))) return true;
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) {
                if (const mackeys::KeyEntry* right = mackeys::FindBySidedVk(vk == VK_SHIFT ? VK_RSHIFT : (vk == VK_CONTROL ? VK_RCONTROL : VK_RMENU))) {
                    if (right->macKeyCode >= 0 && MacBridge_IsMacKeyDown(static_cast<uint16_t>(right->macKeyCode))) return true;
                }
            }
        }
    }
    return false;
}

const mackeys::KeyEntry* EntryForScanAndVk(UINT scan, bool extended) {
    return mackeys::FindByScan(static_cast<uint16_t>((scan & 0xFF) | (extended ? mackeys::kExtendedFlag : 0)));
}

} // namespace

SHORT GetAsyncKeyState(int vk) { return IsKeyDownForQuery(vk) ? static_cast<SHORT>(0x8000) : 0; }

SHORT GetKeyState(int vk) {
    SHORT state = IsKeyDownForQuery(vk) ? static_cast<SHORT>(0xFF80) : 0;
    if (IsToggleKeyOn(vk)) state |= 1;
    return state;
}

BOOL GetKeyboardState(PBYTE state) {
    if (!state) return FALSE;
    for (int vk = 0; vk < 256; ++vk) {
        state[vk] = IsKeyDownForQuery(vk) ? 0x80 : 0;
        if (IsToggleKeyOn(vk)) state[vk] |= 1;
    }
    return TRUE;
}

UINT MapVirtualKeyExW(UINT code, UINT mapType, HKL) {
    switch (mapType) {
    case MAPVK_VK_TO_VSC:
    case MAPVK_VK_TO_VSC_EX: {
        const mackeys::KeyEntry* entry = mackeys::FindByVk(static_cast<uint16_t>(code));
        if (!entry) return 0;
        if (mapType == MAPVK_VK_TO_VSC) return entry->scan & 0xFF;
        return (entry->scan & mackeys::kExtendedFlag) ? (0xE000u | (entry->scan & 0xFF)) : (entry->scan & 0xFF);
    }
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX: {
        const bool extended = (code & 0xFF00) == 0xE000 || (code & 0xFF00) == 0xE100;
        const mackeys::KeyEntry* entry = EntryForScanAndVk(code & 0xFF, extended);
        if (!entry) return 0;
        return mapType == MAPVK_VSC_TO_VK_EX ? entry->sidedVk : entry->vk;
    }
    case MAPVK_VK_TO_CHAR: {
        if (code >= 'A' && code <= 'Z') return code;
        if (code >= '0' && code <= '9') return code;
        const mackeys::KeyEntry* entry = mackeys::FindByVk(static_cast<uint16_t>(code));
        if (!entry || entry->macKeyCode < 0) return 0;
        uint32_t chars[4];
        const int n = MacBridge_TranslateKeyCode(static_cast<uint16_t>(entry->macKeyCode), 0, 0, chars, 4);
        if (n <= 0 || chars[0] < 0x20 || (chars[0] >= 0xF700 && chars[0] <= 0xF8FF)) return 0;
        return static_cast<UINT>(towupper(static_cast<wint_t>(chars[0])));
    }
    default: return 0;
    }
}

UINT MapVirtualKeyW(UINT code, UINT mapType) { return MapVirtualKeyExW(code, mapType, nullptr); }
UINT MapVirtualKeyA(UINT code, UINT mapType) { return MapVirtualKeyExW(code, mapType, nullptr); }

int GetKeyNameTextW(LONG lParam, LPWSTR buffer, int size) {
    if (!buffer || size <= 0) return 0;
    const UINT scan = (static_cast<UINT>(lParam) >> 16) & 0xFF;
    const bool extended = (lParam & (1 << 24)) != 0;
    const mackeys::KeyEntry* entry = EntryForScanAndVk(scan, extended);
    std::wstring name;
    if (entry) {
        // Printable keys report the character of the active keyboard layout, as Windows does.
        if (entry->macKeyCode >= 0 && ((entry->vk >= 'A' && entry->vk <= 'Z') || (entry->vk >= VK_OEM_1 && entry->vk <= VK_OEM_102))) {
            uint32_t chars[4];
            const int n = MacBridge_TranslateKeyCode(static_cast<uint16_t>(entry->macKeyCode), 0, 0, chars, 4);
            if (n > 0 && chars[0] >= 0x20 && !(chars[0] >= 0xF700 && chars[0] <= 0xF8FF)) {
                name.push_back(static_cast<wchar_t>(towupper(static_cast<wint_t>(chars[0]))));
            }
        }
        if (name.empty()) name = macplat::Utf8ToWide(entry->name);
    }
    const size_t n = std::min<size_t>(name.size(), static_cast<size_t>(size - 1));
    wmemcpy(buffer, name.data(), n);
    buffer[n] = 0;
    return static_cast<int>(n);
}

int GetKeyNameTextA(LONG lParam, LPSTR buffer, int size) {
    if (!buffer || size <= 0) return 0;
    wchar_t wide[64];
    GetKeyNameTextW(lParam, wide, 64);
    const std::string utf8 = macplat::WideToUtf8(wide);
    const size_t n = std::min<size_t>(utf8.size(), static_cast<size_t>(size - 1));
    memcpy(buffer, utf8.data(), n);
    buffer[n] = 0;
    return static_cast<int>(n);
}

HKL GetKeyboardLayout(DWORD) { return reinterpret_cast<HKL>(static_cast<uintptr_t>(0x04090409)); }

SHORT VkKeyScanExW(WCHAR ch, HKL) {
    for (int shift = 0; shift < 2; ++shift) {
        for (int option = 0; option < 2; ++option) {
            for (int i = 0; i < mackeys::Count(); ++i) {
                const mackeys::KeyEntry& k = mackeys::At(i);
                if (k.macKeyCode < 0 || (k.vk >= VK_NUMPAD0 && k.vk <= VK_DIVIDE)) continue;
                uint32_t chars[4];
                const int n = MacBridge_TranslateKeyCode(static_cast<uint16_t>(k.macKeyCode), shift, option, chars, 4);
                if (n == 1 && chars[0] == static_cast<uint32_t>(ch)) {
                    // Windows reports AltGr as Ctrl+Alt (6); Option is the Mac equivalent.
                    const int state = (shift ? 1 : 0) | (option ? 6 : 0);
                    return static_cast<SHORT>((state << 8) | (k.vk & 0xFF));
                }
            }
        }
    }
    return -1;
}

SHORT VkKeyScanW(WCHAR ch) { return VkKeyScanExW(ch, nullptr); }
SHORT VkKeyScanA(CHAR ch) { return VkKeyScanExW(static_cast<WCHAR>(static_cast<unsigned char>(ch)), nullptr); }
SHORT VkKeyScanExA(CHAR ch, HKL layout) { return VkKeyScanExW(static_cast<WCHAR>(static_cast<unsigned char>(ch)), layout); }

int ToUnicodeEx(UINT vk, UINT scan, const BYTE* keyState, LPWSTR buffer, int size, UINT, HKL) {
    if (!buffer || size <= 0) return 0;
    const mackeys::KeyEntry* entry = mackeys::FindByVk(static_cast<uint16_t>(vk));
    if (!entry && scan) entry = mackeys::FindByScan(static_cast<uint16_t>(scan));
    if (!entry || entry->macKeyCode < 0) return 0;
    const bool shift = keyState && ((keyState[VK_SHIFT] | keyState[VK_LSHIFT] | keyState[VK_RSHIFT]) & 0x80);
    const bool ctrl = keyState && ((keyState[VK_CONTROL] | keyState[VK_LCONTROL] | keyState[VK_RCONTROL]) & 0x80);
    const bool alt = keyState && ((keyState[VK_MENU] | keyState[VK_LMENU] | keyState[VK_RMENU]) & 0x80);
    const bool caps = keyState && (keyState[VK_CAPITAL] & 1);
    const bool option = alt && ctrl; // AltGr
    if (ctrl && !option) return 0;
    uint32_t chars[4];
    int n = MacBridge_TranslateKeyCode(static_cast<uint16_t>(entry->macKeyCode), shift ? 1 : 0, option ? 1 : 0, chars, 4);
    int written = 0;
    for (int i = 0; i < n && written < size; ++i) {
        uint32_t c = chars[i];
        if (c < 0x20 && c != '\r' && c != '\t' && c != '\b' && c != 0x1B) continue;
        if (c >= 0xF700 && c <= 0xF8FF) continue; // function-key private use area
        if (caps && !shift) c = static_cast<uint32_t>(towupper(static_cast<wint_t>(c)));
        buffer[written++] = static_cast<wchar_t>(c);
    }
    if (written < size) buffer[written] = 0;
    return written;
}

int ToUnicode(UINT vk, UINT scan, const BYTE* keyState, LPWSTR buffer, int size, UINT flags) {
    return ToUnicodeEx(vk, scan, keyState, buffer, size, flags, nullptr);
}

// ---------------------------------------------------------------------------------------------
// Global memory and clipboard
// ---------------------------------------------------------------------------------------------
namespace {
struct GlobalBlock {
    size_t size;
    alignas(16) unsigned char data[1];
};
GlobalBlock* BlockFromHandle(HGLOBAL mem) {
    return mem ? reinterpret_cast<GlobalBlock*>(static_cast<unsigned char*>(mem) - offsetof(GlobalBlock, data)) : nullptr;
}
std::atomic<bool> g_clipboardOpen{ false };
} // namespace

HGLOBAL GlobalAlloc(UINT flags, SIZE_T bytes) {
    auto* block = static_cast<GlobalBlock*>(malloc(offsetof(GlobalBlock, data) + std::max<SIZE_T>(bytes, 1)));
    if (!block) return nullptr;
    block->size = bytes;
    if (flags & GMEM_ZEROINIT) memset(block->data, 0, bytes);
    return block->data;
}

LPVOID GlobalLock(HGLOBAL mem) { return mem; }
BOOL GlobalUnlock(HGLOBAL) { return TRUE; }
SIZE_T GlobalSize(HGLOBAL mem) { return mem ? BlockFromHandle(mem)->size : 0; }

HGLOBAL GlobalFree(HGLOBAL mem) {
    if (mem) free(BlockFromHandle(mem));
    return nullptr;
}

BOOL OpenClipboard(HWND) {
    bool expected = false;
    return g_clipboardOpen.compare_exchange_strong(expected, true);
}
BOOL CloseClipboard() {
    g_clipboardOpen.store(false);
    return TRUE;
}
BOOL EmptyClipboard() { return g_clipboardOpen.load(); }
BOOL IsClipboardFormatAvailable(UINT format) {
    if (format != CF_UNICODETEXT && format != CF_TEXT) return FALSE;
    char* text = MacBridge_GetClipboardText();
    const bool available = text != nullptr;
    free(text);
    return available;
}

HANDLE SetClipboardData(UINT format, HANDLE mem) {
    if (!mem) return nullptr;
    const size_t size = GlobalSize(mem);
    bool ok = false;
    if (format == CF_UNICODETEXT) {
        const auto* text = static_cast<const wchar_t*>(mem);
        ok = MacBridge_SetClipboardText(macplat::WideToUtf8(text, wcsnlen(text, size / sizeof(wchar_t))).c_str());
    } else if (format == CF_TEXT) {
        const std::string text(static_cast<const char*>(mem), strnlen(static_cast<const char*>(mem), size));
        ok = MacBridge_SetClipboardText(text.c_str());
    } else if ((format == CF_DIB || format == CF_DIBV5) && size >= sizeof(BITMAPINFOHEADER)) {
        const auto* header = static_cast<const BITMAPINFOHEADER*>(mem);
        const int w = header->biWidth;
        const int h = std::abs(header->biHeight);
        const bool bottomUp = header->biHeight > 0;
        const int bpp = header->biBitCount;
        const unsigned char* pixels = static_cast<const unsigned char*>(mem) + header->biSize;
        if (w > 0 && h > 0 && (bpp == 32 || bpp == 24)) {
            const size_t stride = ((static_cast<size_t>(w) * bpp + 31) / 32) * 4;
            if (header->biSize + stride * h <= size) {
                std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4);
                for (int y = 0; y < h; ++y) {
                    const unsigned char* row = pixels + stride * (bottomUp ? (h - 1 - y) : y);
                    for (int x = 0; x < w; ++x) {
                        const unsigned char* p = row + x * (bpp / 8);
                        uint8_t* out = &rgba[(static_cast<size_t>(y) * w + x) * 4];
                        out[0] = p[2];
                        out[1] = p[1];
                        out[2] = p[0];
                        out[3] = 255;
                    }
                }
                ok = MacBridge_SetClipboardImageRGBA(rgba.data(), w, h);
            }
        }
    }
    // Ownership passes to the clipboard on Windows; release our copy.
    GlobalFree(mem);
    return ok ? mem : nullptr;
}

HANDLE GetClipboardData(UINT format) {
    if (format != CF_UNICODETEXT && format != CF_TEXT) return nullptr;
    char* text = MacBridge_GetClipboardText();
    if (!text) return nullptr;
    HGLOBAL mem = nullptr;
    if (format == CF_UNICODETEXT) {
        const std::wstring w = macplat::Utf8ToWide(text);
        mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
        if (mem) wmemcpy(static_cast<wchar_t*>(mem), w.c_str(), w.size() + 1);
    } else {
        const size_t len = strlen(text);
        mem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
        if (mem) memcpy(mem, text, len + 1);
    }
    free(text);
    return mem;
}

// ---------------------------------------------------------------------------------------------
// Dialogs and shell
// ---------------------------------------------------------------------------------------------
int MessageBoxW(HWND, LPCWSTR text, LPCWSTR caption, UINT type) {
    const UINT icon = type & 0xF0;
    const int style = icon == MB_ICONERROR ? 2 : (icon == MB_ICONWARNING ? 1 : 0);
    const UINT buttonsType = type & 0x0F;
    int buttons = 0;
    if (buttonsType == MB_OKCANCEL) buttons = 1;
    else if (buttonsType == MB_YESNO) buttons = 2;
    else if (buttonsType == MB_YESNOCANCEL) buttons = 3;
    const int clicked = MacBridge_ShowAlert(macplat::WideToUtf8(caption ? caption : L"Toolscreen").c_str(),
                                            macplat::WideToUtf8(text ? text : L"").c_str(), style, buttons);
    switch (buttons) {
    case 1: return clicked == 0 ? IDOK : IDCANCEL;
    case 2: return clicked == 0 ? IDYES : IDNO;
    case 3: return clicked == 0 ? IDYES : (clicked == 1 ? IDNO : IDCANCEL);
    default: return IDOK;
    }
}

int MessageBoxA(HWND hwnd, LPCSTR text, LPCSTR caption, UINT type) {
    return MessageBoxW(hwnd, macplat::Utf8ToWide(text ? text : "").c_str(), macplat::Utf8ToWide(caption ? caption : "Toolscreen").c_str(),
                       type);
}

BOOL MessageBeep(UINT) { return TRUE; }

namespace {
thread_local DWORD t_commDlgError = 0;

// Extracts "png;jpg" from a Win32 filter list ("Images\0*.png;*.jpg\0All\0*.*\0\0").
std::string ExtensionsFromFilter(LPCWSTR filter) {
    if (!filter) return {};
    const wchar_t* description = filter;
    const wchar_t* pattern = description + wcslen(description) + 1;
    if (!*pattern) return {};
    std::string out;
    std::wstring p = pattern;
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find(L';', start);
        if (end == std::wstring::npos) end = p.size();
        std::wstring item = p.substr(start, end - start);
        const size_t dot = item.find_last_of(L'.');
        if (dot != std::wstring::npos) {
            const std::wstring ext = item.substr(dot + 1);
            if (!ext.empty() && ext != L"*") {
                if (!out.empty()) out.push_back(';');
                out += macplat::WideToUtf8(ext.c_str());
            }
        }
        start = end + 1;
    }
    return out;
}

BOOL CompleteFileDialog(LPOPENFILENAMEW ofn, char* path) {
    if (!path) {
        t_commDlgError = 0;
        return FALSE;
    }
    const std::wstring w = macplat::Utf8ToWide(path);
    free(path);
    if (!ofn->lpstrFile || ofn->nMaxFile <= w.size()) {
        t_commDlgError = FNERR_BUFFERTOOSMALL;
        return FALSE;
    }
    wmemcpy(ofn->lpstrFile, w.c_str(), w.size() + 1);
    const size_t slash = w.find_last_of(L'/');
    ofn->nFileOffset = static_cast<WORD>(slash == std::wstring::npos ? 0 : slash + 1);
    const size_t dot = w.find_last_of(L'.');
    ofn->nFileExtension = static_cast<WORD>(dot == std::wstring::npos || dot < ofn->nFileOffset ? 0 : dot + 1);
    if (ofn->lpstrFileTitle && ofn->nMaxFileTitle > 0) {
        const std::wstring title = w.substr(ofn->nFileOffset);
        wcsncpy_s(ofn->lpstrFileTitle, ofn->nMaxFileTitle, title.c_str(), _TRUNCATE);
    }
    t_commDlgError = 0;
    return TRUE;
}
} // namespace

BOOL GetOpenFileNameW(LPOPENFILENAMEW ofn) {
    if (!ofn) return FALSE;
    const std::string title = ofn->lpstrTitle ? macplat::WideToUtf8(ofn->lpstrTitle) : std::string();
    const std::string dir = ofn->lpstrInitialDir ? macplat::NativePathFromWide(ofn->lpstrInitialDir) : std::string();
    const std::string exts = ExtensionsFromFilter(ofn->lpstrFilter);
    char* path = MacBridge_RunOpenPanel(title.empty() ? nullptr : title.c_str(), dir.empty() ? nullptr : dir.c_str(),
                                        exts.empty() ? nullptr : exts.c_str(), 0);
    return CompleteFileDialog(ofn, path);
}

BOOL GetSaveFileNameW(LPOPENFILENAMEW ofn) {
    if (!ofn) return FALSE;
    const std::string title = ofn->lpstrTitle ? macplat::WideToUtf8(ofn->lpstrTitle) : std::string();
    const std::string dir = ofn->lpstrInitialDir ? macplat::NativePathFromWide(ofn->lpstrInitialDir) : std::string();
    const std::string name = (ofn->lpstrFile && *ofn->lpstrFile) ? macplat::WideToUtf8(ofn->lpstrFile) : std::string();
    const std::string exts = ExtensionsFromFilter(ofn->lpstrFilter);
    char* path = MacBridge_RunSavePanel(title.empty() ? nullptr : title.c_str(), dir.empty() ? nullptr : dir.c_str(),
                                        name.empty() ? nullptr : name.c_str(), exts.empty() ? nullptr : exts.c_str());
    return CompleteFileDialog(ofn, path);
}

DWORD CommDlgExtendedError() { return t_commDlgError; }

HINSTANCE ShellExecuteW(HWND, LPCWSTR op, LPCWSTR file, LPCWSTR, LPCWSTR, INT) {
    if (!file) return reinterpret_cast<HINSTANCE>(static_cast<uintptr_t>(SE_ERR_NOASSOC));
    const std::string target = macplat::WideToUtf8(file);
    const bool isUrl = target.find("://") != std::string::npos || target.rfind("mailto:", 0) == 0;
    const std::string native = isUrl ? target : macplat::NativePathFromNarrow(target.c_str());
    bool ok = false;
    if (op && wcscmp(op, L"explore") == 0) ok = MacBridge_OpenURLOrPath(native.c_str());
    else if (op && wcscmp(op, L"select") == 0) ok = MacBridge_RevealInFinder(native.c_str());
    else ok = MacBridge_OpenURLOrPath(native.c_str());
    return reinterpret_cast<HINSTANCE>(static_cast<uintptr_t>(ok ? 42 : SE_ERR_NOASSOC));
}

HINSTANCE ShellExecuteA(HWND hwnd, LPCSTR op, LPCSTR file, LPCSTR params, LPCSTR dir, INT show) {
    const std::wstring o = op ? macplat::Utf8ToWide(op) : std::wstring();
    const std::wstring f = file ? macplat::Utf8ToWide(file) : std::wstring();
    return ShellExecuteW(hwnd, op ? o.c_str() : nullptr, file ? f.c_str() : nullptr, nullptr, nullptr, show);
}

// ---------------------------------------------------------------------------------------------
// GDI: there is no GDI on macOS. Screen DCs exist so callers can query DPI; everything that
// would capture or draw pixels reports failure and the shared code skips that feature.
// ---------------------------------------------------------------------------------------------
namespace {
HDC ScreenDc() { return reinterpret_cast<HDC>(static_cast<uintptr_t>(0x20)); }
} // namespace

HDC GetDC(HWND hwnd) { return hwnd ? reinterpret_cast<HDC>(hwnd) : ScreenDc(); }
int ReleaseDC(HWND, HDC) { return 1; }
HDC CreateCompatibleDC(HDC) { return nullptr; }
BOOL DeleteDC(HDC) { return TRUE; }
HBITMAP CreateCompatibleBitmap(HDC, int, int) { return nullptr; }
HBITMAP CreateDIBSection(HDC, const BITMAPINFO*, UINT, void** bits, HANDLE, DWORD) {
    if (bits) *bits = nullptr;
    return nullptr;
}
HGDIOBJ SelectObject(HDC, HGDIOBJ) { return nullptr; }
BOOL DeleteObject(HGDIOBJ) { return TRUE; }
HGDIOBJ GetStockObject(int) { return nullptr; }
int GetObjectW(HANDLE, int, LPVOID) { return 0; }
LONG GetBitmapBits(HBITMAP, LONG, LPVOID) { return 0; }
int GetDIBits(HDC, HBITMAP, UINT, UINT, LPVOID, BITMAPINFO*, UINT) { return 0; }
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD) { return FALSE; }
BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD) { return FALSE; }
BOOL PrintWindow(HWND, HDC, UINT) { return FALSE; }
int FillRect(HDC, const RECT*, HBRUSH) { return 0; }
BOOL DrawIconEx(HDC, int, int, HICON, int, int, UINT, HBRUSH, UINT) { return FALSE; }
int SetROP2(HDC, int) { return R2_COPYPEN; }
HBRUSH CreateSolidBrush(COLORREF) { return nullptr; }

int GetDeviceCaps(HDC, int index) {
    switch (index) {
    case LOGPIXELSX:
    case LOGPIXELSY: return static_cast<int>(USER_DEFAULT_SCREEN_DPI * MacBridge_GetCachedBackingScale());
    case HORZRES: return GetSystemMetrics(SM_CXSCREEN);
    case VERTRES: return GetSystemMetrics(SM_CYSCREEN);
    default: return 0;
    }
}

HRESULT DwmGetWindowAttribute(HWND hwnd, DWORD attribute, PVOID value, DWORD size) {
    if (attribute == DWMWA_EXTENDED_FRAME_BOUNDS && value && size >= sizeof(RECT)) {
        return GetWindowRect(hwnd, static_cast<RECT*>(value)) ? S_OK : E_FAIL;
    }
    if (attribute == DWMWA_CLOAKED && value && size >= sizeof(DWORD)) {
        *static_cast<DWORD*>(value) = 0;
        return S_OK;
    }
    return E_FAIL;
}

// ---------------------------------------------------------------------------------------------
// WGL equivalents
// ---------------------------------------------------------------------------------------------
HGLRC wglGetCurrentContext() { return reinterpret_cast<HGLRC>(CGLGetCurrentContext()); }

HDC wglGetCurrentDC() { return CGLGetCurrentContext() ? reinterpret_cast<HDC>(GameHwnd()) : nullptr; }

PROC wglGetProcAddress(LPCSTR name) {
    static void* openGl = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
    if (!name || !openGl) return nullptr;
    return dlsym(openGl, name);
}

BOOL wglMakeCurrent(HDC, HGLRC context) { return CGLSetCurrentContext(reinterpret_cast<CGLContextObj>(context)) == kCGLNoError; }
BOOL SwapBuffers(HDC) { return FALSE; }
