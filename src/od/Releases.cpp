#include "od/Releases.h"
#include "BuildInfo.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Paths.h"

#include <algorithm>
#include <cctype>
#include <mutex>

const ReleaseAsset* Release::assetFor(const std::string& n) const {
    for (auto& a : assets) if (a.name == n) return &a;
    return nullptr;
}

namespace ureleases {
namespace {
std::mutex g_mutex;
std::string cachePath() { return upaths::cacheDir() + "/releases.json"; }
}  // namespace

std::string gameAssetName(const std::string& tag) {
    // The same names the game's updater asks for (src/GameUpdates.cpp), so a
    // build the launcher installs is the build the game would have fetched.
    // The game's BSD and illumos builds spell x86-64 the way those systems do,
    // "amd64"; every other name, Arm on the BSDs included, is the launcher's
    // own tag.
    const std::string os = tag.substr(0, tag.find('-'));
    const bool bsd = os == "freebsd" || os == "openbsd" || os == "netbsd" || os == "illumos";
    if (bsd && tag == os + "-x64") return "OpenDoctrines-" + os + "-amd64.zip";
    return "OpenDoctrines-" + tag + ".zip";
}

std::string gameAssetName() { return gameAssetName(upaths::platformTag()); }

namespace {
// The game's updater keys (GameUpdates::platformKey in Open Doctrines) that a
// Unifico build exists for -- keep in step with docs/platforms.md and the
// build matrix in .github/workflows/build.yml.
const char* const kLaunchable[] = {
    "windows-x64", "windows-x86", "windows-arm64", "macos-x64", "macos-arm64",
    "linux-x64", "linux-arm64", "linux-x86", "linux-armv7", "linux-riscv64",
    "raspios-arm64", "raspios-armhf",
    "freebsd-amd64", "freebsd-arm64", "openbsd-amd64", "openbsd-arm64", "netbsd-amd64", "netbsd-arm64",
    "illumos-amd64", "haiku-x64", "serenity-x64"};

// Game builds the launcher cannot install, by the start of their key, with
// the name a person knows them by. Longest prefixes are not needed: no key
// below is a prefix of another.
struct Manual { const char* prefix; const char* label; };
const Manual kManual[] = {
    {"chromeos-", "ChromeOS"}, {"raspios-", "Raspberry Pi OS"}, {"vita", "PS Vita"},
    {"dos-", "DOS"}, {"wii-", "Wii"}, {"3ds", "Nintendo 3DS"}, {"freertos", "FreeRTOS"},
    {"riscos", "RISC OS"}, {"plan9", "Plan 9"}, {"templeos", "TempleOS"},
    {"android", "Android"}, {"web", "Web"}};

bool endsWith(const std::string& s, const std::string& e) {
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}
std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
}  // namespace

AssetKind assetKind(const std::string& nameIn) {
    const std::string name = lower(nameIn);
    if (name == "opendoctrines.apk") return AssetKind::Manual;
    if (name.rfind("open-doctrines-templeos", 0) == 0) return AssetKind::Manual;
    const std::string pre = "opendoctrines-";
    if (name.rfind(pre, 0) != 0) return AssetKind::Other;   // the server, sums, ...
    std::string key = name.substr(pre.size());
    std::string ext;
    for (const char* e : {".zip", ".deb", ".vpk", ".apk", ".3dsx", ".cia", ".exe"})
        if (endsWith(key, e)) { ext = e; key = key.substr(0, key.size() - ext.size()); break; }
    if (ext.empty()) return AssetKind::Other;
    if (ext == ".zip")
        for (const char* k : kLaunchable) if (key == k) return AssetKind::Launchable;
    for (const auto& m : kManual) if (key.rfind(m.prefix, 0) == 0) return AssetKind::Manual;
    // Packages of a desktop build (.deb of linux-x64, ...) are for a package
    // manager, not for the launcher to unpack.
    if (ext != ".zip") return AssetKind::Manual;
    return AssetKind::Other;
}

std::vector<std::string> manualBuilds(const Release& r) {
    std::vector<std::string> out;
    for (auto& a : r.assets) {
        if (assetKind(a.name) != AssetKind::Manual) continue;
        std::string n = lower(a.name);
        std::string label;
        if (n == "opendoctrines.apk") label = "Android";
        else if (n.rfind("open-doctrines-templeos", 0) == 0) label = "TempleOS";
        else {
            const std::string key = n.substr(std::string("opendoctrines-").size());
            for (const auto& m : kManual) if (key.rfind(m.prefix, 0) == 0) { label = m.label; break; }
            if (label.empty()) label = "Linux packages";
        }
        if (std::find(out.begin(), out.end(), label) == out.end()) out.push_back(label);
    }
    return out;
}

std::string releasePage(const Release& r) {
    return std::string("https://github.com/") + UNIFICO_GITHUB_REPO + "/releases/tag/" + r.tag;
}

std::vector<Release> parse(const std::string& text, const std::string& prefix) {
    std::vector<Release> out;
    json j = ujson::parse(text);
    if (!j.is_array()) return out;
    for (auto& r : j) {
        Release rel;
        rel.tag = ujson::str(r, "tag_name");
        if (rel.tag.rfind(prefix, 0) != 0) continue;
        // "v1.2.2a" is a game release; "vX" must be followed by a digit, so a
        // future "views-..." tag is not mistaken for one.
        if (prefix == "v" && (rel.tag.size() < 2 || !isdigit((unsigned char)rel.tag[1]))) continue;
        if (ujson::flag(r, "draft")) continue;
        rel.name = ujson::str(r, "name", rel.tag);
        rel.version = rel.tag.substr(prefix.size());
        rel.publishedAt = ujson::str(r, "published_at");
        rel.notes = ujson::str(r, "body");
        rel.prerelease = ujson::flag(r, "prerelease");
        if (r.contains("assets") && r["assets"].is_array()) {
            for (auto& a : r["assets"]) {
                ReleaseAsset as;
                as.name = ujson::str(a, "name");
                as.url = ujson::str(a, "browser_download_url");
                as.size = (uint64_t)ujson::num(a, "size");
                std::string d = ujson::str(a, "digest");
                if (d.rfind("sha256:", 0) == 0) as.sha256 = d.substr(7);
                rel.assets.push_back(as);
            }
        }
        out.push_back(rel);
    }
    std::sort(out.begin(), out.end(), [](const Release& a, const Release& b) {
        return compareVersions(a.version, b.version) > 0;
    });
    return out;
}

std::vector<Release> cached() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string text;
    if (!ufs::readFile(cachePath(), text)) return {};
    return parse(text, "v");
}

std::vector<Release> templeos() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::string text;
    if (!ufs::readFile(cachePath(), text)) return {};
    return parse(text, "templeos-v");
}

bool refresh(std::string* error) {
    uhttp::Request r;
    r.url = std::string("https://api.github.com/repos/") + UNIFICO_GITHUB_REPO + "/releases?per_page=100";
    r.headers = {"Accept: application/vnd.github+json", "X-GitHub-Api-Version: 2022-11-28"};
    uhttp::Response res = uhttp::request(r);
    if (!res.ok()) {
        if (error) *error = res.error.empty() ? "GitHub answered " + std::to_string(res.status) : res.error;
        return false;
    }
    if (!ujson::parse(res.body).is_array()) { if (error) *error = "unexpected reply from GitHub"; return false; }
    std::lock_guard<std::mutex> lock(g_mutex);
    return ufs::writeFileAtomic(cachePath(), res.body);
}

int compareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& v, int nums[3], int& stage) {
        nums[0] = nums[1] = nums[2] = 0;
        stage = 3;   // release
        int idx = 0;
        size_t i = 0;
        while (i < v.size() && idx < 3) {
            if (isdigit((unsigned char)v[i])) {
                int n = 0;
                while (i < v.size() && isdigit((unsigned char)v[i])) n = n * 10 + (v[i++] - '0');
                nums[idx++] = n;
            } else if (v[i] == '.') {
                ++i;
            } else {
                break;
            }
        }
        if (i < v.size()) {
            char c = (char)tolower((unsigned char)v[i]);
            stage = c == 'a' ? 0 : c == 'b' ? 1 : c == 's' ? -1 : c == 'r' ? 3 : 2;
        }
    };
    int na[3], nb[3], sa, sb;
    parts(a, na, sa);
    parts(b, nb, sb);
    for (int k = 0; k < 3; ++k) if (na[k] != nb[k]) return na[k] < nb[k] ? -1 : 1;
    if (sa != sb) return sa < sb ? -1 : 1;
    return 0;
}
}  // namespace ureleases
