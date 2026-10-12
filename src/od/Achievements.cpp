#include "od/Achievements.h"
#include "od/Account.h"
#include "od/GrantVerify.h"
#include "BuildInfo.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Jobs.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "ui/Strings.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>

namespace uach {
namespace {
std::mutex g_mutex;
struct Held { bool modded = false; long long iat = 0; };
std::map<std::string, Held> g_held;
// Progress is the game's own counters (data/achievements/progress.json): the
// best value any installation has reached, since a player may have used several.
std::map<std::string, double> g_progress;
std::map<std::string, long long> g_earned;    // achievement id -> held by the signed-in account (or anyone, when signed out)
std::string g_status;

const odach::Def* findDef(const std::string& id) {
    for (int i = 0; i < odach::kCatalogCount; ++i) if (id == odach::kCatalog[i].id) return &odach::kCatalog[i];
    return nullptr;
}

void absorb(const std::string& token, const std::string& wantSub, std::map<std::string, Held>& into) {
    odach::GrantFields f;
    if (!odach::verifyGrantToken(token, Account::get().issuer(), odach::grantPublicKeys(), f)) return;
    if (!findDef(f.ach)) return;
    if (!wantSub.empty() && f.sub != wantSub) return;
    auto& h = into[f.ach];
    if (!h.iat || f.iat < h.iat) { h.iat = f.iat; h.modded = f.modded; }
}

std::string pendingPath() { return upaths::home() + "/achievements-pending.json"; }
}  // namespace

bool keysBaked() { return !odach::grantPublicKeys().empty(); }

namespace {
// Grants the service last returned, kept so a local-only pass (a file changed
// on disk) does not drop them until the next network pass.
std::vector<std::string> g_remote;
std::string g_remoteStatus;
std::set<std::string> g_announced;     // already toasted, or there at start
bool g_baseline = false;
std::vector<std::string> g_fresh;      // newly confirmed, waiting for a toast
std::atomic<bool> g_running{false}, g_again{false}, g_againNet{false};

/** Newest modification time of the files the game writes, across installations. */
long long stamp(const std::vector<std::string>& dirs) {
    long long newest = 0;
    std::error_code ec;
    for (auto& d : dirs)
        for (const char* f : {"/achievements/progress.json", "/achievements/grants.json"}) {
            const auto t = std::filesystem::last_write_time(d + f, ec);
            if (!ec) newest = std::max<long long>(newest, (long long)t.time_since_epoch().count());
        }
    return newest;
}

void run(const std::vector<std::string>& dataDirs, bool network) {
    if (g_running.exchange(true)) {
        // One pass at a time; whatever arrives meanwhile is done straight after.
        g_again = true;
        if (network) g_againNet = true;
        return;
    }
    ujobs::run("achievements", [dataDirs, network](Job&) {
        std::map<std::string, Held> held;
        std::map<std::string, double> progress;
        std::map<std::string, long long> earned;
        for (auto& d : dataDirs) {
            json p = ujson::load(d + "/achievements/progress.json");
            if (p.contains("counters") && p["counters"].is_object())
                for (auto& [k, v] : p["counters"].items())
                    if (v.is_number()) progress[k] = std::max(progress[k], v.get<double>());
            if (p.contains("sets") && p["sets"].is_object())
                for (auto& [k, v] : p["sets"].items())
                    if (v.is_array()) progress[k] = std::max(progress[k], (double)v.size());
            if (p.contains("earned") && p["earned"].is_object())
                for (auto& [k, v] : p["earned"].items())
                    if (v.is_number_integer()) earned[k] = v.get<long long>();
        }
        const std::string sub = Account::get().accountId();
        // Offline first: what installations already hold.
        for (auto& d : dataDirs) {
            json j = ujson::load(d + "/achievements/grants.json");
            if (j.contains("grants") && j["grants"].is_array())
                for (auto& t : j["grants"]) if (t.is_string()) absorb(t.get<std::string>(), sub, held);
        }
        std::vector<std::string> remote;
        std::string status;
        bool fetched = false;
        const std::string tok = Account::get().token();
        if (!keysBaked()) status = N_("This build of the launcher cannot verify achievements.");
        else if (tok.empty()) status = N_("Sign in to see the achievements confirmed for your account.");
        else if (network) {
            uhttp::Response r = uhttp::get(Account::get().issuer() + "/achievements/mine", tok);
            json j = ujson::parse(r.body);
            if (r.ok() && j.is_object() && j.contains("grants") && j["grants"].is_array()) {
                for (auto& t : j["grants"]) if (t.is_string()) remote.push_back(t.get<std::string>());
                fetched = true;
            } else if (r.status == 503) {
                status = N_("The account service is not issuing achievements.");
            } else if (!r.ok()) {
                status = N_("Showing achievements saved on this computer; the account service is unreachable.");
            }
        }
        std::lock_guard<std::mutex> l(g_mutex);
        if (network) { if (fetched || tok.empty()) g_remote = remote; g_remoteStatus = status; }
        else if (tok.empty()) g_remote.clear();
        for (auto& t : g_remote) absorb(t, sub, held);
        // Anything confirmed since the last pass gets a toast -- but not what was
        // already there when the launcher started.
        for (auto& [id, h] : held) {
            if (g_announced.insert(id).second && g_baseline) g_fresh.push_back(id);
        }
        g_baseline = true;
        g_held = held;
        g_progress = progress;
        g_earned = earned;
        g_status = network ? status : (tok.empty() || !keysBaked() ? status : g_remoteStatus);
        ulog::info(std::string("achievements: ") + (network ? "service and files" : "files") + ", " +
                   std::to_string(held.size()) + " confirmed");
        g_running = false;
        return true;
    });
}
}  // namespace

void refresh(const std::vector<std::string>& dataDirs) { run(dataDirs, true); }

void tick(const std::vector<std::string>& dataDirs, double now, bool watching) {
    // Live, without hammering anything: a stat of two small files every two
    // seconds, and the service once a minute while somebody could be earning
    // (a game running, or this page open) or every five minutes otherwise.
    static double lastStat = -1e9, lastNet = 0;
    static long long lastStamp = -1;
    static std::string lastSub = Account::get().accountId();   // App::init already did the first pass
    if (g_again.exchange(false)) run(dataDirs, g_againNet.exchange(false));
    const std::string sub = Account::get().accountId();
    if (sub != lastSub) { lastSub = sub; lastNet = now; run(dataDirs, true); return; }   // signed in or out
    if (now - lastStat >= 2.0) {
        lastStat = now;
        const long long st = stamp(dataDirs);
        if (st != lastStamp) {
            const bool first = lastStamp == -1;
            lastStamp = st;
            if (!first) run(dataDirs, false);
        }
    }
    if (now - lastNet >= (watching ? 60.0 : 300.0)) { lastNet = now; run(dataDirs, true); }
}

std::vector<std::string> takeNewlyConfirmed() {
    std::lock_guard<std::mutex> l(g_mutex);
    std::vector<std::string> out;
    out.swap(g_fresh);
    return out;
}

const char* nameOf(const std::string& id) {
    const odach::Def* d = findDef(id);
    return d ? d->name : "";
}

std::vector<AchView> view() {
    std::lock_guard<std::mutex> l(g_mutex);
    std::vector<AchView> out;
    for (int i = 0; i < odach::kCatalogCount; ++i) {
        AchView v;
        v.def = &odach::kCatalog[i];
        auto it = g_held.find(v.def->id);
        if (it != g_held.end()) { v.granted = true; v.modded = it->second.modded; v.when = it->second.iat; }
        v.earned = v.granted || g_earned.count(v.def->id);
        auto pr = g_progress.find(v.def->stat);
        v.progress = pr == g_progress.end() ? 0 : pr->second;
        out.push_back(v);
    }
    return out;
}

int grantedCount() { std::lock_guard<std::mutex> l(g_mutex); return (int)g_held.size(); }
std::string status() { std::lock_guard<std::mutex> l(g_mutex); return g_status; }

void claim(const std::string& id) {
    {
        json p = ujson::load(pendingPath());
        if (!p.contains("pending") || !p["pending"].is_array()) p["pending"] = json::array();
        bool have = false;
        for (auto& x : p["pending"]) if (x.is_string() && x.get<std::string>() == id) have = true;
        if (!have) p["pending"].push_back(id);
        ujson::save(pendingPath(), p);
    }
    retryPending();
}

void retryPending() {
    const std::string tok = Account::get().token();
    if (tok.empty()) return;
    ujobs::run("claim", [tok](Job&) {
        json p = ujson::load(pendingPath());
        if (!p.contains("pending") || !p["pending"].is_array() || p["pending"].empty()) return true;
        const std::string iss = Account::get().issuer();
        uhttp::Response s = uhttp::postJson(iss + "/achievements/session",
                                            json{{"seal", "0"}, {"build", std::string("unifico-") + UNIFICO_VERSION}}.dump(), tok);
        const std::string play = ujson::str(ujson::parse(s.body), "play");
        if (play.empty()) return false;
        json claims = json::array();
        for (auto& x : p["pending"]) if (x.is_string()) claims.push_back({{"ach", x}, {"ev", "{\"source\":\"unifico\"}"}});
        uhttp::Response r = uhttp::postJson(iss + "/achievements/claim", json{{"play", play}, {"claims", claims}}.dump(), tok);
        json d = ujson::parse(r.body);
        if (!r.ok() || !d.contains("decisions")) return false;
        json still = json::array();
        for (auto& dec : d["decisions"]) {
            const std::string ach = ujson::str(dec, "ach");
            if (dec.contains("token")) continue;                         // granted
            if (ujson::str(dec, "refused") == "unknown") continue;       // never will be
            still.push_back(ach);
        }
        p["pending"] = still;
        ujson::save(pendingPath(), p);
        g_again = true; g_againNet = true;   // the new grant shows on the next frame
        return true;
    });
}
}  // namespace uach
