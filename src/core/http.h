// Minimal HTTP(S) client on WinHTTP: requests, streamed responses and downloads.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kayra {

struct HttpResponse {
    int status = 0;          // 0 = network error (see error)
    std::string body;
    std::string error;
    bool ok() const { return status >= 200 && status < 300; }
};

// on_data, when given, receives the body as it arrives (body stays empty for
// 2xx responses); returning false aborts the transfer.
HttpResponse http_request(const std::string& method, const std::string& url, const std::string& body,
                          const std::vector<std::string>& headers, int timeout_ms = 30000,
                          const std::function<bool(const char*, size_t)>& on_data = nullptr,
                          const std::atomic<bool>* cancel = nullptr, int64_t* content_length = nullptr);

HttpResponse http_get(const std::string& url, int timeout_ms = 15000);

// Downloads to path (via a .part file). progress(got, total) may get total = -1.
bool http_download(const std::string& url, const std::string& path,
                   const std::function<void(int64_t, int64_t)>& progress, const std::atomic<bool>* cancel,
                   std::string* error);

}  // namespace kayra
