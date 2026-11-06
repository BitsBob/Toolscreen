// WinHTTP subset (see include/winhttp.h) on top of NSURLSession.
#include "platform/mac/mac_http_bridge.h"

#include <winhttp.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

enum class Kind { Session, Connection, Request };

struct HttpHandle {
    Kind kind;
    int timeoutMs = 30000;
    // Connection
    std::wstring host;
    INTERNET_PORT port = 0;
    // Request
    std::wstring verb;
    std::wstring path;
    bool secure = false;
    std::string extraHeaders;
    std::vector<unsigned char> body;
    bool sent = false;
    bool received = false;
    int status = 0;
    std::wstring contentType;
    std::vector<unsigned char> response;
    size_t readOffset = 0;
    HttpHandle* parent = nullptr;
};

HttpHandle* H(HINTERNET h) { return static_cast<HttpHandle*>(h); }

} // namespace

HINTERNET WinHttpOpen(LPCWSTR, DWORD, LPCWSTR, LPCWSTR, DWORD) { return new HttpHandle{ Kind::Session }; }

HINTERNET WinHttpConnect(HINTERNET session, LPCWSTR server, INTERNET_PORT port, DWORD) {
    if (!session || !server || H(session)->kind != Kind::Session) return nullptr;
    auto* c = new HttpHandle{ Kind::Connection };
    c->host = server;
    c->port = port;
    c->timeoutMs = H(session)->timeoutMs;
    c->parent = H(session);
    return c;
}

HINTERNET WinHttpOpenRequest(HINTERNET connect, LPCWSTR verb, LPCWSTR objectName, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD flags) {
    if (!connect || H(connect)->kind != Kind::Connection) return nullptr;
    auto* r = new HttpHandle{ Kind::Request };
    r->verb = verb ? verb : L"GET";
    r->path = objectName ? objectName : L"/";
    r->secure = (flags & WINHTTP_FLAG_SECURE) != 0;
    r->timeoutMs = H(connect)->timeoutMs;
    r->parent = H(connect);
    return r;
}

BOOL WinHttpSetTimeouts(HINTERNET handle, int, int connectTimeout, int sendTimeout, int receiveTimeout) {
    if (!handle) return FALSE;
    H(handle)->timeoutMs = std::max({ connectTimeout, sendTimeout, receiveTimeout, 1000 });
    return TRUE;
}

BOOL WinHttpAddRequestHeaders(HINTERNET request, LPCWSTR headers, DWORD length, DWORD) {
    if (!request || !headers) return FALSE;
    const std::wstring w = length == static_cast<DWORD>(-1) ? std::wstring(headers) : std::wstring(headers, length);
    H(request)->extraHeaders += macplat::WideToUtf8(w.c_str()) + "\r\n";
    return TRUE;
}

BOOL WinHttpSendRequest(HINTERNET request, LPCWSTR headers, DWORD headersLength, LPVOID optional, DWORD optionalLength, DWORD,
                        DWORD_PTR) {
    if (!request || H(request)->kind != Kind::Request) return FALSE;
    HttpHandle* r = H(request);
    if (headers) WinHttpAddRequestHeaders(request, headers, headersLength, 0);
    if (optional && optionalLength) {
        const auto* p = static_cast<const unsigned char*>(optional);
        r->body.assign(p, p + optionalLength);
    }
    r->sent = true;
    return TRUE;
}

BOOL WinHttpReceiveResponse(HINTERNET request, LPVOID) {
    if (!request || H(request)->kind != Kind::Request || !H(request)->sent) return FALSE;
    HttpHandle* r = H(request);
    const HttpHandle* c = r->parent;
    std::string url = r->secure ? "https://" : "http://";
    url += macplat::WideToUtf8(c->host.c_str());
    const INTERNET_PORT defaultPort = r->secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    if (c->port != 0 && c->port != defaultPort) url += ":" + std::to_string(c->port);
    std::string path = macplat::WideToUtf8(r->path.c_str());
    if (path.empty() || path[0] != '/') path.insert(path.begin(), '/');
    url += path;

    MacHttpResult result{};
    const std::string verb = macplat::WideToUtf8(r->verb.c_str());
    const int ok = MacHttp_Perform(verb.c_str(), url.c_str(), r->extraHeaders.c_str(), r->body.empty() ? nullptr : r->body.data(),
                                   r->body.size(), r->timeoutMs, &result);
    if (!ok) {
        MacHttp_FreeResult(&result);
        SetLastError(12002 /* ERROR_WINHTTP_TIMEOUT */);
        return FALSE;
    }
    r->status = result.status;
    r->contentType = result.contentType ? macplat::Utf8ToWide(result.contentType) : std::wstring();
    r->response.assign(result.body, result.body + result.bodyLength);
    r->readOffset = 0;
    r->received = true;
    MacHttp_FreeResult(&result);
    return TRUE;
}

BOOL WinHttpQueryHeaders(HINTERNET request, DWORD infoLevel, LPCWSTR, LPVOID buffer, LPDWORD bufferLength, LPDWORD) {
    if (!request || !H(request)->received || !bufferLength) return FALSE;
    HttpHandle* r = H(request);
    const DWORD level = infoLevel & 0xFFFF;
    if (level == WINHTTP_QUERY_STATUS_CODE && (infoLevel & WINHTTP_QUERY_FLAG_NUMBER)) {
        if (!buffer || *bufferLength < sizeof(DWORD)) {
            *bufferLength = sizeof(DWORD);
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        *static_cast<DWORD*>(buffer) = static_cast<DWORD>(r->status);
        *bufferLength = sizeof(DWORD);
        return TRUE;
    }
    std::wstring value;
    if (level == WINHTTP_QUERY_STATUS_CODE) value = std::to_wstring(r->status);
    else if (level == WINHTTP_QUERY_CONTENT_TYPE) value = r->contentType;
    else if (level == WINHTTP_QUERY_CONTENT_LENGTH) value = std::to_wstring(r->response.size());
    else {
        SetLastError(12150 /* ERROR_WINHTTP_HEADER_NOT_FOUND */);
        return FALSE;
    }
    if (value.empty() && level == WINHTTP_QUERY_CONTENT_TYPE) {
        SetLastError(12150);
        return FALSE;
    }
    const DWORD needed = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    if (!buffer || *bufferLength < needed) {
        *bufferLength = needed;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    wmemcpy(static_cast<wchar_t*>(buffer), value.c_str(), value.size() + 1);
    *bufferLength = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    return TRUE;
}

BOOL WinHttpQueryDataAvailable(HINTERNET request, LPDWORD available) {
    if (!request || !H(request)->received || !available) return FALSE;
    HttpHandle* r = H(request);
    *available = static_cast<DWORD>(std::min<size_t>(r->response.size() - r->readOffset, 1u << 20));
    return TRUE;
}

BOOL WinHttpReadData(HINTERNET request, LPVOID buffer, DWORD toRead, LPDWORD read) {
    if (!request || !H(request)->received || !buffer) return FALSE;
    HttpHandle* r = H(request);
    const size_t n = std::min<size_t>(toRead, r->response.size() - r->readOffset);
    memcpy(buffer, r->response.data() + r->readOffset, n);
    r->readOffset += n;
    if (read) *read = static_cast<DWORD>(n);
    return TRUE;
}

BOOL WinHttpCloseHandle(HINTERNET handle) {
    delete H(handle);
    return TRUE;
}

BOOL WinHttpCrackUrl(LPCWSTR url, DWORD urlLength, DWORD, LPURL_COMPONENTS c) {
    if (!url || !c) return FALSE;
    const std::wstring_view s(url, urlLength ? urlLength : wcslen(url));
    const size_t schemeEnd = s.find(L"://");
    if (schemeEnd == std::wstring_view::npos) {
        SetLastError(12006 /* ERROR_WINHTTP_UNRECOGNIZED_SCHEME */);
        return FALSE;
    }
    const std::wstring_view scheme = s.substr(0, schemeEnd);
    if (scheme == L"https") c->nScheme = INTERNET_SCHEME_HTTPS;
    else if (scheme == L"http") c->nScheme = INTERNET_SCHEME_HTTP;
    else c->nScheme = 0;
    const size_t hostStart = schemeEnd + 3;
    size_t pathStart = s.find_first_of(L"/?#", hostStart);
    if (pathStart == std::wstring_view::npos) pathStart = s.size();
    std::wstring_view authority = s.substr(hostStart, pathStart - hostStart);
    const size_t at = authority.rfind(L'@');
    if (at != std::wstring_view::npos) authority = authority.substr(at + 1);
    std::wstring_view host = authority;
    INTERNET_PORT port = c->nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    const size_t colon = authority.rfind(L':');
    if (colon != std::wstring_view::npos && authority.find(L']') == std::wstring_view::npos) {
        host = authority.substr(0, colon);
        port = static_cast<INTERNET_PORT>(wcstol(std::wstring(authority.substr(colon + 1)).c_str(), nullptr, 10));
    }
    c->nPort = port;
    const size_t queryStart = s.find_first_of(L"?#", pathStart);
    const std::wstring_view path = s.substr(pathStart, (queryStart == std::wstring_view::npos ? s.size() : queryStart) - pathStart);
    const std::wstring_view extra = queryStart == std::wstring_view::npos ? std::wstring_view() : s.substr(queryStart);

    auto assign = [](LPWSTR& outPtr, DWORD& outLen, std::wstring_view part) {
        if (outPtr && outLen > 0 && outLen != static_cast<DWORD>(-1)) {
            const size_t n = std::min<size_t>(part.size(), outLen - 1);
            wmemcpy(outPtr, part.data(), n);
            outPtr[n] = 0;
            outLen = static_cast<DWORD>(n);
        } else if (outLen != 0) {
            outPtr = const_cast<LPWSTR>(part.data());
            outLen = static_cast<DWORD>(part.size());
        }
    };
    assign(c->lpszScheme, c->dwSchemeLength, scheme);
    assign(c->lpszHostName, c->dwHostNameLength, host);
    assign(c->lpszUrlPath, c->dwUrlPathLength, path);
    assign(c->lpszExtraInfo, c->dwExtraInfoLength, extra);
    if (c->dwUserNameLength) {
        c->lpszUserName = nullptr;
        c->dwUserNameLength = 0;
    }
    if (c->dwPasswordLength) {
        c->lpszPassword = nullptr;
        c->dwPasswordLength = 0;
    }
    return TRUE;
}
