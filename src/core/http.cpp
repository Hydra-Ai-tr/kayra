#include "core/http.h"

#include <windows.h>
#include <winhttp.h>

#include <filesystem>
#include <fstream>

#include "core/util.h"

namespace kayra {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x = nullptr) : h(x) {}
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    operator HINTERNET() const { return h; }
};

HINTERNET session() {
    static HINTERNET s = [] {
        std::wstring agent = widen(std::string("Kayra/") + KAYRA_VERSION);
        HINTERNET h = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
        if (!h)
            h = WinHttpOpen(L"Kayra", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (h) {
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
            protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
            if (!WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
                protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                WinHttpSetOption(h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
            }
        }
        return h;
    }();
    return s;
}

std::string last_error(const char* what) {
    DWORD e = GetLastError();
    std::string msg = std::string(what) + " (" + std::to_string(e) + ")";
    switch (e) {
        case ERROR_WINHTTP_TIMEOUT: msg += ": timeout"; break;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: msg += ": name not resolved (offline?)"; break;
        case ERROR_WINHTTP_CANNOT_CONNECT: msg += ": cannot connect"; break;
        case ERROR_WINHTTP_CONNECTION_ERROR: msg += ": connection error"; break;
        case ERROR_WINHTTP_SECURE_FAILURE: msg += ": TLS failure"; break;
        default: break;
    }
    return msg;
}

}  // namespace

HttpResponse http_request(const std::string& method, const std::string& url, const std::string& body,
                          const std::vector<std::string>& headers, int timeout_ms,
                          const std::function<bool(const char*, size_t)>& on_data,
                          const std::atomic<bool>* cancel, int64_t* content_length) {
    HttpResponse r;
    if (!session()) {
        r.error = last_error("WinHttpOpen");
        return r;
    }
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[4096];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    wchar_t extra[4096];
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = "bad URL: " + url;
        return r;
    }
    std::wstring full_path = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    if (full_path.empty()) full_path = L"/";

    Handle conn(WinHttpConnect(session(), std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0));
    if (!conn) {
        r.error = last_error("connect");
        return r;
    }
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle req(WinHttpOpenRequest(conn, widen(method).c_str(), full_path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req) {
        r.error = last_error("open request");
        return r;
    }
    WinHttpSetTimeouts(req, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    std::wstring hdr;
    for (auto& h : headers) hdr += widen(h) + L"\r\n";
    BOOL sent = WinHttpSendRequest(req, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(), (DWORD)-1L,
                                   body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
                                   (DWORD)body.size(), 0);
    if (!sent || !WinHttpReceiveResponse(req, nullptr)) {
        r.error = last_error("request failed");
        return r;
    }
    DWORD status = 0, sz = sizeof status;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &sz, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)status;
    if (content_length) {
        wchar_t len[32];
        DWORD lsz = sizeof len;
        *content_length = -1;
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, len, &lsz,
                                WINHTTP_NO_HEADER_INDEX))
            *content_length = _wtoi64(len);
    }
    bool stream = on_data && r.ok();
    std::string buf;
    for (;;) {
        if (cancel && cancel->load()) {
            r.error = "cancelled";
            r.status = 0;
            break;
        }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) {
            r.error = last_error("read failed");
            if (!stream) r.status = 0;
            break;
        }
        if (avail == 0) break;
        buf.resize(avail);
        DWORD got = 0;
        if (!WinHttpReadData(req, buf.data(), avail, &got)) {
            r.error = last_error("read failed");
            break;
        }
        if (got == 0) break;
        if (stream) {
            if (!on_data(buf.data(), got)) break;
        } else {
            r.body.append(buf.data(), got);
        }
    }
    return r;
}

HttpResponse http_get(const std::string& url, int timeout_ms) {
    return http_request("GET", url, "", {"Accept: application/json"}, timeout_ms);
}

bool http_download(const std::string& url, const std::string& path,
                   const std::function<void(int64_t, int64_t)>& progress, const std::atomic<bool>* cancel,
                   std::string* error) {
    std::string part = path + ".part";
    std::ofstream f(std::filesystem::path(widen(part)), std::ios::binary | std::ios::trunc);
    if (!f) {
        if (error) *error = "cannot write " + part;
        return false;
    }
    int64_t got = 0, total = -1;
    HttpResponse r = http_request(
        "GET", url, "", {}, 60000,
        [&](const char* p, size_t n) {
            f.write(p, (std::streamsize)n);
            got += (int64_t)n;
            if (progress) progress(got, total);
            return (bool)f;
        },
        cancel, &total);
    f.close();
    if (!r.ok() || !r.error.empty()) {
        DeleteFileW(widen(part).c_str());
        if (error) *error = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
        return false;
    }
    if (!MoveFileExW(widen(part).c_str(), widen(path).c_str(), MOVEFILE_REPLACE_EXISTING)) {
        if (error) *error = "cannot move the file into place";
        return false;
    }
    return true;
}

}  // namespace kayra
