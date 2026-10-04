#include "core/Fs.h"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace ufs {

bool readFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool writeFileAtomic(const std::string& path, const std::string& data) {
    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) { fs::remove(tmp, ec); return false; }
    fs::rename(tmp, path, ec);
    if (ec) {   // Windows will not rename over an existing file
        fs::remove(path, ec);
        ec.clear();
        fs::rename(tmp, path, ec);
    }
    return !ec;
}

uint64_t dirSize(const std::string& path) {
    std::error_code ec;
    uint64_t total = 0;
    if (!fs::exists(path, ec)) return 0;
    if (fs::is_regular_file(path, ec)) return fs::file_size(path, ec);
    for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_symlink(ec)) { it.disable_recursion_pending(); continue; }
        if (it->is_regular_file(ec)) total += it->file_size(ec);
    }
    return total;
}

uint64_t freeSpace(const std::string& path) {
    std::error_code ec;
    fs::path p(path);
    while (!p.empty() && !fs::exists(p, ec)) p = p.parent_path();
    if (p.empty()) p = ".";
    auto info = fs::space(p, ec);
    return ec ? 0 : (uint64_t)info.available;
}

bool removeAll(const std::string& path, std::string* err) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec && err) *err = ec.message();
    return !ec;
}

bool copyTree(const std::string& from, const std::string& to, std::string* err) {
    std::error_code ec;
    fs::create_directories(to, ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec && err) *err = ec.message();
    return !ec;
}

bool exists(const std::string& path) { std::error_code ec; return fs::exists(path, ec); }
bool isDir(const std::string& path) { std::error_code ec; return fs::is_directory(path, ec); }

std::vector<std::string> listDir(const std::string& path) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(path, ec)) out.push_back(e.path().filename().string());
    return out;
}

std::string humanBytes(uint64_t b) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)b;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[32];
    std::snprintf(buf, sizeof buf, u == 0 ? "%.0f %s" : (v < 10 ? "%.1f %s" : "%.0f %s"), v, units[u]);
    return buf;
}

std::string safeName(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' ||
            c == '?' || c == '*' || (unsigned char)c < 32)
            out += '_';
        else
            out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    // Names Windows reserves whatever the extension. A world could otherwise
    // be saveable on macOS and not on the machine it was copied to.
    static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
                                     "LPT1", "LPT2", "LPT3"};
    std::string up;
    for (char c : out.substr(0, out.find('.'))) up += (char)std::toupper((unsigned char)c);
    for (const char* r : reserved) if (up == r) { out = "_" + out; break; }
    return out.empty() ? "unnamed" : out;
}

std::string join(const std::string& a, const std::string& b) { return (fs::path(a) / b).string(); }
}  // namespace ufs
