// macOS build: MinHook is not used. Hooks are installed by src/platform/mac/mac_hooks.cpp through
// symbol interposition instead of inline detours, so every MinHook entry point reports failure.
#pragma once

typedef enum MH_STATUS {
    MH_UNKNOWN = -1,
    MH_OK = 0,
    MH_ERROR_ALREADY_INITIALIZED,
    MH_ERROR_NOT_INITIALIZED,
    MH_ERROR_ALREADY_CREATED,
    MH_ERROR_NOT_CREATED,
    MH_ERROR_ENABLED,
    MH_ERROR_DISABLED,
    MH_ERROR_NOT_EXECUTABLE,
    MH_ERROR_UNSUPPORTED_FUNCTION,
    MH_ERROR_MEMORY_ALLOC,
    MH_ERROR_MEMORY_PROTECT,
    MH_ERROR_MODULE_NOT_FOUND,
    MH_ERROR_FUNCTION_NOT_FOUND
} MH_STATUS;

#define MH_ALL_HOOKS NULL

inline MH_STATUS MH_Initialize() { return MH_ERROR_UNSUPPORTED_FUNCTION; }
inline MH_STATUS MH_Uninitialize() { return MH_ERROR_NOT_INITIALIZED; }
inline MH_STATUS MH_CreateHook(void*, void*, void**) { return MH_ERROR_UNSUPPORTED_FUNCTION; }
inline MH_STATUS MH_CreateHookApi(const wchar_t*, const char*, void*, void**) { return MH_ERROR_UNSUPPORTED_FUNCTION; }
inline MH_STATUS MH_RemoveHook(void*) { return MH_ERROR_NOT_CREATED; }
inline MH_STATUS MH_EnableHook(void*) { return MH_ERROR_NOT_CREATED; }
inline MH_STATUS MH_DisableHook(void*) { return MH_ERROR_NOT_CREATED; }
inline MH_STATUS MH_QueueEnableHook(void*) { return MH_ERROR_NOT_CREATED; }
inline MH_STATUS MH_QueueDisableHook(void*) { return MH_ERROR_NOT_CREATED; }
inline MH_STATUS MH_ApplyQueued() { return MH_ERROR_NOT_INITIALIZED; }
inline const char* MH_StatusToString(MH_STATUS) { return "MinHook is not available on macOS"; }
