#include "od/Account.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Jobs.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <chrono>
#include <thread>

#if !defined(_WIN32)
#  include <sys/stat.h>
#endif

Account& Account::get() {
    static Account a;
    return a;
}

void Account::init(const std::string& issuer) {
    m_issuer = issuer;
    while (!m_issuer.empty() && m_issuer.back() == '/') m_issuer.pop_back();
    load();
}

std::string Account::nickname() const { std::lock_guard<std::mutex> l(m_mutex); return m_nick; }
std::string Account::accountId() const { std::lock_guard<std::mutex> l(m_mutex); return m_id; }
std::vector<std::string> Account::badges() const { std::lock_guard<std::mutex> l(m_mutex); return m_badges; }
std::string Account::token() const { std::lock_guard<std::mutex> l(m_mutex); return m_token; }
std::string Account::message() const { std::lock_guard<std::mutex> l(m_mutex); return m_msg; }
std::string Account::verifyUrl() const { std::lock_guard<std::mutex> l(m_mutex); return m_verify; }
std::vector<ProviderInfo> Account::providers() const { std::lock_guard<std::mutex> l(m_mutex); return m_providers; }
void Account::setMsg(const std::string& m) { std::lock_guard<std::mutex> l(m_mutex); m_msg = m; }

static void writePrivate(const std::string& path, const std::string& text) {
    // Created empty and narrowed before anything secret is written into it.
    ufs::writeFileAtomic(path, "");
#if !defined(_WIN32)
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
    ufs::writeFileAtomic(path, text);
#if !defined(_WIN32)
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
}

void Account::save() const {
    std::lock_guard<std::mutex> l(m_mutex);
    const std::string path = upaths::home() + "/account.json";
    if (m_token.empty()) { ufs::removeAll(path); return; }
    json j = {{"issuer", m_issuer}, {"token", m_token}, {"nickname", m_nick}, {"id", m_id}};
    writePrivate(path, j.dump());
}

void Account::load() {
    json j = ujson::load(upaths::home() + "/account.json");
    if (ujson::str(j, "issuer") != m_issuer) return;
    std::lock_guard<std::mutex> l(m_mutex);
    m_token = ujson::str(j, "token");
    m_nick = ujson::str(j, "nickname");
    m_id = ujson::str(j, "id");
    if (!m_token.empty()) m_state = State::SignedIn;   // optimistic; bootstrap confirms
}

bool Account::fetchMe(const std::string& token) {
    uhttp::Response r = uhttp::get(m_issuer + "/account/me", token);
    if (r.status == 401) return false;
    json j = ujson::parse(r.body);
    if (!r.ok() || !j.is_object()) return true;   // an outage is not a sign-out
    const json& a = j.contains("account") ? j["account"] : j;
    std::lock_guard<std::mutex> l(m_mutex);
    m_id = ujson::str(a, "id", m_id);
    m_nick = ujson::str(a, "nick", ujson::str(a, "nickname", m_nick));
    m_badges.clear();
    if (a.contains("badges") && a["badges"].is_array())
        for (auto& b : a["badges"]) if (b.is_string()) m_badges.push_back(b.get<std::string>());
    m_linked.clear();
    if (a.contains("linked") && a["linked"].is_array())
        for (auto& b : a["linked"]) if (b.is_string()) m_linked.push_back(b.get<std::string>());
    m_created = (long long)ujson::num(a, "created", 0);
    m_sanctions = parseSanctions(a.contains("sanctions") ? a["sanctions"] : json());
    // A service older than the history still reports what is in force.
    if (m_sanctions.empty() && a.contains("banned") && a["banned"].is_object()) {
        const json& b = a["banned"];
        Sanction s;
        s.until = (long long)ujson::num(b, "until", 0);
        s.kind = s.until ? "timeout" : "ban";
        s.reason = ujson::str(b, "reason");
        s.at = (long long)ujson::num(b, "at", 0);
        m_sanctions.push_back(s);
    }
    return true;
}

std::vector<Sanction> parseSanctions(const json& arr) {
    std::vector<Sanction> out;
    if (!arr.is_array()) return out;
    for (auto& e : arr) {
        if (!e.is_object()) continue;
        Sanction s;
        s.kind = ujson::str(e, "kind");
        if (s.kind != "ban" && s.kind != "timeout" && s.kind != "mods") continue;
        s.reason = ujson::str(e, "reason").substr(0, 300);
        s.at = (long long)ujson::num(e, "at", 0);
        s.until = (long long)ujson::num(e, "until", 0);
        s.liftedAt = (long long)ujson::num(e, "liftedAt", 0);
        out.push_back(s);
        if (out.size() >= 50) break;
    }
    return out;
}

std::vector<Sanction> Account::sanctions() const { std::lock_guard<std::mutex> l(m_mutex); return m_sanctions; }

void Account::refresh() {
    if (m_state != State::SignedIn) return;
    ujobs::run("account", [this](Job&) { fetchMe(token()); return true; });
}

void Account::bootstrap() {
    ujobs::run("account", [this](Job&) {
        uhttp::Response root = uhttp::get(m_issuer + "/");
        m_reachable = root.ok();
        json j = ujson::parse(root.body);
        if (j.is_object() && j.contains("providers") && j["providers"].is_array()) {
            std::vector<ProviderInfo> ps;
            for (auto& p : j["providers"]) ps.push_back({ujson::str(p, "id"), ujson::str(p, "label"), ujson::flag(p, "canCreate", true)});
            std::lock_guard<std::mutex> l(m_mutex);
            m_providers = ps;
        }
        const std::string tok = token();
        if (tok.empty()) return true;
        // Twelve-hour tokens, re-signed on request: refresh at every start so
        // a launcher opened daily never makes anyone sign in again.
        uhttp::Response rr = uhttp::postJson(m_issuer + "/auth/refresh", "{}", tok);
        std::string fresh = ujson::str(ujson::parse(rr.body), "token");
        if (rr.ok() && !fresh.empty()) { std::lock_guard<std::mutex> l(m_mutex); m_token = fresh; }
        if (!fetchMe(token())) {
            std::lock_guard<std::mutex> l(m_mutex);
            m_token.clear();
            m_msg = "Your session expired. Sign in again.";
            m_state = State::SignedOut;
        } else {
            m_state = State::SignedIn;
        }
        save();
        return true;
    });
}

void Account::beginSignIn(const std::string& provider) { flow(provider, false); }
void Account::beginLink(const std::string& provider) { flow(provider, true); }

void Account::flow(const std::string& provider, bool link) {
    if (m_state == State::Starting || m_state == State::WaitingForBrowser) return;
    const State resting = link ? State::SignedIn : State::SignedOut;
    m_cancel = false;
    m_state = State::Starting;
    setMsg("");
    ujobs::run(link ? "link" : "sign in", [this, provider, link, resting](Job&) {
        // Linking is the same browser round trip, made WITH the session token:
        // the result attaches the provider to this account instead of making
        // one. It is the only way a link-only provider (itch.io, Steam) ever
        // becomes a way in.
        json body = {{"provider", provider}, {"purpose", link ? "link" : "login"}};
        uhttp::Response begun = uhttp::postJson(m_issuer + "/auth/device", body.dump(), link ? token() : std::string());
        json b = ujson::parse(begun.body);
        const std::string secret = ujson::str(b, "pollSecret"), verify = ujson::str(b, "verifyUrl");
        if (!begun.ok() || secret.empty() || verify.empty()) {
            setMsg(begun.error.empty() ? ujson::str(b, "message", "Could not start sign-in.") : begun.error);
            m_state = resting;
            return false;
        }
        { std::lock_guard<std::mutex> l(m_mutex); m_verify = verify; }
        m_state = State::WaitingForBrowser;
        uproc::openUrl(verify);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
        const std::string pollBody = json{{"pollSecret", secret}}.dump();
        while (std::chrono::steady_clock::now() < deadline) {
            if (m_cancel) { m_state = resting; return true; }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            uhttp::Response r = uhttp::postJson(m_issuer + "/auth/poll", pollBody);
            if (!r.ok()) continue;
            json p = ujson::parse(r.body);
            const std::string st = ujson::str(p, "status");
            if (st == "pending") continue;
            if (st == "error") { setMsg(ujson::str(p, "message", "Sign-in failed.")); m_state = resting; return false; }
            const std::string kind = ujson::str(p, "kind");
            if (kind == "linked") {
                fetchMe(token());
                setMsg("");
                m_state = State::SignedIn;
                return true;
            }
            if (kind == "session") {
                { std::lock_guard<std::mutex> l(m_mutex); m_token = ujson::str(p, "token"); }
                fetchMe(token());
                save();
                m_state = State::SignedIn;
                return true;
            }
            if (kind == "signup") {
                std::lock_guard<std::mutex> l(m_mutex);
                m_signupTicket = ujson::str(p, "ticket");
                m_nick = ujson::str(p, "suggested");
                m_state = State::NeedsNickname;
                return true;
            }
        }
        setMsg("Sign-in timed out.");
        m_state = resting;
        return false;
    });
}

void Account::cancel() { m_cancel = true; }

void Account::createAccount(const std::string& nickname) {
    std::string ticket;
    { std::lock_guard<std::mutex> l(m_mutex); ticket = m_signupTicket; }
    if (ticket.empty()) return;
    m_state = State::Starting;
    ujobs::run("create account", [this, ticket, nickname](Job&) {
        uhttp::Response r = uhttp::postJson(m_issuer + "/account/create",
                                            json{{"signupTicket", ticket}, {"nickname", nickname}}.dump());
        json j = ujson::parse(r.body);
        if (!r.ok()) {
            setMsg(ujson::str(j, "message", "That nickname could not be used."));
            m_state = State::NeedsNickname;
            return false;
        }
        { std::lock_guard<std::mutex> l(m_mutex); m_token = ujson::str(j, "token"); m_signupTicket.clear(); }
        fetchMe(token());
        save();
        m_state = State::SignedIn;
        return true;
    });
}

void Account::signOut() {
    { std::lock_guard<std::mutex> l(m_mutex); m_token.clear(); m_nick.clear(); m_id.clear(); m_badges.clear(); m_sanctions.clear(); m_linked.clear(); }
    m_state = State::SignedOut;
    save();
}

void Account::handTo(const std::string& dataDir) const {
    const std::string tok = token();
    if (tok.empty()) return;
    // Byte-for-byte the shape AccountClient::Impl::saveToken writes.
    writePrivate(dataDir + "/account.json", json{{"token", tok}, {"issuer", m_issuer}}.dump() + "\n");
}

void Account::takeBackFrom(const std::string& dataDir) {
    json j = ujson::load(dataDir + "/account.json");
    const std::string tok = ujson::str(j, "token");
    if (tok.empty() || ujson::str(j, "issuer") != m_issuer) return;
    if (tok == token()) return;
    // The game refreshed the session, or signed in while we were signed out.
    { std::lock_guard<std::mutex> l(m_mutex); m_token = tok; }
    m_state = State::SignedIn;
    ujobs::run("account", [this](Job&) { fetchMe(token()); save(); return true; });
}

std::vector<std::string> Account::linked() const { std::lock_guard<std::mutex> l(m_mutex); return m_linked; }
long long Account::created() const { std::lock_guard<std::mutex> l(m_mutex); return m_created; }

void Account::unlink(const std::string& provider) {
    if (m_busy) return;
    m_busy = true;
    ujobs::run("unlink", [this, provider](Job&) {
        uhttp::Response r = uhttp::postJson(m_issuer + "/account/unlink", json{{"provider", provider}}.dump(), token());
        if (!r.ok()) setMsg(ujson::str(ujson::parse(r.body), "message", "That sign-in method could not be removed."));
        else setMsg("");
        fetchMe(token());
        m_busy = false;
        return r.ok();
    });
}

void Account::changeNickname(const std::string& nickname) {
    if (m_busy) return;
    m_busy = true;
    ujobs::run("nickname", [this, nickname](Job&) {
        uhttp::Response r = uhttp::postJson(m_issuer + "/account/nickname", json{{"nickname", nickname}}.dump(), token());
        if (!r.ok()) setMsg(ujson::str(ujson::parse(r.body), "message", "That nickname could not be used."));
        else setMsg("");
        fetchMe(token());
        save();
        m_busy = false;
        return r.ok();
    });
}

void Account::beginDelete() {
    if (m_busy) return;
    m_busy = true;
    ujobs::run("delete", [this](Job&) {
        uhttp::Response r = uhttp::postJson(m_issuer + "/account/delete", "{}", token());
        json j = ujson::parse(r.body);
        if (r.ok() && ujson::str(j, "status") == "confirm") {
            DeletePlan p;
            p.confirmation = ujson::str(j, "confirmation");
            for (auto* key : {"willDelete", "willKeep", "cannotReach"}) {
                std::vector<std::string>& dst = std::string(key) == "willDelete" ? p.willDelete
                                              : std::string(key) == "willKeep" ? p.willKeep : p.cannotReach;
                if (j.contains(key) && j[key].is_array())
                    for (auto& x : j[key]) if (x.is_string()) dst.push_back(x.get<std::string>());
            }
            std::lock_guard<std::mutex> l(m_mutex);
            m_deletePlan = p;
            m_haveDeletePlan = true;
        } else {
            setMsg(ujson::str(j, "message", "The account service did not answer."));
        }
        m_busy = false;
        return r.ok();
    });
}

bool Account::deletePlan(DeletePlan& out) const {
    std::lock_guard<std::mutex> l(m_mutex);
    if (!m_haveDeletePlan) return false;
    out = m_deletePlan;
    return true;
}

void Account::cancelDelete() { std::lock_guard<std::mutex> l(m_mutex); m_haveDeletePlan = false; }

void Account::confirmDelete() {
    std::string conf;
    { std::lock_guard<std::mutex> l(m_mutex); conf = m_deletePlan.confirmation; m_haveDeletePlan = false; }
    if (conf.empty() || m_busy) return;
    m_busy = true;
    ujobs::run("delete", [this, conf](Job&) {
        uhttp::Response r = uhttp::postJson(m_issuer + "/account/delete", json{{"confirm", conf}}.dump(), token());
        if (r.ok()) { signOut(); setMsg("Your account was deleted."); }
        else setMsg(ujson::str(ujson::parse(r.body), "message", "The account could not be deleted. Start again."));
        m_busy = false;
        return r.ok();
    });
}

void Account::exportTo(const std::string& path) {
    ujobs::run("export", [this, path](Job&) {
        uhttp::Response r = uhttp::get(m_issuer + "/account/export", token());
        if (!r.ok()) { setMsg("The export could not be fetched."); return false; }
        ufs::writeFileAtomic(path, r.body);
        setMsg("");
        return true;
    });
}
