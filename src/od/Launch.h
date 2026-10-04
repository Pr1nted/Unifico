#pragma once
// Starting a game version: options, environment, memory ceiling, account
// hand-over, console capture and the per-launch log.
#include "core/Process.h"
#include "od/Installs.h"
#include <memory>
#include <string>

namespace ulaunch {
struct Running {
    Install install;
    std::unique_ptr<uproc::Child> child;
    std::string logPath;
    double startedAt = 0;
};
/** `extraArg` is a save path or an opendoctrines:// link; may be empty. */
std::unique_ptr<Running> start(const Install& i, const std::string& extraArg, std::string* error);
/** Split launch options the way a shell would (quotes, backslashes). */
std::vector<std::string> splitArgs(const std::string& s);
}
