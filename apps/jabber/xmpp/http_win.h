// WinHTTP GET/PUT for CAPTCHA media and XEP-0363 HTTP Upload.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstdint>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace jabber {

struct HttpResult {
    bool ok = false;
    int status = 0;
    std::string error;
    std::vector<uint8_t> body;
};

inline bool parse_url(const std::string &url, bool *https, std::wstring *host,
                      INTERNET_PORT *port, std::wstring *path) {
    std::string u = url;
    *https = false;
    if (u.rfind("https://", 0) == 0) {
        *https = true;
        u = u.substr(8);
    } else if (u.rfind("http://", 0) == 0) {
        u = u.substr(7);
    } else {
        return false;
    }
    size_t slash = u.find('/');
    std::string hostport = slash == std::string::npos ? u : u.substr(0, slash);
    std::string p = slash == std::string::npos ? "/" : u.substr(slash);
    std::string h = hostport;
    *port = *https ? 443 : 80;
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
        h = hostport.substr(0, colon);
        *port = (INTERNET_PORT)atoi(hostport.c_str() + colon + 1);
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, h.c_str(), -1, nullptr, 0);
    host->assign(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, h.c_str(), -1, &(*host)[0], n);
    if (!host->empty() && host->back() == 0) host->pop_back();
    n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    path->assign(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &(*path)[0], n);
    if (!path->empty() && path->back() == 0) path->pop_back();
    return !host->empty();
}

inline HttpResult http_request(const std::string &method, const std::string &url,
                               const void *body, size_t body_len,
                               const wchar_t *content_type) {
    HttpResult r;
    bool https = false;
    std::wstring host, path;
    INTERNET_PORT port = 443;
    if (!parse_url(url, &https, &host, &port, &path)) {
        r.error = "bad url";
        return r;
    }
    HINTERNET ses = WinHttpOpen(L"SagradoJabber/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) {
        r.error = "WinHttpOpen failed";
        return r;
    }
    HINTERNET con = WinHttpConnect(ses, host.c_str(), port, 0);
    if (!con) {
        r.error = "WinHttpConnect failed";
        WinHttpCloseHandle(ses);
        return r;
    }
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    std::wstring wmethod(method.begin(), method.end());
    HINTERNET req =
        WinHttpOpenRequest(con, wmethod.c_str(), path.c_str(), nullptr,
                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        r.error = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return r;
    }
    if (content_type) {
        std::wstring hdr = L"Content-Type: ";
        hdr += content_type;
        hdr += L"\r\n";
        WinHttpAddRequestHeaders(req, hdr.c_str(), (ULONG)-1L,
                                 WINHTTP_ADDREQ_FLAG_ADD);
    }
    BOOL ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 (LPVOID)body, (DWORD)body_len, (DWORD)body_len, 0);
    if (!ok || !WinHttpReceiveResponse(req, nullptr)) {
        r.error = "request failed";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
        return r;
    }
    DWORD status = 0, sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz,
                        WINHTTP_NO_HEADER_INDEX);
    r.status = int(status);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        size_t off = r.body.size();
        r.body.resize(off + avail);
        DWORD read = 0;
        if (!WinHttpReadData(req, r.body.data() + off, avail, &read)) break;
        r.body.resize(off + read);
    }
    r.ok = (r.status >= 200 && r.status < 300);
    if (!r.ok && r.error.empty()) r.error = "HTTP " + std::to_string(r.status);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
    return r;
}

inline HttpResult http_get(const std::string &url) {
    return http_request("GET", url, nullptr, 0, nullptr);
}

inline HttpResult http_put(const std::string &url, const void *data, size_t len,
                           const wchar_t *content_type) {
    return http_request("PUT", url, data, len, content_type);
}

} // namespace jabber
