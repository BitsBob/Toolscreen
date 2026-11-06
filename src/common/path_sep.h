#pragma once

// Path separator used when Toolscreen joins paths. Backslash on Windows; the macOS compatibility
// layer (platform/mac/win32_compat.h) defines these as '/' before this header is seen.
#ifndef TS_WSEP
#define TS_WSEP L"\\"
#define TS_SEP "\\"
#define TS_WSEP_CHAR L'\\'
#define TS_SEP_CHAR '\\'
#endif
