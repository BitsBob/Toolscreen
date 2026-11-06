// macOS build: MSVC intrinsics header. x86_64 builds get the real compiler intrinsics; the few MSVC-only
// ones the shared code uses are provided by platform/mac/win32_compat.h (force-included everywhere).
#pragma once
#if defined(__x86_64__)
#include <immintrin.h>
#endif
