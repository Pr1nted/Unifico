#pragma once
// Small filesystem helpers the rest of the launcher shares.
#include <cstdint>
#include <string>
#include <vector>

namespace ufs {
bool readFile(const std::string& path, std::string& out);
/** Write-then-rename: a crash leaves the old file, never half a new one. */
bool writeFileAtomic(const std::string& path, const std::string& data);
/** Bytes under a directory, following no symlinks. */
uint64_t dirSize(const std::string& path);
/** Free bytes on the volume holding `path` (or its nearest existing parent). */
uint64_t freeSpace(const std::string& path);
bool removeAll(const std::string& path, std::string* err = nullptr);
bool copyTree(const std::string& from, const std::string& to, std::string* err = nullptr);
bool exists(const std::string& path);
bool isDir(const std::string& path);
std::vector<std::string> listDir(const std::string& path);   // names only
/** "1.4 GB" */
std::string humanBytes(uint64_t bytes);
/** A name safe on every filesystem the launcher runs on (Windows is the strictest). */
std::string safeName(const std::string& in);
std::string join(const std::string& a, const std::string& b);
}  // namespace ufs
