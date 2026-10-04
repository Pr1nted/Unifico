#pragma once
// nlohmann::json, and the three ways the launcher reads it without throwing.
#include "json.hpp"
#include <string>

using json = nlohmann::json;

namespace ujson {
/** Parse; a discarded value (is_discarded()) on any error. Never throws. */
json parse(const std::string& text);
/** Read a file and parse it; an empty object if absent or malformed. */
json load(const std::string& path);
bool save(const std::string& path, const json& j);
std::string str(const json& j, const char* key, const std::string& def = {});
double num(const json& j, const char* key, double def = 0);
bool flag(const json& j, const char* key, bool def = false);
}  // namespace ujson
