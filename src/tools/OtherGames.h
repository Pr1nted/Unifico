#pragma once
// The hidden shelf: other open strategy games the launcher can fetch and start.
//
// Hidden on purpose. Unifico is Open Doctrines' launcher; these are neighbours,
// not the point. The shelf appears only after the person finds the switch
// (Settings -> About, click the version seven times), and every download says
// what it is and where it comes from before it starts.
//
//   Unciv                 yairm210/Unciv's own release builds (MPL-2.0). Windows
//                         and Linux builds bundle Java; elsewhere the .jar runs
//                         on a Java the system has.
//   Greater Diplomacy 5   GitGetGot415/Greater-Diplomacy-5 from source (GPL-3.0),
//                         in its own Python virtual environment.
//   Greater Diplomacy 4   a browser game on itch.io; the launcher opens its page.
#include "core/Jobs.h"
#include "core/Process.h"
#include <memory>
#include <string>

namespace uother {
enum class Game { Unciv, Gd5, Gd4 };
const char* name(Game g);
const char* licence(Game g);
const char* source(Game g);
std::string dir(Game g);
bool installed(Game g);
/** What is missing on this system before it can run ("Java 11 or newer", "Python 3.10+"), or empty. */
std::string prerequisite(Game g);
JobPtr install(Game g);
bool uninstall(Game g, std::string* error);
/** Start it. GD4 opens a browser and returns nullptr with no error. */
std::unique_ptr<uproc::Child> launch(Game g, std::string* error);
/** Where a translated map for this game should be written. */
std::string mapsDir(Game g);
}
