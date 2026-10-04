#pragma once
// The map translator: open-dragoman, linked in. Converts an Open Doctrines
// .odmap to a Greater Diplomacy 5 map folder or a Unciv map file, and either of
// those back to .odmap. The same library, at the same version, the game uses
// for its own Translate button.
#include "core/Jobs.h"
#include <string>
#include <vector>

namespace utranslate {
bool available();
std::string version();
enum class Target { OdMap, Gd5, Unciv };
/** What a path holds: "Open Doctrines map", "Greater Diplomacy 5 map", ... or empty. */
std::string detect(const std::string& path);
JobPtr convert(const std::string& in, const std::string& out, Target to, int uncivCols = 0, int uncivRows = 0);
/** The notes from the last finished conversion, worst first. */
std::vector<std::string> lastNotes();
}
