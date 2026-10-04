#pragma once
// An installation's data/config.json, edited in place.
//
// The launcher does not hard-code the game's settings: it reads whatever keys
// the file holds and offers an editor matched to each value's type -- a switch
// for a boolean, a number field, a text field -- so a setting added to the game
// next month appears here without a launcher release. Known keys get a
// friendlier label; unknown ones show their own name.
#include "core/Json.h"
#include <string>
#include <vector>

struct ConfigField {
    std::string key;
    std::string label;      // translated when known, else the key
    std::string group;      // "Display", "Gameplay", ...
    enum Kind { Bool, Number, Text } kind = Text;
};

namespace uconfig {
json load(const std::string& dataDir);
bool save(const std::string& dataDir, const json& j);
std::vector<ConfigField> fields(const json& j);
}
