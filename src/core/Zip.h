#pragma once
// Zip archives, through miniz. Every extraction refuses absolute paths and ".."
// -- an archive is downloaded or picked by the user, so it is untrusted input.
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace uzip {
/**
 * Unpack `zip` into `dest`. When `stripSingleRoot` is set and every entry
 * shares one top-level folder, that folder is removed (release zips are
 * "OpenDoctrines-macos-arm64/..."). Unix permission bits are restored, so an
 * extracted binary stays executable.
 */
bool extract(const std::string& zip, const std::string& dest, bool stripSingleRoot,
             const std::function<void(int, int)>& progress, std::string* error);
/** Pack a directory (or a single file) into a new zip. */
bool create(const std::string& source, const std::string& zip, std::string* error);
/** Read one member into memory. */
bool readMember(const std::string& zip, const std::string& name, std::string& out);
}  // namespace uzip
