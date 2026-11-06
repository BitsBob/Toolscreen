// Symbol interposition for the macOS port.
//
// LWJGL resolves every native entry point at runtime: GLFW functions through dlsym() and OpenGL
// functions through CFBundleGetFunctionPointerForName() on OpenGL.framework, both called from
// liblwjgl.dylib. Rebinding those two imports *inside LWJGL's own images* lets Toolscreen hand
// LWJGL its hook functions in place of the real ones, which is how the Windows build's inline
// detours are replaced here. No code pages are modified, so this works under the hardened runtime
// of signed JDKs and on Apple Silicon, and nothing outside LWJGL is affected.
#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>

#include "mac_interpose.h"

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

namespace {

std::atomic<bool> g_installed{ false };

bool ImagePathIsLwjgl(const char* path) {
    if (!path) return false;
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    // liblwjgl.dylib performs the lookups; match the whole family so custom builds are covered too.
    return strncmp(base, "liblwjgl", 8) == 0;
}

// Rebinds `symbolName` in the symbol-pointer sections of one image.
void RebindSymbolInImage(const mach_header_64* header, intptr_t slide, const char* symbolName, void* replacement) {
    const segment_command_64* linkedit = nullptr;
    const symtab_command* symtab = nullptr;
    const dysymtab_command* dysymtab = nullptr;

    const uint8_t* cursor = reinterpret_cast<const uint8_t*>(header) + sizeof(mach_header_64);
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        const load_command* lc = reinterpret_cast<const load_command*>(cursor);
        if (lc->cmd == LC_SEGMENT_64) {
            const auto* seg = reinterpret_cast<const segment_command_64*>(lc);
            if (strcmp(seg->segname, SEG_LINKEDIT) == 0) linkedit = seg;
        } else if (lc->cmd == LC_SYMTAB) {
            symtab = reinterpret_cast<const symtab_command*>(lc);
        } else if (lc->cmd == LC_DYSYMTAB) {
            dysymtab = reinterpret_cast<const dysymtab_command*>(lc);
        }
        cursor += lc->cmdsize;
    }
    if (!linkedit || !symtab || !dysymtab || dysymtab->nindirectsyms == 0) return;

    const uintptr_t linkeditBase = static_cast<uintptr_t>(slide) + linkedit->vmaddr - linkedit->fileoff;
    const auto* symbols = reinterpret_cast<const nlist_64*>(linkeditBase + symtab->symoff);
    const char* strings = reinterpret_cast<const char*>(linkeditBase + symtab->stroff);
    const auto* indirect = reinterpret_cast<const uint32_t*>(linkeditBase + dysymtab->indirectsymoff);

    cursor = reinterpret_cast<const uint8_t*>(header) + sizeof(mach_header_64);
    for (uint32_t i = 0; i < header->ncmds; ++i) {
        const load_command* lc = reinterpret_cast<const load_command*>(cursor);
        cursor += lc->cmdsize;
        if (lc->cmd != LC_SEGMENT_64) continue;
        const auto* seg = reinterpret_cast<const segment_command_64*>(lc);
        if (strncmp(seg->segname, "__DATA", 6) != 0 && strncmp(seg->segname, "__AUTH", 6) != 0) continue;
        const auto* sections = reinterpret_cast<const section_64*>(seg + 1);
        for (uint32_t s = 0; s < seg->nsects; ++s) {
            const section_64& sect = sections[s];
            const uint32_t type = sect.flags & SECTION_TYPE;
            if (type != S_LAZY_SYMBOL_POINTERS && type != S_NON_LAZY_SYMBOL_POINTERS) continue;
            const uint32_t* indices = indirect + sect.reserved1;
            void** slots = reinterpret_cast<void**>(static_cast<uintptr_t>(slide) + sect.addr);
            const size_t count = static_cast<size_t>(sect.size / sizeof(void*));
            for (size_t k = 0; k < count; ++k) {
                const uint32_t index = indices[k];
                if (index & (INDIRECT_SYMBOL_ABS | INDIRECT_SYMBOL_LOCAL)) continue;
                if (index >= symtab->nsyms) continue;
                const char* name = strings + symbols[index].n_un.n_strx;
                if (name[0] != '_' || strcmp(name + 1, symbolName) != 0) continue;
                if (slots[k] == replacement) continue;

                // __DATA_CONST/__AUTH_CONST are read-only after binding; make the page writable.
                const vm_address_t page = reinterpret_cast<vm_address_t>(&slots[k]) & ~static_cast<vm_address_t>(vm_page_size - 1);
                vm_region_basic_info_data_64_t info{};
                mach_msg_type_number_t infoCount = VM_REGION_BASIC_INFO_COUNT_64;
                mach_vm_address_t regionAddress = page;
                mach_vm_size_t regionSize = 0;
                mach_port_t objectName = MACH_PORT_NULL;
                vm_prot_t originalProtection = VM_PROT_READ | VM_PROT_WRITE;
                if (mach_vm_region(mach_task_self(), &regionAddress, &regionSize, VM_REGION_BASIC_INFO_64,
                                   reinterpret_cast<vm_region_info_t>(&info), &infoCount, &objectName) == KERN_SUCCESS) {
                    originalProtection = info.protection;
                }
                const bool needsUnprotect = (originalProtection & VM_PROT_WRITE) == 0;
                if (needsUnprotect &&
                    vm_protect(mach_task_self(), page, vm_page_size, false, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
                    continue;
                }
                slots[k] = replacement;
                if (needsUnprotect) vm_protect(mach_task_self(), page, vm_page_size, false, originalProtection);
            }
        }
    }
}

// ----- Replacements ---------------------------------------------------------------------------------
void* InterposedDlsym(void* handle, const char* symbol) {
    void* result = dlsym(handle, symbol); // Toolscreen's own import: the real dlsym
    if (!result || !symbol) return result;
    if (symbol[0] == 'g' && symbol[1] == 'l') {
        if (symbol[2] == 'f' && symbol[3] == 'w') {
            ToolscreenMacNoteGlfwLibraryHandle(handle);
            if (void* hook = ToolscreenMacResolveGlfwHook(symbol, result)) return hook;
        } else if (void* hook = ToolscreenMacResolveGlHook(symbol, result)) {
            return hook;
        }
    }
    return result;
}

void* InterposedCFBundleGetFunctionPointerForName(CFBundleRef bundle, CFStringRef functionName) {
    void* result = CFBundleGetFunctionPointerForName(bundle, functionName);
    if (!result || !functionName) return result;
    char buffer[160];
    const char* name = CFStringGetCStringPtr(functionName, kCFStringEncodingASCII);
    if (!name && CFStringGetCString(functionName, buffer, sizeof(buffer), kCFStringEncodingASCII)) name = buffer;
    if (name && name[0] == 'g' && name[1] == 'l') {
        if (void* hook = ToolscreenMacResolveGlHook(name, result)) return hook;
    }
    return result;
}

void OnImageAdded(const mach_header* header, intptr_t slide) {
    Dl_info info{};
    if (!dladdr(header, &info) || !ImagePathIsLwjgl(info.dli_fname)) return;
    if (header->magic != MH_MAGIC_64) return;
    const auto* header64 = reinterpret_cast<const mach_header_64*>(header);
    RebindSymbolInImage(header64, slide, "dlsym", reinterpret_cast<void*>(&InterposedDlsym));
    RebindSymbolInImage(header64, slide, "CFBundleGetFunctionPointerForName",
                        reinterpret_cast<void*>(&InterposedCFBundleGetFunctionPointerForName));
    ToolscreenMacNoteLwjglImage(info.dli_fname);
}

} // namespace

extern "C" void ToolscreenMacInstallInterposition(void) {
    if (g_installed.exchange(true)) return;
    // Called for every image already loaded and for each one loaded later.
    _dyld_register_func_for_add_image(&OnImageAdded);
}
