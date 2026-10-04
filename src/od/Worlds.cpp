#include "od/Worlds.h"
#include "core/Fs.h"
#include "core/Json.h"
#include "core/Zip.h"

#include "miniz.h"
#include "miniz_zip.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace uworlds {
namespace {
// Mirrors kShipped in Open Doctrines' src/OdState.cpp. A name missing here
// only makes an export larger; one missing there would lose player data.
const char* kShipped[] = {"STDmaps", "audio", "flags", "fonts", "icons", "symbols", "licenses", "ai",
                          "tips.json", "splashes.json", "credits.txt", "comms", "lang", "dialog",
                          "policies.json", "district_laws.json", "parties.json", "menu_bg.png",
                          "Icon", "MANAGED", "VERSION", "tools", "llm"};
bool shipped(const std::string& top) {
    for (const char* s : kShipped) if (top == s) return true;
    return false;
}
bool safeEntry(const std::string& n) {
    if (n.empty() || n[0] == '/' || n[0] == '\\' || (n.size() > 1 && n[1] == ':')) return false;
    for (const auto& p : fs::path(n)) if (p == "..") return false;
    return true;
}

World read(const fs::path& p, bool mp) {
    World w;
    std::error_code ec;
    w.path = p.string();
    w.file = p.filename().string();
    w.name = p.stem().string();
    w.size = fs::file_size(p, ec);
    w.multiplayer = mp;
    std::string meta;
    if (uzip::readMember(w.path, "metadata.json", meta)) {
        json j = ujson::parse(meta);
        if (j.is_object()) {
            std::string n = ujson::str(j, "save_name");
            if (!n.empty()) w.name = n;
            w.version = ujson::str(j, "version");
            w.lastPlayed = ujson::str(j, "last_played");
            w.turns = (int)ujson::num(j, "turn_count");
        }
    }
    return w;
}

std::string uniquePath(const fs::path& dir, const std::string& stem) {
    std::error_code ec;
    fs::path p = dir / (stem + ".odsv");
    for (int k = 2; fs::exists(p, ec); ++k) p = dir / (stem + " (" + std::to_string(k) + ").odsv");
    return p.string();
}
}  // namespace

std::vector<World> list(const std::string& dataDir) {
    std::vector<World> out;
    std::error_code ec;
    for (auto& [sub, mp] : std::vector<std::pair<std::string, bool>>{{"saves", false}, {"saves/multiplayer", true}}) {
        for (auto& e : fs::directory_iterator(fs::path(dataDir) / sub, ec))
            if (e.is_regular_file(ec) && e.path().extension() == ".odsv") out.push_back(read(e.path(), mp));
    }
    std::sort(out.begin(), out.end(), [](const World& a, const World& b) { return a.lastPlayed > b.lastPlayed; });
    return out;
}

bool rename(const World& w, const std::string& newName, std::string* error) {
    const std::string stem = ufs::safeName(newName);
    const fs::path dest = fs::path(w.path).parent_path() / (stem + ".odsv");
    std::error_code ec;
    if (fs::exists(dest, ec)) { if (error) *error = "A world with that name already exists."; return false; }
    fs::rename(w.path, dest, ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

bool duplicate(const World& w, std::string* error) {
    std::error_code ec;
    fs::copy_file(w.path, uniquePath(fs::path(w.path).parent_path(), fs::path(w.path).stem().string() + " copy"), ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

bool remove(const World& w, std::string* error) {
    std::error_code ec;
    fs::remove(w.path, ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

bool copyTo(const World& w, const std::string& other, std::string* error) {
    std::error_code ec;
    const fs::path dir = fs::path(other) / (w.multiplayer ? "saves/multiplayer" : "saves");
    fs::create_directories(dir, ec);
    fs::copy_file(w.path, uniquePath(dir, fs::path(w.path).stem().string()), ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

bool importFile(const std::string& odsv, const std::string& dataDir, std::string* error) {
    std::string meta;
    if (!uzip::readMember(odsv, "metadata.json", meta)) {
        if (error) *error = "That is not an Open Doctrines world (.odsv).";
        return false;
    }
    std::error_code ec;
    const fs::path dir = fs::path(dataDir) / "saves";
    fs::create_directories(dir, ec);
    fs::copy_file(odsv, uniquePath(dir, fs::path(odsv).stem().string()), ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

bool exportTo(const World& w, const std::string& dest, std::string* error) {
    std::error_code ec;
    fs::copy_file(w.path, dest, fs::copy_options::overwrite_existing, ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

bool exportState(const std::string& dataDir, const std::string& outPath, int* files, std::string* error) {
    mz_zip_archive z{};
    if (!mz_zip_writer_init_file(&z, outPath.c_str(), 0)) { if (error) *error = "cannot create " + outPath; return false; }
    int n = 0;
    std::error_code ec;
    const fs::path root(dataDir);
    for (auto& top : fs::directory_iterator(root, ec)) {
        const std::string name = top.path().filename().string();
        if (name == ".DS_Store" || shipped(name)) continue;
        auto add = [&](const fs::path& f) {
            if (f.extension() == ".odstate") return;   // never nest exports
            const std::string rel = fs::relative(f, root, ec).generic_string();
            const std::string ext = f.extension().string();
            const bool stored = ext == ".odsv" || ext == ".odmap" || ext == ".odmod" || ext == ".zip" || ext == ".png";
            if (mz_zip_writer_add_file(&z, rel.c_str(), f.string().c_str(), nullptr, 0,
                                       stored ? MZ_NO_COMPRESSION : MZ_BEST_COMPRESSION)) ++n;
        };
        if (top.is_directory(ec)) {
            for (auto it = fs::recursive_directory_iterator(top.path(), ec); it != fs::recursive_directory_iterator(); it.increment(ec))
                if (it->is_regular_file(ec) && it->path().filename() != ".DS_Store") add(it->path());
        } else if (top.is_regular_file(ec)) {
            add(top.path());
        }
    }
    bool ok = mz_zip_writer_finalize_archive(&z);
    mz_zip_writer_end(&z);
    if (files) *files = n;
    if (!ok && error) *error = "could not finish the archive";
    return ok;
}

bool importState(const std::string& dataDir, const std::string& archive, int* files, std::string* error) {
    mz_zip_archive z{};
    if (!mz_zip_reader_init_file(&z, archive.c_str(), 0)) { if (error) *error = "That file is not a readable .odstate archive."; return false; }
    const fs::path root(dataDir);
    std::error_code ec;
    int written = 0, refused = 0;
    const int n = (int)mz_zip_reader_get_num_files(&z);
    for (int i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&z, i, &st) || mz_zip_reader_is_file_a_directory(&z, i)) continue;
        std::string name = st.m_filename;
        if (!safeEntry(name)) { ++refused; continue; }
        fs::path dest = root / name;
        if (name.rfind("achievements/", 0) == 0) {
            if (name == "achievements/grants.json") dest = root / "achievements" / "grants.import.json";
            else if (name == "achievements/progress.json") { if (fs::exists(dest, ec)) continue; }
            else { ++refused; continue; }
        }
        fs::create_directories(dest.parent_path(), ec);
        if (mz_zip_reader_extract_to_file(&z, i, dest.string().c_str(), 0)) ++written;
    }
    mz_zip_reader_end(&z);
    if (files) *files = written;
    if (!written) { if (error) *error = refused ? "Every entry in that archive was unsafe." : "That archive held nothing."; return false; }
    if (refused && error) *error = std::to_string(refused) + " unsafe entries were refused.";
    return true;
}
}  // namespace uworlds
