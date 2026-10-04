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
#include "core/Json.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

struct ProviderInfo { std::string id, label; bool canCreate = true; };

/** A ban, timeout or mod-publishing restriction, as the service records it. */
struct Sanction {
    std::string kind;          // "ban", "timeout" or "mods"
    std::string reason;
    long long at = 0;
    long long until = 0;       // 0: indefinite
    long long liftedAt = 0;    // 0: not lifted early
    /** In force at `now`: neither lifted nor run out. */
    bool active(long long now) const { return liftedAt == 0 && (until == 0 || until > now); }
};
/** Parse the service's "sanctions" array. */
std::vector<Sanction> parseSanctions(const json& arr);

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
    /** Provider ids linked to the signed-in account ("google", "steam", ...). */
    std::vector<std::string> linked() const;
    long long created() const;
    /** Every punishment on the account, newest first, lifted ones included. */
    std::vector<Sanction> sanctions() const;
    /** Re-read the account from the service (after an admin action, say). */
    void refresh();
    /** True while a link/unlink/rename/delete request is in flight. */
    bool busy() const { return m_busy.load(); }
    bool reachable() const { return m_reachable.load(); }

    /** Background: probe the service, restore and refresh a stored session. */
    void bootstrap();
    void beginSignIn(const std::string& provider);
    /** Add another way of signing in to this account (the same browser flow). */
    void beginLink(const std::string& provider);
    void unlink(const std::string& provider);
    void changeNickname(const std::string& nickname);
    /** Step one: ask the service what deletion would do. */
    void beginDelete();
    /** What step one said: lines of willDelete / willKeep / cannotReach. */
    struct DeletePlan { std::vector<std::string> willDelete, willKeep, cannotReach; std::string confirmation; };
    bool deletePlan(DeletePlan& out) const;
    void confirmDelete();
    void cancelDelete();
    /** Everything the service holds about the account, as JSON, to a file. */
    void exportTo(const std::string& path);
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
    std::vector<std::string> m_badges, m_linked;
    std::vector<Sanction> m_sanctions;
    long long m_created = 0;
    DeletePlan m_deletePlan;
    bool m_haveDeletePlan = false;
    std::atomic<bool> m_busy{false};
    void flow(const std::string& provider, bool link);
    std::vector<ProviderInfo> m_providers;
    std::atomic<State> m_state{State::SignedOut};
    std::atomic<bool> m_cancel{false}, m_reachable{false};
};
