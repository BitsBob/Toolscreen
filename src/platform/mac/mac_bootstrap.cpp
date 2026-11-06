// Entry points of libToolscreen.dylib on macOS.
//
// The dylib is loaded into Minecraft's JVM as a JVMTI agent:
//     -agentpath:/path/to/libToolscreen.dylib
// (Prism Launcher / MultiMC: Edit Instance -> Settings -> Java -> JVM arguments; the macOS
// installer script adds it automatically). Loading as an agent guarantees Toolscreen is present
// before LWJGL resolves GLFW/OpenGL, which is what the interposition hooks rely on.
//
// The equivalent of the Windows DllMain attach runs from Agent_OnLoad; the detach runs when the JVM
// unloads the agent (or at process exit).
#include "platform/mac/mac_interpose.h"
#include "platform/mac/mac_internal.h"

#include "common/utils.h"

#include <dlfcn.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

bool ToolscreenMacProcessAttach(HMODULE hModule); // bootstrap/dllmain.cpp
void ToolscreenMacProcessDetach(bool processExiting);

namespace {

std::once_flag g_initOnce;
std::atomic<bool> g_initialized{ false };

std::string DirectoryOfThisImage() {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<const void*>(&DirectoryOfThisImage), &info) || !info.dli_fname) return {};
    std::string path = info.dli_fname;
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// liblogger (the module logger used for run verification) is shipped next to Toolscreen and loaded
// alongside it, mirroring what the Windows installer injects.
void LoadLibLogger() {
    const std::string dir = DirectoryOfThisImage();
    for (const char* name : { "liblogger.dylib", "liblogger_universal.dylib" }) {
        const std::string path = dir + "/" + name;
        if (dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL)) {
            fprintf(stderr, "[Toolscreen] Loaded %s\n", path.c_str());
            return;
        }
    }
}

void AtExitDetach() { ToolscreenMacProcessDetach(true); }

void InitializeOnce(const char* reason) {
    std::call_once(g_initOnce, [reason] {
        ToolscreenMacInstallInterposition();
        LoadLibLogger();
        const HMODULE self = macwin::ModuleHandleForAddress(reinterpret_cast<const void*>(&InitializeOnce));
        fprintf(stderr, "[Toolscreen] Initializing (%s)\n", reason);
        if (ToolscreenMacProcessAttach(self)) {
            g_initialized.store(true, std::memory_order_release);
            atexit(&AtExitDetach);
        } else {
            fprintf(stderr, "[Toolscreen] Initialization failed; see toolscreen/logs/latest.log\n");
        }
    });
}

} // namespace

// Lazy initialisation for setups that load the dylib some other way (DYLD_INSERT_LIBRARIES, a Java
// agent calling System.load, ...): the first GLFW window creation completes it.
void ToolscreenMacEnsureInitialized() { InitializeOnce("first GLFW window"); }

// Runs when the dylib is mapped: start interposing LWJGL lookups as early as possible. Heavier work
// (threads, file I/O) waits for Agent_OnLoad, outside dyld's loader lock.
__attribute__((constructor)) static void ToolscreenMacImageLoaded() { ToolscreenMacInstallInterposition(); }

extern "C" __attribute__((visibility("default"))) int Agent_OnLoad(void* /*JavaVM* vm*/, char* options, void* /*reserved*/) {
    (void)options;
    InitializeOnce("JVMTI agent");
    return 0; // JNI_OK
}

extern "C" __attribute__((visibility("default"))) int Agent_OnAttach(void* /*JavaVM* vm*/, char* options, void* /*reserved*/) {
    (void)options;
    fprintf(stderr,
            "[Toolscreen] Attached to a running JVM. LWJGL may already have resolved GLFW/OpenGL; launch with "
            "-agentpath instead for full functionality.\n");
    InitializeOnce("late attach");
    return 0;
}

extern "C" __attribute__((visibility("default"))) void Agent_OnUnload(void* /*JavaVM* vm*/) {
    if (g_initialized.load(std::memory_order_acquire)) ToolscreenMacProcessDetach(true);
}
