#pragma once
// The launcher updating itself, in the order that cannot leave a person with
// no launcher at all:
//
//   1. CHECK THE DISK. Three times the download free, on the volume the
//      launcher lives on (staging is beside it, so the final step is a rename,
//      never a copy across volumes).
//   2. DOWNLOAD the new build, and check it against the release's SHA-256.
//   3. UNPACK it beside the running one, as <install>.new.
//   4. AUTHORISE IT FROM THE OLD ONE. On macOS a downloaded app carries
//      com.apple.quarantine, and Gatekeeper refuses an unsigned one outright
//      ("damaged"). The old instance -- already trusted, already running --
//      strips the attribute from the new bundle with removexattr(2) before
//      anything executes it. Elsewhere it sets the executable bit.
//   5. HAND OVER. The old instance starts the new one with
//      --finish-update <old path> <old pid> and exits.
//   6. The NEW one waits for the old pid to be gone, moves over any data that
//      lived beside the old install (a portable install's UnificoData/),
//      deletes the old instance, renames ITSELF to the old path, and restarts
//      from there -- so shortcuts, the Dock and the Start menu still point at
//      the right file.
//
// If anything fails before step 6, the old launcher is untouched and keeps
// running. If step 6 fails after the delete, the new one is still sitting at
// <install>.new and runs from there; the next start repairs the name.
#include "core/Jobs.h"
#include <string>

namespace uupdate {
struct Available {
    std::string version;
    std::string url;
    std::string sha256;
    uint64_t size = 0;
    std::string notes;
};
/** Ask the release list; blocking. False when up to date or unreachable. */
bool check(Available& out, std::string* error);
/** Steps 1-5. On success the caller must quit immediately. */
JobPtr apply(const Available& a);
/** Step 6, called from main() when started with --finish-update. Returns the exit code. */
int finish(const std::string& oldPath, long long oldPid);
/** Startup tidy-up: a leftover .new or .old beside us from an interrupted update. */
void tidy();
/** Release asset name for this platform. */
std::string assetName();
/** Exposed for tests: the staging path for an install path. */
std::string stagingFor(const std::string& installPath);
}
