#pragma once
// Bug reports and suggestions, through the same service and the same form the
// game's "Report a problem" uses (Open Doctrines net/src/feedback/report.ts).
// Both are published -- a bug becomes a public issue -- and signed with the
// account's nickname, so they need a sign-in; the byline comes from the
// session token on the service's side, never from anything sent here.
#include "core/Jobs.h"
#include <string>

namespace ufeedback {
/** The service's categories, in its order. */
extern const char* const kCategories[8];
const char* categoryLabel(int i);   // English; translated where drawn
/**
 * `diagnostics` is optional text the person chose to attach (the launcher log
 * and the game console). Capped at the service's limit.
 */
JobPtr send(bool bug, int category, const std::string& title, const std::string& body, const std::string& diagnostics);
}
