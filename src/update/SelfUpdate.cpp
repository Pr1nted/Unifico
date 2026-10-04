#include "update/SelfUpdate.h"
#include "BuildInfo.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"
#include "core/Sha256.h"
#include "core/Zip.h"
#include "od/Releases.h"

#include <chrono>
#include <filesystem>
#include <thread>

#if defined(__APPLE__)
#  include <sys/xattr.h>
#endif
#if !defined(_WIN32)
#  include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace uupdate {
namespace {
// Settings written by an older launcher, brought up to this version's shape.
// Today that is only the marker; a future rename of a key lands here.
void migrate() {
    json s = ujson::load(upaths::home() + "/settings.json");
    s["lastLauncherVersion"] = UNIFICO_VERSION;
    ujson::save(upaths::home() + "/settings.json", s);
}
void stripQuarantine(const std::string& path) {
#if defined(__APPLE__)
    std::error_code ec;
    removexattr(path.c_str(), "com.apple.quarantine", XATTR_NOFOLLOW);
    for (auto it = fs::recursive_directory_iterator(path, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
        removexattr(it->path().string().c_str(), "com.apple.quarantine", XATTR_NOFOLLOW);
#else
    (void)path;
#endif
}

std::string exeInside(const std::string& installPath) {
#if defined(__APPLE__)
    if (fs::path(installPath).extension() == ".app") return installPath + "/Contents/MacOS/Unifico";
#endif
    return installPath;
}
}  // namespace

std::string assetName() {
    const std::string tag = upaths::platformTag();
    return "Unifico-" + tag + ".zip";
}

std::string stagingFor(const std::string& installPath) { return installPath + ".new"; }

bool check(Available& out, std::string* error) {
    uhttp::Request r;
    r.url = std::string("https://api.github.com/repos/") + UNIFICO_SELF_REPO + "/releases/latest";
    r.headers = {"Accept: application/vnd.github+json"};
    uhttp::Response res = uhttp::request(r);
    json j = ujson::parse(res.body);
    if (!res.ok() || !j.is_object()) { if (error) *error = res.error.empty() ? "no release list" : res.error; return false; }
    std::string tag = ujson::str(j, "tag_name");
    std::string ver = tag;
    if (!ver.empty() && (ver[0] == 'v' || ver[0] == 'V')) ver = ver.substr(1);
    if (ver.rfind("unifico-v", 0) == 0) ver = ver.substr(9);
    if (ureleases::compareVersions(ver, UNIFICO_VERSION) <= 0) return false;
    for (auto& a : j["assets"]) {
        if (ujson::str(a, "name") != assetName()) continue;
        out.version = ver;
        out.url = ujson::str(a, "browser_download_url");
        out.size = (uint64_t)ujson::num(a, "size");
        std::string d = ujson::str(a, "digest");
        if (d.rfind("sha256:", 0) == 0) out.sha256 = d.substr(7);
        out.notes = ujson::str(j, "body");
        return true;
    }
    if (error) *error = "the new version has no build for " + upaths::platformTag();
    return false;
}

JobPtr apply(const Available& a) {
    return ujobs::run("Updating Unifico", [a](Job& job) {
        const std::string install = upaths::selfInstallPath();
        const std::string staging = stagingFor(install);
        const std::string parent = fs::path(install).parent_path().string();
        // 1. disk
        const uint64_t need = a.size * 3 + 16ull * 1024 * 1024;
        const uint64_t free = ufs::freeSpace(parent);
        if (free && free < need) {
            job.fail("Not enough disk space to update: " + ufs::humanBytes(need) + " needed, " + ufs::humanBytes(free) + " free.");
            return false;
        }
        // 2. download + verify
        const std::string zip = upaths::cacheDir() + "/" + assetName();
        std::string err;
        job.setStatus("Downloading Unifico " + a.version + "...");
        if (!uhttp::download(a.url, zip, [&](uint64_t d, uint64_t t) { job.progress = t ? 0.7f * d / t : -1.0f; }, &job.cancel, &err)) {
            job.fail("Download failed: " + err); return false;
        }
        if (!a.sha256.empty() && usha::hexOfFile(zip) != a.sha256) {
            ufs::removeAll(zip);
            job.fail("The update did not match its checksum; it was not installed.");
            return false;
        }
        // 3. unpack beside us
        job.setStatus("Unpacking...");
        const std::string unpack = parent + "/.unifico-update";
        ufs::removeAll(unpack);
        ufs::removeAll(staging);
        if (!uzip::extract(zip, unpack, true, nullptr, &err)) { job.fail("Unpacking failed: " + err); return false; }
        std::error_code ec;
        fs::path found;
        for (auto& e : fs::directory_iterator(unpack, ec)) {
            const std::string n = e.path().filename().string();
            if (n == "Unifico.app" || n == "Unifico" || n == "Unifico.exe") found = e.path();
        }
        if (found.empty()) { ufs::removeAll(unpack); job.fail("The update archive had no launcher in it."); return false; }
        fs::rename(found, staging, ec);
        ufs::removeAll(unpack);
        if (ec) { job.fail("Could not stage the update: " + ec.message()); return false; }
        // 4. authorise it, from here
        stripQuarantine(staging);
#if !defined(_WIN32)
        ::chmod(exeInside(staging).c_str(), 0755);
#endif
        ufs::removeAll(zip);
        // 5. hand over
        job.setStatus("Restarting into " + a.version + "...");
        if (!uproc::spawnDetached(exeInside(staging), {"--finish-update", install, std::to_string(uproc::selfPid())})) {
            job.fail("Could not start the new version.");
            return false;
        }
        ulog::info("handed over to " + staging);
        return true;
    });
}

int finish(const std::string& oldPath, long long oldPid) {
    ulog::info("finishing update over " + oldPath);
    // Wait for the old instance to let go of its files (Windows will not
    // delete a running executable; elsewhere this is courtesy).
    for (int i = 0; i < 300 && uproc::pidAlive(oldPid); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::error_code ec;
    const std::string me = upaths::selfInstallPath();
    const fs::path parent = fs::path(oldPath).parent_path();
    // SWITCHING THE DATA OVER. Everything the launcher keeps lives in its home
    // directory (or, portable, in UnificoData/ beside the install -- and the
    // staged copy was unpacked beside the old one, so it already sees the same
    // folder). No file has to move; what can change between versions is the
    // SHAPE of what is there, so the new version migrates it now, before the
    // old one is gone, and records where it came from.
    migrate();
    // Delete the old instance...
    for (int i = 0; i < 20; ++i) {
        fs::remove_all(oldPath, ec);
        if (!ec && !fs::exists(oldPath, ec)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if (fs::exists(oldPath, ec)) {
        ulog::error("could not remove the old launcher at " + oldPath + "; running from " + me);
        return 0;
    }
    // ...and take its name.
    fs::rename(me, oldPath, ec);
    if (ec) { ulog::error("could not rename " + me + " to " + oldPath + ": " + ec.message()); return 0; }
    uproc::spawnDetached(exeInside(oldPath), {"--updated"});
    return 0;
}

void tidy() {
    const std::string install = upaths::selfInstallPath();
    std::error_code ec;
    const std::string parent = fs::path(install).parent_path().string();
    ufs::removeAll(parent + "/.unifico-update");
    // A staged copy beside us that is not us: an update that never handed over.
    const std::string staging = stagingFor(install);
    if (fs::exists(staging, ec) && !fs::equivalent(staging, install, ec)) ufs::removeAll(staging);
    // We ARE the staged copy, and the old name is free: finish the rename.
    if (install.size() > 4 && install.compare(install.size() - 4, 4, ".new") == 0) {
        const std::string target = install.substr(0, install.size() - 4);
        if (!fs::exists(target, ec)) {
            fs::rename(install, target, ec);
            if (!ec) ulog::info("completed an interrupted update rename");
        }
    }
}
}  // namespace uupdate
