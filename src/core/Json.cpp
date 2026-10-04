#include "core/Json.h"
#include "core/Fs.h"

namespace ujson {
json parse(const std::string& text) { return json::parse(text, nullptr, false); }
json load(const std::string& path) {
    std::string t;
    if (!ufs::readFile(path, t)) return json::object();
    json j = parse(t);
    return j.is_discarded() ? json::object() : j;
}
bool save(const std::string& path, const json& j) { return ufs::writeFileAtomic(path, j.dump(2)); }
std::string str(const json& j, const char* key, const std::string& def) {
    if (!j.is_object()) return def;
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : def;
}
double num(const json& j, const char* key, double def) {
    if (!j.is_object()) return def;
    auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : def;
}
bool flag(const json& j, const char* key, bool def) {
    if (!j.is_object()) return def;
    auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : def;
}
}  // namespace ujson
