# Toolscreen on macOS

This fork adds a native macOS build of Toolscreen (Apple Silicon and Intel, macOS 13.3+). It is the same
Toolscreen — modes, EyeZoom, mirrors, overlays, hotkeys, rebinds and the settings GUI come from the shared
source — loaded into Minecraft's JVM as a JVMTI agent instead of an injected DLL.

## Installing

1. Download/build `Toolscreen-<version>-macos` (see [Building](#building)).
2. Run the installer and pick your instance:

   ```bash
   ./install-macos.sh
   ```

   or pass the instance folder directly (Prism: right-click the instance → **Folder**):

   ```bash
   ./install-macos.sh "$HOME/Library/Application Support/PrismLauncher/instances/<instance>"
   ```

   It copies `libToolscreen.dylib` and `liblogger.dylib` to `~/.local/share/toolscreen-macos` and adds
   `-agentpath:~/.local/share/toolscreen-macos/libToolscreen.dylib` (expanded) to the instance's JVM arguments.
3. Launch the instance, switch to fullscreen (F11) and press <kbd>Ctrl</kbd>+<kbd>I</kbd> for the settings.

**Manual install:** in Prism Launcher / MultiMC open *Edit Instance → Settings → Java*, tick **JVM arguments**
and add `-agentpath:/absolute/path/to/libToolscreen.dylib`. Keep `liblogger.dylib` in the same folder. The path
must not contain spaces.

**Uninstall:** `./install-macos.sh --uninstall [instance]`, then delete `~/.local/share/toolscreen-macos`.

Config, profiles and logs live in `~/.config/toolscreen` (or `<instance>/.minecraft/toolscreen` if that folder
exists), exactly like the Windows build. Configs are interchangeable between Windows and macOS: keys are stored
as Windows virtual-key codes on both.

## What works

| Feature | macOS |
| --- | --- |
| Modes / resizing (Fullscreen, Thin, Wide, EyeZoom, custom), transitions | ✅ |
| EyeZoom | ✅ |
| Mirrors, image overlays, backgrounds, borders | ✅ |
| Ninjabrain Bot overlay (via Ninjabrain Bot's API) | ✅ |
| Hotkeys, key rebinding, sensitivity override | ✅ (inside Minecraft) |
| Settings GUI, profiles, themes, fonts, translations | ✅ |
| Minecraft 1.16 (legacy OpenGL 2.1 context) and 1.17+ (OpenGL 4.1 core) | ✅ |
| Window overlays (capturing another app's window) | ❌ not yet (needs ScreenCaptureKit) |
| Browser overlays | ❌ (WebView2 is Windows-only) |
| OBS game-capture redirect, OBS virtual camera | ❌ — OBS on macOS captures the window as shown |
| Vulkan renderer (Minecraft 26.2+ `--graphicsBackend vulkan`) | ❌ — use the OpenGL backend |
| System mouse speed / key repeat overrides | ❌ by design: Toolscreen never changes macOS settings |
| Borderless-window toggle | ❌ — use Minecraft's fullscreen |
| Custom `.cur`/`.ani` cursor files | ❌ |

Rebinds and key suppression change the input Minecraft receives; other apps are unaffected. Mode sizes are in
framebuffer pixels, so on a Retina display Fullscreen is the panel's native resolution (for example 3456×2234).

## How it works

* **Loading.** `libToolscreen.dylib` exports `Agent_OnLoad`; `-agentpath` makes the JVM load it before any game
  code runs. `Agent_OnLoad` performs the same process initialisation as the Windows `DllMain`
  (`InitializeToolscreenProcess` in `src/bootstrap/dllmain.cpp`) and loads `liblogger.dylib` from the same folder.
* **Hooks without code patching** (`src/platform/mac/mac_interpose.mm`, `mac_hooks.cpp`). LWJGL resolves every
  GLFW function with `dlsym` and every OpenGL function with `CFBundleGetFunctionPointerForName`, both called from
  `liblwjgl.dylib`. Toolscreen rebinds those two imports *inside LWJGL's images only*, so LWJGL receives
  Toolscreen's hook functions (`glfwSwapBuffers`, `glViewport`, `glBindFramebuffer`, the GLFW callback setters, …)
  in place of the real ones. Nothing outside LWJGL is affected and no executable pages are written, which keeps
  this compatible with the hardened runtime of signed JDKs.
* **Input** (`mac_hooks.cpp`). Toolscreen registers its own GLFW callbacks and turns them into the Win32 window
  messages the shared input pipeline (`src/hooks/input_hook.cpp`) already understands — `WM_KEYDOWN` with VK
  codes and set-1 scan codes, `WM_CHAR` in UTF-16, `WM_MOUSEMOVE`, `WM_INPUT` for mouse-look deltas (so the
  sensitivity override runs unchanged), `WM_SIZE`, focus messages. Whatever the pipeline forwards to the
  "original window procedure" is translated back into Minecraft's GLFW callbacks. The game is told its window is
  as large as its framebuffer, so Toolscreen and Minecraft both work in pixels as on Windows.
* **Win32 compatibility layer** (`src/platform/mac/win32_compat*.{h,cpp}`, `mac_platform.mm`). The shared code
  is written against `<windows.h>`; on macOS a force-included header supplies the subset it uses (types,
  messages, kernel objects, files, timers, clipboard, dialogs, monitors, cursor, keyboard layout translation,
  WinHTTP over NSURLSession, …) implemented on POSIX/Cocoa. The Windows build is unaffected.
* **OpenGL** (`mac_gl_compat.cpp`). Minecraft 1.16 runs on Apple's legacy 2.1 context, which only accepts
  GLSL 1.20. Toolscreen's GLSL 3.30 shaders are translated on the fly for that context, and core entry points
  are pointed at their APPLE/ARB equivalents (vertex array objects, instancing). GL 4.2+ calls macOS lacks get
  fallbacks.
* Windows-only modules (WebView2, Vulkan layer, OBS virtual camera, PE hook chaining) are replaced by stubs in
  `mac_feature_stubs.cpp` so the shared code takes its existing "unavailable" paths.

## Building

Requirements: Xcode or the Command Line Tools, CMake ≥ 3.21, Ninja (`brew install cmake ninja`) and a JDK
(for liblogger's JNI headers).

```bash
./build-macos.sh            # universal release → out/macos-dist/Toolscreen-<version>-macos(.zip)
./build-macos.sh --test     # …and run the unit tests
./build-macos.sh --debug    # host-architecture RelWithDebInfo build in out/build-macos
```

## Testing

* Unit tests: `ctest --test-dir out/build-macos-release --output-on-failure`.
* In-game tests: Toolscreen's runner (`src/testing/game_test_mode.cpp`) works on macOS too. Start the game (or
  any LWJGL program that mimics it) with the agent and these environment variables:

  ```bash
  TOOLSCREEN_GAME_TEST_RESULTS=/tmp/results.jsonl TOOLSCREEN_GAME_TEST_EXIT=close \
  TOOLSCREEN_GAME_TEST_TOOLSCREEN_DIR="$PWD/toolscreen" java -XstartOnFirstThread \
    -agentpath:out/build-macos-release/bin/libToolscreen.dylib ...
  ```

  Results are JSON lines; Vulkan-, SDL- and fixture-specific tests report `skip`.
* Rendering diagnostics (developer aids, off unless set):
  * `TOOLSCREEN_MAC_DEBUG_GLSTATE=1` logs any OpenGL state Toolscreen's per-frame work leaves changed.
    Minecraft's `GlStateManager` and Sodium cache GL state, so any line it prints is a bug.
  * `TOOLSCREEN_MAC_DEBUG_SCRIPT` drives the game and saves frames without anyone at the keyboard, e.g.
    `TOOLSCREEN_MAC_DEBUG_DIR=/tmp/ts TOOLSCREEN_MAC_DEBUG_SCRIPT="12 capture title; 13 click 0.5 0.41; 40 gui 1; 43 capture gui; 45 quit"`.
    Steps are `<seconds after the window opened> <command>`: `capture NAME` (PNG of the presented frame),
    `click X Y` (fractions of the window), `key GLFW_KEY`, `text STRING`, `gui 0|1`, `quit`. While a script
    is set the game cannot capture the system mouse cursor.

## Troubleshooting

* **Nothing happens in game:** check that the JVM arguments contain the `-agentpath`, then look for
  `[Toolscreen]` lines in the launcher's log and for `~/.config/toolscreen/logs/latest.log`.
* **"library load disallowed by system policy":** the dylib is quarantined; run
  `xattr -dr com.apple.quarantine ~/.local/share/toolscreen-macos` (the installer does this).
* **Hotkeys do nothing:** macOS may reserve some combinations (Mission Control, Spotlight, …). Rebind them in
  the settings GUI.
