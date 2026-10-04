#pragma once
// Game versions on this machine: the ones the launcher installed side by side
// under <home>/versions/<tag>/, and existing installs it was pointed at
// ("adopted") so that switching to the launcher costs nobody their worlds.
#include "core/Jobs.h"
#include "od/Releases.h"
#include <cstdint>
#include <string>
#include <vector>

struct Install {
    std::string tag;          // "v1.2.2a", or "external:<n>" for an adopted one
    std::string version;      // "1.2.2a"
    std::string root;         // the folder the release zip unpacks to
    std::string exe;          // what to run
    std::string dataDir;      // where the game will find data/ (ends without '/')
    bool external = false;
    long long installedAt = 0;
    std::string label() const;
};

struct DiskUsage {
    uint64_t total = 0, saves = 0, mods = 0, maps = 0, game = 0;
};

namespace uinstalls {
std::vector<Install> list();
bool find(const std::string& tag, Install& out);
/** The installed version to play when none is chosen: the newest. */
bool newest(Install& out);

/**
 * Download, verify and unpack a release. Refuses up front when the disk has
 * less than three times the download free (zip + unpacked + headroom).
 */
JobPtr install(const Release& r);
bool uninstall(const Install& i, bool keepWorlds, std::string* error);
DiskUsage usage(const Install& i);

/** Places a copy of the game is often installed outside the launcher. */
std::vector<std::string> detectExisting();
/** Register an existing install (a folder holding the binary and data/). */
bool adopt(const std::string& path, std::string* error);
void forget(const Install& i);

/** Find the binary and data/ under an install root. */
bool locate(const std::string& root, std::string& exe, std::string& dataDir);
/**
 * macOS: remove com.apple.quarantine from everything under `path`, so a build
 * the launcher downloaded opens without Gatekeeper's "damaged" dialog. Done
 * with removexattr(2) directly -- no `xattr` subprocess. A no-op elsewhere.
 */
void clearQuarantine(const std::string& path);
/** Where worlds go when a version is removed with "keep worlds". */
std::string worldsArchiveDir();
}  // namespace uinstalls
