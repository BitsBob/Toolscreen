// macOS build: replacement for Dear ImGui's Win32 platform backend.
//
// Toolscreen's input pipeline speaks Win32 window messages; on macOS those messages are synthesised
// from GLFW callbacks (src/platform/mac/mac_input_bridge.cpp) and fed to this backend, which
// translates them into ImGui IO events exactly like imgui_impl_win32 does on Windows.
#pragma once
#include "imgui.h"

IMGUI_IMPL_API bool ImGui_ImplWin32_Init(void* hwnd);
IMGUI_IMPL_API bool ImGui_ImplWin32_InitForOpenGL(void* hwnd);
IMGUI_IMPL_API void ImGui_ImplWin32_Shutdown();
IMGUI_IMPL_API void ImGui_ImplWin32_NewFrame();
IMGUI_IMPL_API void ImGui_ImplWin32_EnableDpiAwareness();
IMGUI_IMPL_API float ImGui_ImplWin32_GetDpiScaleForHwnd(void* hwnd);
IMGUI_IMPL_API float ImGui_ImplWin32_GetDpiScaleForMonitor(void* monitor);
