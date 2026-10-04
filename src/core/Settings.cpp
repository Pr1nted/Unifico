#include "core/Settings.h"
#include "core/Paths.h"

Settings& Settings::get() {
    static Settings s;
    return s;
}

void Settings::load() {
    json j = ujson::load(upaths::home() + "/settings.json");
    language = ujson::str(j, "language");
    hideWhileRunning = ujson::flag(j, "hideWhileRunning", true);
    closeOnLaunch = ujson::flag(j, "closeOnLaunch", false);
    showConsoleOnLaunch = ujson::flag(j, "showConsoleOnLaunch", false);
    keepLogs = ujson::flag(j, "keepLogs", true);
    selectedVersion = ujson::str(j, "selectedVersion");
    showPrereleases = ujson::flag(j, "showPrereleases", true);
    otherGamesUnlocked = ujson::flag(j, "otherGamesUnlocked", false);
    autoUpdateLauncher = ujson::flag(j, "autoUpdateLauncher", true);
    analyticsConsent = ujson::str(j, "analyticsConsent");
    analyticsClientId = ujson::str(j, "analyticsClientId");
    uiScale = ujson::str(j, "uiScale", "auto");
    accent = (int)ujson::num(j, "accent", 0xC9A227) & 0xFFFFFF;
    accentFromGame = ujson::flag(j, "accentFromGame", false);
    fullscreen = ujson::flag(j, "fullscreen", false);
    launch.clear();
    if (j.contains("launch") && j["launch"].is_object()) {
        for (auto& [tag, o] : j["launch"].items()) {
            LaunchOptions lo;
            lo.args = ujson::str(o, "args");
            lo.env = ujson::str(o, "env");
            lo.memoryLimitMB = (int)ujson::num(o, "memoryLimitMB", 0);
            lo.console = ujson::flag(o, "console", false);
            launch[tag] = lo;
        }
    }
}

void Settings::save() const {
    // Merged into what is on disk, so a key this version does not know (written
    // by a newer launcher, or by the updater) survives a save by this one.
    json j = ujson::load(upaths::home() + "/settings.json");
    if (!j.is_object()) j = json::object();
    j["language"] = language;
    j["hideWhileRunning"] = hideWhileRunning;
    j["closeOnLaunch"] = closeOnLaunch;
    j["showConsoleOnLaunch"] = showConsoleOnLaunch;
    j["keepLogs"] = keepLogs;
    j["selectedVersion"] = selectedVersion;
    j["showPrereleases"] = showPrereleases;
    j["otherGamesUnlocked"] = otherGamesUnlocked;
    j["autoUpdateLauncher"] = autoUpdateLauncher;
    j["analyticsConsent"] = analyticsConsent;
    if (analyticsConsent == "yes") j["analyticsClientId"] = analyticsClientId;
    else j.erase("analyticsClientId");
    j["uiScale"] = uiScale;
    j["accent"] = accent;
    j["accentFromGame"] = accentFromGame;
    j["fullscreen"] = fullscreen;
    j["launch"] = json::object();
    for (auto& [tag, lo] : launch)
        j["launch"][tag] = {{"args", lo.args}, {"env", lo.env}, {"memoryLimitMB", lo.memoryLimitMB}, {"console", lo.console}};
    ujson::save(upaths::home() + "/settings.json", j);
}
