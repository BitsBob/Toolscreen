// State shared between the Win32 compatibility layer and the macOS hook/input glue.
// C++ only (sees the compat typedefs); never include from Objective-C++.
#pragma once

#include <cstdint>
#include <string>

namespace macwin {

// ----- The game window --------------------------------------------------------------------------
// On macOS an HWND for Minecraft's window is the GLFWwindow* value. Other applications' windows
// are encoded as tagged CGWindowIDs (see ForeignHwnd/ForeignWindowId).
void SetGameWindow(void* glfwWindow, void* nsWindow, uint32_t ownerThreadId);
void ClearGameWindow(void* glfwWindow);
HWND GameHwnd();
void* GameNSWindow();
DWORD GameWindowThreadId();
inline bool IsGameHwnd(HWND hwnd) { return hwnd != nullptr && hwnd == GameHwnd(); }

HWND ForeignHwnd(uint32_t windowId);
bool IsForeignHwnd(HWND hwnd);
uint32_t ForeignWindowId(HWND hwnd);
HWND DesktopHwnd();

// Real framebuffer (client) size of the game window in pixels, maintained by the hook layer.
void SetFramebufferSize(int width, int height);
bool GetFramebufferSize(int& width, int& height);
// Called on the main thread whenever window geometry may have changed.
void RefreshWindowGeometry();

// ----- Window procedure chain -------------------------------------------------------------------
// The base procedure translates Win32 messages back into GLFW callbacks (mac_input_bridge.cpp).
void SetBaseWindowProc(WNDPROC proc);
WNDPROC CurrentWindowProc();
WNDPROC ReplaceWindowProc(WNDPROC proc); // returns the previous procedure
// Runs `msg` through the current window procedure on the calling thread, with the per-message
// context (GetMessageTime/GetMessageExtraInfo) set for its duration.
LRESULT DispatchToGameWindow(UINT msg, WPARAM wParam, LPARAM lParam, ULONG_PTR extraInfo = 0, DWORD time = 0);

// ----- Message queues ---------------------------------------------------------------------------
bool PostGameWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam, ULONG_PTR extraInfo = 0);
// Drains messages posted to the game window. Must run on the game window's thread.
void PumpGameWindowMessages();
DWORD CurrentMessageTime();
ULONG_PTR CurrentMessageExtraInfo();

// ----- Physical input state (before Toolscreen's pipeline) -----------------------------------------
void SetPhysicalKeyDown(uint16_t vk, bool down);
bool IsPhysicalKeyDown(uint16_t vk);
void ResetPhysicalInputState();

// ----- WH_KEYBOARD_LL emulation ---------------------------------------------------------------------
bool HasLowLevelKeyboardHooks();
// Returns true when a hook swallowed the event.
bool RunLowLevelKeyboardHooks(WPARAM message, const KBDLLHOOKSTRUCT& info);

// ----- Raw input records ----------------------------------------------------------------------------
// HRAWINPUT handles point at thread-local RAWINPUT records created by the input bridge.
HRAWINPUT MakeRawMouseInput(LONG dx, LONG dy, USHORT buttonFlags = 0, USHORT buttonData = 0);

// ----- Misc -----------------------------------------------------------------------------------------
std::string ModulePathForHandle(HMODULE module);
HMODULE ModuleHandleForAddress(const void* address);

} // namespace macwin
