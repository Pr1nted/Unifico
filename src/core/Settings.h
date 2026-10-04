#pragma once
// The launcher's own preferences, in <home>/settings.json.
#include "core/Json.h"
#include <map>
#include <string>

struct LaunchOptions {
    std::string args;            // extra command-line arguments, shell-split
    std::string env;             // KEY=VALUE per line
    int memoryLimitMB = 0;       // 0 = no ceiling
    bool console = false;        // open the console panel when this version starts
};

struct Settings {
    std::string language;                  // empty = follow the system
    bool hideWhileRunning = true;          // hide the window while the game runs
    bool closeOnLaunch = false;            // quit the launcher when the game starts
    bool showConsoleOnLaunch = false;
    bool keepLogs = true;
    std::string selectedVersion;           // tag, e.g. "v1.2.2a"; empty = newest
    bool showPrereleases = true;           // every Open Doctrines release is a pre-release today
    bool otherGamesUnlocked = false;       // the hidden shelf: Unciv, Greater Diplomacy
    bool autoUpdateLauncher = true;
    // Analytics: "" = never asked, "yes", "no". Nothing is sent unless "yes".
    std::string analyticsConsent;
    std::string analyticsClientId;         // random, made only after "yes"; deleted on "no"
    std::map<std::string, LaunchOptions> launch;   // per version tag
    std::string uiScale = "auto";
    bool fullscreen = false;
    int accent = 0xC9A227;                 // 0xRRGGBB; the website's gold by default
    bool accentFromGame = false;           // use the selected version's own accent instead

    static Settings& get();
    void load();
    void save() const;
    LaunchOptions& optionsFor(const std::string& tag) { return launch[tag]; }
};
