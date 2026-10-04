#pragma once
// Where everything lives.
//
//   home()          the launcher's own data: settings, account, cache, logs
//     versions/<v>/ one installed game version each, unpacked exactly as its
//                   release zip is (binary beside data/)
//     games/        the other games, when unlocked: unciv/, gd5/, gd4/, templeos/
//     cache/        release lists, news, downloaded fonts, update staging
//     logs/         one file per game launch, and the launcher's own
//
//   macOS    ~/Library/Application Support/Unifico
//   Windows  %APPDATA%\Unifico
//   Linux/BSD $XDG_DATA_HOME/unifico, else ~/.local/share/unifico
//
// UNIFICO_HOME overrides it, which is how the tests and a portable install
// (a USB stick) keep everything beside them.
#include <string>

namespace upaths {
std::string home();
std::string versionsDir();
std::string gamesDir();
std::string cacheDir();
std::string logsDir();
std::string downloadsDir();     // the user's Downloads folder, for "save log"
std::string userHome();

/** The running executable. */
std::string selfExe();
/**
 * What an update replaces: on macOS the enclosing Unifico.app, elsewhere the
 * executable itself.
 */
std::string selfInstallPath();

/** This machine as release asset names spell it: "macos-arm64", "windows-x64", ... */
std::string platformTag();
/** "macos", "windows", "linux", "freebsd", "openbsd". */
std::string osName();
}  // namespace upaths
