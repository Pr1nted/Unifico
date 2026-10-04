#pragma once
// The Open Doctrines account, signed in once in the launcher and handed to
// every version it starts.
//
// Same service, same device flow and the same token the game uses
// (src/net/AccountClient.h in Open Doctrines): the launcher asks for a sign-in,
// the person finishes it in their browser, and the session token lands in
// <home>/account.json, mode 0600. At launch it is written into the
// installation's data/account.json in exactly the shape the game stores it, so
// the game opens already signed in. After the game exits, a token the GAME
// obtained (because the launcher was signed out) is taken back up, so signing
// in in either place signs in both.
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

struct ProviderInfo { std::string id, label; bool canCreate = true; };

class Account {
public:
    static Account& get();
    void init(const std::string& issuer);
    const std::string& issuer() const { return m_issuer; }

    enum class State { SignedOut, Starting, WaitingForBrowser, NeedsNickname, SignedIn };
    State state() const { return m_state.load(); }
    std::string nickname() const;
    std::string accountId() const;
    std::vector<std::string> badges() const;
    std::string token() const;
    std::string message() const;
    std::string verifyUrl() const;
    std::vector<ProviderInfo> providers() const;
    bool reachable() const { return m_reachable.load(); }

    /** Background: probe the service, restore and refresh a stored session. */
    void bootstrap();
    void beginSignIn(const std::string& provider);
    void cancel();
    void createAccount(const std::string& nickname);
    void signOut();

    /** Put the session into an installation's data/ before a launch. */
    void handTo(const std::string& dataDir) const;
    /** After a game exits: adopt a session it made, if we have none. */
    void takeBackFrom(const std::string& dataDir);

private:
    void save() const;
    void load();
    bool fetchMe(const std::string& token);
    void setMsg(const std::string& m);
    mutable std::mutex m_mutex;
    std::string m_issuer, m_token, m_nick, m_id, m_msg, m_verify, m_signupTicket;
    std::vector<std::string> m_badges;
    std::vector<ProviderInfo> m_providers;
    std::atomic<State> m_state{State::SignedOut};
    std::atomic<bool> m_cancel{false}, m_reachable{false};
};
