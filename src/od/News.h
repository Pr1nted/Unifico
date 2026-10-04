#pragma once
// The announcement board, from the same service and in the same shape the
// game's main menu reads (Open Doctrines src/net/Announcements.h). Shown as
// text: the launcher draws no markup and follows no button except the two it
// understands -- "join" (start the game with the invite) and "community".
#include <string>
#include <vector>

struct NewsItem {
    std::string id, title, body, buttonLabel, buttonAction, buttonParam;
    long long postedAt = 0;
};

namespace unews {
/** Cached copy, instantly. */
std::vector<NewsItem> cached();
/** Refresh from the service; blocking. */
bool refresh(const std::string& issuer);
/** The dialogue markup stripped to plain text. */
std::string plain(const std::string& marked);
}
