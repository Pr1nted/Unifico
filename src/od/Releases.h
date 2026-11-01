#pragma once
// What has been released, from GitHub's release list for the game.
//
// Cached in <cache>/releases.json so the Installations page opens instantly and
// works offline; refreshed in the background at startup.
#include <cstdint>
#include <string>
#include <vector>

struct ReleaseAsset {
    std::string name;
    std::string url;
    uint64_t size = 0;
    std::string sha256;   // from GitHub's "digest", when it gives one
};

struct Release {
    std::string tag;          // "v1.2.2a"
    std::string name;
    std::string version;      // "1.2.2a"
    std::string publishedAt;  // ISO date
    std::string notes;        // markdown body
    bool prerelease = false;
    std::vector<ReleaseAsset> assets;
    const ReleaseAsset* assetFor(const std::string& assetName) const;
};

namespace ureleases {
/** The zip this machine runs, by the game's own naming (GameUpdates::assetName). */
std::string gameAssetName();
/** The same, for a platform tag ("netbsd-arm64"). Exposed for the tests. */
std::string gameAssetName(const std::string& platformTag);
/** What a release asset is to the launcher, by its name alone.
 *  Launchable: a game zip some Unifico build installs (OpenDoctrines-<key>.zip
 *              for a key the launcher ships on -- docs/platforms.md).
 *  Manual:     a game build for a system the launcher does not run on, or in
 *              a form it does not unpack (.deb, .apk, .vpk; DOS, Wii, FreeRTOS,
 *              ...). Shown as "download it yourself", never installed.
 *  Other:      anything else (the server, checksums, the web build).
 *  Exposed for the tests: a new asset name in a game release must never make
 *  the list fail or be mistaken for this machine's build. */
enum class AssetKind { Launchable, Manual, Other };
AssetKind assetKind(const std::string& assetName);
/** Readable names of a release's Manual builds ("DOS", "Wii", ...), unique. */
std::vector<std::string> manualBuilds(const Release& r);
/** The release's page on GitHub, where the manual builds are downloaded. */
std::string releasePage(const Release& r);
/** Parse GitHub's /releases JSON. Exposed for the tests. */
std::vector<Release> parse(const std::string& json, const std::string& tagPrefix);
/** Cached list, newest first (game releases only: tags "v..."). */
std::vector<Release> cached();
/** Fetch from GitHub and refresh the cache. Blocking. */
bool refresh(std::string* error);
/** TempleOS builds: tags "templeos-v...". */
std::vector<Release> templeos();
/** Compare two version strings like "1.2.2a" (alpha < beta < release). */
int compareVersions(const std::string& a, const std::string& b);
}  // namespace ureleases
