#pragma once
// Hours played, the way Steam counts them: time each game was running.
//
// Two sources, because a person may start the game without the launcher:
//   - the launcher times every session it starts, per game and per version
//     (<home>/playtime.json);
//   - Open Doctrines keeps its own counter, seconds_played, in each
//     installation's data/achievements/progress.json, whoever started it.
// For Open Doctrines the larger of the two totals is shown -- they overlap for
// sessions the launcher started, so adding them would count those twice.
#include <string>
#include <vector>

namespace uplay {
/** Record a finished session. `key`: "od:<tag>", "unciv", "gd5", "gd4", "templeos". */
void add(const std::string& key, double seconds);
double seconds(const std::string& key);
long long lastPlayed(const std::string& key);
/** Open Doctrines across every version and both sources. */
double openDoctrines(const std::vector<std::string>& dataDirs);
long long openDoctrinesLast();
/** "12.4 hours", "35 minutes", "never". */
std::string human(double seconds);
}
