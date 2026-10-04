#pragma once
// Mods of one installation: the .odmod files in data/mods/, whether each is
// switched on (data/mods.json, the game's own file), and the community
// directory on the account service.
#include "core/Jobs.h"
#include <cstdint>
#include <string>
#include <vector>

struct InstalledMod {
    std::string file;      // "example.odmod"
    std::string id;        // from mods.json when the game has seen it; else the stem
    bool enabled = false;
    bool known = false;    // the game has recorded it in mods.json
    uint64_t size = 0;
};

struct DirectoryMod {
    std::string id, name, version, summary, by, get, sha256, sideLabel;
    uint64_t size = 0;
};

namespace umods {
std::vector<InstalledMod> installed(const std::string& dataDir);
bool setEnabled(const std::string& dataDir, const std::string& id, bool on);
bool remove(const std::string& dataDir, const InstalledMod& m, std::string* error);
bool installFile(const std::string& dataDir, const std::string& odmod, std::string* error);
/** First page of the directory. Blocking. */
std::vector<DirectoryMod> browse(const std::string& issuer, std::string* error);
/** Download one into an installation, checked against the author's declared hash. */
JobPtr installFromDirectory(const std::string& issuer, const DirectoryMod& m, const std::string& dataDir);
}  // namespace umods
