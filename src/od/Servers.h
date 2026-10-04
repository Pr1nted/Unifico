#pragma once
// The servers an installation knows -- the game's own data/servers.json, which
// holds only names, account services and invite codes, never credentials.
#include <string>
#include <vector>

struct ServerEntry {
    std::string name, issuer, code, lastHostName, address;
    long long lastJoined = 0;
};

namespace uservers {
std::vector<ServerEntry> load(const std::string& dataDir);
/** The join link the game accepts on its command line. */
std::string joinUrl(const ServerEntry& s);
}
