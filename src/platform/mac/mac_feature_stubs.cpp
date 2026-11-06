// macOS stand-ins for Toolscreen modules that are inherently Windows-specific.
//
//  * Vulkan backend + OBS redirect layer (render/vulkan/*): Minecraft's Vulkan renderer (26.2+) is not
//    supported on macOS; the OpenGL path is used for every supported version.
//  * Browser overlays (features/browser_overlay.cpp): built on WebView2.
//  * Virtual camera (features/virtual_camera.cpp): writes into the OBS Virtual Camera's Windows shared
//    memory ring.
//  * Third-party detour chaining (hooks/hook_chain.cpp): parses PE images/IATs. The macOS hooks never
//    patch code, so there is nothing to chain behind.
//  * Thread stack dumps for the in-game tests (testing/thread_stack_dump.cpp): DbgHelp.
//
// Every entry point reports "unavailable" so the shared code takes its existing fallback paths.

#include "features/browser_overlay.h"
#include "features/virtual_camera.h"
#include "hooks/hook_chain.h"
#include "render/vulkan/vulkan_hooks.h"
#include "render/vulkan/vulkan_renderer.h"
#include "testing/thread_stack_dump.h"

#include <atomic>
#include <cstdint>
#include <sstream>
#include <dlfcn.h>

// ---------------------------------------------------------------------------------------------
// Vulkan
// ---------------------------------------------------------------------------------------------
namespace VulkanHooks {
bool EnableProcessLocalObsRedirectLayer(HMODULE) { return false; }
void NotifyModuleLoaded(HMODULE) {}
FARPROC InterceptLoaderGetProcAddress(HMODULE, LPCSTR, FARPROC realFunction) { return realFunction; }
void Shutdown() {}
bool InstallIfAvailable() { return false; }
} // namespace VulkanHooks

namespace VulkanRenderer {
bool IsReady() { return false; }
FrameTrackingStats GetFrameTrackingStats() { return {}; }
void SetFrameTrackingProbeEnabled(bool) {}
StreamingTextureStats GetStreamingTextureStats(const std::string&) { return {}; }
bool GetColorPickerFrame(uintptr_t& textureId, int& width, int& height) {
    textureId = 0;
    width = height = 0;
    return false;
}
bool TryGetColorPickerSample(int, int, std::array<float, 4>&) { return false; }
bool GetBundledGuiTexture(int, uintptr_t& textureId) {
    textureId = 0;
    return false;
}
bool GetGuiRgbaTexture(const std::string&, const unsigned char*, int, int, uint64_t, uintptr_t& textureId) {
    textureId = 0;
    return false;
}
bool GetMirrorHasContentForTests(const std::string&, bool& hasContent) {
    hasContent = false;
    return false;
}
EyeZoomSnapshotStats GetEyeZoomSnapshotStatsForTests() { return {}; }
bool TryGetPresentedPixelForTests(int, int, std::array<float, 4>&) { return false; }
void RequestFrameCaptureForTests() {}
bool TryGetFrameCaptureForTests(std::vector<uint8_t>& rgba, int& width, int& height) {
    rgba.clear();
    width = height = 0;
    return false;
}
} // namespace VulkanRenderer

namespace ToolscreenVulkanLayerTest {
bool GetTrackedDevice(VkDevice& device, PFN_vkGetDeviceProcAddr& layerGdpa, PFN_vkGetDeviceProcAddr& nextGdpa) {
    device = VK_NULL_HANDLE;
    layerGdpa = nullptr;
    nextGdpa = nullptr;
    return false;
}
} // namespace ToolscreenVulkanLayerTest

// ---------------------------------------------------------------------------------------------
// Browser overlays (WebView2)
// ---------------------------------------------------------------------------------------------
void StartBrowserOverlayThread() {}
void StopBrowserOverlayThread() {}
void CleanupBrowserOverlayCache() {}
void RemoveBrowserOverlayFromCache(const std::string&) {}
void RequestBrowserOverlayRefresh(const std::string&) {}
bool StageBrowserOverlayTestFrame(const BrowserOverlayConfig&, const std::vector<unsigned char>&, int, int) { return false; }
bool PrepareBrowserOverlayTexture(const BrowserOverlayConfig&, BrowserOverlayTextureFrame& outFrame) {
    outFrame = {};
    return false;
}
bool AcquireBrowserOverlayPixelFrame(const BrowserOverlayConfig&, BrowserOverlayPixelFrame& outFrame) {
    outFrame = {};
    return false;
}

// ---------------------------------------------------------------------------------------------
// Virtual camera (OBS Virtual Camera shared memory on Windows)
// ---------------------------------------------------------------------------------------------
std::atomic<bool> g_virtualCameraActive{ false };
bool StartVirtualCamera(uint32_t, uint32_t) { return false; }
void StopVirtualCamera() {}
bool WriteVirtualCameraFrame(const uint8_t*, uint32_t, uint32_t, uint64_t) { return false; }
bool WriteVirtualCameraFrameNV12(const uint8_t*, uint32_t, uint32_t, uint64_t) { return false; }
bool WriteVirtualCameraFrameNV12Planes(const uint8_t*, const uint8_t*, uint32_t, uint32_t, uint64_t) { return false; }
bool AcquireVirtualCameraGpuFrame(uint64_t, VirtualCameraGpuFrame& outFrame) {
    outFrame = {};
    return false;
}
bool PublishVirtualCameraGpuFrame(const VirtualCameraGpuFrame&) { return false; }
void AbandonVirtualCameraGpuFrame(const VirtualCameraGpuFrame&) {}
bool IsVirtualCameraActive() { return false; }
bool IsVirtualCameraDriverInstalled() { return false; }
bool IsVirtualCameraInUseByOBS() { return false; }
bool ShouldCaptureVirtualCameraFrame() { return false; }
bool EnsureVirtualCameraSize(uint32_t, uint32_t) { return false; }
bool GetVirtualCameraResolution(uint32_t& outWidth, uint32_t& outHeight) {
    outWidth = outHeight = 0;
    return false;
}
void GetVirtualCameraMonitorSize(uint32_t& outWidth, uint32_t& outHeight) { outWidth = outHeight = 0; }
bool GetPreferredVirtualCameraResolution(uint32_t& outWidth, uint32_t& outHeight) {
    outWidth = outHeight = 0;
    return false;
}
void OnGameWindowResized(uint32_t, uint32_t) {}
bool FlushPendingVirtualCameraResize() { return false; }
void RequestVirtualCameraRecoveryFrames() {}
std::string GetVirtualCameraError() { return "The OBS virtual camera output is not available on macOS."; }

// ---------------------------------------------------------------------------------------------
// Third-party detour chaining
// ---------------------------------------------------------------------------------------------
namespace HookChain {
bool TryCreateAndEnableHook(void*, void*, void** outOriginal, const char*) {
    if (outOriginal) *outOriginal = nullptr;
    return false;
}
bool IsAllowedSwapBuffersThirdPartyHookAddress(const void*) { return false; }
void RefreshAllThirdPartyHookChains() {}
void* GetThirdPartyWglSwapBuffersHookTarget() { return nullptr; }
std::string DescribeAddressWithOwner(const void* addr) {
    std::ostringstream out;
    out << addr;
    Dl_info info{};
    if (addr && dladdr(addr, &info) && info.dli_fname) {
        out << " (" << info.dli_fname;
        if (info.dli_sname) out << "!" << info.dli_sname;
        out << ")";
    }
    return out.str();
}
} // namespace HookChain

// ---------------------------------------------------------------------------------------------
// In-game test diagnostics
// ---------------------------------------------------------------------------------------------
namespace GameTest {
std::string DumpInterestingThreadStacks() { return "Thread stack dumps are not available on macOS."; }
} // namespace GameTest
