#include "od/Analytics.h"
#include "BuildInfo.h"
#include "core/Http.h"
#include "core/Jobs.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "ui/Strings.h"

#include <mutex>
#include <random>
#include <vector>

namespace uanalytics {
namespace {
std::mutex g_mutex;
std::string g_issuer;
json g_queue = json::array();
// One per launcher run: GA shows Measurement Protocol events in Realtime and
// sessions only when they carry one. Identifies this run, never a person.
const long long g_session = []() { std::random_device rd; return (long long)(rd() & 0x7fffffff) + 1; }();

std::string newClientId() {
    std::random_device rd;
    std::uniform_int_distribution<int> d(0, 15);
    const char* hex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) s += '-';
        s += hex[d(rd)];
    }
    return s;
}
}  // namespace

void init(const std::string& issuer) { g_issuer = issuer; }

std::string clientId() { return Settings::get().analyticsClientId; }

void setConsent(bool yes) {
    Settings& s = Settings::get();
    s.analyticsConsent = yes ? "yes" : "no";
    if (yes && s.analyticsClientId.empty()) s.analyticsClientId = newClientId();
    if (!yes) {
        s.analyticsClientId.clear();
        std::lock_guard<std::mutex> l(g_mutex);
        g_queue = json::array();
    }
    s.save();
}

void event(const std::string& name, const std::map<std::string, std::string>& params) {
    const Settings& s = Settings::get();
    if (s.analyticsConsent != "yes" || s.analyticsClientId.empty()) return;
    json p = json::object();
    for (auto& [k, v] : params) p[k] = v;
    std::lock_guard<std::mutex> l(g_mutex);
    if (g_queue.size() < 40) g_queue.push_back({{"name", name}, {"params", p}});
}

void flush() {
    const Settings& s = Settings::get();
    if (s.analyticsConsent != "yes" || s.analyticsClientId.empty() || g_issuer.empty()) return;
    json batch;
    {
        std::lock_guard<std::mutex> l(g_mutex);
        if (g_queue.empty()) return;
        batch = g_queue;
        g_queue = json::array();
    }
    const std::string body = json{{"client_id", s.analyticsClientId}, {"session_id", g_session}, {"events", batch}}.dump();
    const std::string url = g_issuer + "/analytics/event";
    ujobs::run("analytics", [url, body](Job&) { return uhttp::postJson(url, body).ok(); });
}
}  // namespace uanalytics
