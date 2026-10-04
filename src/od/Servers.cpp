#include "od/Servers.h"
#include "core/Json.h"

#include <algorithm>

namespace uservers {
std::vector<ServerEntry> load(const std::string& dataDir) {
    std::vector<ServerEntry> out;
    json j = ujson::load(dataDir + "/servers.json");
    const json* arr = nullptr;
    if (j.is_array()) arr = &j;
    else if (j.is_object() && j.contains("servers") && j["servers"].is_array()) arr = &j["servers"];
    if (!arr) return out;
    for (auto& s : *arr) {
        ServerEntry e;
        e.name = ujson::str(s, "name");
        e.issuer = ujson::str(s, "issuer");
        e.code = ujson::str(s, "code");
        e.lastHostName = ujson::str(s, "lastHostName");
        e.address = ujson::str(s, "address");
        e.lastJoined = (long long)ujson::num(s, "lastJoined");
        if (!e.name.empty()) out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const ServerEntry& a, const ServerEntry& b) { return a.lastJoined > b.lastJoined; });
    return out;
}

std::string joinUrl(const ServerEntry& s) {
    return s.code.empty() ? std::string() : "opendoctrines://join/" + s.code;
}
}  // namespace uservers
