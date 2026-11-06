// Kernel-object style handles for the macOS Win32 compatibility layer.
#pragma once

#include <memory>

namespace macwin {

enum class HandleKind { Event, Timer, Mutex, Thread, File, Process, Hook };

struct HandleObject {
    explicit HandleObject(HandleKind k) : kind(k) {}
    virtual ~HandleObject() = default;
    const HandleKind kind;
};

// Registers an object and returns its HANDLE. Handles stay valid until CloseHandle.
HANDLE RegisterHandle(std::shared_ptr<HandleObject> object);
std::shared_ptr<HandleObject> LookupHandle(HANDLE handle);
template <class T> std::shared_ptr<T> LookupHandleAs(HANDLE handle, HandleKind kind) {
    std::shared_ptr<HandleObject> object = LookupHandle(handle);
    if (!object || object->kind != kind) return nullptr;
    return std::static_pointer_cast<T>(object);
}

} // namespace macwin
