#include "od/Mods.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "core/Sha256.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace umods {
std::vector<InstalledMod> installed(const std::string& dataDir) {
    std::vector<InstalledMod> out;
    json state = ujson::load(dataDir + "/mods.json");
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dataDir + "/mods", ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".odmod") continue;
        InstalledMod m;
        m.file = e.path().filename().string();
        m.id = e.path().stem().string();
        m.size = e.file_size(ec);
        if (state.contains("mods") && state["mods"].is_array())
            for (auto& s : state["mods"])
                if (ujson::str(s, "id") == m.id) { m.enabled = ujson::flag(s, "enabled"); m.known = true; }
        out.push_back(m);
    }
    return out;
}

bool setEnabled(const std::string& dataDir, const std::string& id, bool on) {
    json state = ujson::load(dataDir + "/mods.json");
    if (!state.is_object()) state = json::object();
    state["schema"] = 1;
    if (!state.contains("mods") || !state["mods"].is_array()) state["mods"] = json::array();
    bool found = false;
    for (auto& s : state["mods"]) if (ujson::str(s, "id") == id) { s["enabled"] = on; found = true; }
    // A mod the game has not seen yet: record it the way the game would, with
    // no grants. The game asks for each grant itself the first time the mod
    // runs; enabling here cannot grant anything on the player's behalf.
    if (!found) state["mods"].push_back({{"id", id}, {"enabled", on}, {"grants", 0}});
    return ujson::save(dataDir + "/mods.json", state);
}

bool remove(const std::string& dataDir, const InstalledMod& m, std::string* error) {
    std::error_code ec;
    fs::remove(fs::path(dataDir) / "mods" / m.file, ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

bool installFile(const std::string& dataDir, const std::string& odmod, std::string* error) {
    if (fs::path(odmod).extension() != ".odmod") { if (error) *error = "Mods are .odmod files."; return false; }
    std::error_code ec;
    fs::create_directories(fs::path(dataDir) / "mods", ec);
    fs::copy_file(odmod, fs::path(dataDir) / "mods" / fs::path(odmod).filename(), fs::copy_options::overwrite_existing, ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

std::vector<DirectoryMod> browse(const std::string& issuer, std::string* error) {
    std::vector<DirectoryMod> out;
    uhttp::Response r = uhttp::get(issuer + "/mods?limit=50");
    if (!r.ok()) { if (error) *error = r.error.empty() ? "The mod directory did not answer." : r.error; return out; }
    json j = ujson::parse(r.body);
    if (!j.is_object() || !j.contains("mods") || !j["mods"].is_array()) return out;
    for (auto& m : j["mods"]) {
        DirectoryMod d;
        d.id = ujson::str(m, "id");
        d.name = ujson::str(m, "name", d.id);
        d.version = ujson::str(m, "version");
        d.summary = ujson::str(m, "summary");
        d.by = ujson::str(m, "by");
        d.get = ujson::str(m, "get");
        d.sha256 = ujson::str(m, "declaredSha256");
        d.sideLabel = ujson::str(m, "sideLabel");
        d.size = (uint64_t)ujson::num(m, "sizeBytes");
        if (!d.id.empty()) out.push_back(d);
    }
    return out;
}

JobPtr installFromDirectory(const std::string& issuer, const DirectoryMod& m, const std::string& dataDir) {
    return ujobs::run("Installing " + m.name, [issuer, m, dataDir](Job& job) {
        const std::string tmp = upaths::cacheDir() + "/" + ufs::safeName(m.id) + ".odmod";
        std::string err;
        job.setStatus("Downloading " + m.name + "...");
        if (!uhttp::download(issuer + m.get, tmp, [&](uint64_t d, uint64_t t) { job.progress = t ? (float)d / t : -1.0f; },
                             &job.cancel, &err)) {
            job.fail("Download failed: " + err);
            return false;
        }
        if (!m.sha256.empty() && usha::hexOfFile(tmp) != m.sha256) {
            ufs::removeAll(tmp);
            job.fail("The file is not the one its author declared (checksum mismatch). Not installed.");
            return false;
        }
        std::error_code ec;
        fs::create_directories(fs::path(dataDir) / "mods", ec);
        fs::copy_file(tmp, fs::path(dataDir) / "mods" / (ufs::safeName(m.id) + ".odmod"), fs::copy_options::overwrite_existing, ec);
        ufs::removeAll(tmp);
        if (ec) { job.fail(ec.message()); return false; }
        job.setStatus("Installed " + m.name + ". Switch it on here or in the game's Mod Menu.");
        return true;
    });
}
}  // namespace umods
