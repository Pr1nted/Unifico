#pragma once
// The admin screens the game has (Game_DevReports.cpp, Game_AdminAnnounce.cpp),
// for an account carrying the `developer` badge: the news board, the report
// queue, and looking somebody up to time them out, ban or pardon them.
//
// Nothing here is a secret. Every request carries the signed-in session token,
// and the SERVICE checks the badge on each one; an account without it gets a
// 404 from every route. The launcher hiding the page is a convenience, not the
// security -- the same arrangement as the game's.
#include "od/Account.h"

#include <string>
#include <vector>

namespace uadmin {

struct NewsEntry {
    std::string id, title, body, buttonLabel, buttonAction, buttonParam;
    long long postedAt = 0, until = 0;
    bool hidden = false;
};

struct Report {
    std::string id, reporterNick, accusedNick, accusedId, reason, note, message, server, status, outcome, decidedBy;
    std::vector<std::string> context;
    long long at = 0, decidedAt = 0, until = 0;
};

struct Profile {
    bool valid = false;
    std::string id, nickname, banReason;
    std::vector<std::string> badges;
    long long created = 0, bannedUntil = 0;
    int linkedCount = 0, against = 0, filed = 0;
    bool banned = false;
    std::vector<Sanction> sanctions;
};

/** The signed-in account holds the developer badge. */
bool isAdmin();

/** Fetch the overview, the board and the queue. */
void refresh();
long long accounts();          // -1 until known
bool accountsExact();
std::vector<NewsEntry> news();
std::vector<Report> reports();
Profile profile();
/** A sentence about the last request: "Saving...", or what the service said. */
std::string status();
bool busy();

/** Post or replace an entry (the id is the key). */
void saveNews(const NewsEntry& e);
/** "hide", "show" or "purge" an entry. */
void newsOp(const std::string& op, const std::string& id);
/** Close a report: "ban", "timeout" (with days) or "dismiss". */
void decide(const std::string& id, const std::string& action, double days, const std::string& reason);
/** Look somebody up by nickname or account id. */
void lookup(const std::string& query);
/** Act on the looked-up account: "ban", "timeout" (days) or "dismiss" (pardon). */
void act(const std::string& action, double days, const std::string& reason);

/** "3d", "6h 30m", "2w", "90m", "45" -> days. A bare number is days. 0 if unreadable. */
double parseDays(const std::string& text);
/** "4 hours", "3 days", "permanent" -- from now to `until`. */
std::string describeUntil(long long until, long long now);

}  // namespace uadmin
