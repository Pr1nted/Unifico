#pragma once
// Removing everything: every installed version, the other games, the
// launcher's data and cache -- and the launcher itself.
//
// What is NOT touched: installations the launcher adopted rather than made
// (it never deletes what it did not install), and anything the person chose to
// keep (worlds can be exported to a folder first).
#include <string>
#include <vector>

namespace uuninstall {
struct Plan {
    std::vector<std::string> paths;   // what will be deleted
    uint64_t bytes = 0;
};
Plan plan(bool includeSelf);
/** Delete it all. When includeSelf, schedules the launcher's own removal and the caller must quit. */
bool run(const Plan& p, bool includeSelf, std::string* error);
}
