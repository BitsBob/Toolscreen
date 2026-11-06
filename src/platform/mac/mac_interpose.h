// Interface between the interposition engine (mac_interpose.mm) and the hook tables (mac_hooks.cpp).
// Plain C types only, so it can be shared between Objective-C++ and Win32-compat C++ translation units.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Starts rebinding dlsym/CFBundleGetFunctionPointerForName inside LWJGL's native images.
void ToolscreenMacInstallInterposition(void);

// Implemented by mac_hooks.cpp. Each returns the replacement for `name` (recording `real` as the
// function to forward to), or NULL to let LWJGL use the real function.
void* ToolscreenMacResolveGlfwHook(const char* name, void* real);
void* ToolscreenMacResolveGlHook(const char* name, void* real);
// The dlopen handle LWJGL uses for GLFW (used to look up GLFW functions Toolscreen needs itself).
void ToolscreenMacNoteGlfwLibraryHandle(void* handle);
// Called once per LWJGL image that has been rebound (diagnostics).
void ToolscreenMacNoteLwjglImage(const char* path);

#ifdef __cplusplus
}
#endif
