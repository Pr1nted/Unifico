#include "tools/TempleOS.h"
#include "core/Fs.h"
#include "core/Http.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Zip.h"
#include "od/Releases.h"

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

namespace utemple {
namespace {
const int kMonitorPort = 45454;
std::string home() { return upaths::gamesDir() + "/templeos"; }
std::string isoPath() { return home() + "/TempleOS.ISO"; }
std::string payloadDir() { return home() + "/payload"; }
std::string payloadIso() { return home() + "/payload.iso"; }

// ---- ISO9660, the smallest correct one ----------------------------------
void both16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = v & 0xFF; b[at + 1] = v >> 8; b[at + 2] = v >> 8; b[at + 3] = v & 0xFF;
}
void both32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) { b[at + i] = (v >> (8 * i)) & 0xFF; b[at + 7 - i] = (v >> (8 * i)) & 0xFF; }
}
void str(std::vector<uint8_t>& b, size_t at, const std::string& s, size_t len) {
    for (size_t i = 0; i < len; ++i) b[at + i] = i < s.size() ? (uint8_t)s[i] : ' ';
}
std::string isoName(const std::string& in) {
    std::string out;
    for (char c : in) {
        char u = (char)toupper((unsigned char)c);
        out += (isalnum((unsigned char)u) || u == '.' || u == '_') ? u : '_';
    }
    return out.substr(0, 30);
}
std::vector<uint8_t> dirRecord(uint32_t lba, uint32_t size, bool dir, const std::string& id) {
    const size_t n = id.size();
    size_t len = 33 + n + ((n % 2 == 0) ? 1 : 0);
    std::vector<uint8_t> r(len, 0);
    r[0] = (uint8_t)len;
    both32(r, 2, lba);
    both32(r, 10, size);
    std::time_t t = std::time(nullptr);
    std::tm* g = std::gmtime(&t);
    r[18] = (uint8_t)g->tm_year; r[19] = (uint8_t)(g->tm_mon + 1); r[20] = (uint8_t)g->tm_mday;
    r[21] = (uint8_t)g->tm_hour; r[22] = (uint8_t)g->tm_min; r[23] = (uint8_t)g->tm_sec;
    r[25] = dir ? 2 : 0;
    both16(r, 28, 1);
    r[32] = (uint8_t)n;
    std::memcpy(&r[33], id.data(), n);
    return r;
}
}  // namespace

bool writeIso(const std::string& srcDir, const std::string& out, const std::string& volume, std::string* error) {
    // Layout: 16 system sectors, PVD (16), terminator (17), L path table (18),
    // M path table (19), root directory (20), one subdirectory (21..), files.
    struct F { std::string name; std::string path; uint32_t size; uint32_t lba; };
    std::vector<F> files;
    std::error_code ec;
    const std::string sub = isoName(fs::path(srcDir).filename().string().empty() ? "FILES" : fs::path(srcDir).filename().string());
    for (auto& e : fs::directory_iterator(srcDir, ec))
        if (e.is_regular_file(ec)) files.push_back({isoName(e.path().filename().string()), e.path().string(), (uint32_t)e.file_size(ec), 0});
    const uint32_t S = 2048;
    // The subdirectory's extent: ".", "..", then one record per file.
    size_t subBytes = 34 + 34;
    for (auto& f : files) subBytes += 33 + f.name.size() + 2 + ((f.name.size() + 2) % 2 == 0 ? 1 : 0);
    const uint32_t subSectors = (uint32_t)((subBytes + S - 1) / S);
    uint32_t lba = 21 + subSectors;
    for (auto& f : files) { f.lba = lba; lba += (f.size + S - 1) / S; if (f.size == 0) lba += 0; }
    const uint32_t total = lba;

    std::vector<uint8_t> img((size_t)total * S, 0);
    // PVD
    size_t p = 16 * S;
    img[p] = 1; std::memcpy(&img[p + 1], "CD001", 5); img[p + 6] = 1;
    { std::vector<uint8_t> v(img.begin(), img.end()); }
    auto put = [&](size_t at, const std::vector<uint8_t>& r) { std::memcpy(&img[at], r.data(), r.size()); };
    std::vector<uint8_t> tmp(8, 0);
    str(img, p + 8, "", 32);
    str(img, p + 40, isoName(volume), 32);
    { std::vector<uint8_t> b(8); both32(b, 0, total); std::memcpy(&img[p + 80], b.data(), 8); }
    { std::vector<uint8_t> b(4); both16(b, 0, 1); std::memcpy(&img[p + 120], b.data(), 4); std::memcpy(&img[p + 124], b.data(), 4); }
    { std::vector<uint8_t> b(4); both16(b, 0, (uint16_t)S); std::memcpy(&img[p + 128], b.data(), 4); }
    // Path table: root (dir 1) and the subdirectory (dir 2, parent 1).
    std::vector<uint8_t> pt;
    auto ptRec = [&](const std::string& id, uint32_t extent, uint16_t parent, bool bigEndian) {
        std::vector<uint8_t> r(8 + id.size() + (id.size() % 2), 0);
        r[0] = (uint8_t)id.size();
        for (int i = 0; i < 4; ++i) r[2 + i] = bigEndian ? (extent >> (24 - 8 * i)) & 0xFF : (extent >> (8 * i)) & 0xFF;
        r[6] = bigEndian ? parent >> 8 : parent & 0xFF;
        r[7] = bigEndian ? parent & 0xFF : parent >> 8;
        std::memcpy(&r[8], id.data(), id.size());
        return r;
    };
    std::vector<uint8_t> L, M;
    for (bool be : {false, true}) {
        auto& t = be ? M : L;
        auto a = ptRec(std::string(1, '\0'), 20, 1, be);
        auto b = ptRec(sub, 21, 1, be);
        t.insert(t.end(), a.begin(), a.end());
        t.insert(t.end(), b.begin(), b.end());
    }
    { std::vector<uint8_t> b(8); both32(b, 0, (uint32_t)L.size()); std::memcpy(&img[p + 132], b.data(), 8); }
    img[p + 140] = 18; // L table LBA, little-endian
    img[p + 148 + 3] = 19; // M table LBA, big-endian
    put(p + 156, dirRecord(20, S, true, std::string(1, '\0')));
    str(img, p + 190, "", 128); str(img, p + 318, "UNIFICO", 128); str(img, p + 446, "UNIFICO", 128); str(img, p + 574, "OPEN DOCTRINES", 128);
    str(img, p + 702, "", 37); str(img, p + 739, "", 37); str(img, p + 776, "", 37);
    for (int k = 0; k < 4; ++k) { str(img, p + 813 + k * 17, "0000000000000000", 16); img[p + 813 + k * 17 + 16] = 0; }
    img[p + 881] = 1;
    // Terminator
    img[17 * S] = 255; std::memcpy(&img[17 * S + 1], "CD001", 5); img[17 * S + 6] = 1;
    std::memcpy(&img[18 * S], L.data(), L.size());
    std::memcpy(&img[19 * S], M.data(), M.size());
    // Root directory
    size_t at = 20 * S;
    for (auto& r : {dirRecord(20, S, true, std::string(1, '\0')), dirRecord(20, S, true, std::string(1, '\1')),
                    dirRecord(21, subSectors * S, true, sub)}) { put(at, r); at += r.size(); }
    // Subdirectory: records never straddle a sector.
    at = 21 * S;
    for (auto& r : {dirRecord(21, subSectors * S, true, std::string(1, '\0')), dirRecord(20, S, true, std::string(1, '\1'))}) { put(at, r); at += r.size(); }
    for (auto& f : files) {
        auto r = dirRecord(f.lba, f.size, false, f.name + ";1");
        if ((at % S) + r.size() > S) at = (at / S + 1) * S;
        put(at, r);
        at += r.size();
        std::ifstream in(f.path, std::ios::binary);
        in.read((char*)&img[(size_t)f.lba * S], f.size);
    }
    std::ofstream o(out, std::ios::binary | std::ios::trunc);
    o.write((const char*)img.data(), (std::streamsize)img.size());
    if (!o && error) *error = "could not write " + out;
    return (bool)o;
}

std::string qemu() {
    std::string q = uproc::which("qemu-system-x86_64");
#if defined(_WIN32)
    if (q.empty() && ufs::exists("C:\\Program Files\\qemu\\qemu-system-x86_64.exe")) q = "C:\\Program Files\\qemu\\qemu-system-x86_64.exe";
#endif
    return q;
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

bool ready() { return ufs::exists(isoPath()) && ufs::exists(payloadIso()); }

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
        // The release is templeos/<files>; the CD carries that one folder.
        std::string src = home() + "/unpacked/templeos";
        if (!ufs::isDir(src)) src = home() + "/unpacked";
        fs::rename(src, payloadDir(), ec);
        ufs::removeAll(home() + "/unpacked");
        job.setStatus("Building the game CD...");
        fs::remove(payloadIso(), ec);
        bool made = false;
#if defined(__APPLE__)
        made = uproc::runQuiet("/usr/bin/hdiutil", {"makehybrid", "-iso", "-joliet", "-default-volume-name", "ODPAYLOAD",
                                                    "-o", payloadIso(), payloadDir()}) == 0;
#else
        for (const char* tool : {"xorriso", "mkisofs", "genisoimage"}) {
            std::string t = uproc::which(tool);
            if (t.empty()) continue;
            std::vector<std::string> args = std::string(tool) == "xorriso"
                ? std::vector<std::string>{"-as", "mkisofs", "-J", "-V", "ODPAYLOAD", "-o", payloadIso(), payloadDir()}
                : std::vector<std::string>{"-J", "-V", "ODPAYLOAD", "-o", payloadIso(), payloadDir()};
            if (uproc::runQuiet(t, args) == 0) { made = true; break; }
        }
#endif
        if (!made && !writeIso(payloadDir(), payloadIso(), "ODPAYLOAD", &err)) { job.fail(err); return false; }
        return true;
    });
}

std::unique_ptr<uproc::Child> boot(std::string* error) {
    const std::string q = qemu();
    if (q.empty()) { if (error) *error = qemuHelp(); return nullptr; }
    uproc::Spec s;
    s.exe = q;
    s.args = {"-m", "1024", "-cdrom", isoPath(),
              "-drive", "file=" + payloadIso() + ",format=raw,if=ide,index=3,media=cdrom",
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

bool sendKeys(const std::vector<std::string>& keys) {
#if defined(_WIN32)
    static bool wsa = false;
    if (!wsa) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); wsa = true; }
#endif
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(kMonitorPort);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(fd, (sockaddr*)&a, sizeof a) != 0) {
#if defined(_WIN32)
        closesocket(fd);
#else
        close(fd);
#endif
        return false;
    }
    for (auto& k : keys) {
        std::string line = "sendkey " + k + "\n";
        send(fd, line.c_str(), (int)line.size(), 0);
#if defined(_WIN32)
        Sleep(80);
#else
        usleep(80000);
#endif
    }
#if defined(_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
    return true;
}

bool typeText(const std::string& text) {
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
    return sendKeys(keys);
}

std::vector<Step> steps(const std::string& drive) {
    const std::string d = drive.empty() ? "T" : drive.substr(0, 1);
    return {
        {"When TempleOS asks to install onto the hard drive, answer no.", {"n"}, ""},
        {"When it offers the tour, answer no.", {"n"}, ""},
        {"Go to the game's CD.", {}, "Cd(\"" + d + ":/TEMPLEOS\");\n"},
        {"Compile and start Open Doctrines.", {}, "#include \"ODGame\"\nODStart;\n"},
    };
}
}  // namespace utemple
