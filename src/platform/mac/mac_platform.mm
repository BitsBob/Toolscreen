// Cocoa implementation of mac_platform_bridge.h.
#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "mac_platform_bridge.h"

#include <crt_externs.h>
#include <libproc.h>
#include <pthread.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct MonitorEntry {
    MacRect frame;
    MacRect work;
    double scale;
};

struct WindowCache {
    bool valid = false;
    MacRect client{};
    MacRect window{};
    double scale = 1.0;
    bool visible = false;
    bool minimized = false;
    bool zoomed = false;
    bool foreground = false;
    uint32_t windowNumber = 0;
    int monitorIndex = 0;
    std::vector<MonitorEntry> monitors;
    double primaryHeightPoints = 0.0;
};

std::mutex g_cacheMutex;
WindowCache g_cache;
// Strong reference to the most recently refreshed window so foreign threads can message it safely.
NSWindow* __strong g_gameWindow = nil;

// Conversion between Cocoa screen coordinates (points, bottom-left origin of the primary screen)
// and the Windows-style screen pixel space (top-left origin, scaled by the game window's backing
// scale so that the game window's client rect is measured in its framebuffer pixels).
MacRect CocoaRectToScreenPixels(NSRect r, double primaryHeight, double scale) {
    const double topPoints = primaryHeight - (r.origin.y + r.size.height);
    MacRect out;
    out.left = (int32_t)std::lround(r.origin.x * scale);
    out.top = (int32_t)std::lround(topPoints * scale);
    out.right = out.left + (int32_t)std::lround(r.size.width * scale);
    out.bottom = out.top + (int32_t)std::lround(r.size.height * scale);
    return out;
}

double PrimaryScreenHeightPoints() {
    NSArray<NSScreen*>* screens = [NSScreen screens];
    if (screens.count == 0) return 0.0;
    return screens[0].frame.size.height;
}

// Before Minecraft's window exists, measure in the main display's backing pixels so early sizing
// (e.g. the default config's mode sizes) matches the framebuffer the game will get.
double DefaultBackingScale() {
    NSScreen* main = [NSScreen mainScreen] ?: [NSScreen screens].firstObject;
    return main && main.backingScaleFactor > 0 ? main.backingScaleFactor : 1.0;
}

void RefreshMonitorsLocked(WindowCache& cache) {
    cache.monitors.clear();
    if (!cache.valid) cache.scale = DefaultBackingScale();
    NSArray<NSScreen*>* screens = [NSScreen screens];
    cache.primaryHeightPoints = screens.count ? screens[0].frame.size.height : 0.0;
    for (NSScreen* screen in screens) {
        MonitorEntry entry;
        entry.scale = screen.backingScaleFactor;
        entry.frame = CocoaRectToScreenPixels(screen.frame, cache.primaryHeightPoints, cache.scale);
        entry.work = CocoaRectToScreenPixels(screen.visibleFrame, cache.primaryHeightPoints, cache.scale);
        cache.monitors.push_back(entry);
    }
}

CFDataRef g_keyboardLayoutData = nullptr; // retained; guarded by g_layoutMutex
std::mutex g_layoutMutex;

void RefreshKeyboardLayoutOnMainThread() {
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (!source) source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    if (!source) return;
    CFDataRef data = (CFDataRef)TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData);
    if (data) CFRetain(data);
    CFRelease(source);
    std::lock_guard<std::mutex> lock(g_layoutMutex);
    if (g_keyboardLayoutData) CFRelease(g_keyboardLayoutData);
    g_keyboardLayoutData = data;
}

std::atomic<int> g_cursorDisplayCount{ 0 };

NSString* NSStringFromUtf8(const char* s) { return s ? [NSString stringWithUTF8String:s] : @""; }

char* CopyUtf8(NSString* s) {
    if (!s) return nullptr;
    const char* utf8 = s.UTF8String;
    return utf8 ? strdup(utf8) : nullptr;
}

} // namespace

static void EnsureMonitorsCached();

extern "C" {

// ----- Geometry -----------------------------------------------------------------------------------
void MacBridge_RefreshWindowCache(void* nsWindowPtr) {
    NSWindow* window = (__bridge NSWindow*)nsWindowPtr;
    static int s_layoutRefreshCountdown = 0;
    if (--s_layoutRefreshCountdown <= 0) {
        RefreshKeyboardLayoutOnMainThread();
        s_layoutRefreshCountdown = 120;
    }

    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (!window) {
        g_cache.valid = false;
        g_gameWindow = nil;
        RefreshMonitorsLocked(g_cache);
        return;
    }
    g_gameWindow = window;
    g_cache.scale = window.backingScaleFactor > 0 ? window.backingScaleFactor : 1.0;
    const double primaryHeight = PrimaryScreenHeightPoints();
    const NSRect frame = window.frame;
    const NSRect content = [window contentRectForFrameRect:frame];
    g_cache.client = CocoaRectToScreenPixels(content, primaryHeight, g_cache.scale);
    g_cache.window = CocoaRectToScreenPixels(frame, primaryHeight, g_cache.scale);
    g_cache.visible = window.visible;
    g_cache.minimized = window.miniaturized;
    g_cache.zoomed = window.zoomed || ((window.styleMask & NSWindowStyleMaskFullScreen) != 0);
    g_cache.foreground = NSApp.active && window.keyWindow;
    g_cache.windowNumber = (uint32_t)window.windowNumber;
    RefreshMonitorsLocked(g_cache);
    g_cache.monitorIndex = 0;
    NSScreen* screen = window.screen;
    NSArray<NSScreen*>* screens = [NSScreen screens];
    for (NSUInteger i = 0; i < screens.count; ++i) {
        if (screens[i] == screen) {
            g_cache.monitorIndex = (int)i;
            break;
        }
    }
    g_cache.valid = true;
}

int MacBridge_GetCachedClientRectInScreen(MacRect* out) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (!g_cache.valid || !out) return 0;
    *out = g_cache.client;
    return 1;
}

int MacBridge_GetCachedWindowRectInScreen(MacRect* out) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (!g_cache.valid || !out) return 0;
    *out = g_cache.window;
    return 1;
}

double MacBridge_GetCachedBackingScale(void) {
    EnsureMonitorsCached();
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    return g_cache.scale > 0 ? g_cache.scale : 1.0;
}

int MacBridge_IsCachedWindowVisible(void) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    return g_cache.valid && g_cache.visible;
}
int MacBridge_IsCachedWindowMinimized(void) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    return g_cache.valid && g_cache.minimized;
}
int MacBridge_IsCachedWindowZoomed(void) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    return g_cache.valid && g_cache.zoomed;
}
int MacBridge_IsCachedWindowForeground(void) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    return g_cache.valid && g_cache.foreground;
}

static void EnsureMonitorsCached() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (!g_cache.monitors.empty()) return;
    // Read directly rather than hopping to the main queue: before GLFW first polls events nothing
    // services it, and NSScreen's geometry is safe to read from any thread.
    @autoreleasepool {
        RefreshMonitorsLocked(g_cache);
    }
}

int MacBridge_GetMonitorCount(void) {
    EnsureMonitorsCached();
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    return (int)g_cache.monitors.size();
}

int MacBridge_GetMonitorRect(int index, MacRect* outFrame, MacRect* outWorkArea) {
    EnsureMonitorsCached();
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (index < 0 || index >= (int)g_cache.monitors.size()) return 0;
    if (outFrame) *outFrame = g_cache.monitors[(size_t)index].frame;
    if (outWorkArea) *outWorkArea = g_cache.monitors[(size_t)index].work;
    return 1;
}

int MacBridge_GetMonitorIndexForCachedWindow(void) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    return g_cache.valid ? g_cache.monitorIndex : 0;
}

int MacBridge_GetMonitorIndexForPoint(int32_t x, int32_t y) {
    EnsureMonitorsCached();
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    for (size_t i = 0; i < g_cache.monitors.size(); ++i) {
        const MacRect& r = g_cache.monitors[i].frame;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return (int)i;
    }
    return -1;
}

double MacBridge_GetMonitorScale(int index) {
    EnsureMonitorsCached();
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    if (index < 0 || index >= (int)g_cache.monitors.size()) return 1.0;
    return g_cache.monitors[(size_t)index].scale;
}

// ----- Cursor -------------------------------------------------------------------------------------
int MacBridge_GetCursorPosScreenPixels(int32_t* x, int32_t* y) {
    CGEventRef event = CGEventCreate(nullptr);
    if (!event) return 0;
    const CGPoint p = CGEventGetLocation(event); // global display coordinates, top-left origin, points
    CFRelease(event);
    const double scale = MacBridge_GetCachedBackingScale();
    if (x) *x = (int32_t)std::floor(p.x * scale);
    if (y) *y = (int32_t)std::floor(p.y * scale);
    return 1;
}

int MacBridge_SetCursorPosScreenPixels(int32_t x, int32_t y) {
    const double scale = MacBridge_GetCachedBackingScale();
    const CGPoint p = CGPointMake((double)x / scale, (double)y / scale);
    if (CGWarpMouseCursorPosition(p) != kCGErrorSuccess) return 0;
    // Without re-association, macOS suppresses mouse motion for ~250ms after a warp.
    CGAssociateMouseAndMouseCursorPosition(true);
    return 1;
}

int MacBridge_ShowCursor(int show) {
    const int previous = show ? g_cursorDisplayCount.fetch_add(1) : g_cursorDisplayCount.fetch_sub(1);
    const int now = show ? previous + 1 : previous - 1;
    if (show && previous == -1 && now == 0) {
        MacBridge_RunOnMainThreadAsync([](void*) { [NSCursor unhide]; }, nullptr);
    } else if (!show && previous == 0 && now == -1) {
        MacBridge_RunOnMainThreadAsync([](void*) { [NSCursor hide]; }, nullptr);
    }
    return now;
}

int MacBridge_IsCursorVisible(void) {
    return g_cursorDisplayCount.load() >= 0 && CGCursorIsVisible();
}

void MacBridge_SetCursor(void* cursorPtr) {
    if (!cursorPtr) return;
    NSCursor* cursor = (__bridge NSCursor*)cursorPtr;
    if ([NSThread isMainThread]) {
        [cursor set];
    } else {
        dispatch_async(dispatch_get_main_queue(), ^{
          [cursor set];
        });
    }
}

void* MacBridge_GetSystemCursor(int id) {
    NSCursor* cursor = nil;
    switch (id) {
    case 32513: cursor = [NSCursor IBeamCursor]; break;          // IDC_IBEAM
    case 32515: cursor = [NSCursor crosshairCursor]; break;      // IDC_CROSS
    case 32644: cursor = [NSCursor resizeLeftRightCursor]; break;// IDC_SIZEWE
    case 32645: cursor = [NSCursor resizeUpDownCursor]; break;   // IDC_SIZENS
    case 32646: cursor = [NSCursor openHandCursor]; break;       // IDC_SIZEALL
    case 32648: cursor = [NSCursor operationNotAllowedCursor]; break; // IDC_NO
    case 32649: cursor = [NSCursor pointingHandCursor]; break;   // IDC_HAND
    default: cursor = [NSCursor arrowCursor]; break;
    }
    return (__bridge void*)cursor; // system cursors are singletons; no ownership transfer
}

void* MacBridge_CreateCursorFromRGBA(const uint8_t* rgba, int width, int height, int hotX, int hotY) {
    if (!rgba || width <= 0 || height <= 0) return nullptr;
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                    pixelsWide:width
                                                                    pixelsHigh:height
                                                                 bitsPerSample:8
                                                               samplesPerPixel:4
                                                                      hasAlpha:YES
                                                                      isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace
                                                                   bytesPerRow:width * 4
                                                                  bitsPerPixel:32];
    if (!rep) return nullptr;
    memcpy(rep.bitmapData, rgba, (size_t)width * (size_t)height * 4);
    NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
    [image addRepresentation:rep];
    NSCursor* cursor = [[NSCursor alloc] initWithImage:image hotSpot:NSMakePoint(hotX, hotY)];
    return (__bridge_retained void*)cursor;
}

void MacBridge_ReleaseCursor(void* cursor) {
    if (cursor) CFRelease(cursor);
}

int MacBridge_GetCursorImageRGBA(void* cursorPtr, uint8_t** outPixels, int* outWidth, int* outHeight, int* outHotX, int* outHotY) {
    if (!cursorPtr || !outPixels) return 0;
    NSCursor* cursor = (__bridge NSCursor*)cursorPtr;
    NSImage* image = cursor.image;
    if (!image) return 0;
    CGImageRef cg = [image CGImageForProposedRect:nullptr context:nil hints:nil];
    if (!cg) return 0;
    const size_t w = CGImageGetWidth(cg), h = CGImageGetHeight(cg);
    uint8_t* pixels = (uint8_t*)calloc(w * h, 4);
    if (!pixels) return 0;
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(pixels, w, h, 8, w * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    if (!ctx) {
        free(pixels);
        return 0;
    }
    CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), cg);
    CGContextRelease(ctx);
    const double sx = image.size.width > 0 ? (double)w / image.size.width : 1.0;
    *outPixels = pixels;
    if (outWidth) *outWidth = (int)w;
    if (outHeight) *outHeight = (int)h;
    if (outHotX) *outHotX = (int)std::lround(cursor.hotSpot.x * sx);
    if (outHotY) *outHotY = (int)std::lround(cursor.hotSpot.y * sx);
    return 1;
}

// ----- Keyboard -----------------------------------------------------------------------------------
int MacBridge_IsCapsLockOn(void) {
    return (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kCGEventFlagMaskAlphaShift) != 0;
}

int MacBridge_TranslateKeyCode(uint16_t macKeyCode, int shift, int option, uint32_t* outChars, int maxChars) {
    if (!outChars || maxChars <= 0) return 0;
    CFDataRef data = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_layoutMutex);
        if (g_keyboardLayoutData) data = (CFDataRef)CFRetain(g_keyboardLayoutData);
    }
    if (!data) {
        if ([NSThread isMainThread]) {
            RefreshKeyboardLayoutOnMainThread();
            std::lock_guard<std::mutex> lock(g_layoutMutex);
            if (g_keyboardLayoutData) data = (CFDataRef)CFRetain(g_keyboardLayoutData);
        }
        if (!data) return 0;
    }
    const UCKeyboardLayout* layout = (const UCKeyboardLayout*)CFDataGetBytePtr(data);
    UInt32 modifierState = 0;
    if (shift) modifierState |= (shiftKey >> 8) & 0xFF;
    if (option) modifierState |= (optionKey >> 8) & 0xFF;
    UInt32 deadKeyState = 0;
    UniChar buffer[8];
    UniCharCount length = 0;
    const OSStatus status = UCKeyTranslate(layout, macKeyCode, kUCKeyActionDisplay, modifierState, LMGetKbdType(),
                                           kUCKeyTranslateNoDeadKeysBit, &deadKeyState, 8, &length, buffer);
    CFRelease(data);
    if (status != noErr || length == 0) return 0;
    int written = 0;
    for (UniCharCount i = 0; i < length && written < maxChars; ++i) {
        uint32_t cp = buffer[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < length) {
            const uint32_t low = buffer[i + 1];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        outChars[written++] = cp;
    }
    return written;
}

int MacBridge_IsMacKeyDown(uint16_t macKeyCode) {
    return CGEventSourceKeyState(kCGEventSourceStateHIDSystemState, macKeyCode) ? 1 : 0;
}

void MacBridge_GetKeyRepeatTimings(int* initialDelayMs, int* repeatIntervalMs) {
    // NSEvent reports seconds; defaults are roughly 15/2 ticks (250ms/33ms) on current macOS.
    const double initial = [NSEvent keyRepeatDelay];
    const double interval = [NSEvent keyRepeatInterval];
    if (initialDelayMs) *initialDelayMs = (int)std::lround(initial * 1000.0);
    if (repeatIntervalMs) *repeatIntervalMs = (int)std::lround(interval * 1000.0);
}

int MacBridge_GetDoubleClickIntervalMs(void) { return (int)std::lround([NSEvent doubleClickInterval] * 1000.0); }

// ----- Shell / UI ---------------------------------------------------------------------------------
int MacBridge_OpenURLOrPath(const char* urlOrPath) {
    if (!urlOrPath || !*urlOrPath) return 0;
    NSString* s = NSStringFromUtf8(urlOrPath);
    NSURL* url = nil;
    if ([s hasPrefix:@"http://"] || [s hasPrefix:@"https://"] || [s hasPrefix:@"file://"] || [s hasPrefix:@"mailto:"]) {
        url = [NSURL URLWithString:s];
    } else {
        url = [NSURL fileURLWithPath:s];
    }
    if (!url) return 0;
    return [[NSWorkspace sharedWorkspace] openURL:url] ? 1 : 0;
}

int MacBridge_RevealInFinder(const char* path) {
    if (!path) return 0;
    NSURL* url = [NSURL fileURLWithPath:NSStringFromUtf8(path)];
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[ url ]];
    return 1;
}

struct AlertRequest {
    const char* title;
    const char* message;
    int style;
    int buttons;
    int result;
};

int MacBridge_ShowAlert(const char* title, const char* message, int style, int buttons) {
    AlertRequest request{ title, message, style, buttons, 0 };
    MacBridge_RunOnMainThreadSync(
        [](void* ctx) {
            AlertRequest* r = (AlertRequest*)ctx;
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = NSStringFromUtf8(r->title);
            alert.informativeText = NSStringFromUtf8(r->message);
            alert.alertStyle = r->style == 2 ? NSAlertStyleCritical : (r->style == 1 ? NSAlertStyleWarning : NSAlertStyleInformational);
            switch (r->buttons) {
            case 1:
                [alert addButtonWithTitle:@"OK"];
                [alert addButtonWithTitle:@"Cancel"];
                break;
            case 2:
                [alert addButtonWithTitle:@"Yes"];
                [alert addButtonWithTitle:@"No"];
                break;
            case 3:
                [alert addButtonWithTitle:@"Yes"];
                [alert addButtonWithTitle:@"No"];
                [alert addButtonWithTitle:@"Cancel"];
                break;
            default:
                [alert addButtonWithTitle:@"OK"];
                break;
            }
            const NSModalResponse response = [alert runModal];
            r->result = (int)(response - NSAlertFirstButtonReturn);
        },
        &request);
    return request.result;
}

struct PanelRequest {
    const char* title;
    const char* initialDir;
    const char* defaultName;
    const char* extensions;
    int chooseDirectories;
    bool save;
    char* result;
};

static NSArray<UTType*>* ContentTypesFromExtensions(const char* extensions) {
    if (!extensions || !*extensions) return nil;
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (NSString* ext in [NSStringFromUtf8(extensions) componentsSeparatedByString:@";"]) {
        NSString* trimmed = [ext stringByTrimmingCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@" *."]];
        if (trimmed.length == 0) continue;
        UTType* type = [UTType typeWithFilenameExtension:trimmed];
        if (type) [types addObject:type];
    }
    return types.count ? types : nil;
}

static void RunPanel(void* ctx) {
    PanelRequest* r = (PanelRequest*)ctx;
    NSSavePanel* panel = nil;
    if (r->save) {
        panel = [NSSavePanel savePanel];
        if (r->defaultName) panel.nameFieldStringValue = NSStringFromUtf8(r->defaultName);
    } else {
        NSOpenPanel* open = [NSOpenPanel openPanel];
        open.canChooseFiles = !r->chooseDirectories;
        open.canChooseDirectories = r->chooseDirectories != 0;
        open.allowsMultipleSelection = NO;
        panel = open;
    }
    if (r->title) panel.message = NSStringFromUtf8(r->title);
    if (r->initialDir && *r->initialDir) panel.directoryURL = [NSURL fileURLWithPath:NSStringFromUtf8(r->initialDir)];
    NSArray<UTType*>* types = ContentTypesFromExtensions(r->extensions);
    if (types) panel.allowedContentTypes = types;
    [NSApp activateIgnoringOtherApps:YES];
    if ([panel runModal] == NSModalResponseOK) {
        NSURL* url = r->save ? panel.URL : ((NSOpenPanel*)panel).URLs.firstObject;
        r->result = CopyUtf8(url.path);
    }
}

char* MacBridge_RunOpenPanel(const char* title, const char* initialDir, const char* extensions, int chooseDirectories) {
    PanelRequest r{ title, initialDir, nullptr, extensions, chooseDirectories, false, nullptr };
    MacBridge_RunOnMainThreadSync(RunPanel, &r);
    return r.result;
}

char* MacBridge_RunSavePanel(const char* title, const char* initialDir, const char* defaultName, const char* extensions) {
    PanelRequest r{ title, initialDir, defaultName, extensions, 0, true, nullptr };
    MacBridge_RunOnMainThreadSync(RunPanel, &r);
    return r.result;
}

// ----- Clipboard ----------------------------------------------------------------------------------
int MacBridge_SetClipboardText(const char* utf8) {
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    return [pb setString:NSStringFromUtf8(utf8) forType:NSPasteboardTypeString] ? 1 : 0;
}

char* MacBridge_GetClipboardText(void) {
    NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    return CopyUtf8(s);
}

int MacBridge_SetClipboardImageRGBA(const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return 0;
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                    pixelsWide:width
                                                                    pixelsHigh:height
                                                                 bitsPerSample:8
                                                               samplesPerPixel:4
                                                                      hasAlpha:YES
                                                                      isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace
                                                                   bytesPerRow:width * 4
                                                                  bitsPerPixel:32];
    if (!rep) return 0;
    memcpy(rep.bitmapData, rgba, (size_t)width * (size_t)height * 4);
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    NSPasteboard* pb = [NSPasteboard generalPasteboard];
    [pb clearContents];
    return [pb setData:png forType:NSPasteboardTypePNG] ? 1 : 0;
}

// ----- Foreign windows ----------------------------------------------------------------------------
static void FillWindowInfo(NSDictionary* info, MacForeignWindowInfo* out, double primaryHeightUnused, double scale) {
    (void)primaryHeightUnused;
    memset(out, 0, sizeof(*out));
    out->windowId = [info[(id)kCGWindowNumber] unsignedIntValue];
    out->ownerPid = [info[(id)kCGWindowOwnerPID] intValue];
    out->layer = [info[(id)kCGWindowLayer] intValue];
    out->onScreen = [info[(id)kCGWindowIsOnscreen] boolValue] ? 1 : 0;
    CGRect bounds = CGRectZero;
    CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(id)kCGWindowBounds], &bounds);
    // CG window bounds already use a top-left origin in global display points.
    out->frame.left = (int32_t)std::lround(bounds.origin.x * scale);
    out->frame.top = (int32_t)std::lround(bounds.origin.y * scale);
    out->frame.right = out->frame.left + (int32_t)std::lround(bounds.size.width * scale);
    out->frame.bottom = out->frame.top + (int32_t)std::lround(bounds.size.height * scale);
    NSString* title = info[(id)kCGWindowName];
    NSString* owner = info[(id)kCGWindowOwnerName];
    if (title) strlcpy(out->title, title.UTF8String ?: "", sizeof(out->title));
    if (owner) strlcpy(out->ownerName, owner.UTF8String ?: "", sizeof(out->ownerName));
}

int MacBridge_ListWindows(MacForeignWindowInfo* out, int max) {
    if (!out || max <= 0) return 0;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!list) return 0;
    const double scale = MacBridge_GetCachedBackingScale();
    NSArray* windows = (__bridge_transfer NSArray*)list;
    int count = 0;
    for (NSDictionary* info in windows) {
        if (count >= max) break;
        FillWindowInfo(info, &out[count], 0, scale);
        ++count;
    }
    return count;
}

int MacBridge_GetWindowInfo(uint32_t windowId, MacForeignWindowInfo* out) {
    if (!out || windowId == 0) return 0;
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow, (CGWindowID)windowId);
    if (!list) return 0;
    NSArray* windows = (__bridge_transfer NSArray*)list;
    if (windows.count == 0) return 0;
    FillWindowInfo(windows[0], out, 0, MacBridge_GetCachedBackingScale());
    return 1;
}

uint32_t MacBridge_GetCachedWindowNumber(void) {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    return g_cache.valid ? g_cache.windowNumber : 0;
}

void MacBridge_ActivateGameWindow(void) {
    MacBridge_RunOnMainThreadAsync(
        [](void*) {
            [NSApp activateIgnoringOtherApps:YES];
            NSWindow* window = nil;
            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                window = g_gameWindow;
            }
            [window makeKeyAndOrderFront:nil];
        },
        nullptr);
}

// ----- Process ------------------------------------------------------------------------------------
char* MacBridge_CopyProcessPath(int pid) {
    char buffer[PROC_PIDPATHINFO_MAXSIZE];
    const int len = proc_pidpath(pid, buffer, sizeof(buffer));
    if (len <= 0) return nullptr;
    return strndup(buffer, (size_t)len);
}

char* MacBridge_CopyProcessCommandLine(void) {
    const int argc = *_NSGetArgc();
    char** argv = *_NSGetArgv();
    std::string joined;
    for (int i = 0; i < argc; ++i) {
        if (i) joined.push_back(' ');
        const char* arg = argv[i] ? argv[i] : "";
        const bool needsQuotes = strchr(arg, ' ') != nullptr;
        if (needsQuotes) joined.push_back('"');
        joined += arg;
        if (needsQuotes) joined.push_back('"');
    }
    return strdup(joined.c_str());
}

// ----- Main-thread dispatch -----------------------------------------------------------------------
void MacBridge_RunOnMainThreadSync(void (*fn)(void*), void* ctx) {
    if (!fn) return;
    if (pthread_main_np()) {
        fn(ctx);
        return;
    }
    dispatch_sync(dispatch_get_main_queue(), ^{
      fn(ctx);
    });
}

void MacBridge_RunOnMainThreadAsync(void (*fn)(void*), void* ctx) {
    if (!fn) return;
    dispatch_async(dispatch_get_main_queue(), ^{
      fn(ctx);
    });
}

int MacBridge_IsMainThread(void) { return pthread_main_np() ? 1 : 0; }

void MacBridge_PostEmptyEvent(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
      NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                          location:NSZeroPoint
                                     modifierFlags:0
                                         timestamp:0
                                      windowNumber:0
                                           context:nil
                                           subtype:0
                                             data1:0
                                             data2:0];
      [NSApp postEvent:event atStart:YES];
    });
}

int MacBridge_WritePNG(const char* path, const uint8_t* rgba, int width, int height) {
    if (!path || !rgba || width <= 0 || height <= 0) return 0;
    @autoreleasepool {
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                        pixelsWide:width
                                                                        pixelsHigh:height
                                                                     bitsPerSample:8
                                                                   samplesPerPixel:4
                                                                          hasAlpha:YES
                                                                          isPlanar:NO
                                                                    colorSpaceName:NSDeviceRGBColorSpace
                                                                       bytesPerRow:static_cast<NSInteger>(width) * 4
                                                                      bitsPerPixel:32];
        if (!rep) return 0;
        std::memcpy([rep bitmapData], rgba, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        return png && [png writeToFile:[NSString stringWithUTF8String:path] atomically:YES] ? 1 : 0;
    }
}

} // extern "C"
