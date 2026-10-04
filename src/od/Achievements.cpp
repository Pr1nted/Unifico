#include "od/Achievements.h"
#include "od/Account.h"
#include "od/GrantVerify.h"
#include "BuildInfo.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Jobs.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "ui/Strings.h"

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

void refresh(const std::vector<std::string>& dataDirs) {
    ujobs::run("achievements", [dataDirs](Job&) {
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
        std::string status;
        const std::string tok = Account::get().token();
        if (!keysBaked()) status = N_("This build of the launcher cannot verify achievements.");
        else if (tok.empty()) status = N_("Sign in to see the achievements confirmed for your account.");
        else {
            uhttp::Response r = uhttp::get(Account::get().issuer() + "/achievements/mine", tok);
            json j = ujson::parse(r.body);
            if (r.ok() && j.is_object() && j.contains("grants") && j["grants"].is_array()) {
                for (auto& t : j["grants"]) if (t.is_string()) absorb(t.get<std::string>(), sub, held);
            } else if (r.status == 503) {
                status = N_("The account service is not issuing achievements.");
            } else if (!r.ok()) {
                status = N_("Showing achievements saved on this computer; the account service is unreachable.");
            }
        }
        std::lock_guard<std::mutex> l(g_mutex);
        g_held = held;
        g_progress = progress;
        g_earned = earned;
        g_status = status;
        return true;
    });
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
        return true;
    });
}
}  // namespace uach
