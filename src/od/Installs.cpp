#include "od/Installs.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Sha256.h"
#include "core/Zip.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>

#if defined(__APPLE__)
#  include <sys/xattr.h>
#endif

namespace fs = std::filesystem;

std::string Install::label() const {
    return external ? version + " (" + root + ")" : version;
}

namespace uinstalls {
namespace {
std::string externalPath() { return upaths::home() + "/external.json"; }

std::string readVersionFile(const std::string& dataDir) {
    std::string v;
    if (ufs::readFile(dataDir + "/VERSION", v)) {
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
    }
    return v.empty() ? "unknown" : v;
}
}  // namespace

std::string worldsArchiveDir() {
    const std::string d = upaths::home() + "/kept-worlds";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

bool locate(const std::string& root, std::string& exe, std::string& dataDir) {
    std::error_code ec;
    const fs::path r(root);
    std::vector<fs::path> exes = {
        r / "OpenDoctrines.app" / "Contents" / "MacOS" / "OpenDoctrines",
        r / "OpenDoctrines.exe",
        r / "OpenDoctrines",
        r / "Contents" / "MacOS" / "OpenDoctrines",          // pointed at the .app itself
    };
    for (auto& e : exes) {
        if (fs::is_regular_file(e, ec)) { exe = e.string(); break; }
    }
    if (exe.empty()) return false;
    // The game's own rule (Game::init): data/ beside the binary, else one level
    // up -- which is Contents/data inside a bundle -- else the zip root.
    const fs::path exeDir = fs::path(exe).parent_path();
    for (const fs::path& d : {exeDir / "data", exeDir.parent_path() / "data", r / "data"}) {
        if (fs::is_directory(d / "fonts", ec)) { dataDir = d.string(); return true; }
    }
    dataDir = (r / "data").string();
    return true;
}

std::vector<Install> list() {
    std::vector<Install> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(upaths::versionsDir(), ec)) {
        if (!e.is_directory(ec)) continue;
        const std::string name = e.path().filename().string();
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) continue;
        json m = ujson::load((e.path() / ".unifico-install.json").string());
        Install i;
        i.tag = name;
        i.root = e.path().string();
        if (!locate(i.root, i.exe, i.dataDir)) continue;
        i.version = ujson::str(m, "version", name[0] == 'v' ? name.substr(1) : name);
        i.installedAt = (long long)ujson::num(m, "installedAt");
        out.push_back(i);
    }
    json ext = ujson::load(externalPath());
    if (ext.contains("installs") && ext["installs"].is_array()) {
        int n = 0;
        for (auto& p : ext["installs"]) {
            if (!p.is_string()) continue;
            Install i;
            i.root = p.get<std::string>();
            i.external = true;
            i.tag = "external:" + std::to_string(n++);
            if (!locate(i.root, i.exe, i.dataDir)) continue;
            i.version = readVersionFile(i.dataDir);
            out.push_back(i);
        }
    }
    std::sort(out.begin(), out.end(), [](const Install& a, const Install& b) {
        if (a.external != b.external) return !a.external;
        return ureleases::compareVersions(a.version, b.version) > 0;
    });
    return out;
}

bool find(const std::string& tag, Install& out) {
    for (auto& i : list()) if (i.tag == tag) { out = i; return true; }
    return false;
}

bool newest(Install& out) {
    auto l = list();
    if (l.empty()) return false;
    out = l.front();
    return true;
}

void clearQuarantine(const std::string& path) {
#if defined(__APPLE__)
    std::error_code ec;
    removexattr(path.c_str(), "com.apple.quarantine", XATTR_NOFOLLOW);
    if (fs::is_directory(path, ec))
        for (auto it = fs::recursive_directory_iterator(path, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
            removexattr(it->path().string().c_str(), "com.apple.quarantine", XATTR_NOFOLLOW);
#else
    (void)path;
#endif
}

JobPtr install(const Release& rel) {
    return ujobs::run("Installing " + rel.version, [rel](Job& job) {
        const std::string assetName = ureleases::gameAssetName();
        const ReleaseAsset* a = rel.assetFor(assetName);
        if (!a) { job.fail("This release has no build for this computer (" + assetName + ")."); return false; }
        const std::string dest = upaths::versionsDir() + "/" + rel.tag;
        const uint64_t need = a->size * 3 + 64ull * 1024 * 1024;
        const uint64_t free = ufs::freeSpace(upaths::versionsDir());
        if (free && free < need) {
            job.fail("Not enough disk space: " + ufs::humanBytes(need) + " needed, " + ufs::humanBytes(free) + " free.");
            return false;
        }
        const std::string zip = upaths::cacheDir() + "/" + rel.tag + "-" + assetName;
        job.setStatus("Downloading " + ufs::humanBytes(a->size) + "...");
        std::string err;
        if (!uhttp::download(a->url, zip, [&](uint64_t d, uint64_t t) {
                if (!t) t = a->size;
                job.progress = t ? 0.85f * (float)d / (float)t : -1.0f;
            }, &job.cancel, &err)) {
            job.fail("Download failed: " + err);
            return false;
        }
        if (!a->sha256.empty()) {
            job.setStatus("Checking the download...");
            const std::string got = usha::hexOfFile(zip);
            if (got != a->sha256) {
                ufs::removeAll(zip);
                job.fail("The download did not match the release's checksum. Nothing was installed.");
                return false;
            }
        }
        job.setStatus("Unpacking...");
        const std::string tmp = dest + ".tmp";
        ufs::removeAll(tmp);
        if (!uzip::extract(zip, tmp, true, [&](int i, int n) { job.progress = 0.85f + 0.14f * (float)i / (float)std::max(1, n); }, &err)) {
            ufs::removeAll(tmp);
            job.fail("Unpacking failed: " + err);
            return false;
        }
        std::string exe, data;
        if (!locate(tmp, exe, data)) {
            ufs::removeAll(tmp);
            job.fail("The archive did not contain the game.");
            return false;
        }
        clearQuarantine(tmp);
        // The game's own updater stays out of a launcher install: it would
        // replace files under a version the launcher is keeping as it is.
        ufs::writeFileAtomic(tmp + "/MANAGED", "Unifico\n");
        json m = {{"tag", rel.tag}, {"version", rel.version}, {"installedAt", (long long)std::time(nullptr)},
                  {"platform", upaths::platformTag()}, {"asset", assetName}, {"sha256", a->sha256}};
        ujson::save(tmp + "/.unifico-install.json", m);
        ufs::removeAll(dest);
        std::error_code ec;
        fs::rename(tmp, dest, ec);
        if (ec) { job.fail("Could not finish installing: " + ec.message()); return false; }
        ufs::removeAll(zip);
        job.setStatus("Installed " + rel.version);
        ulog::info("installed " + rel.tag);
        return true;
    });
}

bool uninstall(const Install& i, bool keepWorlds, std::string* error) {
    if (keepWorlds && ufs::isDir(i.dataDir + "/saves")) {
        char stamp[32];
        std::time_t t = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&t));
        const std::string dest = worldsArchiveDir() + "/" + ufs::safeName(i.version) + "-" + stamp;
        if (!ufs::copyTree(i.dataDir + "/saves", dest, error)) return false;
        ulog::info("kept worlds of " + i.version + " in " + dest);
    }
    if (i.external) { forget(i); return true; }   // never delete what we did not install
    return ufs::removeAll(i.root, error);
}

DiskUsage usage(const Install& i) {
    DiskUsage u;
    u.total = ufs::dirSize(i.root);
    u.saves = ufs::dirSize(i.dataDir + "/saves");
    u.mods = ufs::dirSize(i.dataDir + "/mods");
    u.maps = ufs::dirSize(i.dataDir + "/custom_maps") + ufs::dirSize(i.dataDir + "/projects");
    u.game = u.total > u.saves + u.mods + u.maps ? u.total - u.saves - u.mods - u.maps : 0;
    return u;
}

std::vector<std::string> detectExisting() {
    std::vector<std::string> cands;
    const std::string h = upaths::userHome();
#if defined(__APPLE__)
    cands = {"/Applications/OpenDoctrines.app", h + "/Applications/OpenDoctrines.app",
             h + "/Library/Application Support/Steam/steamapps/common/Open Doctrines",
             h + "/Library/Application Support/itch/apps/open-doctrines", h + "/Downloads/OpenDoctrines-macos-arm64",
             h + "/Downloads/OpenDoctrines-macos-x64"};
#elif defined(_WIN32)
    const char* pf = std::getenv("ProgramFiles");
    const char* la = std::getenv("LOCALAPPDATA");
    const char* ad = std::getenv("APPDATA");
    if (pf) cands.push_back(std::string(pf) + "\\OpenDoctrines");
    if (la) cands.push_back(std::string(la) + "\\Programs\\OpenDoctrines");
    if (ad) cands.push_back(std::string(ad) + "\\itch\\apps\\open-doctrines");
    cands.push_back("C:\\Program Files (x86)\\Steam\\steamapps\\common\\Open Doctrines");
    cands.push_back(h + "\\Downloads\\OpenDoctrines-windows-x64");
#else
    cands = {h + "/.local/share/Steam/steamapps/common/Open Doctrines", h + "/.config/itch/apps/open-doctrines",
             h + "/Downloads/OpenDoctrines-linux-x64", h + "/Games/OpenDoctrines", "/opt/OpenDoctrines"};
#endif
    std::vector<std::string> found;
    std::vector<Install> known = list();
    for (auto& c : cands) {
        std::string root = c;
        // A bundle is adopted by its parent folder, which is what a zip unpacks to.
        if (root.size() > 4 && root.compare(root.size() - 4, 4, ".app") == 0) root = fs::path(root).parent_path().string();
        std::string exe, data;
        if (!locate(root, exe, data) && !locate(c, exe, data)) continue;
        bool dup = false;
        for (auto& k : known) if (fs::equivalent(k.exe, exe)) dup = true;
        if (!dup) found.push_back(c);
    }
    return found;
}

bool adopt(const std::string& path, std::string* error) {
    std::string exe, data;
    std::string root = path;
    if (!locate(root, exe, data)) {
        if (error) *error = "No Open Doctrines found in that folder.";
        return false;
    }
    json ext = ujson::load(externalPath());
    if (!ext.contains("installs") || !ext["installs"].is_array()) ext["installs"] = json::array();
    for (auto& p : ext["installs"]) if (p.is_string() && p.get<std::string>() == root) return true;
    ext["installs"].push_back(root);
    return ujson::save(externalPath(), ext);
}

void forget(const Install& i) {
    json ext = ujson::load(externalPath());
    if (!ext.contains("installs") || !ext["installs"].is_array()) return;
    json keep = json::array();
    for (auto& p : ext["installs"]) if (!p.is_string() || p.get<std::string>() != i.root) keep.push_back(p);
    ext["installs"] = keep;
    ujson::save(externalPath(), ext);
}
}  // namespace uinstalls
