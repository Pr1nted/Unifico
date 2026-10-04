#include "od/Admin.h"
#include "core/Http.h"
#include "core/Jobs.h"
#include "raylib.h"
#include "ui/Strings.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <functional>
#include <mutex>

namespace uadmin {
namespace {

std::mutex g_mutex;
long long g_accounts = -1;
bool g_exact = true;
std::vector<NewsEntry> g_news;
std::vector<Report> g_reports;
Profile g_profile;
std::string g_status;
std::atomic<int> g_inflight{0};

std::string base() {
    std::string b = Account::get().issuer();
    while (!b.empty() && b.back() == '/') b.pop_back();
    return b;
}

void setStatus(const std::string& s) { std::lock_guard<std::mutex> l(g_mutex); g_status = s; }

/** What a failed request means to whoever is looking at the page. */
std::string failure(const uhttp::Response& r) {
    if (r.status == 404) {
        // 404 is the service's answer to an account without the badge, so it
        // does not confirm the route exists. A real "not found" carries a code.
        const std::string code = ujson::str(ujson::parse(r.body), "code");
        if (code.empty() || code == "not_found") return N_("This account may not use the admin tools.");
    }
    const std::string said = ujson::str(ujson::parse(r.body), "message");
    if (!said.empty()) return said;
    return r.status ? N_("That did not go through.") : N_("Could not reach the service.");
}

std::vector<NewsEntry> parseNews(const json& j) {
    std::vector<NewsEntry> out;
    if (!j.is_object() || !j.contains("all") || !j["all"].is_array()) return out;
    for (auto& it : j["all"]) {
        NewsEntry n;
        n.id = ujson::str(it, "id");
        n.title = ujson::str(it, "title");
        n.body = ujson::str(it, "body");
        n.buttonLabel = ujson::str(it, "buttonLabel");
        n.buttonAction = ujson::str(it, "buttonAction");
        n.buttonParam = ujson::str(it, "buttonParam");
        n.postedAt = (long long)ujson::num(it, "postedAt");
        n.until = (long long)ujson::num(it, "until");
        n.hidden = ujson::flag(it, "hidden");
        if (!n.id.empty()) out.push_back(n);
    }
    return out;
}

Report parseReport(const json& it) {
    Report r;
    r.id = ujson::str(it, "id");
    r.at = (long long)ujson::num(it, "at");
    r.reporterNick = ujson::str(it, "reporterNick");
    r.accusedNick = ujson::str(it, "accusedNick");
    r.accusedId = ujson::str(it, "accusedId");
    r.reason = ujson::str(it, "reason");
    r.note = ujson::str(it, "note");
    r.message = ujson::str(it, "message");
    r.server = ujson::str(it, "server");
    r.status = ujson::str(it, "status", "open");
    r.outcome = ujson::str(it, "outcome");
    r.decidedBy = ujson::str(it, "decidedBy");
    r.decidedAt = (long long)ujson::num(it, "decidedAt");
    r.until = (long long)ujson::num(it, "until");
    if (it.contains("context") && it["context"].is_array())
        for (auto& c : it["context"]) if (c.is_string() && r.context.size() < 20) r.context.push_back(c.get<std::string>());
    return r;
}

Profile parseProfile(const json& j) {
    Profile p;
    const json& it = j.contains("profile") ? j["profile"] : j;
    if (!it.is_object()) return p;
    p.id = ujson::str(it, "id");
    p.nickname = ujson::str(it, "nickname");
    p.created = (long long)ujson::num(it, "created");
    p.linkedCount = (int)ujson::num(it, "linkedCount");
    p.banned = ujson::flag(it, "banned");
    p.banReason = ujson::str(it, "banReason");
    p.bannedUntil = (long long)ujson::num(it, "bannedUntil");
    if (it.contains("badges") && it["badges"].is_array())
        for (auto& b : it["badges"]) if (b.is_string()) p.badges.push_back(b.get<std::string>());
    if (it.contains("against") && it["against"].is_array()) p.against = (int)it["against"].size();
    if (it.contains("filed") && it["filed"].is_array()) p.filed = (int)it["filed"].size();
    p.sanctions = parseSanctions(it.contains("sanctions") ? it["sanctions"] : json());
    p.valid = !p.id.empty();
    return p;
}

/** Run one request off the frame, counting it so the page can show it is busy. */
void work(const char* what, std::function<void()> fn) {
    ++g_inflight;
    setStatus(what);
    ujobs::run("admin", [fn](Job&) {
        fn();
        --g_inflight;
        return true;
    });
}

std::string token() { return Account::get().token(); }

}  // namespace

bool isAdmin() {
    if (Account::get().state() != Account::State::SignedIn) return false;
    const auto b = Account::get().badges();
    return std::find(b.begin(), b.end(), "developer") != b.end();
}

void refresh() {
    if (!isAdmin()) return;
    work(N_("Fetching..."), [] {
        const std::string b = base(), tok = token();
        uhttp::Response o = uhttp::get(b + "/moderation/overview", tok);
        uhttp::Response n = uhttp::get(b + "/moderation/announcements", tok);
        uhttp::Response r = uhttp::get(b + "/moderation/reports", tok);
        std::lock_guard<std::mutex> l(g_mutex);
        if (o.ok()) {
            const json j = ujson::parse(o.body);
            g_accounts = (long long)ujson::num(j, "accounts", -1);
            g_exact = ujson::flag(j, "exact", true);
        }
        if (n.ok()) g_news = parseNews(ujson::parse(n.body));
        if (r.ok()) {
            g_reports.clear();
            const json j = ujson::parse(r.body);
            if (j.is_object() && j.contains("reports") && j["reports"].is_array())
                for (auto& it : j["reports"]) g_reports.push_back(parseReport(it));
        }
        g_status = n.ok() && r.ok() ? "" : failure(!n.ok() ? n : r);
    });
}

long long accounts() { std::lock_guard<std::mutex> l(g_mutex); return g_accounts; }
bool accountsExact() { std::lock_guard<std::mutex> l(g_mutex); return g_exact; }
std::vector<NewsEntry> news() { std::lock_guard<std::mutex> l(g_mutex); return g_news; }
std::vector<Report> reports() { std::lock_guard<std::mutex> l(g_mutex); return g_reports; }
Profile profile() { std::lock_guard<std::mutex> l(g_mutex); return g_profile; }
std::string status() { std::lock_guard<std::mutex> l(g_mutex); return g_status; }
bool busy() { return g_inflight.load() > 0; }

static void postNews(const json& body) {
    work(N_("Saving..."), [body] {
        uhttp::Response r = uhttp::postJson(base() + "/moderation/announcement", body.dump(), token());
        std::lock_guard<std::mutex> l(g_mutex);
        // The reply is the whole board, so a save refreshes the list.
        if (r.ok()) { g_news = parseNews(ujson::parse(r.body)); g_status = N_("Saved."); }
        else g_status = failure(r);
    });
}

void saveNews(const NewsEntry& e) {
    json item = {{"id", e.id}, {"title", e.title}, {"body", e.body}};
    if (!e.buttonAction.empty()) {
        item["buttonAction"] = e.buttonAction;
        item["buttonLabel"] = e.buttonLabel;
        if (!e.buttonParam.empty()) item["buttonParam"] = e.buttonParam;
    }
    if (e.until > 0) item["until"] = e.until;
    postNews({{"op", "put"}, {"item", item}});
}

void newsOp(const std::string& op, const std::string& id) { postNews({{"op", op}, {"id", id}}); }

void decide(const std::string& id, const std::string& action, double days, const std::string& reason) {
    json body = {{"id", id}, {"action", action}, {"reason", reason}};
    if (action == "timeout") body["days"] = days;
    work(N_("Working..."), [body] {
        uhttp::Response r = uhttp::postJson(base() + "/moderation/decide", body.dump(), token());
        if (!r.ok()) { setStatus(failure(r)); return; }
        uhttp::Response q = uhttp::get(base() + "/moderation/reports", token());
        std::lock_guard<std::mutex> l(g_mutex);
        g_status = N_("Done.");
        if (q.ok()) {
            g_reports.clear();
            const json j = ujson::parse(q.body);
            if (j.is_object() && j.contains("reports") && j["reports"].is_array())
                for (auto& it : j["reports"]) g_reports.push_back(parseReport(it));
        }
    });
}

void lookup(const std::string& query) {
    if (query.empty()) return;
    work(N_("Looking..."), [query] {
        uhttp::Response r = uhttp::get(base() + "/moderation/account?q=" + uhttp::urlEncode(query), token());
        std::lock_guard<std::mutex> l(g_mutex);
        if (r.ok()) { g_profile = parseProfile(ujson::parse(r.body)); g_status.clear(); }
        else { g_profile = Profile{}; g_status = r.status == 404 && ujson::str(ujson::parse(r.body), "code") == "no_account"
                                                     ? N_("Nobody by that name or id.") : failure(r); }
    });
}

void act(const std::string& action, double days, const std::string& reason) {
    std::string id;
    { std::lock_guard<std::mutex> l(g_mutex); id = g_profile.id; }
    if (id.empty()) return;
    // By id, not the nickname typed: a nickname can change between the lookup
    // and the button, and acting on a stale one acts on whoever holds it now.
    json body = {{"q", id}, {"action", action}, {"reason", reason.empty() ? N_("Conduct towards other players.") : reason}};
    if (action == "timeout") body["days"] = days;
    work(N_("Working..."), [body] {
        uhttp::Response r = uhttp::postJson(base() + "/moderation/account", body.dump(), token());
        std::lock_guard<std::mutex> l(g_mutex);
        if (r.ok()) { g_profile = parseProfile(ujson::parse(r.body)); g_status = N_("Done."); }
        else g_status = failure(r);
    });
}

double parseDays(const std::string& text) {
    double total = 0;
    bool any = false;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && !std::isdigit((unsigned char)text[i])) ++i;
        if (i >= text.size()) break;
        double n = 0;
        while (i < text.size() && std::isdigit((unsigned char)text[i])) n = n * 10 + (text[i++] - '0');
        while (i < text.size() && text[i] == ' ') ++i;
        char unit = 'd';
        if (i < text.size() && std::isalpha((unsigned char)text[i])) unit = (char)std::tolower((unsigned char)text[i++]);
        while (i < text.size() && std::isalpha((unsigned char)text[i])) ++i;   // "weeks", "hours"
        switch (unit) {
            case 'w': total += n * 7; break;
            case 'h': total += n / 24; break;
            case 'm': total += n / 1440; break;
            default: total += n; break;
        }
        any = true;
    }
    return any ? total : 0;
}

std::string describeUntil(long long until, long long now) {
    if (until <= 0) return T("permanent");
    const long long left = until - now;
    if (left <= 0) return T("ended");
    if (left < 3600) return TextFormat(T("%d minutes"), (int)std::max(1LL, left / 60));
    if (left < 2 * 86400) return TextFormat(T("%d hours"), (int)(left / 3600));
    return TextFormat(T("%d days"), (int)(left / 86400));
}

}  // namespace uadmin
