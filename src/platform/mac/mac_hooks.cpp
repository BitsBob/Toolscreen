// macOS hook layer: GLFW/OpenGL entry points handed to LWJGL, and the bridge between GLFW's
// callbacks and Toolscreen's Win32-message input pipeline.
//
// Windows Toolscreen subclasses GLFW's window procedure: every keyboard/mouse/size message passes
// through SubclassedWndProc (hooks/input_hook.cpp), which may block, rebind or translate it before
// calling the original procedure (GLFW), which in turn invokes Minecraft's callbacks.
//
// On macOS the same pipeline runs unchanged:
//   GLFW callback (registered by us)  ->  Win32 message  ->  current window procedure
//   (SubclassedWndProc once Toolscreen subclassed the window)  ->  ...  ->  MacGameWndProc
//   (the "original" procedure)  ->  Minecraft's GLFW callback.
//
// Minecraft's view of the window uses framebuffer pixels for both the window size and cursor
// coordinates (as on Windows); GLFW's point-based values are converted at this boundary.
#include "platform/mac/mac_interpose.h"
#include "platform/mac/mac_internal.h"
#include "platform/mac/mac_keymap.h"
#include "platform/mac/mac_platform_bridge.h"

#include "common/utils.h"
#include "testing/game_test_mode.h"

#include <OpenGL/OpenGL.h>
#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

// ---------------------------------------------------------------------------------------------
// Symbols provided by bootstrap/dllmain.cpp (shared with the Windows build)
// ---------------------------------------------------------------------------------------------
typedef void (*GLFWWINDOWSIZEFUN)(void* window, int width, int height);
typedef void (*GLFWFRAMEBUFFERSIZEFUN)(void* window, int width, int height);
typedef BOOL(WINAPI* WGLSWAPBUFFERS)(HDC);
typedef BOOL(WINAPI* SETCURSORPOSPROC)(int, int);
typedef void(WINAPI* GLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void(APIENTRY* GLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void(APIENTRY* GLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void(APIENTRY* PFNGLBLITFRAMEBUFFERPROC_HOOK)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void (*GLFWSETINPUTMODE)(void* window, int mode, int value);
typedef void (*GLFWSETCURSOR)(void* window, void* cursor);
typedef void* (*GLFWCREATEWINDOW)(int width, int height, const char* title, void* monitor, void* share);
typedef HWND (*GLFWGETWIN32WINDOW)(void* window);
typedef void (*GLFWWINDOWHINT)(int hint, int value);
typedef void (*GLFWDESTROYWINDOW)(void* window);
typedef GLFWWINDOWSIZEFUN (*GLFWSETWINDOWSIZECALLBACK)(void* window, GLFWWINDOWSIZEFUN callback);
typedef GLFWFRAMEBUFFERSIZEFUN (*GLFWSETFRAMEBUFFERSIZECALLBACK)(void* window, GLFWFRAMEBUFFERSIZEFUN callback);
typedef void* (*GLFWGETCURRENTCONTEXTPROC)();
typedef UINT(WINAPI* GETRAWINPUTDATAPROC)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);

extern WGLSWAPBUFFERS owglSwapBuffers;
extern SETCURSORPOSPROC oSetCursorPos;
extern GLVIEWPORTPROC oglViewport;
extern GLBINDFRAMEBUFFERPROC oglBindFramebuffer;
extern GLBINDTEXTUREPROC oglBindTexture;
extern PFNGLBLITFRAMEBUFFERPROC_HOOK oglBlitFramebuffer;
extern GLFWSETINPUTMODE oglfwSetInputMode;
extern GLFWSETCURSOR oglfwSetCursor;
extern GLFWCREATEWINDOW oglfwCreateWindow;
extern GLFWGETWIN32WINDOW glfwGetWin32WindowProc;
extern GLFWWINDOWHINT oglfwWindowHint;
extern GLFWDESTROYWINDOW oglfwDestroyWindow;
extern GLFWSETWINDOWSIZECALLBACK oglfwSetWindowSizeCallback;
extern GLFWSETFRAMEBUFFERSIZECALLBACK oglfwSetFramebufferSizeCallback;
extern std::atomic<GLFWGETCURRENTCONTEXTPROC> glfwGetCurrentContextProc;
extern GETRAWINPUTDATAPROC oGetRawInputData;
extern std::atomic<bool> g_glBlitFramebufferHooked;

BOOL WINAPI hkwglSwapBuffers(HDC hDc);
BOOL WINAPI hkSetCursorPos(int X, int Y);
UINT WINAPI hkGetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);
void hkglfwSetInputMode(void* window, int mode, int value);
void hkglfwSetCursor(void* window, void* cursor);
void* hkglfwCreateWindow(int width, int height, const char* title, void* monitor, void* share);
void hkglfwDestroyWindow(void* window);
void hkglfwWindowHint(int hint, int value);
GLFWWINDOWSIZEFUN hkglfwSetWindowSizeCallback(void* window, GLFWWINDOWSIZEFUN callback);
GLFWFRAMEBUFFERSIZEFUN hkglfwSetFramebufferSizeCallback(void* window, GLFWFRAMEBUFFERSIZEFUN callback);
void APIENTRY hkglBlitFramebuffer(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
void APIENTRY ToolscreenMac_glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void APIENTRY ToolscreenMac_glBindTexture(GLenum target, GLuint texture);
void APIENTRY ToolscreenMac_glBindFramebuffer(GLenum target, GLuint framebuffer);

void ToolscreenMacEnsureInitialized(); // mac_bootstrap.cpp

namespace {

using namespace mackeys;

// ---------------------------------------------------------------------------------------------
// GLFW types and constants
// ---------------------------------------------------------------------------------------------
using GLFWwindow = void;
typedef void (*GLFWkeyfun)(GLFWwindow*, int, int, int, int);
typedef void (*GLFWcharfun)(GLFWwindow*, unsigned int);
typedef void (*GLFWcharmodsfun)(GLFWwindow*, unsigned int, int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow*, int, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow*, double, double);
typedef void (*GLFWscrollfun)(GLFWwindow*, double, double);
typedef void (*GLFWwindowfocusfun)(GLFWwindow*, int);
typedef void (*GLFWcursorenterfun)(GLFWwindow*, int);
typedef void (*GLFWwindowposfun)(GLFWwindow*, int, int);

constexpr int kGlfwCursor = 0x00033001;
constexpr int kGlfwCursorNormal = 0x00034001;
constexpr int kGlfwCursorDisabled = 0x00034003;
constexpr int kGlfwIconified = 0x00020002;

struct RealGlfw {
    void (*swapBuffers)(GLFWwindow*) = nullptr;
    void (*pollEvents)() = nullptr;
    void (*waitEvents)() = nullptr;
    void (*waitEventsTimeout)(double) = nullptr;
    void* (*getCocoaWindow)(GLFWwindow*) = nullptr;
    void* (*getCurrentContext)() = nullptr;
    void (*getFramebufferSize)(GLFWwindow*, int*, int*) = nullptr;
    void (*getWindowSize)(GLFWwindow*, int*, int*) = nullptr;
    void (*getCursorPos)(GLFWwindow*, double*, double*) = nullptr;
    void (*setCursorPos)(GLFWwindow*, double, double) = nullptr;
    void (*setInputMode)(GLFWwindow*, int, int) = nullptr;
    int (*getWindowAttrib)(GLFWwindow*, int) = nullptr;
    int (*getKey)(GLFWwindow*, int) = nullptr;
    int (*getMouseButton)(GLFWwindow*, int) = nullptr;
    void (*setWindowShouldClose)(GLFWwindow*, int) = nullptr;
    void (*setWindowMonitor)(GLFWwindow*, void*, int, int, int, int, int) = nullptr;
    void (*setWindowSize)(GLFWwindow*, int, int) = nullptr;
    void* (*createWindow)(int, int, const char*, void*, void*) = nullptr;
    void (*destroyWindow)(GLFWwindow*) = nullptr;
    void (*setCursor)(GLFWwindow*, void*) = nullptr;
    void (*windowHint)(int, int) = nullptr;
    GLFWkeyfun (*setKeyCallback)(GLFWwindow*, GLFWkeyfun) = nullptr;
    GLFWcharfun (*setCharCallback)(GLFWwindow*, GLFWcharfun) = nullptr;
    GLFWcharmodsfun (*setCharModsCallback)(GLFWwindow*, GLFWcharmodsfun) = nullptr;
    GLFWmousebuttonfun (*setMouseButtonCallback)(GLFWwindow*, GLFWmousebuttonfun) = nullptr;
    GLFWcursorposfun (*setCursorPosCallback)(GLFWwindow*, GLFWcursorposfun) = nullptr;
    GLFWscrollfun (*setScrollCallback)(GLFWwindow*, GLFWscrollfun) = nullptr;
    GLFWwindowfocusfun (*setWindowFocusCallback)(GLFWwindow*, GLFWwindowfocusfun) = nullptr;
    GLFWcursorenterfun (*setCursorEnterCallback)(GLFWwindow*, GLFWcursorenterfun) = nullptr;
    GLFWwindowposfun (*setWindowPosCallback)(GLFWwindow*, GLFWwindowposfun) = nullptr;
    GLFWWINDOWSIZEFUN (*setWindowSizeCallback)(GLFWwindow*, GLFWWINDOWSIZEFUN) = nullptr;
    GLFWFRAMEBUFFERSIZEFUN (*setFramebufferSizeCallback)(GLFWwindow*, GLFWFRAMEBUFFERSIZEFUN) = nullptr;
};
RealGlfw g_real;
std::atomic<void*> g_glfwHandle{ nullptr };

// Minecraft's callbacks, captured when it registers them.
struct GameCallbacks {
    GLFWkeyfun key = nullptr;
    GLFWcharfun character = nullptr;
    GLFWcharmodsfun charMods = nullptr;
    GLFWmousebuttonfun mouseButton = nullptr;
    GLFWcursorposfun cursorPos = nullptr;
    GLFWscrollfun scroll = nullptr;
    GLFWwindowfocusfun focus = nullptr;
    GLFWcursorenterfun cursorEnter = nullptr;
    GLFWwindowposfun windowPos = nullptr;
    GLFWWINDOWSIZEFUN windowSize = nullptr;
    GLFWFRAMEBUFFERSIZEFUN framebufferSize = nullptr;
};
GameCallbacks g_game;

// Input state as Minecraft sees it (after Toolscreen's pipeline), backing glfwGetKey & friends.
std::array<std::atomic<uint8_t>, GLFW_KEY_LAST_ + 1> g_gameKeys{};
std::array<std::atomic<uint8_t>, GLFW_MOUSE_BUTTON_LAST_ + 1> g_gameButtons{};

struct BridgeState {
    int cursorMode = kGlfwCursorNormal;
    double lastRawX = 0.0, lastRawY = 0.0; // last GLFW cursor position (points / GLFW virtual units)
    double fracX = 0.0, fracY = 0.0;       // sub-unit remainder of raw motion (1/256 point units)
    double virtualX = 0.0, virtualY = 0.0; // the disabled-cursor position delivered to Minecraft
    int lastClientX = 0, lastClientY = 0;  // last cursor position in client pixels
    LPARAM lastKeyLParam = 0;
    WCHAR pendingHighSurrogate = 0;
    double pendingScrollY = 0.0, pendingScrollX = 0.0;
    int lastSizeW = -1, lastSizeH = -1;
    bool legacyContext = false;
    bool legacyContextKnown = false;
};
BridgeState g_bridge;

bool IsGameWindow(GLFWwindow* window) { return window && window == static_cast<void*>(macwin::GameHwnd()); }

double BackingScale() { return MacBridge_GetCachedBackingScale(); }

int CurrentGameMods() {
    int mods = 0;
    if (g_gameKeys[GLFW_KEY_LEFT_SHIFT_] || g_gameKeys[GLFW_KEY_RIGHT_SHIFT_]) mods |= GLFW_MOD_SHIFT_;
    if (g_gameKeys[GLFW_KEY_LEFT_CONTROL_] || g_gameKeys[GLFW_KEY_RIGHT_CONTROL_]) mods |= GLFW_MOD_CONTROL_;
    if (g_gameKeys[GLFW_KEY_LEFT_ALT_] || g_gameKeys[GLFW_KEY_RIGHT_ALT_]) mods |= GLFW_MOD_ALT_;
    if (g_gameKeys[GLFW_KEY_LEFT_SUPER_] || g_gameKeys[GLFW_KEY_RIGHT_SUPER_]) mods |= GLFW_MOD_SUPER_;
    return mods;
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

void RefreshFramebufferSize(GLFWwindow* window) {
    if (!g_real.getFramebufferSize || !window) return;
    int w = 0, h = 0;
    g_real.getFramebufferSize(window, &w, &h);
    if (w > 0 && h > 0) macwin::SetFramebufferSize(w, h);
}

// Runs a message through the window procedure chain, after any messages other threads posted.
LRESULT Dispatch(UINT msg, WPARAM wParam, LPARAM lParam) {
    macwin::PumpGameWindowMessages();
    return macwin::DispatchToGameWindow(msg, wParam, lParam);
}

// ---------------------------------------------------------------------------------------------
// MacGameWndProc: the "original" window procedure. Win32 messages -> Minecraft's GLFW callbacks.
// ---------------------------------------------------------------------------------------------
const KeyEntry* ResolveKeyFromMessage(WPARAM wParam, LPARAM lParam) {
    const uint16_t vk = static_cast<uint16_t>(wParam & 0xFF);
    const uint16_t scan = static_cast<uint16_t>((lParam >> 16) & 0xFF);
    const bool extended = (lParam & (static_cast<LPARAM>(1) << 24)) != 0;
    if (scan != 0) {
        const KeyEntry* byScan = FindByScan(static_cast<uint16_t>(scan | (extended ? kExtendedFlag : 0)));
        if (byScan && (byScan->vk == vk || byScan->sidedVk == vk)) return byScan;
    }
    return FindByVk(vk);
}

void DeliverKey(GLFWwindow* window, UINT msg, WPARAM wParam, LPARAM lParam) {
    const KeyEntry* entry = ResolveKeyFromMessage(wParam, lParam);
    const bool up = msg == WM_KEYUP || msg == WM_SYSKEYUP;
    const int key = entry ? entry->glfwKey : GLFW_KEY_UNKNOWN_;
    const int scancode = (entry && entry->macKeyCode >= 0) ? entry->macKeyCode : static_cast<int>((lParam >> 16) & 0xFF);
    int action = GLFW_PRESS_;
    if (up) {
        action = GLFW_RELEASE_;
    } else if (key >= 0 && key <= GLFW_KEY_LAST_ && g_gameKeys[key]) {
        action = GLFW_REPEAT_;
    }
    if (key >= 0 && key <= GLFW_KEY_LAST_) {
        if (up && !g_gameKeys[key]) return; // GLFW drops releases of keys it never saw pressed
        g_gameKeys[key] = up ? 0 : 1;
    }
    if (g_game.key) g_game.key(window, key, scancode, action, CurrentGameMods());
}

void DeliverChar(GLFWwindow* window, UINT msg, WPARAM wParam) {
    uint32_t unit = static_cast<uint32_t>(wParam);
    if (unit >= 0xD800 && unit <= 0xDBFF) {
        g_bridge.pendingHighSurrogate = static_cast<WCHAR>(unit);
        return;
    }
    uint32_t cp = unit;
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
        if (!g_bridge.pendingHighSurrogate) return;
        cp = 0x10000 + ((static_cast<uint32_t>(g_bridge.pendingHighSurrogate) - 0xD800) << 10) + (unit - 0xDC00);
    }
    g_bridge.pendingHighSurrogate = 0;
    if (cp < 32 || (cp > 126 && cp < 160)) return; // same filter as GLFW's _glfwInputChar
    const bool plain = msg != WM_SYSCHAR;
    if (g_game.charMods) g_game.charMods(window, cp, CurrentGameMods());
    if (plain && g_game.character) g_game.character(window, cp);
}

void DeliverMouseButton(GLFWwindow* window, UINT msg, WPARAM wParam) {
    int button = 0;
    bool down = false;
    switch (msg) {
    case WM_LBUTTONDOWN: button = 0; down = true; break;
    case WM_LBUTTONUP: button = 0; break;
    case WM_RBUTTONDOWN: button = 1; down = true; break;
    case WM_RBUTTONUP: button = 1; break;
    case WM_MBUTTONDOWN: button = 2; down = true; break;
    case WM_MBUTTONUP: button = 2; break;
    case WM_XBUTTONDOWN: button = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4; down = true; break;
    case WM_XBUTTONUP: button = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4; break;
    default: return;
    }
    if (!down && !g_gameButtons[button]) return;
    g_gameButtons[button] = down ? 1 : 0;
    if (g_game.mouseButton) g_game.mouseButton(window, button, down ? GLFW_PRESS_ : GLFW_RELEASE_, CurrentGameMods());
}

void DeliverRawMotion(GLFWwindow* window, LPARAM lParam) {
    if (g_bridge.cursorMode != kGlfwCursorDisabled) return;
    RAWINPUT raw{};
    UINT size = sizeof(raw);
    // hkGetRawInputData applies Toolscreen's sensitivity scaling exactly as on Windows.
    if (hkGetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) return;
    if (raw.header.dwType != RIM_TYPEMOUSE) return;
    // Raw records carry motion in 1/256 point units so sub-point deltas survive.
    g_bridge.virtualX += raw.data.mouse.lLastX / 256.0;
    g_bridge.virtualY += raw.data.mouse.lLastY / 256.0;
    if (g_game.cursorPos) g_game.cursorPos(window, g_bridge.virtualX, g_bridge.virtualY);
}

void DeliverScroll(GLFWwindow* window, UINT msg, WPARAM wParam) {
    const short delta = static_cast<short>(HIWORD(wParam));
    if (msg == WM_MOUSEWHEEL) {
        double y = delta / static_cast<double>(WHEEL_DELTA);
        if (std::lround(g_bridge.pendingScrollY * WHEEL_DELTA) == delta) y = g_bridge.pendingScrollY; // keep trackpad precision
        if (g_game.scroll) g_game.scroll(window, 0.0, y);
    } else {
        double x = -delta / static_cast<double>(WHEEL_DELTA);
        if (std::lround(-g_bridge.pendingScrollX * WHEEL_DELTA) == delta) x = g_bridge.pendingScrollX;
        if (g_game.scroll) g_game.scroll(window, x, 0.0);
    }
}

void DeliverSize(GLFWwindow* window, LPARAM lParam) {
    const int w = LOWORD(lParam);
    const int h = HIWORD(lParam);
    if (w <= 0 || h <= 0) return;
    // GLFW's Win32 backend ignores WM_SIZE messages that repeat the previous size.
    if (w == g_bridge.lastSizeW && h == g_bridge.lastSizeH) return;
    g_bridge.lastSizeW = w;
    g_bridge.lastSizeH = h;
    if (g_game.framebufferSize) g_game.framebufferSize(window, w, h);
    if (g_game.windowSize) g_game.windowSize(window, w, h);
}

LRESULT CALLBACK MacGameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    GLFWwindow* window = static_cast<void*>(hwnd);
    switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: DeliverKey(window, msg, wParam, lParam); return 0;
    case WM_CHAR:
    case WM_SYSCHAR: DeliverChar(window, msg, wParam); return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP: DeliverMouseButton(window, msg, wParam); return msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP ? TRUE : 0;
    case WM_MOUSEMOVE:
        if (g_bridge.cursorMode != kGlfwCursorDisabled && g_game.cursorPos) {
            g_game.cursorPos(window, static_cast<double>(GET_X_LPARAM(lParam)), static_cast<double>(GET_Y_LPARAM(lParam)));
        }
        return 0;
    case WM_INPUT: DeliverRawMotion(window, lParam); return 0;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: DeliverScroll(window, msg, wParam); return 0;
    case WM_SIZE: DeliverSize(window, lParam); return 0;
    case WM_CLOSE:
        // GLFW's Win32 window procedure turns WM_CLOSE into a close request.
        if (g_real.setWindowShouldClose) g_real.setWindowShouldClose(window, 1);
        return 0;
    case WM_SETFOCUS:
        if (g_game.focus) g_game.focus(window, 1);
        return 0;
    case WM_KILLFOCUS:
        if (g_game.focus) g_game.focus(window, 0);
        return 0;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ---------------------------------------------------------------------------------------------
// GLFW callbacks registered by Toolscreen: physical input -> Win32 messages
// ---------------------------------------------------------------------------------------------
LPARAM BuildKeyLParam(const KeyEntry& e, bool down, bool wasDown, bool contextAlt) {
    LPARAM lp = 1;
    lp |= static_cast<LPARAM>(e.scan & 0xFF) << 16;
    if (e.scan & kExtendedFlag) lp |= static_cast<LPARAM>(1) << 24;
    if (contextAlt) lp |= static_cast<LPARAM>(1) << 29;
    if (!down || wasDown) lp |= static_cast<LPARAM>(1) << 30;
    if (!down) lp |= static_cast<LPARAM>(1) << 31;
    return lp;
}

void Bridge_Key(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (!IsGameWindow(window)) {
        if (g_game.key) g_game.key(window, key, scancode, action, mods);
        return;
    }
    const KeyEntry* entry = FindByGlfwKey(key);
    if (!entry) entry = FindByMacKeyCode(scancode);
    if (!entry) {
        // A key Windows has no virtual-key code for: it bypasses Toolscreen.
        if (g_game.key) g_game.key(window, key, scancode, action, mods);
        return;
    }
    const bool down = action != GLFW_RELEASE_;
    const bool wasDown = macwin::IsPhysicalKeyDown(entry->sidedVk);
    const bool otherAltDown = entry->vk == VK_MENU ? macwin::IsPhysicalKeyDown(entry->sidedVk == VK_LMENU ? VK_RMENU : VK_LMENU)
                                                   : macwin::IsPhysicalKeyDown(VK_MENU);
    const bool altDown = otherAltDown || (entry->vk == VK_MENU && down);
    const bool ctrlDown = macwin::IsPhysicalKeyDown(VK_CONTROL) || (entry->vk == VK_CONTROL && down);
    // Windows reports keys pressed with Alt (but not AltGr, i.e. Ctrl+Alt) as system keys.
    const bool sys = (altDown && !ctrlDown) || entry->vk == VK_F10 || (entry->vk == VK_MENU && !ctrlDown);
    const UINT msg = down ? (sys ? WM_SYSKEYDOWN : WM_KEYDOWN) : (sys ? WM_SYSKEYUP : WM_KEYUP);

    KBDLLHOOKSTRUCT ll{};
    ll.vkCode = entry->sidedVk;
    ll.scanCode = entry->scan & 0xFF;
    ll.flags = ((entry->scan & kExtendedFlag) ? LLKHF_EXTENDED : 0) | (down ? 0 : LLKHF_UP) | (altDown ? LLKHF_ALTDOWN : 0);
    ll.time = GetTickCount();
    if (macwin::RunLowLevelKeyboardHooks(msg, ll)) return;

    macwin::SetPhysicalKeyDown(entry->sidedVk, down);
    if (entry->sidedVk != entry->vk && !down) {
        // Generic modifier state is derived from the sided keys.
        macwin::SetPhysicalKeyDown(entry->vk, false);
    }
    const LPARAM lp = BuildKeyLParam(*entry, down, wasDown, sys && altDown);
    g_bridge.lastKeyLParam = lp;
    Dispatch(msg, entry->vk, lp);
}

void Bridge_CharMods(GLFWwindow* window, unsigned int codepoint, int mods) {
    if (!IsGameWindow(window)) {
        if (g_game.charMods) g_game.charMods(window, codepoint, mods);
        if (g_game.character) g_game.character(window, codepoint);
        return;
    }
    LPARAM lp = g_bridge.lastKeyLParam & ~(static_cast<LPARAM>(1) << 31);
    if (codepoint > 0xFFFF) {
        const uint32_t v = codepoint - 0x10000;
        Dispatch(WM_CHAR, 0xD800 + (v >> 10), lp);
        Dispatch(WM_CHAR, 0xDC00 + (v & 0x3FF), lp);
    } else {
        Dispatch(WM_CHAR, codepoint, lp);
    }
}

void Bridge_MouseButton(GLFWwindow* window, int button, int action, int mods) {
    if (!IsGameWindow(window) || button > 4) {
        if (g_game.mouseButton) g_game.mouseButton(window, button, action, mods);
        return;
    }
    static const int kVk[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
    const bool down = action == GLFW_PRESS_;
    macwin::SetPhysicalKeyDown(static_cast<uint16_t>(kVk[button]), down);
    UINT msg = 0;
    WPARAM wParam = CurrentMouseKeyFlags();
    switch (button) {
    case 0: msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP; break;
    case 1: msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP; break;
    case 2: msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP; break;
    default:
        msg = down ? WM_XBUTTONDOWN : WM_XBUTTONUP;
        wParam = MAKEWPARAM(wParam, button == 3 ? XBUTTON1 : XBUTTON2);
        break;
    }
    Dispatch(msg, wParam, MAKELPARAM(g_bridge.lastClientX, g_bridge.lastClientY));
}

void Bridge_CursorPos(GLFWwindow* window, double x, double y) {
    if (!IsGameWindow(window)) {
        if (g_game.cursorPos) g_game.cursorPos(window, x, y);
        return;
    }
    if (g_bridge.cursorMode == kGlfwCursorDisabled) {
        const double dx = x - g_bridge.lastRawX;
        const double dy = y - g_bridge.lastRawY;
        g_bridge.lastRawX = x;
        g_bridge.lastRawY = y;
        g_bridge.fracX += dx * 256.0;
        g_bridge.fracY += dy * 256.0;
        const LONG ix = static_cast<LONG>(std::trunc(g_bridge.fracX));
        const LONG iy = static_cast<LONG>(std::trunc(g_bridge.fracY));
        g_bridge.fracX -= ix;
        g_bridge.fracY -= iy;
        if (ix != 0 || iy != 0) Dispatch(WM_INPUT, 0 /* RIM_INPUT */, reinterpret_cast<LPARAM>(macwin::MakeRawMouseInput(ix, iy)));
        return;
    }
    g_bridge.lastRawX = x;
    g_bridge.lastRawY = y;
    const double scale = BackingScale();
    g_bridge.lastClientX = static_cast<int>(std::floor(x * scale));
    g_bridge.lastClientY = static_cast<int>(std::floor(y * scale));
    Dispatch(WM_MOUSEMOVE, CurrentMouseKeyFlags(), MAKELPARAM(g_bridge.lastClientX, g_bridge.lastClientY));
}

void Bridge_Scroll(GLFWwindow* window, double xoff, double yoff) {
    if (!IsGameWindow(window)) {
        if (g_game.scroll) g_game.scroll(window, xoff, yoff);
        return;
    }
    POINT screen{ g_bridge.lastClientX, g_bridge.lastClientY };
    ClientToScreen(macwin::GameHwnd(), &screen);
    if (yoff != 0.0) {
        long delta = std::lround(yoff * WHEEL_DELTA);
        if (delta == 0) delta = yoff > 0 ? 1 : -1;
        g_bridge.pendingScrollY = yoff;
        Dispatch(WM_MOUSEWHEEL, MAKEWPARAM(CurrentMouseKeyFlags(), static_cast<short>(delta)), MAKELPARAM(screen.x, screen.y));
        g_bridge.pendingScrollY = 0.0;
    }
    if (xoff != 0.0) {
        long delta = std::lround(-xoff * WHEEL_DELTA); // GLFW's xoffset is the negated Win32 horizontal wheel
        if (delta == 0) delta = xoff > 0 ? -1 : 1;
        g_bridge.pendingScrollX = xoff;
        Dispatch(WM_MOUSEHWHEEL, MAKEWPARAM(CurrentMouseKeyFlags(), static_cast<short>(delta)), MAKELPARAM(screen.x, screen.y));
        g_bridge.pendingScrollX = 0.0;
    }
}

void Bridge_Focus(GLFWwindow* window, int focused) {
    if (!IsGameWindow(window)) {
        if (g_game.focus) g_game.focus(window, focused);
        return;
    }
    macwin::RefreshWindowGeometry();
    if (focused) {
        Dispatch(WM_ACTIVATEAPP, TRUE, 0);
        Dispatch(WM_ACTIVATE, WA_ACTIVE, 0);
        Dispatch(WM_SETFOCUS, 0, 0);
    } else {
        Dispatch(WM_ACTIVATE, WA_INACTIVE, 0);
        Dispatch(WM_ACTIVATEAPP, FALSE, 0);
        Dispatch(WM_KILLFOCUS, 0, 0);
    }
}

void Bridge_CursorEnter(GLFWwindow* window, int entered) {
    if (IsGameWindow(window) && !entered) Dispatch(WM_MOUSELEAVE, 0, 0);
    if (g_game.cursorEnter) g_game.cursorEnter(window, entered);
}

void Bridge_WindowPos(GLFWwindow* window, int x, int y) {
    if (IsGameWindow(window)) {
        macwin::RefreshWindowGeometry();
        POINT origin{ 0, 0 };
        ClientToScreen(macwin::GameHwnd(), &origin);
        Dispatch(WM_MOVE, 0, MAKELPARAM(origin.x, origin.y));
    }
    if (g_game.windowPos) g_game.windowPos(window, x, y);
}

void Bridge_FramebufferSize(GLFWwindow* window, int width, int height) {
    if (!IsGameWindow(window)) {
        if (g_game.framebufferSize) g_game.framebufferSize(window, width, height);
        return;
    }
    if (width > 0 && height > 0) macwin::SetFramebufferSize(width, height);
    macwin::RefreshWindowGeometry();
    const bool iconified = g_real.getWindowAttrib && g_real.getWindowAttrib(window, kGlfwIconified);
    Dispatch(WM_SIZE, iconified ? SIZE_MINIMIZED : SIZE_RESTORED, MAKELPARAM(width, height));
}

void Bridge_WindowSize(GLFWwindow* window, int width, int height) {
    if (!IsGameWindow(window)) {
        if (g_game.windowSize) g_game.windowSize(window, width, height);
        return;
    }
    // Minecraft's window size is reported in framebuffer pixels from the framebuffer callback.
    macwin::RefreshWindowGeometry();
}

void RegisterBridgeCallbacks(GLFWwindow* window) {
    if (g_real.setKeyCallback) g_real.setKeyCallback(window, &Bridge_Key);
    if (g_real.setCharModsCallback) g_real.setCharModsCallback(window, &Bridge_CharMods);
    if (g_real.setCharCallback) g_real.setCharCallback(window, nullptr);
    if (g_real.setMouseButtonCallback) g_real.setMouseButtonCallback(window, &Bridge_MouseButton);
    if (g_real.setCursorPosCallback) g_real.setCursorPosCallback(window, &Bridge_CursorPos);
    if (g_real.setScrollCallback) g_real.setScrollCallback(window, &Bridge_Scroll);
    if (g_real.setWindowFocusCallback) g_real.setWindowFocusCallback(window, &Bridge_Focus);
    if (g_real.setCursorEnterCallback) g_real.setCursorEnterCallback(window, &Bridge_CursorEnter);
    if (g_real.setWindowPosCallback) g_real.setWindowPosCallback(window, &Bridge_WindowPos);
    if (g_real.setFramebufferSizeCallback) g_real.setFramebufferSizeCallback(window, &Bridge_FramebufferSize);
    if (g_real.setWindowSizeCallback) g_real.setWindowSizeCallback(window, &Bridge_WindowSize);
}

// ---------------------------------------------------------------------------------------------
// Functions handed to LWJGL in place of GLFW's
// ---------------------------------------------------------------------------------------------
template <class Fn> Fn StoreCallback(Fn& slot, Fn callback) {
    Fn previous = slot;
    slot = callback;
    return previous;
}

GLFWkeyfun Hk_SetKeyCallback(GLFWwindow* w, GLFWkeyfun cb) {
    if (!IsGameWindow(w)) return g_real.setKeyCallback ? g_real.setKeyCallback(w, cb) : nullptr;
    return StoreCallback(g_game.key, cb);
}
GLFWcharfun Hk_SetCharCallback(GLFWwindow* w, GLFWcharfun cb) {
    if (!IsGameWindow(w)) return g_real.setCharCallback ? g_real.setCharCallback(w, cb) : nullptr;
    return StoreCallback(g_game.character, cb);
}
GLFWcharmodsfun Hk_SetCharModsCallback(GLFWwindow* w, GLFWcharmodsfun cb) {
    if (!IsGameWindow(w)) return g_real.setCharModsCallback ? g_real.setCharModsCallback(w, cb) : nullptr;
    return StoreCallback(g_game.charMods, cb);
}
GLFWmousebuttonfun Hk_SetMouseButtonCallback(GLFWwindow* w, GLFWmousebuttonfun cb) {
    if (!IsGameWindow(w)) return g_real.setMouseButtonCallback ? g_real.setMouseButtonCallback(w, cb) : nullptr;
    return StoreCallback(g_game.mouseButton, cb);
}
GLFWcursorposfun Hk_SetCursorPosCallback(GLFWwindow* w, GLFWcursorposfun cb) {
    if (!IsGameWindow(w)) return g_real.setCursorPosCallback ? g_real.setCursorPosCallback(w, cb) : nullptr;
    return StoreCallback(g_game.cursorPos, cb);
}
GLFWscrollfun Hk_SetScrollCallback(GLFWwindow* w, GLFWscrollfun cb) {
    if (!IsGameWindow(w)) return g_real.setScrollCallback ? g_real.setScrollCallback(w, cb) : nullptr;
    return StoreCallback(g_game.scroll, cb);
}
GLFWwindowfocusfun Hk_SetWindowFocusCallback(GLFWwindow* w, GLFWwindowfocusfun cb) {
    if (!IsGameWindow(w)) return g_real.setWindowFocusCallback ? g_real.setWindowFocusCallback(w, cb) : nullptr;
    return StoreCallback(g_game.focus, cb);
}
GLFWcursorenterfun Hk_SetCursorEnterCallback(GLFWwindow* w, GLFWcursorenterfun cb) {
    if (!IsGameWindow(w)) return g_real.setCursorEnterCallback ? g_real.setCursorEnterCallback(w, cb) : nullptr;
    return StoreCallback(g_game.cursorEnter, cb);
}
GLFWwindowposfun Hk_SetWindowPosCallback(GLFWwindow* w, GLFWwindowposfun cb) {
    if (!IsGameWindow(w)) return g_real.setWindowPosCallback ? g_real.setWindowPosCallback(w, cb) : nullptr;
    return StoreCallback(g_game.windowPos, cb);
}

// Toolscreen's own resize-callback hooks call these as the "original" GLFW setters.
GLFWWINDOWSIZEFUN Orig_SetWindowSizeCallback(void* w, GLFWWINDOWSIZEFUN cb) {
    if (!IsGameWindow(w)) return g_real.setWindowSizeCallback ? g_real.setWindowSizeCallback(w, cb) : nullptr;
    return StoreCallback(g_game.windowSize, cb);
}
GLFWFRAMEBUFFERSIZEFUN Orig_SetFramebufferSizeCallback(void* w, GLFWFRAMEBUFFERSIZEFUN cb) {
    if (!IsGameWindow(w)) return g_real.setFramebufferSizeCallback ? g_real.setFramebufferSizeCallback(w, cb) : nullptr;
    return StoreCallback(g_game.framebufferSize, cb);
}

HWND Orig_GetWin32Window(void* window) { return static_cast<HWND>(window); }
void* Orig_GetCurrentContext() { return g_real.getCurrentContext ? g_real.getCurrentContext() : nullptr; }

void* Orig_CreateWindow(int width, int height, const char* title, void* monitor, void* share) {
    void* window = g_real.createWindow ? g_real.createWindow(width, height, title, monitor, share) : nullptr;
    if (!window || macwin::GameHwnd()) return window;
    void* nsWindow = g_real.getCocoaWindow ? g_real.getCocoaWindow(window) : nullptr;
    macwin::SetGameWindow(window, nsWindow, GetCurrentThreadId());
    macwin::SetBaseWindowProc(&MacGameWndProc);
    macwin::RefreshWindowGeometry();
    RefreshFramebufferSize(window);
    RegisterBridgeCallbacks(window);
    double x = 0, y = 0;
    if (g_real.getCursorPos) g_real.getCursorPos(window, &x, &y);
    g_bridge.lastRawX = x;
    g_bridge.lastRawY = y;
    LogCategory("init", "[macOS] Minecraft window created; GLFW input is routed through Toolscreen.");
    // The interposed GLFW/GL entry points are live from here on (the Windows build sets this once its
    // detours are enabled).
    g_gameWindowHooksReady.store(true, std::memory_order_release);
    GameTest::StartRunner(macwin::GameHwnd());
    return window;
}

void Orig_DestroyWindow(void* window) {
    if (IsGameWindow(window)) {
        macwin::ClearGameWindow(window);
        g_game = GameCallbacks{};
    }
    if (g_real.destroyWindow) g_real.destroyWindow(window);
}

void Orig_SetInputMode(void* window, int mode, int value) {
    if (g_real.setInputMode) g_real.setInputMode(window, mode, value);
    if (!IsGameWindow(window) || mode != kGlfwCursor) return;
    const int previous = g_bridge.cursorMode;
    g_bridge.cursorMode = value;
    if (previous != value && g_real.getCursorPos) {
        double x = 0, y = 0;
        g_real.getCursorPos(window, &x, &y);
        g_bridge.lastRawX = x;
        g_bridge.lastRawY = y;
        g_bridge.fracX = g_bridge.fracY = 0.0;
    }
}

void* Hk_CreateWindow(int width, int height, const char* title, void* monitor, void* share) {
    ToolscreenMacEnsureInitialized();
    return hkglfwCreateWindow(width, height, title, monitor, share);
}

// Real-swap target for SwapBuffersHook_Impl.
BOOL WINAPI Orig_SwapBuffers(HDC dc) {
    if (g_real.swapBuffers) g_real.swapBuffers(static_cast<void*>(dc));
    return TRUE;
}

bool CurrentContextIsLegacy() {
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    return version && version[0] == '2' && version[1] == '.';
}

void Hk_SwapBuffers(GLFWwindow* window) {
    if (!IsGameWindow(window)) {
        if (g_real.swapBuffers) g_real.swapBuffers(window);
        return;
    }
    macwin::PumpGameWindowMessages();
    macwin::RefreshWindowGeometry();
    RefreshFramebufferSize(window);

    // Apple's legacy (2.1) context keeps Minecraft's fixed-function alpha test enabled across the
    // frame; it would discard Toolscreen's translucent overlay fragments.
    if (!g_bridge.legacyContextKnown && CGLGetCurrentContext()) {
        g_bridge.legacyContext = CurrentContextIsLegacy();
        g_bridge.legacyContextKnown = true;
    }
    const bool restoreAlphaTest = g_bridge.legacyContext && glIsEnabled(GL_ALPHA_TEST);
    if (restoreAlphaTest) glDisable(GL_ALPHA_TEST);

    hkwglSwapBuffers(reinterpret_cast<HDC>(window));

    if (restoreAlphaTest) glEnable(GL_ALPHA_TEST);
}

void Hk_PollEvents() {
    macwin::PumpGameWindowMessages();
    if (g_real.pollEvents) g_real.pollEvents();
    macwin::PumpGameWindowMessages();
}

void Hk_WaitEvents() {
    macwin::PumpGameWindowMessages();
    if (g_real.waitEvents) g_real.waitEvents();
    macwin::PumpGameWindowMessages();
}

void Hk_WaitEventsTimeout(double timeout) {
    macwin::PumpGameWindowMessages();
    if (g_real.waitEventsTimeout) g_real.waitEventsTimeout(timeout);
    macwin::PumpGameWindowMessages();
}

int Hk_GetKey(GLFWwindow* window, int key) {
    if (!IsGameWindow(window)) return g_real.getKey ? g_real.getKey(window, key) : 0;
    if (key < 0 || key > GLFW_KEY_LAST_) return GLFW_RELEASE_;
    return g_gameKeys[key] ? GLFW_PRESS_ : GLFW_RELEASE_;
}

int Hk_GetMouseButton(GLFWwindow* window, int button) {
    if (!IsGameWindow(window)) return g_real.getMouseButton ? g_real.getMouseButton(window, button) : 0;
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST_) return GLFW_RELEASE_;
    return g_gameButtons[button] ? GLFW_PRESS_ : GLFW_RELEASE_;
}

void Hk_GetWindowSize(GLFWwindow* window, int* width, int* height) {
    if (!IsGameWindow(window) || !g_real.getFramebufferSize) {
        if (g_real.getWindowSize) g_real.getWindowSize(window, width, height);
        return;
    }
    // Minecraft is told its window is as large as its framebuffer (1 window unit = 1 pixel).
    g_real.getFramebufferSize(window, width, height);
}

void Hk_GetCursorPos(GLFWwindow* window, double* x, double* y) {
    if (!IsGameWindow(window)) {
        if (g_real.getCursorPos) g_real.getCursorPos(window, x, y);
        return;
    }
    if (g_bridge.cursorMode == kGlfwCursorDisabled) {
        if (x) *x = g_bridge.virtualX;
        if (y) *y = g_bridge.virtualY;
        return;
    }
    double px = 0, py = 0;
    if (g_real.getCursorPos) g_real.getCursorPos(window, &px, &py);
    const double scale = BackingScale();
    if (x) *x = px * scale;
    if (y) *y = py * scale;
}

// Minecraft keeps its windowed size in the units it was given (framebuffer pixels) and hands that size back
// when it leaves fullscreen; GLFW expects window points.
int PixelsToPoints(int pixels) { return std::max(1, static_cast<int>(std::lround(pixels / BackingScale()))); }

void Hk_SetWindowMonitor(GLFWwindow* window, void* monitor, int x, int y, int width, int height, int refresh) {
    if (IsGameWindow(window) && !monitor) {
        width = PixelsToPoints(width);
        height = PixelsToPoints(height);
    }
    if (g_real.setWindowMonitor) g_real.setWindowMonitor(window, monitor, x, y, width, height, refresh);
}

void Hk_SetWindowSize(GLFWwindow* window, int width, int height) {
    if (IsGameWindow(window)) {
        width = PixelsToPoints(width);
        height = PixelsToPoints(height);
    }
    if (g_real.setWindowSize) g_real.setWindowSize(window, width, height);
}

// oSetCursorPos target: screen pixels -> GLFW window points.
BOOL WINAPI Orig_SetCursorPosScreen(int X, int Y) {
    const HWND hwnd = macwin::GameHwnd();
    if (!hwnd || !g_real.setCursorPos) return SetCursorPos(X, Y);
    POINT p{ X, Y };
    ScreenToClient(hwnd, &p);
    const double scale = BackingScale();
    g_real.setCursorPos(static_cast<void*>(hwnd), p.x / scale, p.y / scale);
    return TRUE;
}

void Hk_SetCursorPos(GLFWwindow* window, double x, double y) {
    if (!IsGameWindow(window)) {
        if (g_real.setCursorPos) g_real.setCursorPos(window, x, y);
        return;
    }
    const double scale = BackingScale();
    if (g_bridge.cursorMode == kGlfwCursorDisabled) {
        g_bridge.virtualX = x;
        g_bridge.virtualY = y;
        if (g_real.setCursorPos) g_real.setCursorPos(window, x / scale, y / scale);
        if (g_real.getCursorPos) g_real.getCursorPos(window, &g_bridge.lastRawX, &g_bridge.lastRawY);
        return;
    }
    // Like GLFW's Win32 backend: client -> screen, then SetCursorPos (which Toolscreen hooks).
    g_bridge.virtualX = x;
    g_bridge.virtualY = y;
    POINT p{ static_cast<LONG>(std::lround(x)), static_cast<LONG>(std::lround(y)) };
    ClientToScreen(macwin::GameHwnd(), &p);
    hkSetCursorPos(p.x, p.y);
}

// ---------------------------------------------------------------------------------------------
// Hook tables
// ---------------------------------------------------------------------------------------------
template <class T> void Assign(T& slot, void* real) { slot = reinterpret_cast<T>(real); }

void RecordRealGlfw(const char* name, void* real) {
    static const std::unordered_map<std::string, void (*)(void*)> kRecorders = {
        { "glfwSwapBuffers", [](void* p) { Assign(g_real.swapBuffers, p); } },
        { "glfwPollEvents", [](void* p) { Assign(g_real.pollEvents, p); } },
        { "glfwWaitEvents", [](void* p) { Assign(g_real.waitEvents, p); } },
        { "glfwWaitEventsTimeout", [](void* p) { Assign(g_real.waitEventsTimeout, p); } },
        { "glfwGetCocoaWindow", [](void* p) { Assign(g_real.getCocoaWindow, p); } },
        { "glfwGetCurrentContext", [](void* p) { Assign(g_real.getCurrentContext, p); } },
        { "glfwGetFramebufferSize", [](void* p) { Assign(g_real.getFramebufferSize, p); } },
        { "glfwGetWindowSize", [](void* p) { Assign(g_real.getWindowSize, p); } },
        { "glfwGetCursorPos", [](void* p) { Assign(g_real.getCursorPos, p); } },
        { "glfwSetCursorPos", [](void* p) { Assign(g_real.setCursorPos, p); } },
        { "glfwSetInputMode", [](void* p) { Assign(g_real.setInputMode, p); } },
        { "glfwGetWindowAttrib", [](void* p) { Assign(g_real.getWindowAttrib, p); } },
        { "glfwGetKey", [](void* p) { Assign(g_real.getKey, p); } },
        { "glfwGetMouseButton", [](void* p) { Assign(g_real.getMouseButton, p); } },
        { "glfwSetWindowShouldClose", [](void* p) { Assign(g_real.setWindowShouldClose, p); } },
        { "glfwSetWindowMonitor", [](void* p) { Assign(g_real.setWindowMonitor, p); } },
        { "glfwSetWindowSize", [](void* p) { Assign(g_real.setWindowSize, p); } },
        { "glfwCreateWindow", [](void* p) { Assign(g_real.createWindow, p); } },
        { "glfwDestroyWindow", [](void* p) { Assign(g_real.destroyWindow, p); } },
        { "glfwSetCursor", [](void* p) { Assign(g_real.setCursor, p); } },
        { "glfwWindowHint", [](void* p) { Assign(g_real.windowHint, p); } },
        { "glfwSetKeyCallback", [](void* p) { Assign(g_real.setKeyCallback, p); } },
        { "glfwSetCharCallback", [](void* p) { Assign(g_real.setCharCallback, p); } },
        { "glfwSetCharModsCallback", [](void* p) { Assign(g_real.setCharModsCallback, p); } },
        { "glfwSetMouseButtonCallback", [](void* p) { Assign(g_real.setMouseButtonCallback, p); } },
        { "glfwSetCursorPosCallback", [](void* p) { Assign(g_real.setCursorPosCallback, p); } },
        { "glfwSetScrollCallback", [](void* p) { Assign(g_real.setScrollCallback, p); } },
        { "glfwSetWindowFocusCallback", [](void* p) { Assign(g_real.setWindowFocusCallback, p); } },
        { "glfwSetCursorEnterCallback", [](void* p) { Assign(g_real.setCursorEnterCallback, p); } },
        { "glfwSetWindowPosCallback", [](void* p) { Assign(g_real.setWindowPosCallback, p); } },
        { "glfwSetWindowSizeCallback", [](void* p) { Assign(g_real.setWindowSizeCallback, p); } },
        { "glfwSetFramebufferSizeCallback", [](void* p) { Assign(g_real.setFramebufferSizeCallback, p); } },
    };
    auto it = kRecorders.find(name);
    if (it != kRecorders.end() && real) it->second(real);
}

void WireDllmainOriginals() {
    static std::once_flag once;
    std::call_once(once, [] {
        owglSwapBuffers = &Orig_SwapBuffers;
        oSetCursorPos = &Orig_SetCursorPosScreen;
        oGetRawInputData = &GetRawInputData;
        oglfwSetInputMode = &Orig_SetInputMode;
        oglfwCreateWindow = &Orig_CreateWindow;
        oglfwDestroyWindow = &Orig_DestroyWindow;
        oglfwSetWindowSizeCallback = &Orig_SetWindowSizeCallback;
        oglfwSetFramebufferSizeCallback = &Orig_SetFramebufferSizeCallback;
        glfwGetWin32WindowProc = &Orig_GetWin32Window;
        glfwGetCurrentContextProc.store(&Orig_GetCurrentContext, std::memory_order_release);
    });
}

} // namespace

extern "C" void ToolscreenMacNoteGlfwLibraryHandle(void* handle) {
    void* expected = nullptr;
    if (!handle || !g_glfwHandle.compare_exchange_strong(expected, handle)) return;
    // Resolve the GLFW functions Toolscreen calls itself, whether or not LWJGL ever asks for them.
    static const char* kNeeded[] = { "glfwGetCocoaWindow",   "glfwGetCurrentContext", "glfwGetFramebufferSize",     "glfwGetWindowSize",
                                     "glfwGetCursorPos",     "glfwSetCursorPos",      "glfwSetInputMode",           "glfwGetWindowAttrib",
                                     "glfwGetKey",           "glfwGetMouseButton",    "glfwSwapBuffers",            "glfwPollEvents",
                                     "glfwCreateWindow",     "glfwDestroyWindow",     "glfwSetCursor",              "glfwWindowHint",
                                     "glfwSetKeyCallback",   "glfwSetCharCallback",   "glfwSetCharModsCallback",    "glfwSetMouseButtonCallback",
                                     "glfwSetCursorPosCallback", "glfwSetScrollCallback", "glfwSetWindowFocusCallback", "glfwSetCursorEnterCallback",
                                     "glfwSetWindowPosCallback", "glfwSetWindowSizeCallback", "glfwSetFramebufferSizeCallback",
                                     "glfwWaitEvents",       "glfwWaitEventsTimeout", "glfwSetWindowShouldClose",
                                     "glfwSetWindowMonitor", "glfwSetWindowSize" };
    for (const char* name : kNeeded) RecordRealGlfw(name, dlsym(handle, name));
    WireDllmainOriginals();
    oglfwSetCursor = reinterpret_cast<GLFWSETCURSOR>(g_real.setCursor);
    oglfwWindowHint = reinterpret_cast<GLFWWINDOWHINT>(g_real.windowHint);
}

extern "C" void* ToolscreenMacResolveGlfwHook(const char* name, void* real) {
    RecordRealGlfw(name, real);
    WireDllmainOriginals();
    const std::string n = name;
    if (n == "glfwCreateWindow") return reinterpret_cast<void*>(&Hk_CreateWindow);
    if (n == "glfwDestroyWindow") return reinterpret_cast<void*>(&hkglfwDestroyWindow);
    if (n == "glfwWindowHint") {
        oglfwWindowHint = reinterpret_cast<GLFWWINDOWHINT>(real);
        return reinterpret_cast<void*>(&hkglfwWindowHint);
    }
    if (n == "glfwSetCursor") {
        oglfwSetCursor = reinterpret_cast<GLFWSETCURSOR>(real);
        return reinterpret_cast<void*>(&hkglfwSetCursor);
    }
    if (n == "glfwSwapBuffers") return reinterpret_cast<void*>(&Hk_SwapBuffers);
    if (n == "glfwPollEvents") return reinterpret_cast<void*>(&Hk_PollEvents);
    if (n == "glfwWaitEvents") return reinterpret_cast<void*>(&Hk_WaitEvents);
    if (n == "glfwWaitEventsTimeout") return reinterpret_cast<void*>(&Hk_WaitEventsTimeout);
    if (n == "glfwSetInputMode") return reinterpret_cast<void*>(&hkglfwSetInputMode);
    if (n == "glfwSetCursorPos") return reinterpret_cast<void*>(&Hk_SetCursorPos);
    if (n == "glfwGetCursorPos") return reinterpret_cast<void*>(&Hk_GetCursorPos);
    if (n == "glfwGetWindowSize") return reinterpret_cast<void*>(&Hk_GetWindowSize);
    if (n == "glfwSetWindowSize") return reinterpret_cast<void*>(&Hk_SetWindowSize);
    if (n == "glfwSetWindowMonitor") return reinterpret_cast<void*>(&Hk_SetWindowMonitor);
    if (n == "glfwGetKey") return reinterpret_cast<void*>(&Hk_GetKey);
    if (n == "glfwGetMouseButton") return reinterpret_cast<void*>(&Hk_GetMouseButton);
    if (n == "glfwSetKeyCallback") return reinterpret_cast<void*>(&Hk_SetKeyCallback);
    if (n == "glfwSetCharCallback") return reinterpret_cast<void*>(&Hk_SetCharCallback);
    if (n == "glfwSetCharModsCallback") return reinterpret_cast<void*>(&Hk_SetCharModsCallback);
    if (n == "glfwSetMouseButtonCallback") return reinterpret_cast<void*>(&Hk_SetMouseButtonCallback);
    if (n == "glfwSetCursorPosCallback") return reinterpret_cast<void*>(&Hk_SetCursorPosCallback);
    if (n == "glfwSetScrollCallback") return reinterpret_cast<void*>(&Hk_SetScrollCallback);
    if (n == "glfwSetWindowFocusCallback") return reinterpret_cast<void*>(&Hk_SetWindowFocusCallback);
    if (n == "glfwSetCursorEnterCallback") return reinterpret_cast<void*>(&Hk_SetCursorEnterCallback);
    if (n == "glfwSetWindowPosCallback") return reinterpret_cast<void*>(&Hk_SetWindowPosCallback);
    if (n == "glfwSetWindowSizeCallback") return reinterpret_cast<void*>(&hkglfwSetWindowSizeCallback);
    if (n == "glfwSetFramebufferSizeCallback") return reinterpret_cast<void*>(&hkglfwSetFramebufferSizeCallback);
    return nullptr;
}

extern "C" void* ToolscreenMacResolveGlHook(const char* name, void* real) {
    const std::string n = name;
    if (n == "glViewport") {
        oglViewport = reinterpret_cast<GLVIEWPORTPROC>(real);
        return reinterpret_cast<void*>(&ToolscreenMac_glViewport);
    }
    if (n == "glBindTexture") {
        oglBindTexture = reinterpret_cast<GLBINDTEXTUREPROC>(real);
        return reinterpret_cast<void*>(&ToolscreenMac_glBindTexture);
    }
    if (n == "glBindFramebuffer") {
        oglBindFramebuffer = reinterpret_cast<GLBINDFRAMEBUFFERPROC>(real);
        return reinterpret_cast<void*>(&ToolscreenMac_glBindFramebuffer);
    }
    if (n == "glBlitFramebuffer") {
        oglBlitFramebuffer = reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC_HOOK>(real);
        g_glBlitFramebufferHooked.store(true, std::memory_order_release);
        return reinterpret_cast<void*>(&hkglBlitFramebuffer);
    }
    return nullptr;
}

extern "C" void ToolscreenMacNoteLwjglImage(const char* path) {
    static std::atomic<int> s_logged{ 0 };
    if (s_logged.fetch_add(1) < 8) {
        // Logging may not be initialised yet when early LWJGL images load; stderr is always safe.
        fprintf(stderr, "[Toolscreen] Interposed LWJGL symbol lookups in %s\n", path ? path : "?");
    }
}
