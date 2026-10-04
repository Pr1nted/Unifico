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
    return true;
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

void Account::beginSignIn(const std::string& provider) {
    if (m_state == State::Starting || m_state == State::WaitingForBrowser) return;
    m_cancel = false;
    m_state = State::Starting;
    setMsg("");
    ujobs::run("sign in", [this, provider](Job&) {
        json body = {{"provider", provider}, {"purpose", "login"}};
        uhttp::Response begun = uhttp::postJson(m_issuer + "/auth/device", body.dump());
        json b = ujson::parse(begun.body);
        const std::string secret = ujson::str(b, "pollSecret"), verify = ujson::str(b, "verifyUrl");
        if (!begun.ok() || secret.empty() || verify.empty()) {
            setMsg(begun.error.empty() ? ujson::str(b, "message", "Could not start sign-in.") : begun.error);
            m_state = State::SignedOut;
            return false;
        }
        { std::lock_guard<std::mutex> l(m_mutex); m_verify = verify; }
        m_state = State::WaitingForBrowser;
        uproc::openUrl(verify);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
        const std::string pollBody = json{{"pollSecret", secret}}.dump();
        while (std::chrono::steady_clock::now() < deadline) {
            if (m_cancel) { m_state = State::SignedOut; return true; }
            std::this_thread::sleep_for(std::chrono::seconds(2));
            uhttp::Response r = uhttp::postJson(m_issuer + "/auth/poll", pollBody);
            if (!r.ok()) continue;
            json p = ujson::parse(r.body);
            const std::string st = ujson::str(p, "status");
            if (st == "pending") continue;
            if (st == "error") { setMsg(ujson::str(p, "message", "Sign-in failed.")); m_state = State::SignedOut; return false; }
            const std::string kind = ujson::str(p, "kind");
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
        m_state = State::SignedOut;
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
    { std::lock_guard<std::mutex> l(m_mutex); m_token.clear(); m_nick.clear(); m_id.clear(); m_badges.clear(); }
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
