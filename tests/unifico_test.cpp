// The launcher's logic without a window: versions, checksums, grants, worlds,
// archives. Run with the repository root as the first argument.
#include "core/Fs.h"
#include "core/Json.h"
#include "core/Sha256.h"
#include "core/Zip.h"
#include "od/GrantVerify.h"
#include "od/Releases.h"
#include "od/Worlds.h"
#include "od/Support.h"
#include "core/Paths.h"
#include "update/SelfUpdate.h"
#include "tools/TempleOS.h"

#include "miniz.h"
#include "miniz_zip.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

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
    // support detection, by header
    {
        fs::create_directories(tmp);
        auto bin = [&](const char* name, std::vector<unsigned char> head) {
            head.resize(256, 0);
            std::ofstream o(tmp / name, std::ios::binary);
            o.write((const char*)head.data(), (std::streamsize)head.size());
            return (tmp / name).string();
        };
        const std::string machoArm = bin("m-arm", {0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01});
        const std::string machoX64 = bin("m-x64", {0xCF, 0xFA, 0xED, 0xFE, 0x07, 0x00, 0x00, 0x01});
        std::vector<unsigned char> elf = {0x7F, 'E', 'L', 'F', 2, 1, 1, 0};
        elf.resize(18, 0); elf.push_back(0x3E); elf.push_back(0);
        const std::string elfX64 = bin("e-x64", elf);
        std::vector<unsigned char> pe(0x40, 0); pe[0] = 'M'; pe[1] = 'Z'; pe[0x3C] = 0x40;
        pe.insert(pe.end(), {'P', 'E', 0, 0, 0x64, 0x86});
        const std::string peX64 = bin("p-x64", pe);
        ok(usupport::describe(machoArm) == "macOS arm64", "Mach-O arm64 read");
        ok(usupport::describe(elfX64) == "Linux/BSD x64", "ELF x64 read");
        ok(usupport::describe(peX64) == "Windows x64", "PE x64 read");
        const std::string host = upaths::platformTag();
        if (host == "macos-arm64") {
            ok(usupport::binary(machoArm).ok, "native build runs");
            ok(!usupport::binary(peX64).ok && !usupport::binary(elfX64).ok, "other systems' builds refused");
        } else if (host == "linux-x64") {
            ok(usupport::binary(elfX64).ok, "native build runs");
            ok(!usupport::binary(machoX64).ok && !usupport::binary(peX64).ok, "other systems' builds refused");
        } else if (host == "linux-arm64") {
            ok(!usupport::binary(elfX64).ok, "x64 ELF refused on arm64 Linux");
        } else if (host == "windows-x64" || host == "windows-arm64") {
            ok(usupport::binary(peX64).ok, "x64 runs on Windows (natively or emulated)");
            ok(!usupport::binary(elfX64).ok, "ELF refused on Windows");
        }
        ok(!usupport::binary((tmp / "missing").string()).ok, "missing program refused");
    }
    {
        // The TempleOS game disk: read it back the way a FAT32 driver would.
        const fs::path src = tmp / "tos";
        fs::create_directories(src);
        std::string big(5000, 'x');
        for (size_t i = 0; i < big.size(); ++i) big[i] = (char)('a' + i % 26);
        ufs::writeFileAtomic((src / "ODGame.HC").string(), big);
        ufs::writeFileAtomic((src / "RulesTest.HC").string(), "rt");
        ufs::writeFileAtomic((src / "font.odf").string(), "f");
        const std::string img = (tmp / "tos.img").string();
        std::string err;
        ok(utemple::writeFat32(src.string(), img, "ODGAME", &err), "FAT32 image written");
        std::string d;
        ufs::readFile(img, d);
        auto u16 = [&](size_t at) { return (uint32_t)(uint8_t)d[at] | ((uint32_t)(uint8_t)d[at + 1] << 8); };
        auto u32 = [&](size_t at) { return u16(at) | (u16(at + 2) << 16); };
        ok(d.size() > 1024 && (uint8_t)d[510] == 0x55 && (uint8_t)d[0x1BE + 4] == 0x0B, "MBR with a FAT32 partition");
        const size_t pb = (size_t)u32(0x1BE + 8) * 512;
        const uint32_t spc = (uint8_t)d[pb + 13], rsvd = u16(pb + 14), nf = (uint8_t)d[pb + 16], fsz = u32(pb + 36);
        const uint32_t tot = u32(pb + 32);
        ok(d.compare(pb + 82, 5, "FAT32") == 0 && spc == 1 && nf == 2, "FAT32 boot sector");
        ok((tot - rsvd - nf * fsz) / spc >= 65525, "enough clusters to be FAT32 at all");
        const size_t fat = pb + rsvd * 512, data = pb + (rsvd + nf * fsz) * 512;
        auto readChain = [&](uint32_t c, uint32_t size) {
            std::string o;
            while (c >= 2 && c < 0x0FFFFFF8 && o.size() < size) { o.append(d, data + (c - 2) * 512, 512); c = u32(fat + c * 4) & 0x0FFFFFFF; }
            o.resize(size);
            return o;
        };
        const std::string root = readChain(2, 4096);
        bool found = false, lfn = false, mangled = false;
        for (size_t e = 0; e + 32 <= root.size() && root[e]; e += 32) {
            if ((uint8_t)root[e + 11] == 0x0F) { lfn = true; continue; }
            const std::string n = root.substr(e, 11);
            const uint32_t c = (u16(0) * 0) + ((uint32_t)(uint8_t)root[e + 26] | ((uint32_t)(uint8_t)root[e + 27] << 8) |
                               ((uint32_t)(uint8_t)root[e + 20] << 16) | ((uint32_t)(uint8_t)root[e + 21] << 24));
            const uint32_t sz = (uint32_t)(uint8_t)root[e + 28] | ((uint32_t)(uint8_t)root[e + 29] << 8) |
                                ((uint32_t)(uint8_t)root[e + 30] << 16) | ((uint32_t)(uint8_t)root[e + 31] << 24);
            if (n == "ODGAME  HC ") found = readChain(c, sz) == big;
            if (n == "RULEST~1HC ") mangled = readChain(c, sz) == "rt";
        }
        ok(found, "a multi-cluster file reads back intact");
        ok(mangled && lfn, "long names get a ~1 short name and long-name entries");
    }
    ok(ufs::safeName("CON") == "_CON" && ufs::safeName("a/b:c") == "a_b_c", "safe names");
    ok(uupdate::stagingFor("/Applications/Unifico.app") == "/Applications/Unifico.app.new", "staging beside");
    fs::remove_all(tmp);
    std::printf("unifico: %d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
