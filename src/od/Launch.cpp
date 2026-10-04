#include "od/Launch.h"
#include "od/Account.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Settings.h"

#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

namespace ulaunch {
std::vector<std::string> splitArgs(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool inQ = false, any = false;
    char q = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inQ) {
            if (c == q) inQ = false;
            else if (c == '\\' && q == '"' && i + 1 < s.size()) cur += s[++i];
            else cur += c;
        } else if (c == '"' || c == '\'') { inQ = true; q = c; any = true; }
        else if (c == '\\' && i + 1 < s.size()) { cur += s[++i]; any = true; }
        else if (c == ' ' || c == '\t' || c == '\n') { if (any || !cur.empty()) out.push_back(cur); cur.clear(); any = false; }
        else { cur += c; any = true; }
    }
    if (any || !cur.empty()) out.push_back(cur);
    return out;
}

std::unique_ptr<Running> start(const Install& i, const std::string& extraArg, std::string* error) {
    const LaunchOptions& lo = Settings::get().optionsFor(i.tag);
    uproc::Spec spec;
    spec.exe = i.exe;
    spec.args = splitArgs(lo.args);
    if (!extraArg.empty()) spec.args.push_back(extraArg);
    spec.cwd = fs::path(i.exe).parent_path().string();
    spec.memoryLimitMB = lo.memoryLimitMB;
    // How the game knows who started it: the self-updater stands down, and
    // the "Proper Channels" achievement counts. See GameUpdates::managedInstall.
    spec.env.push_back({"OD_LAUNCHER", "unifico"});
    size_t start = 0;
    while (start < lo.env.size()) {
        size_t nl = lo.env.find('\n', start);
        std::string line = lo.env.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        size_t eq = line.find('=');
        if (eq != std::string::npos && eq > 0) spec.env.push_back({line.substr(0, eq), line.substr(eq + 1)});
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    char stamp[32];
    std::time_t t = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::localtime(&t));
    auto run = std::make_unique<Running>();
    run->install = i;
    run->logPath = upaths::logsDir() + "/" + (i.external ? std::string("external") : i.tag) + "-" + stamp + ".log";
    spec.logPath = Settings::get().keepLogs ? run->logPath : std::string();
    Account::get().handTo(i.dataDir);
    run->child = std::make_unique<uproc::Child>();
    ulog::info("launching " + i.exe);
    if (!run->child->start(spec, error)) return nullptr;
    return run;
}
}  // namespace ulaunch
