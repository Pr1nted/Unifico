// The launcher's logic without a window: versions, checksums, grants, worlds,
// archives. Run with the repository root as the first argument.
#include "core/Fs.h"
#include "core/Json.h"
#include "core/Sha256.h"
#include "core/Zip.h"
#include "od/GrantVerify.h"
#include "od/Releases.h"
#include "od/Worlds.h"
#include "update/SelfUpdate.h"

#include "miniz.h"
#include "miniz_zip.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
static int g_n = 0, g_fail = 0;
static void ok(bool c, const char* what) { ++g_n; if (!c) { ++g_fail; std::fprintf(stderr, "FAIL: %s\n", what); } }
static void spit(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s; }

int main(int argc, char** argv) {
    const fs::path root = argc > 1 ? argv[1] : ".";
    // versions
    ok(ureleases::compareVersions("1.2.2a", "1.2.10a") < 0, "1.2.2a < 1.2.10a");
    ok(ureleases::compareVersions("1.3.0a", "1.3.0b") < 0, "alpha < beta");
    ok(ureleases::compareVersions("1.3.0b", "1.3.0") < 0, "beta < release");
    ok(ureleases::compareVersions("0.1.0", "0.1.0") == 0, "equal");
    // GitHub's release JSON
    auto rels = ureleases::parse(R"([{"tag_name":"v1.2.2a","name":"v1.2.2a","prerelease":true,"assets":[{"name":"OpenDoctrines-macos-arm64.zip","size":10,"browser_download_url":"u","digest":"sha256:ab"}]},
        {"tag_name":"templeos-v1.2.2a","assets":[]},{"tag_name":"views-x","assets":[]},{"tag_name":"v1.10.0a","draft":true,"assets":[]}])", "v");
    ok(rels.size() == 1 && rels[0].version == "1.2.2a", "only game tags, no drafts");
    ok(rels[0].assets[0].sha256 == "ab", "digest read");
    // sha256 (FIPS 180-2 vector)
    ok(usha::hexOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
    // grants, against the Node-made vectors the game's test uses too
    json v = ujson::load((root / "tests/data/achievement_grants.json").string());
    auto keys = odach::parseGrantKeys(ujson::str(v, "key"));
    odach::GrantFields f;
    ok(odach::verifyGrantToken(ujson::str(v, "good"), ujson::str(v, "issuer"), keys, f), "good grant");
    ok(!odach::verifyGrantToken(ujson::str(v, "tampered"), ujson::str(v, "issuer"), keys, f), "tampered refused");
    ok(!odach::verifyGrantToken(ujson::str(v, "otherKey_signed"), ujson::str(v, "issuer"), keys, f), "stranger refused");

    const fs::path tmp = fs::temp_directory_path() / "unifico_test";
    fs::remove_all(tmp);
    // zip-slip refused
    {
        mz_zip_archive z{};
        const std::string zp = (tmp / "evil.zip").string();
        fs::create_directories(tmp);
        mz_zip_writer_init_file(&z, zp.c_str(), 0);
        mz_zip_writer_add_mem(&z, "root/ok.txt", "x", 1, 0);
        mz_zip_writer_add_mem(&z, "root/../../escape.txt", "x", 1, 0);
        mz_zip_writer_finalize_archive(&z);
        mz_zip_writer_end(&z);
        std::string err;
        uzip::extract(zp, (tmp / "out").string(), true, nullptr, &err);
        ok(fs::exists(tmp / "out/ok.txt"), "single root stripped");
        ok(!fs::exists(tmp / "escape.txt") && !fs::exists(tmp.parent_path() / "escape.txt"), "zip-slip refused");
    }
    // .odstate import keeps the game's achievement rule
    {
        const fs::path src = tmp / "src", dst = tmp / "dst";
        spit(src / "achievements/grants.json", "{\"grants\":[\"forged\"]}");
        spit(src / "achievements/progress.json", "{\"counters\":{\"turns_ended\":999}}");
        spit(src / "achievements/evil", "x");
        spit(src / "saves/w.odsv", "notreally");
        spit(src / "fonts/shipped.ttf", "x");
        int n = 0;
        std::string err;
        ok(uworlds::exportState(src.string(), (tmp / "s.odstate").string(), &n, &err), "export");
        std::string a;
        ok(!uzip::readMember((tmp / "s.odstate").string(), "fonts/shipped.ttf", a), "shipped content not exported");
        spit(dst / "achievements/progress.json", "{\"counters\":{\"turns_ended\":3}}");
        uworlds::importState(dst.string(), (tmp / "s.odstate").string(), &n, &err);
        ok(!fs::exists(dst / "achievements/grants.json"), "grants never written in place");
        ok(fs::exists(dst / "achievements/grants.import.json"), "grants offered for verification");
        std::string prog;
        ufs::readFile((dst / "achievements/progress.json").string(), prog);
        ok(prog.find("999") == std::string::npos, "own progress kept");
        ok(!fs::exists(dst / "achievements/evil"), "unknown achievement file refused");
        ok(fs::exists(dst / "saves/w.odsv"), "worlds restored");
    }
    ok(ufs::safeName("CON") == "_CON" && ufs::safeName("a/b:c") == "a_b_c", "safe names");
    ok(uupdate::stagingFor("/Applications/Unifico.app") == "/Applications/Unifico.app.new", "staging beside");
    fs::remove_all(tmp);
    std::printf("unifico: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
