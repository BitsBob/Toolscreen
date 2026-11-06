// Dear ImGui platform backend for the macOS port, driven by Win32-style window messages.
//
// Toolscreen's input pipeline (hooks/input_hook.cpp) forwards window messages to
// ImGui_ImplWin32_WndProcHandler. On macOS those messages are synthesised from GLFW callbacks by
// mac_input_bridge.cpp, so this backend implements the same contract as Dear ImGui's own
// imgui_impl_win32.cpp: client-area pixel coordinates, VK_* key codes and UTF-16 WM_CHAR units.
#include "imgui.h"
#include "imgui_impl_win32.h"

#include "platform/mac/mac_platform_bridge.h"

#include <cfloat>
#include <cstdlib>

namespace {

struct BackendData {
    HWND hwnd = nullptr;
    LARGE_INTEGER ticksPerSecond{};
    LARGE_INTEGER time{};
    ImGuiMouseCursor lastMouseCursor = ImGuiMouseCursor_COUNT;
    int mouseButtonsDown = 0;
    bool mouseTracked = false;
    char* clipboardText = nullptr;
    WCHAR pendingHighSurrogate = 0;
};

BackendData* GetBackendData() {
    return ImGui::GetCurrentContext() ? static_cast<BackendData*>(ImGui::GetIO().BackendPlatformUserData) : nullptr;
}

bool IsVkDown(int vk) { return (::GetKeyState(vk) & 0x8000) != 0; }

void AddKeyEvent(ImGuiIO& io, ImGuiKey key, bool down, int nativeKeycode, int nativeScancode = -1) {
    io.AddKeyEvent(key, down);
    io.SetKeyEventNativeData(key, nativeKeycode, nativeScancode);
}

void UpdateKeyModifiers(ImGuiIO& io) {
    io.AddKeyEvent(ImGuiMod_Ctrl, IsVkDown(VK_CONTROL));
    io.AddKeyEvent(ImGuiMod_Shift, IsVkDown(VK_SHIFT));
    io.AddKeyEvent(ImGuiMod_Alt, IsVkDown(VK_MENU));
    io.AddKeyEvent(ImGuiMod_Super, IsVkDown(VK_LWIN) || IsVkDown(VK_RWIN));
}

const char* GetClipboardText(ImGuiContext*) {
    BackendData* bd = GetBackendData();
    if (!bd) return "";
    free(bd->clipboardText);
    bd->clipboardText = MacBridge_GetClipboardText();
    return bd->clipboardText ? bd->clipboardText : "";
}

void SetClipboardText(ImGuiContext*, const char* text) { MacBridge_SetClipboardText(text ? text : ""); }

void UpdateMouseCursor(ImGuiIO& io, ImGuiMouseCursor cursor) {
    if (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) return;
    if (cursor == ImGuiMouseCursor_None || io.MouseDrawCursor) return; // Toolscreen controls visibility itself
    LPCWSTR id = IDC_ARROW;
    switch (cursor) {
    case ImGuiMouseCursor_TextInput: id = IDC_IBEAM; break;
    case ImGuiMouseCursor_ResizeAll: id = IDC_SIZEALL; break;
    case ImGuiMouseCursor_ResizeEW: id = IDC_SIZEWE; break;
    case ImGuiMouseCursor_ResizeNS: id = IDC_SIZENS; break;
    case ImGuiMouseCursor_ResizeNESW: id = IDC_SIZENESW; break;
    case ImGuiMouseCursor_ResizeNWSE: id = IDC_SIZENWSE; break;
    case ImGuiMouseCursor_Hand: id = IDC_HAND; break;
    case ImGuiMouseCursor_NotAllowed: id = IDC_NO; break;
    default: break;
    }
    ::SetCursor(::LoadCursorW(nullptr, id));
}

} // namespace

ImGuiKey ImGui_ImplWin32_KeyEventToImGuiKey(WPARAM wParam, LPARAM lParam) {
    if (wParam == VK_RETURN && (HIWORD(lParam) & KF_EXTENDED)) return ImGuiKey_KeypadEnter;
    switch (wParam) {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_OEM_7: return ImGuiKey_Apostrophe;
    case VK_OEM_COMMA: return ImGuiKey_Comma;
    case VK_OEM_MINUS: return ImGuiKey_Minus;
    case VK_OEM_PERIOD: return ImGuiKey_Period;
    case VK_OEM_2: return ImGuiKey_Slash;
    case VK_OEM_1: return ImGuiKey_Semicolon;
    case VK_OEM_PLUS: return ImGuiKey_Equal;
    case VK_OEM_4: return ImGuiKey_LeftBracket;
    case VK_OEM_5: return ImGuiKey_Backslash;
    case VK_OEM_6: return ImGuiKey_RightBracket;
    case VK_OEM_3: return ImGuiKey_GraveAccent;
    case VK_OEM_102: return ImGuiKey_Oem102;
    case VK_CAPITAL: return ImGuiKey_CapsLock;
    case VK_SCROLL: return ImGuiKey_ScrollLock;
    case VK_NUMLOCK: return ImGuiKey_NumLock;
    case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
    case VK_PAUSE: return ImGuiKey_Pause;
    case VK_NUMPAD0: return ImGuiKey_Keypad0;
    case VK_NUMPAD1: return ImGuiKey_Keypad1;
    case VK_NUMPAD2: return ImGuiKey_Keypad2;
    case VK_NUMPAD3: return ImGuiKey_Keypad3;
    case VK_NUMPAD4: return ImGuiKey_Keypad4;
    case VK_NUMPAD5: return ImGuiKey_Keypad5;
    case VK_NUMPAD6: return ImGuiKey_Keypad6;
    case VK_NUMPAD7: return ImGuiKey_Keypad7;
    case VK_NUMPAD8: return ImGuiKey_Keypad8;
    case VK_NUMPAD9: return ImGuiKey_Keypad9;
    case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
    case VK_DIVIDE: return ImGuiKey_KeypadDivide;
    case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case VK_ADD: return ImGuiKey_KeypadAdd;
    case VK_LSHIFT: return ImGuiKey_LeftShift;
    case VK_LCONTROL: return ImGuiKey_LeftCtrl;
    case VK_LMENU: return ImGuiKey_LeftAlt;
    case VK_LWIN: return ImGuiKey_LeftSuper;
    case VK_RSHIFT: return ImGuiKey_RightShift;
    case VK_RCONTROL: return ImGuiKey_RightCtrl;
    case VK_RMENU: return ImGuiKey_RightAlt;
    case VK_RWIN: return ImGuiKey_RightSuper;
    case VK_APPS: return ImGuiKey_Menu;
    case '0': return ImGuiKey_0;
    case '1': return ImGuiKey_1;
    case '2': return ImGuiKey_2;
    case '3': return ImGuiKey_3;
    case '4': return ImGuiKey_4;
    case '5': return ImGuiKey_5;
    case '6': return ImGuiKey_6;
    case '7': return ImGuiKey_7;
    case '8': return ImGuiKey_8;
    case '9': return ImGuiKey_9;
    default: break;
    }
    if (wParam >= 'A' && wParam <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (wParam - 'A'));
    if (wParam >= VK_F1 && wParam <= VK_F24) return static_cast<ImGuiKey>(ImGuiKey_F1 + (wParam - VK_F1));
    if (wParam == VK_BROWSER_BACK) return ImGuiKey_AppBack;
    if (wParam == VK_BROWSER_FORWARD) return ImGuiKey_AppForward;
    return ImGuiKey_None;
}

bool ImGui_ImplWin32_Init(void* hwnd) {
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");
    auto* bd = IM_NEW(BackendData)();
    bd->hwnd = static_cast<HWND>(hwnd);
    ::QueryPerformanceFrequency(&bd->ticksPerSecond);
    ::QueryPerformanceCounter(&bd->time);
    io.BackendPlatformUserData = bd;
    io.BackendPlatformName = "imgui_impl_win32_mac";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos;
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetClipboardTextFn = GetClipboardText;
    platformIo.Platform_SetClipboardTextFn = SetClipboardText;
    ImGui::GetMainViewport()->PlatformHandle = ImGui::GetMainViewport()->PlatformHandleRaw = hwnd;
    return true;
}

bool ImGui_ImplWin32_InitForOpenGL(void* hwnd) { return ImGui_ImplWin32_Init(hwnd); }

void ImGui_ImplWin32_Shutdown() {
    BackendData* bd = GetBackendData();
    if (!bd) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetClipboardTextFn = nullptr;
    platformIo.Platform_SetClipboardTextFn = nullptr;
    free(bd->clipboardText);
    io.BackendPlatformName = nullptr;
    io.BackendPlatformUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_HasSetMousePos);
    IM_DELETE(bd);
}

void ImGui_ImplWin32_NewFrame() {
    BackendData* bd = GetBackendData();
    IM_ASSERT(bd != nullptr && "Did you call ImGui_ImplWin32_Init()?");
    ImGuiIO& io = ImGui::GetIO();

    RECT rect{};
    ::GetClientRect(bd->hwnd, &rect);
    io.DisplaySize = ImVec2(static_cast<float>(rect.right - rect.left), static_cast<float>(rect.bottom - rect.top));

    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);
    io.DeltaTime = static_cast<float>(now.QuadPart - bd->time.QuadPart) / static_cast<float>(bd->ticksPerSecond.QuadPart);
    if (io.DeltaTime <= 0.0f) io.DeltaTime = 1.0f / 1000.0f;
    bd->time = now;

    const bool focused = ::GetForegroundWindow() == bd->hwnd;
    if (focused) {
        if (io.WantSetMousePos) {
            POINT pos{ static_cast<LONG>(io.MousePos.x), static_cast<LONG>(io.MousePos.y) };
            if (::ClientToScreen(bd->hwnd, &pos)) ::SetCursorPos(pos.x, pos.y);
        }
        if (!io.WantSetMousePos && !bd->mouseTracked) {
            POINT pos{};
            if (::GetCursorPos(&pos) && ::ScreenToClient(bd->hwnd, &pos)) io.AddMousePosEvent(static_cast<float>(pos.x), static_cast<float>(pos.y));
        }
    }

    // Shift/Command key-ups can be lost when both sides are released together.
    if (ImGui::IsKeyDown(ImGuiKey_LeftShift) && !IsVkDown(VK_LSHIFT)) AddKeyEvent(io, ImGuiKey_LeftShift, false, VK_LSHIFT);
    if (ImGui::IsKeyDown(ImGuiKey_RightShift) && !IsVkDown(VK_RSHIFT)) AddKeyEvent(io, ImGuiKey_RightShift, false, VK_RSHIFT);
    if (ImGui::IsKeyDown(ImGuiKey_LeftSuper) && !IsVkDown(VK_LWIN)) AddKeyEvent(io, ImGuiKey_LeftSuper, false, VK_LWIN);
    if (ImGui::IsKeyDown(ImGuiKey_RightSuper) && !IsVkDown(VK_RWIN)) AddKeyEvent(io, ImGuiKey_RightSuper, false, VK_RWIN);

    const ImGuiMouseCursor cursor = io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
    if (bd->lastMouseCursor != cursor) {
        bd->lastMouseCursor = cursor;
        UpdateMouseCursor(io, cursor);
    }
}

void ImGui_ImplWin32_EnableDpiAwareness() {}
float ImGui_ImplWin32_GetDpiScaleForHwnd(void*) { return static_cast<float>(MacBridge_GetCachedBackingScale()); }
float ImGui_ImplWin32_GetDpiScaleForMonitor(void*) { return static_cast<float>(MacBridge_GetCachedBackingScale()); }

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!ImGui::GetCurrentContext()) return 0;
    BackendData* bd = GetBackendData();
    if (!bd) return 0;
    ImGuiIO& io = ImGui::GetIO();

    switch (msg) {
    case WM_MOUSEMOVE:
        bd->mouseTracked = true;
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMousePosEvent(static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam)));
        return 0;
    case WM_MOUSELEAVE:
        bd->mouseTracked = false;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK: {
        int button = 0;
        if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) button = 1;
        if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONDBLCLK) button = 2;
        if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONDBLCLK) button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 3 : 4;
        if (bd->mouseButtonsDown == 0 && ::GetCapture() == nullptr) ::SetCapture(hwnd);
        bd->mouseButtonsDown |= 1 << button;
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMouseButtonEvent(button, true);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP: {
        int button = 0;
        if (msg == WM_RBUTTONUP) button = 1;
        if (msg == WM_MBUTTONUP) button = 2;
        if (msg == WM_XBUTTONUP) button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 3 : 4;
        bd->mouseButtonsDown &= ~(1 << button);
        if (bd->mouseButtonsDown == 0 && ::GetCapture() == hwnd) ::ReleaseCapture();
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        io.AddMouseButtonEvent(button, false);
        return 0;
    }
    case WM_MOUSEWHEEL:
        io.AddMouseWheelEvent(0.0f, static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA));
        return 0;
    case WM_MOUSEHWHEEL:
        io.AddMouseWheelEvent(-static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA), 0.0f);
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP: {
        const bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
        if (wParam < 256) {
            UpdateKeyModifiers(io);
            const ImGuiKey key = ImGui_ImplWin32_KeyEventToImGuiKey(wParam, lParam);
            const int vk = static_cast<int>(wParam);
            const int scancode = static_cast<int>(LOBYTE(HIWORD(lParam)));
            if (key != ImGuiKey_None) AddKeyEvent(io, key, down, vk, scancode);
            if (vk == VK_SHIFT) {
                if (IsVkDown(VK_LSHIFT) == down) AddKeyEvent(io, ImGuiKey_LeftShift, down, VK_LSHIFT, scancode);
                if (IsVkDown(VK_RSHIFT) == down) AddKeyEvent(io, ImGuiKey_RightShift, down, VK_RSHIFT, scancode);
            } else if (vk == VK_CONTROL) {
                if (IsVkDown(VK_LCONTROL) == down) AddKeyEvent(io, ImGuiKey_LeftCtrl, down, VK_LCONTROL, scancode);
                if (IsVkDown(VK_RCONTROL) == down) AddKeyEvent(io, ImGuiKey_RightCtrl, down, VK_RCONTROL, scancode);
            } else if (vk == VK_MENU) {
                if (IsVkDown(VK_LMENU) == down) AddKeyEvent(io, ImGuiKey_LeftAlt, down, VK_LMENU, scancode);
                if (IsVkDown(VK_RMENU) == down) AddKeyEvent(io, ImGuiKey_RightAlt, down, VK_RMENU, scancode);
            }
        }
        return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        io.AddFocusEvent(msg == WM_SETFOCUS);
        return 0;
    case WM_CHAR:
    case WM_SYSCHAR: {
        // WM_CHAR carries UTF-16 units; supplementary characters arrive as surrogate pairs.
        const auto unit = static_cast<unsigned int>(wParam);
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            bd->pendingHighSurrogate = static_cast<WCHAR>(unit);
            return 0;
        }
        if (unit >= 0xDC00 && unit <= 0xDFFF && bd->pendingHighSurrogate) {
            const unsigned int cp = 0x10000 + ((bd->pendingHighSurrogate - 0xD800) << 10) + (unit - 0xDC00);
            bd->pendingHighSurrogate = 0;
            io.AddInputCharacter(cp);
            return 0;
        }
        bd->pendingHighSurrogate = 0;
        if (unit > 0 && unit < 0x110000 && msg == WM_CHAR) io.AddInputCharacter(unit);
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            UpdateMouseCursor(io, io.MouseDrawCursor ? ImGuiMouseCursor_None : ImGui::GetMouseCursor());
            return 1;
        }
        return 0;
    default: return 0;
    }
}
