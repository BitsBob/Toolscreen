// Plain-C HTTP client used by the WinHTTP shim (implemented in mac_http.mm).
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MacHttpResult {
    int status;
    char* contentType;
    unsigned char* body;
    size_t bodyLength;
    char* error;
} MacHttpResult;

// Performs a blocking request. Returns 1 when an HTTP response was received (any status).
int MacHttp_Perform(const char* method, const char* url, const char* headersCRLF, const void* body, size_t bodyLength, int timeoutMs,
                    MacHttpResult* out);
void MacHttp_FreeResult(MacHttpResult* result);

#ifdef __cplusplus
}
#endif
