#pragma once
// The system's own file chooser. Modal and blocking, which is what a chooser
// is. Empty string when the person cancels or no chooser exists.
//
//   macOS    osascript (AppleScript's choose file / choose folder)
//   Windows  the common dialog (GetOpenFileName / IFileDialog for folders)
//   Linux/BSD zenity, then kdialog
#include <string>

namespace udialog {
std::string openFile(const std::string& title, const std::string& extension = {});
std::string openFolder(const std::string& title);
std::string saveFile(const std::string& title, const std::string& defaultName);
}
