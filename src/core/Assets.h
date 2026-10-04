#pragma once
// Files compiled into the binary by tools/embed.py: "fonts/ss3-400.ttf",
// "art/hero-0.jpg", "lang/de.json", ... Decompressed on first use and cached.
#include <string>
#include <vector>

namespace uassets {
const std::string* get(const std::string& path);    // nullptr if absent
std::vector<std::string> list(const std::string& prefix);
}
