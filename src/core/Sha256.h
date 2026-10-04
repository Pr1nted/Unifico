#pragma once
// SHA-256, for checking downloads against the digest a release publishes.
#include <cstdint>
#include <string>

namespace usha {
std::string hexOfFile(const std::string& path);   // empty if unreadable
std::string hexOf(const std::string& data);
}
