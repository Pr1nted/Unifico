#include "core/Paths.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#  include <shlobj.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#  include <climits>
#else
#  include <climits>
#  include <unistd.h>
#  if defined(__FreeBSD__) || defined(__OpenBSD__)
#    include <sys/types.h>
#    include <sys/sysctl.h>
#  endif
#endif

namespace fs = std::filesystem;

namespace upaths {
namespace {
std::string ensure(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    return p.string();
}
}  // namespace

std::string userHome() {
#if defined(_WIN32)
    if (const char* h = std::getenv("USERPROFILE")) return h;
    return "C:\\";
#else
    if (const char* h = std::getenv("HOME")) return h;
    return "/tmp";
#endif
}

std::string home() {
    static std::string cached;
    if (!cached.empty()) return cached;
    if (const char* o = std::getenv("UNIFICO_HOME"); o && *o) return cached = ensure(o);
    // Portable mode: a folder called UnificoData beside the launcher (a USB
    // stick, a school machine) holds everything instead of the user profile.
    {
        std::error_code ec;
        fs::path beside = fs::path(selfInstallPath()).parent_path() / "UnificoData";
        if (fs::is_directory(beside, ec)) return cached = beside.string();
    }
#if defined(_WIN32)
    if (const char* a = std::getenv("APPDATA")) return cached = ensure(fs::path(a) / "Unifico");
    return cached = ensure(fs::path(userHome()) / "Unifico");
#elif defined(__APPLE__)
    return cached = ensure(fs::path(userHome()) / "Library" / "Application Support" / "Unifico");
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return cached = ensure(fs::path(x) / "unifico");
    return cached = ensure(fs::path(userHome()) / ".local" / "share" / "unifico");
#endif
}

std::string versionsDir() { return ensure(fs::path(home()) / "versions"); }
std::string gamesDir() { return ensure(fs::path(home()) / "games"); }
std::string cacheDir() { return ensure(fs::path(home()) / "cache"); }
std::string logsDir() { return ensure(fs::path(home()) / "logs"); }

std::string downloadsDir() {
    fs::path d = fs::path(userHome()) / "Downloads";
    std::error_code ec;
    if (fs::is_directory(d, ec)) return d.string();
    return userHome();
}

std::string selfExe() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    return fs::path(std::wstring(buf, n)).string();
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    std::error_code ec;
    return fs::canonical(buf, ec).string();
#elif defined(__FreeBSD__)
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};
    char buf[PATH_MAX];
    size_t len = sizeof buf;
    if (sysctl(mib, 4, buf, &len, nullptr, 0) == 0) return buf;
    return {};
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) { buf[n] = 0; return buf; }
    if (const char* a = std::getenv("UNIFICO_SELF")) return a;   // OpenBSD has no /proc
    return {};
#endif
}

std::string selfInstallPath() {
    const std::string exe = selfExe();
#if defined(__APPLE__)
    // .../Unifico.app/Contents/MacOS/Unifico -> .../Unifico.app
    fs::path p(exe);
    for (int i = 0; i < 3 && p.has_parent_path(); ++i) p = p.parent_path();
    if (p.extension() == ".app") return p.string();
#endif
    return exe;
}

std::string osName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#else
    return "linux";
#endif
}

std::string platformTag() {
#if defined(__aarch64__) || defined(_M_ARM64)
    const char* arch = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    const char* arch = "x64";
#elif defined(__i386__) || defined(_M_IX86)
    const char* arch = "x86";
#elif defined(__arm__)
    const char* arch = "armv7";
#elif defined(__riscv)
    const char* arch = "riscv64";
#else
    const char* arch = "unknown";
#endif
    return osName() + "-" + arch;
}
}  // namespace upaths
