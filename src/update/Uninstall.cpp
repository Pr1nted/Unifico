#include "update/Uninstall.h"
#include "core/Fs.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Process.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace uuninstall {
Plan plan(bool includeSelf) {
    Plan p;
    p.paths.push_back(upaths::home());
    if (includeSelf) p.paths.push_back(upaths::selfInstallPath());
    for (auto& x : p.paths) p.bytes += ufs::dirSize(x);
    return p;
}

bool run(const Plan& p, bool includeSelf, std::string* error) {
    const std::string self = upaths::selfInstallPath();
    for (auto& x : p.paths) {
        if (x == self) continue;   // last, and differently
        std::string e;
        if (!ufs::removeAll(x, &e)) { if (error) *error = x + ": " + e; return false; }
    }
    if (!includeSelf) return true;
#if defined(_WIN32)
    // A running .exe cannot delete itself. A short script outlives us: it
    // waits for this process to exit, removes the executable, then itself.
    const std::string bat = (fs::temp_directory_path() / "unifico-uninstall.cmd").string();
    std::ofstream f(bat);
    f << "@echo off\r\n:wait\r\ntasklist /FI \"PID eq " << uproc::selfPid() << "\" | find \"" << uproc::selfPid()
      << "\" >nul && (timeout /t 1 /nobreak >nul & goto wait)\r\n"
      << "del /f /q \"" << self << "\"\r\n"
      << "del /f /q \"%~f0\"\r\n";
    f.close();
    return uproc::spawnDetached("C:\\Windows\\System32\\cmd.exe", {"/c", bat});
#else
    // On macOS, Linux and the BSDs an open file can be unlinked; the process
    // keeps running from its mapped image and the space is freed on exit.
    std::string e;
    if (!ufs::removeAll(self, &e)) { if (error) *error = e; return false; }
    ulog::info("removed " + self);
    return true;
#endif
}
}  // namespace uuninstall
