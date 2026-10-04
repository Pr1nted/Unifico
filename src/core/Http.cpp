#include "core/Http.h"
#include "BuildInfo.h"

#include <cstdio>
#include <filesystem>

#if defined(_WIN32)
#  include <windows.h>
#  include <winhttp.h>
#else
#  include <curl/curl.h>
#endif

namespace fs = std::filesystem;

namespace uhttp {

static const char* kUserAgent = "Unifico/" UNIFICO_VERSION " (+https://opendoctrines.pages.dev)";

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

#if !defined(_WIN32)
namespace {
struct Sink {
    std::string* body = nullptr;
    size_t max = 0;
    bool overflow = false;
    std::FILE* file = nullptr;
    const std::function<void(uint64_t, uint64_t)>* progress = nullptr;
    const std::atomic<bool>* cancel = nullptr;
};
size_t onData(char* p, size_t sz, size_t n, void* u) {
    Sink* s = (Sink*)u;
    const size_t len = sz * n;
    if (s->file) return std::fwrite(p, 1, len, s->file);
    if (s->body->size() + len > s->max) { s->overflow = true; return 0; }
    s->body->append(p, len);
    return len;
}
int onProgress(void* u, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    Sink* s = (Sink*)u;
    if (s->cancel && s->cancel->load()) return 1;
    if (s->progress && *s->progress) (*s->progress)((uint64_t)now, (uint64_t)total);
    return 0;
}
struct CurlInit { CurlInit() { curl_global_init(CURL_GLOBAL_DEFAULT); } };
CurlInit g_curlInit;
}  // namespace

Response request(const Request& r) {
    Response res;
    CURL* c = curl_easy_init();
    if (!c) { res.error = "could not start a network request"; return res; }
    Sink sink;
    sink.body = &res.body;
    sink.max = r.maxBytes;
    curl_slist* headers = nullptr;
    for (auto& h : r.headers) headers = curl_slist_append(headers, h.c_str());
    if (!r.bearer.empty()) headers = curl_slist_append(headers, ("Authorization: Bearer " + r.bearer).c_str());
    if (!r.body.empty()) headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, r.url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)r.timeoutSec);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    if (headers) curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    if (r.method == "POST") {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, r.body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)r.body.size());
    } else if (r.method != "GET") {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, r.method.c_str());
    }
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    res.status = rc == CURLE_OK ? (int)status : 0;
    if (rc != CURLE_OK) res.error = sink.overflow ? "the reply was too large" : curl_easy_strerror(rc);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    return res;
}

bool download(const std::string& url, const std::string& path,
              const std::function<void(uint64_t, uint64_t)>& progress,
              const std::atomic<bool>* cancel, std::string* error) {
    std::error_code ec;
    if (fs::path(path).has_parent_path()) fs::create_directories(fs::path(path).parent_path(), ec);
    const std::string part = path + ".part";
    std::FILE* f = std::fopen(part.c_str(), "wb");
    if (!f) { if (error) *error = "cannot write " + part; return false; }
    CURL* c = curl_easy_init();
    Sink sink;
    sink.file = f;
    sink.progress = &progress;
    sink.cancel = cancel;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 64L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    std::fclose(f);
    if (rc != CURLE_OK) {
        fs::remove(part, ec);
        if (error) *error = (cancel && cancel->load()) ? "cancelled" : curl_easy_strerror(rc);
        return false;
    }
    fs::remove(path, ec);
    fs::rename(part, path, ec);
    if (ec && error) *error = ec.message();
    return !ec;
}
#else
namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
struct Handles {
    HINTERNET session = nullptr, connect = nullptr, request = nullptr;
    ~Handles() {
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
    }
};
// Opens and sends; leaves the request ready for WinHttpReadData.
bool open(const Request& r, Handles& h, int& status, std::string& err, uint64_t* contentLength) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256], path[2048];
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    wchar_t extra[2048]; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
    std::wstring wurl = widen(r.url);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { err = "bad URL"; return false; }
    std::wstring ua = widen(kUserAgent);
    h.session = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.session) { err = "could not start a network request"; return false; }
    WinHttpSetTimeouts(h.session, 15000, 15000, r.timeoutSec * 1000, r.timeoutSec * 1000);
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(h.session, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof redir);
    h.connect = WinHttpConnect(h.session, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    if (!h.connect) { err = "could not reach the server"; return false; }
    std::wstring fullPath = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    h.request = WinHttpOpenRequest(h.connect, widen(r.method).c_str(), fullPath.c_str(), nullptr,
                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!h.request) { err = "could not open the request"; return false; }
    DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(h.request, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof decomp);
    std::wstring hdrs;
    for (auto& x : r.headers) hdrs += widen(x) + L"\r\n";
    if (!r.bearer.empty()) hdrs += L"Authorization: Bearer " + widen(r.bearer) + L"\r\n";
    if (!r.body.empty()) hdrs += L"Content-Type: application/json\r\n";
    if (!WinHttpSendRequest(h.request, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(),
                            hdrs.empty() ? 0 : (DWORD)-1, (LPVOID)(r.body.empty() ? nullptr : r.body.data()),
                            (DWORD)r.body.size(), (DWORD)r.body.size(), 0) ||
        !WinHttpReceiveResponse(h.request, nullptr)) {
        err = "the server did not answer";
        return false;
    }
    DWORD code = 0, sz = sizeof code;
    WinHttpQueryHeaders(h.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &code, &sz, nullptr);
    status = (int)code;
    if (contentLength) {
        wchar_t buf[32]; DWORD bl = sizeof buf;
        *contentLength = WinHttpQueryHeaders(h.request, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, buf, &bl, nullptr)
                             ? (uint64_t)_wtoi64(buf) : 0;
    }
    return true;
}
}  // namespace

Response request(const Request& r) {
    Response res;
    Handles h;
    int status = 0;
    if (!open(r, h, status, res.error, nullptr)) return res;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(h.request, &avail) && avail) {
        std::string chunk(avail, 0);
        DWORD read = 0;
        if (!WinHttpReadData(h.request, &chunk[0], avail, &read)) break;
        res.body.append(chunk.data(), read);
        if (res.body.size() > r.maxBytes) { res.error = "the reply was too large"; return res; }
    }
    res.status = status;
    return res;
}

bool download(const std::string& url, const std::string& path,
              const std::function<void(uint64_t, uint64_t)>& progress,
              const std::atomic<bool>* cancel, std::string* error) {
    Request r;
    r.url = url;
    r.timeoutSec = 120;
    Handles h;
    int status = 0;
    uint64_t total = 0;
    std::string err;
    if (!open(r, h, status, err, &total) || status < 200 || status >= 300) {
        if (error) *error = err.empty() ? "HTTP " + std::to_string(status) : err;
        return false;
    }
    std::error_code ec;
    if (fs::path(path).has_parent_path()) fs::create_directories(fs::path(path).parent_path(), ec);
    const std::string part = path + ".part";
    std::FILE* f = _wfopen(fs::path(part).wstring().c_str(), L"wb");
    if (!f) { if (error) *error = "cannot write " + part; return false; }
    uint64_t done = 0;
    DWORD avail = 0;
    std::string chunk;
    while (WinHttpQueryDataAvailable(h.request, &avail) && avail) {
        if (cancel && cancel->load()) { std::fclose(f); fs::remove(part, ec); if (error) *error = "cancelled"; return false; }
        chunk.resize(avail);
        DWORD read = 0;
        if (!WinHttpReadData(h.request, &chunk[0], avail, &read)) break;
        std::fwrite(chunk.data(), 1, read, f);
        done += read;
        if (progress) progress(done, total);
    }
    std::fclose(f);
    if (total && done != total) { fs::remove(part, ec); if (error) *error = "the download was cut short"; return false; }
    fs::remove(path, ec);
    fs::rename(part, path, ec);
    return !ec;
}
#endif

Response get(const std::string& url, const std::string& bearer) {
    Request r; r.url = url; r.bearer = bearer;
    return request(r);
}

Response postJson(const std::string& url, const std::string& json, const std::string& bearer) {
    Request r; r.method = "POST"; r.url = url; r.body = json.empty() ? "{}" : json; r.bearer = bearer;
    return request(r);
}
}  // namespace uhttp
