#pragma once
// The launcher's own log: a file in the launcher's home, and the last few
// hundred lines in memory for the console panel. Thread-safe.
#include <string>
#include <vector>

namespace ulog {
void init(const std::string& path);
void info(const std::string& line);
void warn(const std::string& line);
void error(const std::string& line);
std::vector<std::string> tail(size_t n);
std::string path();
}  // namespace ulog
