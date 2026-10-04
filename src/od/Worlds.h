#pragma once
// Worlds (.odsv saves) of one installation, managed from outside the game.
//
// Every operation is on files the game itself reads and writes, so nothing
// here needs the game running -- and nothing here may run WHILE the game runs
// against the same data/, which the Worlds page enforces by locking itself.
#include <cstdint>
#include <string>
#include <vector>

struct World {
    std::string path;        // .../data/saves/<file>.odsv
    std::string file;        // "<file>.odsv"
    std::string name;        // save_name from metadata.json, else the file stem
    std::string version;     // game version that wrote it
    std::string lastPlayed;
    int turns = 0;
    uint64_t size = 0;
    bool multiplayer = false;
};

namespace uworlds {
std::vector<World> list(const std::string& dataDir);
bool rename(const World& w, const std::string& newName, std::string* error);
bool duplicate(const World& w, std::string* error);
bool remove(const World& w, std::string* error);
/** Copy a world into another installation's data/. */
bool copyTo(const World& w, const std::string& otherDataDir, std::string* error);
bool importFile(const std::string& odsv, const std::string& dataDir, std::string* error);
bool exportTo(const World& w, const std::string& destPath, std::string* error);

/**
 * The whole player state of an installation as a .odstate, by the game's own
 * rules (src/OdState.cpp): everything under data/ except what the build ships.
 */
bool exportState(const std::string& dataDir, const std::string& outPath, int* files, std::string* error);
/**
 * Restore a .odstate into an installation, with the game's achievement rule:
 * grants are only ever OFFERED (achievements/grants.import.json), for the game
 * to verify, and an install's own progress is kept over the archive's.
 */
bool importState(const std::string& dataDir, const std::string& archive, int* files, std::string* error);
}  // namespace uworlds
