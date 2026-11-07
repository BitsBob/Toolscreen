// Plain-C interface to the Cocoa side of the macOS port (implemented in mac_platform.mm).
//
// This header deliberately uses only fundamental types so it can be included both from C++
// translation units that see the Win32 compatibility layer and from Objective-C++ ones (where
// <windows.h>-style typedefs such as BOOL would clash with Objective-C).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----- Geometry -------------------------------------------------------------------------------
// "Screen pixels" use the Windows convention: a top-left origin at the primary display, measured
// in backing pixels of the display the game window is on.
typedef struct MacRect { int32_t left, top, right, bottom; } MacRect;

// Refreshes cached window/screen state for `nsWindow`. Main thread only (called once per frame and
// on window events); every other geometry query below reads the cache and is thread-safe.
void MacBridge_RefreshWindowCache(void* nsWindow);
// Content-area frame of the game window in screen pixels (false when no window is cached).
int MacBridge_GetCachedClientRectInScreen(MacRect* out);
// Whole-window frame (title bar included) in screen pixels.
int MacBridge_GetCachedWindowRectInScreen(MacRect* out);
// Backing scale factor of the game window (2.0 on Retina displays).
double MacBridge_GetCachedBackingScale(void);
int MacBridge_IsCachedWindowVisible(void);
int MacBridge_IsCachedWindowMinimized(void);
int MacBridge_IsCachedWindowZoomed(void);
// True when Minecraft is the active application and its window is the key window.
int MacBridge_IsCachedWindowForeground(void);

// Monitors, in screen pixels. Index 0 is the primary display.
int MacBridge_GetMonitorCount(void);
int MacBridge_GetMonitorRect(int index, MacRect* outFrame, MacRect* outWorkArea);
int MacBridge_GetMonitorIndexForCachedWindow(void);
int MacBridge_GetMonitorIndexForPoint(int32_t x, int32_t y);
double MacBridge_GetMonitorScale(int index);

// ----- Cursor ---------------------------------------------------------------------------------
int MacBridge_GetCursorPosScreenPixels(int32_t* x, int32_t* y);
int MacBridge_SetCursorPosScreenPixels(int32_t x, int32_t y);
// Shows or hides the system cursor (balanced like ShowCursor; returns the new display count).
int MacBridge_ShowCursor(int show);
int MacBridge_IsCursorVisible(void);
// Sets the active cursor image (an NSCursor created by MacBridge_CreateCursorFromRGBA or a system
// cursor id). Must be called on the main thread to take effect immediately; other threads defer.
void MacBridge_SetCursor(void* cursor);
void* MacBridge_GetSystemCursor(int win32CursorId);
void* MacBridge_CreateCursorFromRGBA(const uint8_t* rgba, int width, int height, int hotX, int hotY);
void MacBridge_ReleaseCursor(void* cursor);
// Copies the cursor image as straight RGBA (caller frees with free()).
int MacBridge_GetCursorImageRGBA(void* cursor, uint8_t** outPixels, int* outWidth, int* outHeight, int* outHotX, int* outHotY);

// ----- Keyboard -------------------------------------------------------------------------------
// Whether the capslock toggle is on.
int MacBridge_IsCapsLockOn(void);
// Translates a macOS virtual key code with the current keyboard layout. `shift`/`altGr` select the
// modifier state. Returns the number of UTF-32 code points written (0 for dead/no output).
int MacBridge_TranslateKeyCode(uint16_t macKeyCode, int shift, int option, uint32_t* outChars, int maxChars);
// Physical key state from the window server (used as a fallback for keys Toolscreen has not seen).
int MacBridge_IsMacKeyDown(uint16_t macKeyCode);
// System key repeat timings in milliseconds.
void MacBridge_GetKeyRepeatTimings(int* initialDelayMs, int* repeatIntervalMs);
// Double-click interval in milliseconds.
int MacBridge_GetDoubleClickIntervalMs(void);

// ----- Shell / UI -----------------------------------------------------------------------------
// Opens a URL (http/https/file) or a filesystem path with the default handler.
int MacBridge_OpenURLOrPath(const char* urlOrPath);
// Reveals a filesystem path in Finder.
int MacBridge_RevealInFinder(const char* path);
// Shows a modal alert. `style`: 0 info, 1 warning, 2 error. `buttons`: 0 OK, 1 OK/Cancel, 2 Yes/No,
// 3 Yes/No/Cancel. Returns the 0-based index of the clicked button.
int MacBridge_ShowAlert(const char* title, const char* message, int style, int buttons);
// Runs an open-file panel. `extensions` is a ';'-separated list ("png;jpg") or NULL for any file.
// Returns a malloc'd UTF-8 path, or NULL if cancelled.
char* MacBridge_RunOpenPanel(const char* title, const char* initialDir, const char* extensions, int chooseDirectories);
char* MacBridge_RunSavePanel(const char* title, const char* initialDir, const char* defaultName, const char* extensions);

// ----- Clipboard ------------------------------------------------------------------------------
int MacBridge_SetClipboardText(const char* utf8);
// Returns a malloc'd UTF-8 string or NULL.
char* MacBridge_GetClipboardText(void);
// Places a 32-bit top-down BGRA or RGBA image on the pasteboard as PNG/TIFF.
int MacBridge_SetClipboardImageRGBA(const uint8_t* rgba, int width, int height);

// ----- Windows of other applications ------------------------------------------------------------
typedef struct MacForeignWindowInfo {
    uint32_t windowId;
    int32_t ownerPid;
    int32_t layer;
    int onScreen;
    MacRect frame; // screen pixels
    char title[256];
    char ownerName[256];
} MacForeignWindowInfo;
// Enumerates on-screen windows front to back. Returns the number written (up to `max`).
int MacBridge_ListWindows(MacForeignWindowInfo* out, int max);
int MacBridge_GetWindowInfo(uint32_t windowId, MacForeignWindowInfo* out);
// The CGWindowID of the game window (0 when unknown).
uint32_t MacBridge_GetCachedWindowNumber(void);
// Brings the game window/app to the front.
void MacBridge_ActivateGameWindow(void);

// ----- Process --------------------------------------------------------------------------------
// Path of the executable of `pid` (malloc'd UTF-8) or NULL.
char* MacBridge_CopyProcessPath(int pid);
// The JVM's command line, joined with spaces (malloc'd UTF-8).
char* MacBridge_CopyProcessCommandLine(void);

// ----- Main-thread dispatch ---------------------------------------------------------------------
// Runs `fn(ctx)` on the main thread (synchronously when already on it).
void MacBridge_RunOnMainThreadSync(void (*fn)(void*), void* ctx);
void MacBridge_RunOnMainThreadAsync(void (*fn)(void*), void* ctx);
int MacBridge_IsMainThread(void);
// Wakes the Cocoa event loop so that glfwWaitEvents/glfwPollEvents return promptly.
void MacBridge_PostEmptyEvent(void);

// ----- Diagnostics ------------------------------------------------------------------------------
// Writes top-down RGBA pixels as a PNG file. Returns false on failure.
int MacBridge_WritePNG(const char* path, const uint8_t* rgba, int width, int height);

#ifdef __cplusplus
}
#endif
