#include "od/Support.h"
#include "core/Fs.h"
#include "core/Paths.h"
#include "ui/Strings.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace usupport {
namespace {
enum class Os { Unknown, Mac, Windows, Elf };
struct Kind { Os os = Os::Unknown; std::vector<std::string> archs; };

uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

std::string machArch(uint32_t cpu) {
    switch (cpu) {
        case 0x01000007: return "x64";
        case 0x0100000C: return "arm64";
        case 7: return "x86";
        case 12: return "armv7";
        default: return "unknown";
    }
}

Kind inspect(const std::string& path) {
    Kind k;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return k;
    uint8_t h[4096] = {0};
    const size_t n = std::fread(h, 1, sizeof h, f);
    if (n < 64) { std::fclose(f); return k; }
    const uint32_t m = be32(h);
    if (m == 0xCAFEBABE || m == 0xCAFEBABF) {                 // universal (fat) Mach-O
        k.os = Os::Mac;
        const uint32_t count = be32(h + 4);
        const size_t stride = m == 0xCAFEBABE ? 20 : 32;
        for (uint32_t i = 0; i < count && 8 + (i + 1) * stride <= n; ++i) k.archs.push_back(machArch(be32(h + 8 + i * stride)));
    } else if (le32(h) == 0xFEEDFACF || le32(h) == 0xFEEDFACE) {  // thin Mach-O
        k.os = Os::Mac;
        k.archs.push_back(machArch(le32(h + 4)));
    } else if (h[0] == 0x7F && h[1] == 'E' && h[2] == 'L' && h[3] == 'F') {
        k.os = Os::Elf;
        switch (le16(h + 18)) {
            case 0x3E: k.archs.push_back("x64"); break;
            case 0xB7: k.archs.push_back("arm64"); break;
            case 0x03: k.archs.push_back("x86"); break;
            case 0x28: k.archs.push_back("armv7"); break;
            case 0xF3: k.archs.push_back("riscv64"); break;
            default: k.archs.push_back("unknown");
        }
    } else if (h[0] == 'M' && h[1] == 'Z') {
        const uint32_t pe = le32(h + 0x3C);
        if (pe + 6 < n && std::memcmp(h + pe, "PE\0\0", 4) == 0) {
            k.os = Os::Windows;
            switch (le16(h + pe + 4)) {
                case 0x8664: k.archs.push_back("x64"); break;
                case 0xAA64: k.archs.push_back("arm64"); break;
                case 0x014C: k.archs.push_back("x86"); break;
                default: k.archs.push_back("unknown");
            }
        }
    }
    std::fclose(f);
    return k;
}

std::string hostArch() {
    const std::string t = upaths::platformTag();
    return t.substr(t.find('-') + 1);
}

const char* osName(Os o) {
    switch (o) { case Os::Mac: return "macOS"; case Os::Windows: return "Windows"; case Os::Elf: return "Linux/BSD"; default: return "unknown"; }
}
}  // namespace

std::string host() { return upaths::osName() + " " + hostArch(); }

std::string describe(const std::string& exe) {
    Kind k = inspect(exe);
    std::string s = osName(k.os);
    for (size_t i = 0; i < k.archs.size(); ++i) s += (i ? "+" : " ") + k.archs[i];
    return s;
}

Verdict binary(const std::string& exe) {
    Verdict v;
    if (!ufs::exists(exe)) { v.ok = false; v.why = N_("The game's program is missing from this installation."); return v; }
    Kind k = inspect(exe);
    const std::string os = upaths::osName(), arch = hostArch();
    const bool wantMac = os == "macos", wantWin = os == "windows", wantElf = !wantMac && !wantWin;
    if (k.os == Os::Unknown) { v.ok = false; v.why = N_("This file is not a program this launcher recognises."); return v; }
    if ((k.os == Os::Mac) != wantMac || (k.os == Os::Windows) != wantWin || (k.os == Os::Elf) != wantElf) {
        v.ok = false;
        v.why = N_("This is a build for %s, and this computer runs %s.");
        v.a1 = osName(k.os);
        v.a2 = host();
        return v;
    }
    auto has = [&](const char* a) { for (auto& x : k.archs) if (x == a) return true; return false; };
    if (has(arch.c_str())) return v;
    // The emulation each system actually provides.
    if (wantMac && arch == "arm64" && has("x64")) {
        if (ufs::exists("/Library/Apple/usr/share/rosetta/rosetta")) return v;
        v.ok = false;
        v.why = N_("This is an Intel build. Install Rosetta (softwareupdate --install-rosetta) or install the Apple Silicon build.");
        return v;
    }
    if (wantWin && arch == "arm64" && (has("x64") || has("x86"))) return v;
    if (wantWin && arch == "x64" && has("x86")) return v;
    v.ok = false;
    v.why = N_("This is a build for %s, and this computer runs %s.");
    v.a1 = describe(exe);
    v.a2 = host();
    return v;
}
}  // namespace usupport
