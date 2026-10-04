#pragma once
// Launcher statistics. NOTHING is sent unless Settings::analyticsConsent is
// "yes"; the first run asks, and the default is no. See Open Doctrines'
// net/PRIVACY.md, "Launcher statistics", and net/src/analytics/ga.ts for the
// fixed list of events and values the service will forward.
#include <map>
#include <string>

namespace uanalytics {
void init(const std::string& issuer);
/** Queue an event; dropped immediately when consent is not "yes". */
void event(const std::string& name, const std::map<std::string, std::string>& params = {});
/** Record the answer. "yes" creates the random client id; "no" deletes it. */
void setConsent(bool yes);
/** Send what is queued, in the background. Called every few seconds. */
void flush();
std::string clientId();
}
