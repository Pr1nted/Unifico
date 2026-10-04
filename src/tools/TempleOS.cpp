#include "tools/TempleOS.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Zip.h"
#include "od/Releases.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

#ifndef N_
#define N_(s) s   // marked for the translator; drawn through T() on the page
#endif

namespace utemple {
namespace {
const int kMonitorPort = 45454;
std::string home() { return upaths::gamesDir() + "/templeos"; }
std::string isoPath() { return home() + "/TempleOS.ISO"; }
std::string payloadDir() { return home() + "/payload"; }
std::string diskPath() { return home() + "/game-disk.img"; }
}  // namespace

// ---- FAT32, the disk TempleOS can actually read ------------------------
//
// TempleOS 5.03 reads its own RedSea format and FAT32, and nothing else: a
// second CD in ordinary ISO9660 mounts (as U:) and then answers every request
// with "File System Not Supported". So the game travels on a small FAT32 hard
// disk, laid out the way macOS's own `hdiutil -fs "MS-DOS FAT32"` lays one
// out, which is the layout first proven to work in the guest: an MBR with one
// type-0x0B partition at LBA 1, 512-byte sectors and clusters, two FATs, the
// root directory at cluster 2. Mixed-case names carry long-name entries.
namespace {
void le16(std::vector<uint8_t>& b, size_t at, uint16_t v) { b[at] = v & 0xFF; b[at + 1] = v >> 8; }
void le32(std::vector<uint8_t>& b, size_t at, uint32_t v) { for (int i = 0; i < 4; ++i) b[at + i] = (v >> (8 * i)) & 0xFF; }

/** The 8.3 name, padded to 11, made unique with ~N when it had to be cut. */
std::string shortName(const std::string& name, std::vector<std::string>& taken, bool* lossy) {
    auto clean = [](const std::string& in, size_t max, bool& cut) {
        std::string o;
        for (char c : in) {
            const char u = (char)toupper((unsigned char)c);
            if (isalnum((unsigned char)u) || u == '_' || u == '-' || u == '~') o += u;
            else cut = true;
        }
        if (o.size() > max) { o.resize(max); cut = true; }
        return o;
    };
    const size_t dot = name.rfind('.');
    std::string base = dot == std::string::npos ? name : name.substr(0, dot);
    std::string ext = dot == std::string::npos ? "" : name.substr(dot + 1);
    bool cut = false;
    std::string b = clean(base, 8, cut), e = clean(ext, 3, cut);
    if (b.empty()) { b = "FILE"; cut = true; }
    *lossy = cut;
    auto pack = [](std::string bb, std::string ee) { bb.resize(8, ' '); ee.resize(3, ' '); return bb + ee; };
    std::string sn = pack(b, e);
    for (int n = 1; cut || std::find(taken.begin(), taken.end(), sn) != taken.end(); ++n) {
        const std::string tail = "~" + std::to_string(n);
        sn = pack(b.substr(0, 8 - tail.size()) + tail, e);
        cut = false;
        if (std::find(taken.begin(), taken.end(), sn) == taken.end()) break;
    }
    taken.push_back(sn);
    return sn;
}
}  // namespace

bool writeFat32(const std::string& srcDir, const std::string& out, const std::string& volume, std::string* error) {
    const uint32_t S = 512, start = 1, total = 131070, rsvd = 32;
    // Clusters left once the FATs are paid for; solve for the FAT size.
    uint32_t fatSz = 1;
    for (int i = 0; i < 8; ++i) {
        const uint32_t clusters = total - rsvd - 2 * fatSz;
        fatSz = ((clusters + 2) * 4 + S - 1) / S;
    }
    const uint32_t dataLba = rsvd + 2 * fatSz;   // relative to the partition
    const uint32_t clusters = total - dataLba;

    struct F { std::string name, path; uint32_t size, first = 0; };
    std::vector<F> files;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(srcDir, ec))
        if (e.is_regular_file(ec)) files.push_back({e.path().filename().string(), e.path().string(), (uint32_t)e.file_size(ec)});
    std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.name < b.name; });

    // The root directory's entries first, so its size is known.
    std::vector<uint8_t> root;
    auto entry = [&](const std::string& n11, uint8_t attr, uint32_t cluster, uint32_t size) {
        std::vector<uint8_t> e(32, 0);
        std::memcpy(&e[0], n11.data(), 11);
        e[11] = attr;
        std::time_t t = std::time(nullptr);
        std::tm* g = std::localtime(&t);
        const uint16_t tm = (uint16_t)((g->tm_hour << 11) | (g->tm_min << 5) | (g->tm_sec / 2));
        const uint16_t dt = (uint16_t)(((g->tm_year - 80) << 9) | ((g->tm_mon + 1) << 5) | g->tm_mday);
        le16(e, 14, tm); le16(e, 16, dt); le16(e, 18, dt); le16(e, 22, tm); le16(e, 24, dt);
        le16(e, 20, (uint16_t)(cluster >> 16)); le16(e, 26, (uint16_t)(cluster & 0xFFFF));
        le32(e, 28, size);
        return e;
    };
    std::string vol = volume.substr(0, 11);
    for (auto& c : vol) c = (char)toupper((unsigned char)c);
    vol.resize(11, ' ');
    { auto v = entry(vol, 0x08, 0, 0); root.insert(root.end(), v.begin(), v.end()); }

    // Allocate: root at cluster 2, files contiguous after it.
    std::vector<std::string> taken;
    std::vector<std::pair<size_t, std::string>> names;   // index into files, 11-char short name
    for (size_t i = 0; i < files.size(); ++i) { bool lossy = false; names.push_back({i, shortName(files[i].name, taken, &lossy)}); }
    size_t rootBytes = 32;
    for (auto& [i, sn] : names) {
        const std::string& n = files[i].name;
        bool mixed = false;
        std::string up;
        for (char c : n) up += (char)toupper((unsigned char)c);
        std::string fromShort = sn.substr(0, 8);
        while (!fromShort.empty() && fromShort.back() == ' ') fromShort.pop_back();
        std::string ex = sn.substr(8);
        while (!ex.empty() && ex.back() == ' ') ex.pop_back();
        if (!ex.empty()) fromShort += "." + ex;
        mixed = n != fromShort;
        rootBytes += 32 + (mixed ? 32 * ((n.size() + 12) / 13) : 0);
    }
    const uint32_t rootClusters = (uint32_t)((rootBytes + S - 1) / S);
    uint32_t next = 2 + rootClusters;
    for (auto& f : files) {
        const uint32_t n = std::max<uint32_t>(1, (f.size + S - 1) / S);
        f.first = f.size ? next : 0;
        if (f.size) next += n;
    }
    if (next >= clusters + 2) { if (error) *error = "the game does not fit on the disk"; return false; }

    for (auto& [i, sn] : names) {
        const F& f = files[i];
        std::string fromShort = sn.substr(0, 8);
        while (!fromShort.empty() && fromShort.back() == ' ') fromShort.pop_back();
        std::string ex = sn.substr(8);
        while (!ex.empty() && ex.back() == ' ') ex.pop_back();
        if (!ex.empty()) fromShort += "." + ex;
        if (f.name != fromShort) {
            uint8_t sum = 0;
            for (int k = 0; k < 11; ++k) sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + (uint8_t)sn[k]);
            const int parts = (int)((f.name.size() + 12) / 13);
            for (int p = parts; p >= 1; --p) {
                std::vector<uint8_t> e(32, 0xFF);
                e[0] = (uint8_t)(p | (p == parts ? 0x40 : 0));
                e[11] = 0x0F; e[12] = 0; e[13] = sum; e[26] = 0; e[27] = 0;
                static const int pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
                for (int k = 0; k < 13; ++k) {
                    const size_t ci = (size_t)(p - 1) * 13 + k;
                    uint16_t ch = ci < f.name.size() ? (uint8_t)f.name[ci] : (ci == f.name.size() ? 0 : 0xFFFF);
                    le16(e, pos[k], ch);
                }
                root.insert(root.end(), e.begin(), e.end());
            }
        }
        auto e = entry(sn, 0x20, f.first, f.size);
        root.insert(root.end(), e.begin(), e.end());
    }

    std::vector<uint8_t> img((size_t)(start + total) * S, 0);
    // MBR
    img[0x1BE + 4] = 0x0B;
    { std::vector<uint8_t> t(8); le32(t, 0, start); le32(t, 4, total); std::memcpy(&img[0x1BE + 8], t.data(), 8); }
    img[0x1BE + 1] = 0xFE; img[0x1BE + 2] = 0xFF; img[0x1BE + 3] = 0xFF;
    img[0x1BE + 5] = 0xFE; img[0x1BE + 6] = 0xFF; img[0x1BE + 7] = 0xFF;
    img[510] = 0x55; img[511] = 0xAA;
    // Boot sector
    std::vector<uint8_t> bs(S, 0);
    bs[0] = 0xEB; bs[1] = 0x58; bs[2] = 0x90;
    std::memcpy(&bs[3], "UNIFICO ", 8);
    le16(bs, 11, (uint16_t)S); bs[13] = 1; le16(bs, 14, (uint16_t)rsvd); bs[16] = 2;
    bs[21] = 0xF8; le16(bs, 24, 63); le16(bs, 26, 255); le32(bs, 28, start); le32(bs, 32, total);
    le32(bs, 36, fatSz); le32(bs, 44, 2); le16(bs, 48, 1); le16(bs, 50, 6);
    bs[64] = 0x80; bs[66] = 0x29; le32(bs, 67, (uint32_t)std::time(nullptr));
    std::memcpy(&bs[71], vol.data(), 11); std::memcpy(&bs[82], "FAT32   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;
    std::vector<uint8_t> fsi(S, 0);
    le32(fsi, 0, 0x41615252); le32(fsi, 484, 0x61417272);
    le32(fsi, 488, clusters - (next - 2)); le32(fsi, 492, next); le32(fsi, 508, 0xAA550000);
    auto at = [&](uint32_t lba) { return (size_t)(start + lba) * S; };
    std::memcpy(&img[at(0)], bs.data(), S); std::memcpy(&img[at(1)], fsi.data(), S);
    std::memcpy(&img[at(6)], bs.data(), S); std::memcpy(&img[at(7)], fsi.data(), S);
    // FATs
    std::vector<uint8_t> fat((size_t)fatSz * S, 0);
    le32(fat, 0, 0x0FFFFFF8); le32(fat, 4, 0x0FFFFFFF);
    auto chain = [&](uint32_t first, uint32_t n) {
        for (uint32_t c = first; c < first + n; ++c) le32(fat, (size_t)c * 4, c + 1 < first + n ? c + 1 : 0x0FFFFFFF);
    };
    chain(2, rootClusters);
    for (auto& f : files) if (f.size) chain(f.first, (f.size + S - 1) / S);
    std::memcpy(&img[at(rsvd)], fat.data(), fat.size());
    std::memcpy(&img[at(rsvd + fatSz)], fat.data(), fat.size());
    // Data
    auto clusterAt = [&](uint32_t c) { return at(dataLba + (c - 2)); };
    std::memcpy(&img[clusterAt(2)], root.data(), root.size());
    for (auto& f : files) {
        if (!f.size) continue;
        std::ifstream in(f.path, std::ios::binary);
        in.read((char*)&img[clusterAt(f.first)], f.size);
        if (!in) { if (error) *error = "could not read " + f.path; return false; }
    }
    std::ofstream o(out, std::ios::binary | std::ios::trunc);
    o.write((const char*)img.data(), (std::streamsize)img.size());
    if (!o && error) *error = "could not write " + out;
    return (bool)o;
}

std::string qemu() {
    std::string q = uproc::which("qemu-system-x86_64");
#if defined(_WIN32)
    for (const char* c : {"C:\\Program Files\\qemu\\qemu-system-x86_64.exe", "C:\\Program Files (x86)\\qemu\\qemu-system-x86_64.exe"})
        if (q.empty() && ufs::exists(c)) q = c;
#else
    // A launcher opened from the desktop does not get the shell's PATH, so the
    // usual package-manager homes are looked in by name as well.
    for (const char* c : {"/opt/homebrew/bin/qemu-system-x86_64", "/usr/local/bin/qemu-system-x86_64",
                          "/opt/local/bin/qemu-system-x86_64", "/usr/bin/qemu-system-x86_64", "/usr/pkg/bin/qemu-system-x86_64"})
        if (q.empty() && ufs::exists(c)) q = c;
#endif
    return q;
}

QemuStatus qemuStatus() {
    // Asked once per run: whether it exists AND starts. A copy that is present
    // but broken (a half-finished Homebrew upgrade, a missing library) should
    // say so here rather than as an emulator window that never appears.
    static QemuStatus cached;
    static bool asked = false;
    if (asked && cached.ok) return cached;
    asked = true;
    cached = QemuStatus{};
    cached.path = qemu();
    if (cached.path.empty()) { cached.problem = qemuHelp(); return cached; }
    if (uproc::runQuiet(cached.path, {"--version"}) != 0) {
        cached.problem = "QEMU is installed at " + cached.path + " but does not start. Reinstall it.";
        return cached;
    }
    cached.ok = true;
    return cached;
}

std::string qemuHelp() {
#if defined(__APPLE__)
    return "Install QEMU with Homebrew:  brew install qemu";
#elif defined(_WIN32)
    return "Install QEMU from qemu.weilnetz.de/w64 (the launcher opens the page).";
#elif defined(__FreeBSD__)
    return "Install QEMU:  pkg install qemu";
#elif defined(__OpenBSD__)
    return "Install QEMU:  pkg_add qemu";
#else
    return "Install QEMU from your package manager, e.g.  sudo apt install qemu-system-x86";
#endif
}

bool ready() { return ufs::exists(isoPath()) && ufs::exists(payloadDir() + "/ODGame.HC"); }

JobPtr prepare() {
    return ujobs::run("Preparing TempleOS", [](Job& job) {
        std::error_code ec;
        fs::create_directories(home(), ec);
        std::string err;
        if (!ufs::exists(isoPath())) {
            job.setStatus("Downloading the TempleOS live CD from templeos.org...");
            if (!uhttp::download("https://templeos.org/Downloads/TempleOS.ISO", isoPath(),
                                 [&](uint64_t a, uint64_t t) { job.progress = t ? 0.6f * a / t : -1.0f; }, &job.cancel, &err)) {
                job.fail("TempleOS download failed: " + err); return false;
            }
        }
        auto rels = ureleases::templeos();
        if (rels.empty()) { ureleases::refresh(nullptr); rels = ureleases::templeos(); }
        if (rels.empty()) { job.fail("No TempleOS edition has been released yet."); return false; }
        const ReleaseAsset* a = nullptr;
        for (auto& as : rels.front().assets) if (as.name.find(".zip") != std::string::npos) a = &as;
        if (!a) { job.fail("The TempleOS release has no zip."); return false; }
        const std::string zip = upaths::cacheDir() + "/" + a->name;
        job.setStatus("Downloading Open Doctrines for TempleOS " + rels.front().version + "...");
        if (!uhttp::download(a->url, zip, [&](uint64_t d, uint64_t t) { job.progress = 0.6f + (t ? 0.3f * d / t : 0); }, &job.cancel, &err)) {
            job.fail(err); return false;
        }
        ufs::removeAll(payloadDir());
        if (!uzip::extract(zip, home() + "/unpacked", false, nullptr, &err)) { job.fail(err); return false; }
        // The release is templeos/<files>; the disk carries that one folder's files.
        std::string src = home() + "/unpacked/templeos";
        if (!ufs::isDir(src)) src = home() + "/unpacked";
        fs::rename(src, payloadDir(), ec);
        ufs::removeAll(home() + "/unpacked");
        fs::remove(home() + "/payload.iso", ec);   // the old, unreadable CD
        job.setStatus("Building the game disk...");
        if (!writeFat32(payloadDir(), diskPath(), "ODGAME", &err)) { job.fail(err); return false; }
        return true;
    });
}

std::unique_ptr<uproc::Child> boot(std::string* error) {
    const QemuStatus qs = qemuStatus();
    if (!qs.ok) { if (error) *error = qs.problem; return nullptr; }
    // Rebuilt at every boot: it takes a moment, and it means a disk the guest
    // wrote to (answering "y" to the installer, say) never outlives the run.
    std::string err;
    if (!writeFat32(payloadDir(), diskPath(), "ODGAME", &err)) { if (error) *error = err; return nullptr; }
    uproc::Spec s;
    s.exe = qs.path;
    // The game disk is the primary master, which Mount offers as drive 1; the
    // live CD is the secondary master, as TempleOS's own install expects.
    s.args = {"-m", "1024",
              "-drive", "file=" + diskPath() + ",format=raw,if=ide,index=0",
              "-drive", "file=" + isoPath() + ",format=raw,if=ide,index=2,media=cdrom",
              "-boot", "d", "-monitor", "tcp:127.0.0.1:" + std::to_string(kMonitorPort) + ",server,nowait",
              "-name", "Open Doctrines on TempleOS"};
#if defined(__linux__)
    if (ufs::exists("/dev/kvm")) { s.args.push_back("-accel"); s.args.push_back("kvm"); }
#elif defined(__APPLE__) && defined(__x86_64__)
    s.args.push_back("-accel"); s.args.push_back("hvf");
#endif
    s.cwd = home();
    s.logPath = upaths::logsDir() + "/templeos.log";
    auto c = std::make_unique<uproc::Child>();
    if (!c->start(s, error)) return nullptr;
    return c;
}

namespace {
#if defined(_WIN32)
using Sock = SOCKET;
void sockClose(Sock s) { closesocket(s); }
void napMs(int ms) { Sleep(ms); }
#else
using Sock = int;
void sockClose(Sock s) { close(s); }
void napMs(int ms) { usleep(ms * 1000); }
#endif

/** Send monitor commands, one connection, `gapMs` between them. */
bool monitor(const std::vector<std::string>& lines, int gapMs) {
#if defined(_WIN32)
    static bool wsa = false;
    if (!wsa) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); wsa = true; }
#endif
    Sock fd = socket(AF_INET, SOCK_STREAM, 0);
#if defined(_WIN32)
    if (fd == INVALID_SOCKET) return false;
#else
    if (fd < 0) return false;
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(kMonitorPort);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(fd, (sockaddr*)&a, sizeof a) != 0) { sockClose(fd); return false; }
    for (auto& l : lines) {
        const std::string line = l + "\n";
        send(fd, line.c_str(), (int)line.size(), 0);
        napMs(gapMs);
    }
    sockClose(fd);
    return true;
}

std::vector<std::string> keysFor(const std::string& text) {
    // The same table templeos/vm.sh uses: QEMU key names, shift for the rest.
    std::vector<std::string> keys;
    for (char c : text) {
        if (c >= 'a' && c <= 'z') keys.push_back(std::string(1, c));
        else if (c >= 'A' && c <= 'Z') keys.push_back("shift-" + std::string(1, (char)tolower(c)));
        else if (c >= '0' && c <= '9') keys.push_back(std::string(1, c));
        else switch (c) {
            case ' ': keys.push_back("spc"); break;
            case '/': keys.push_back("slash"); break;
            case '.': keys.push_back("dot"); break;
            case ',': keys.push_back("comma"); break;
            case '-': keys.push_back("minus"); break;
            case ';': keys.push_back("semicolon"); break;
            case '=': keys.push_back("equal"); break;
            case '"': keys.push_back("shift-apostrophe"); break;
            case '\'': keys.push_back("apostrophe"); break;
            case '#': keys.push_back("shift-3"); break;
            case '(': keys.push_back("shift-9"); break;
            case ')': keys.push_back("shift-0"); break;
            case ':': keys.push_back("shift-semicolon"); break;
            case '_': keys.push_back("shift-minus"); break;
            case '*': keys.push_back("shift-8"); break;
            case '\n': keys.push_back("ret"); break;
            default: break;
        }
    }
    return keys;
}

/** The framebuffer as 8-bit grey, from QEMU's screendump. Empty on failure. */
struct Frame { int w = 0, h = 0; std::vector<uint8_t> grey; };
Frame grab() {
    const std::string path = (fs::temp_directory_path() / "unifico-templeos.ppm").string();
    std::error_code ec;
    fs::remove(path, ec);
    std::string quoted;
    for (char c : path) { if (c == '"' || c == '\\') quoted += '\\'; quoted += c; }
    if (!monitor({"screendump \"" + quoted + "\""}, 50)) return {};
    // Written asynchronously: wait until the file stops growing.
    uintmax_t last = 0;
    for (int i = 0; i < 40; ++i) {
        napMs(150);
        const uintmax_t n = fs::exists(path, ec) ? fs::file_size(path, ec) : 0;
        if (n > 0 && n == last) break;
        last = n;
    }
    std::string d;
    if (!ufs::readFile(path, d) || d.size() < 16 || d[0] != 'P' || d[1] != '6') return {};
    size_t p = 2;
    auto num = [&]() {
        while (p < d.size() && (isspace((unsigned char)d[p]) || d[p] == '#')) {
            if (d[p] == '#') while (p < d.size() && d[p] != '\n') ++p;
            else ++p;
        }
        int v = 0;
        while (p < d.size() && isdigit((unsigned char)d[p])) v = v * 10 + (d[p++] - '0');
        return v;
    };
    Frame f;
    f.w = num(); f.h = num();
    num();
    ++p;
    if (f.w <= 0 || f.h <= 0 || d.size() < p + (size_t)f.w * f.h * 3) return {};
    f.grey.resize((size_t)f.w * f.h);
    for (size_t i = 0; i < f.grey.size(); ++i) {
        const uint8_t* px = (const uint8_t*)&d[p + i * 3];
        f.grey[i] = (uint8_t)((px[0] * 3 + px[1] * 6 + px[2]) / 10);
    }
    return f;
}

/** Pixels that differ noticeably, ignoring the title bar's ticking clock. */
int changed(const Frame& a, const Frame& b) {
    if (a.w != b.w || a.h != b.h || a.grey.empty()) return 1 << 30;
    int n = 0;
    for (int y = 16; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x) {
            const int i = y * a.w + x;
            if (std::abs((int)a.grey[i] - (int)b.grey[i]) > 24) ++n;
        }
    return n;
}

/**
 * Wait until the guest stops doing things: the screen holding still for three
 * samples in a row. vm.sh learnt this the hard way -- a key sent while the
 * machine is still booting or compiling lands in the wrong prompt.
 */
bool settle(Job& job, int maxSec, int minSec) {
    for (int i = 0; i < minSec * 10; ++i) { if (job.cancel) return false; napMs(100); }
    Frame prev;
    int stable = 0;
    const double until = (double)std::time(nullptr) + maxSec;
    while ((double)std::time(nullptr) < until) {
        if (job.cancel) return false;
        Frame f = grab();
        if (f.grey.empty()) return false;          // the emulator went away
        if (!prev.grey.empty()) {
            stable = changed(prev, f) < 600 ? stable + 1 : 0;
            if (stable >= 3) return true;
        }
        prev = std::move(f);
        napMs(2000);
    }
    return true;   // a slow machine: carry on rather than give up
}
}  // namespace

bool sendKeys(const std::vector<std::string>& keys) {
    std::vector<std::string> lines;
    for (auto& k : keys) lines.push_back("sendkey " + k);
    return monitor(lines, 80);
}

bool typeText(const std::string& text) { return sendKeys(keysFor(text)); }

bool screenshot(const std::string& ppmPath) {
    std::string quoted;
    for (char c : ppmPath) { if (c == '"' || c == '\\') quoted += '\\'; quoted += c; }
    if (!monitor({"screendump \"" + quoted + "\""}, 50)) return false;
    std::error_code ec;
    for (int i = 0; i < 40 && !fs::exists(ppmPath, ec); ++i) napMs(150);
    napMs(500);
    return fs::exists(ppmPath, ec);
}

JobPtr autoStart() {
    return ujobs::run("Starting TempleOS", [](Job& job) {
        struct Act { const char* status; int maxSec, minSec; std::vector<std::string> keys; std::string text; };
        // The exact sequence proven against TempleOS 5.03 under QEMU: two boot
        // questions, Mount's five prompts for the game disk, then the game.
        const Act acts[] = {
            {N_("Booting TempleOS..."), 300, 20, {"n"}, ""},
            {N_("Answering TempleOS's questions..."), 200, 6, {"n"}, ""},
            {N_("Answering TempleOS's questions..."), 120, 4, {"ret"}, ""},
            {N_("Mounting the game disk..."), 60, 3, {}, "Mount;\n"},
            {N_("Mounting the game disk..."), 60, 3, {}, "C\n"},
            {N_("Mounting the game disk..."), 60, 3, {"p"}, ""},
            {N_("Mounting the game disk..."), 60, 3, {}, "1\n"},
            {N_("Mounting the game disk..."), 60, 3, {"ret"}, ""},
            {N_("Opening the game disk..."), 30, 2, {}, "Cd(\"C:/\");\n"},
            // The shell holds an #include until the next statement arrives, so
            // both lines go together.
            {N_("Compiling Open Doctrines..."), 30, 2, {}, "#include \"ODGame\"\nODStart;\n"},
        };
        const int n = (int)(sizeof acts / sizeof acts[0]);
        for (int i = 0; i < n; ++i) {
            job.setStatus(acts[i].status);
            job.progress = (float)i / n;
            if (!settle(job, acts[i].maxSec, acts[i].minSec)) {
                job.fail(job.cancel ? N_("Stopped.") : N_("The emulator stopped answering."));
                return false;
            }
            const bool ok = acts[i].text.empty() ? sendKeys(acts[i].keys) : typeText(acts[i].text);
            if (!ok) { job.fail(N_("Could not reach the emulator's monitor.")); return false; }
        }
        job.setStatus(N_("Open Doctrines is starting in the emulator."));
        job.progress = 1;
        return true;
    });
}

std::vector<Step> steps() {
    return {
        {"When TempleOS asks to install onto the hard drive, answer no.", {"n"}, ""},
        {"When it offers the tour, answer no.", {"n"}, ""},
        {"Start mounting the game disk.", {}, "Mount;\n"},
        {"Give it drive letter C.", {}, "C\n"},
        {"Let it probe the hardware.", {"p"}, ""},
        {"Pick the hard drive (number 1).", {}, "1\n"},
        {"Finish mounting.", {"ret"}, ""},
        {"Go to the game disk.", {}, "Cd(\"C:/\");\n"},
        {"Compile and start Open Doctrines.", {}, "#include \"ODGame\"\nODStart;\n"},
    };
}
}  // namespace utemple
