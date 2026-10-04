#include "od/Discord.h"
#include "core/Process.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <cerrno>
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

namespace udiscord {
namespace {
std::string g_app, g_details, g_state, g_lastSent;
long long g_started = 0;
double g_retryAt = 0, g_nextSend = 0;
bool g_ready = false, g_wantClear = false;
std::string g_inbox;

#if defined(_WIN32)
HANDLE g_pipe = INVALID_HANDLE_VALUE;
bool open_() {
    for (int i = 0; i < 10; ++i) {
        std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) { g_pipe = h; return true; }
    }
    return false;
}
bool isOpen() { return g_pipe != INVALID_HANDLE_VALUE; }
void close_() { if (isOpen()) CloseHandle(g_pipe); g_pipe = INVALID_HANDLE_VALUE; }
bool send_(const char* p, size_t n) {
    DWORD w = 0;
    return WriteFile(g_pipe, p, (DWORD)n, &w, nullptr) && w == n;
}
// Never blocks: only what PeekNamedPipe says is already there is read.
int recv_(char* buf, size_t cap) {
    DWORD avail = 0;
    if (!PeekNamedPipe(g_pipe, nullptr, 0, nullptr, &avail, nullptr)) return -1;
    if (!avail) return 0;
    DWORD r = 0;
    if (!ReadFile(g_pipe, buf, (DWORD)std::min<size_t>(cap, avail), &r, nullptr)) return -1;
    return (int)r;
}
#else
int g_fd = -1;
bool open_() {
    std::vector<std::string> roots;
    for (const char* v : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
        if (const char* e = std::getenv(v)) { std::string r = e; while (!r.empty() && r.back() == '/') r.pop_back(); if (!r.empty()) roots.push_back(r); }
    roots.push_back("/tmp");
    for (auto& root : roots)
        for (const char* sub : {"", "/app/com.discordapp.Discord", "/snap.discord"})
            for (int i = 0; i < 10; ++i) {
                std::string path = root + sub + "/discord-ipc-" + std::to_string(i);
                if (path.size() + 1 > sizeof(sockaddr_un::sun_path)) continue;
                int s = ::socket(AF_UNIX, SOCK_STREAM, 0);
                if (s < 0) continue;
                sockaddr_un a{};
                a.sun_family = AF_UNIX;
                std::strncpy(a.sun_path, path.c_str(), sizeof(a.sun_path) - 1);
                if (::connect(s, (sockaddr*)&a, sizeof a) == 0) {
                    ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#if defined(__APPLE__)
                    int one = 1;
                    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
                    g_fd = s;
                    return true;
                }
                ::close(s);
            }
    return false;
}
bool isOpen() { return g_fd >= 0; }
void close_() { if (g_fd >= 0) ::close(g_fd); g_fd = -1; }
bool send_(const char* p, size_t n) {
    size_t sent = 0;
    while (sent < n) {
#ifdef MSG_NOSIGNAL
        ssize_t k = ::send(g_fd, p + sent, n - sent, MSG_NOSIGNAL);
#else
        ssize_t k = ::send(g_fd, p + sent, n - sent, 0);
#endif
        if (k <= 0) return false;
        sent += (size_t)k;
    }
    return true;
}
int recv_(char* buf, size_t cap) {
    ssize_t n = ::recv(g_fd, buf, cap, 0);
    if (n > 0) return (int)n;
    if (n == 0) return -1;
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
}
#endif

std::string esc(const std::string& v) {
    std::string o;
    for (char c : v) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if ((unsigned char)c >= 0x20) o += c;
    }
    return o;
}

bool frame(uint32_t op, const std::string& payload) {
    std::vector<char> f(8 + payload.size());
    const uint32_t len = (uint32_t)payload.size();
    std::memcpy(f.data(), &op, 4);
    std::memcpy(f.data() + 4, &len, 4);
    std::memcpy(f.data() + 8, payload.data(), payload.size());
    return send_(f.data(), f.size());
}

void drop() { close_(); g_ready = false; g_lastSent.clear(); g_inbox.clear(); }

void readReplies() {
    char buf[2048];
    for (;;) {
        int n = recv_(buf, sizeof buf);
        if (n < 0) { drop(); return; }
        if (n == 0) break;
        g_inbox.append(buf, (size_t)n);
    }
    while (g_inbox.size() >= 8) {
        uint32_t op = 0, len = 0;
        std::memcpy(&op, g_inbox.data(), 4);
        std::memcpy(&len, g_inbox.data() + 4, 4);
        if (len > (1u << 20)) { drop(); return; }
        if (g_inbox.size() < 8 + len) break;
        const std::string body = g_inbox.substr(8, len);
        g_inbox.erase(0, 8 + len);
        if (op == 2) { drop(); return; }   // close: wrong app id, or Discord quit
        if (body.find("\"READY\"") != std::string::npos) g_ready = true;
    }
}
}  // namespace

void init(const std::string& appId) { g_app = appId; }

void set(const std::string& details, const std::string& state, long long startedAt) {
    g_details = details;
    g_state = state;
    g_started = startedAt;
    g_wantClear = details.empty();
}

void clear() { set("", "", 0); }

bool connected() { return isOpen() && g_ready; }

void tick(double now) {
    if (g_app.empty()) return;
    if (isOpen()) readReplies();
    if (!isOpen()) {
        if (g_wantClear || now < g_retryAt) return;   // nothing to show: do not even connect
        g_retryAt = now + 30;
        if (!open_()) return;
        if (!frame(0, "{\"v\":1,\"client_id\":\"" + esc(g_app) + "\"}")) { drop(); return; }
    }
    if (!g_ready || now < g_nextSend) return;
    std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"unifico\",\"args\":{\"pid\":" + std::to_string(uproc::selfPid());
    if (g_wantClear) payload += "}}";
    else {
        payload += ",\"activity\":{\"details\":\"" + esc(g_details) + "\"";
        if (!g_state.empty()) payload += ",\"state\":\"" + esc(g_state) + "\"";
        if (g_started > 0) payload += ",\"timestamps\":{\"start\":" + std::to_string(g_started) + "}";
        payload += ",\"assets\":{\"large_image\":\"unifico\",\"large_text\":\"Unifico\"}}}}";
    }
    if (payload == g_lastSent) return;
    if (!frame(1, payload)) { drop(); g_retryAt = now + 30; return; }
    g_lastSent = payload;
    g_nextSend = now + 4;   // Discord's rate limit; faster updates are silently dropped
}
}  // namespace udiscord
