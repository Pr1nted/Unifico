#include "od/Playtime.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "ui/Strings.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace uplay {
namespace {
std::mutex g_mutex;
std::string path() { return upaths::home() + "/playtime.json"; }
}

void add(const std::string& key, double secs) {
    if (secs < 1) return;
    std::lock_guard<std::mutex> l(g_mutex);
    json j = ujson::load(path());
    if (!j.contains("games") || !j["games"].is_object()) j["games"] = json::object();
    json& g = j["games"][key];
    if (!g.is_object()) g = json::object();
    g["seconds"] = ujson::num(g, "seconds") + secs;
    g["sessions"] = ujson::num(g, "sessions") + 1;
    g["last"] = (long long)std::time(nullptr);
    ujson::save(path(), j);
}

double seconds(const std::string& key) {
    std::lock_guard<std::mutex> l(g_mutex);
    json j = ujson::load(path());
    return j.contains("games") && j["games"].contains(key) ? ujson::num(j["games"][key], "seconds") : 0;
}

long long lastPlayed(const std::string& key) {
    std::lock_guard<std::mutex> l(g_mutex);
    json j = ujson::load(path());
    return j.contains("games") && j["games"].contains(key) ? (long long)ujson::num(j["games"][key], "last") : 0;
}

double openDoctrines(const std::vector<std::string>& dataDirs) {
    double launcher = 0, game = 0;
    {
        std::lock_guard<std::mutex> l(g_mutex);
        json j = ujson::load(path());
        if (j.contains("games") && j["games"].is_object())
            for (auto& [k, v] : j["games"].items()) if (k.rfind("od:", 0) == 0) launcher += ujson::num(v, "seconds");
    }
    for (auto& d : dataDirs) {
        json p = ujson::load(d + "/achievements/progress.json");
        if (p.contains("counters")) game += ujson::num(p["counters"], "seconds_played");
    }
    return std::max(launcher, game);
}

long long openDoctrinesLast() {
    std::lock_guard<std::mutex> l(g_mutex);
    json j = ujson::load(path());
    long long last = 0;
    if (j.contains("games") && j["games"].is_object())
        for (auto& [k, v] : j["games"].items()) if (k.rfind("od:", 0) == 0) last = std::max(last, (long long)ujson::num(v, "last"));
    return last;
}

std::string human(double s) {
    char b[64];
    if (s < 60) return T("less than a minute");
    if (s < 3600) { std::snprintf(b, sizeof b, T("%d minutes"), (int)(s / 60)); return b; }
    std::snprintf(b, sizeof b, T("%.1f hours"), s / 3600.0);
    return b;
}
}  // namespace uplay
