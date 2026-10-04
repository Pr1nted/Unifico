#pragma once
// HTTPS through the operating system: WinHTTP on Windows, the system libcurl
// elsewhere. Blocking; call from a Jobs worker, never from the frame.
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uhttp {

struct Response {
    int status = 0;          // 0 = never completed
    std::string body;
    std::string error;       // a sentence, empty on success
    bool ok() const { return status >= 200 && status < 300; }
};

struct Request {
    std::string method = "GET";
    std::string url;
    std::string body;                     // sent as application/json when non-empty
    std::string bearer;
    std::vector<std::string> headers;     // "Name: value"
    int timeoutSec = 30;
    size_t maxBytes = 16 * 1024 * 1024;
};

Response request(const Request& r);
Response get(const std::string& url, const std::string& bearer = {});
Response postJson(const std::string& url, const std::string& json, const std::string& bearer = {});

/**
 * Stream `url` to `path` (via path + ".part", renamed on success). `progress`
 * gets (done, total); total is 0 when the server did not say. Setting
 * `*cancel` stops it. Follows redirects -- GitHub asset URLs are all redirects.
 */
bool download(const std::string& url, const std::string& path,
              const std::function<void(uint64_t, uint64_t)>& progress,
              const std::atomic<bool>* cancel, std::string* error);

std::string urlEncode(const std::string& s);
}  // namespace uhttp
