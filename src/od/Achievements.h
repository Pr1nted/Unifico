#pragma once
// The achievement collection, as the launcher shows it.
//
// Only SIGNED GRANTS are shown as earned, and every one is verified here
// against the public keys built into the launcher -- the same keys and the
// same code (od/GrantVerify.cpp) the game uses. Grants come from the account
// service (/achievements/mine) and, offline, from every installation's
// data/achievements/grants.json; neither source is trusted until it verifies.
//
// Two achievements happen in the launcher itself (the map translator, running
// TempleOS); the launcher claims those the way the game claims the rest.
#include "gen/catalog.gen.h"
#include <string>
#include <vector>

struct AchView {
    const odach::Def* def = nullptr;
    bool granted = false;
    bool modded = false;
    long long when = 0;
    bool earned = false;     // the game saw it; not yet confirmed
    double progress = 0;     // best counter value across installations
};

namespace uach {
void refresh(const std::vector<std::string>& dataDirs);
std::vector<AchView> view();
int grantedCount();
bool keysBaked();
std::string status();      // empty, or a sentence (English; the page translates)
/** An achievement earned in the launcher: claimed now, or queued until signed in. */
void claim(const std::string& id);
void retryPending();
}
