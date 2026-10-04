#include "od/GameConfig.h"
#include "core/Fs.h"

#include <algorithm>
#include <map>

namespace uconfig {
json load(const std::string& dataDir) { return ujson::load(dataDir + "/config.json"); }

bool save(const std::string& dataDir, const json& j) {
    // A backup of the version the game last wrote, once per launcher session,
    // so a bad edit is one rename away from undone.
    static bool backedUp = false;
    std::string old;
    if (!backedUp && ufs::readFile(dataDir + "/config.json", old)) {
        ufs::writeFileAtomic(dataDir + "/config.json.unifico-backup", old);
        backedUp = true;
    }
    return ujson::save(dataDir + "/config.json", j);
}

std::vector<ConfigField> fields(const json& j) {
    // English labels; the Settings page passes them through T().
    static const std::map<std::string, std::pair<const char*, const char*>> known = {
        {"fullscreen", {"Fullscreen", "Display"}}, {"vsync", {"Vertical sync", "Display"}},
        {"targetFps", {"Frame rate limit", "Display"}}, {"uiScale", {"Interface scale", "Display"}},
        {"resolutionIndex", {"Resolution", "Display"}}, {"musicVolume", {"Music volume", "Audio"}},
        {"sfxVolume", {"Sound effects volume", "Audio"}}, {"masterVolume", {"Master volume", "Audio"}},
        {"language", {"Language", "General"}}, {"aiDifficulty", {"AI difficulty", "Gameplay"}},
        {"skipViewingOrders", {"Skip the orders replay after a turn", "Gameplay"}},
        {"autosave", {"Autosave", "Gameplay"}}, {"debugMode", {"Developer mode", "Advanced"}},
        {"showConsole", {"Show the in-game console", "Advanced"}}, {"accountIssuer", {"Account service", "Advanced"}},
        {"flySpeed", {"Camera speed", "Controls"}}, {"maxZoom", {"Maximum zoom", "Controls"}},
        {"usageReports", {"Send anonymous session length", "Privacy"}},
    };
    std::vector<ConfigField> out;
    if (!j.is_object()) return out;
    for (auto& [k, v] : j.items()) {
        ConfigField f;
        f.key = k;
        if (v.is_boolean()) f.kind = ConfigField::Bool;
        else if (v.is_number()) f.kind = ConfigField::Number;
        else if (v.is_string()) f.kind = ConfigField::Text;
        else continue;   // nested objects and arrays are the game's to edit
        auto it = known.find(k);
        f.label = it != known.end() ? it->second.first : k;
        f.group = it != known.end() ? it->second.second : "Other";
        out.push_back(f);
    }
    std::stable_sort(out.begin(), out.end(), [](const ConfigField& a, const ConfigField& b) {
        if (a.group != b.group) return a.group == "Other" ? false : (b.group == "Other" ? true : a.group < b.group);
        return a.label < b.label;
    });
    return out;
}
}  // namespace uconfig
