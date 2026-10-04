#pragma once
// The launcher's state and its frame. Each page lives in src/tabs/; they share
// this one struct rather than talking through globals.
#include "core/Jobs.h"
#include "od/Installs.h"
#include "od/Launch.h"
#include "od/News.h"
#include "od/Releases.h"
#include "update/SelfUpdate.h"
#include "raylib.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum class Tab { Play, Installs, Worlds, Mods, Achievements, Servers, Account, Settings, Tools, Admin };
enum class Shelf { OpenDoctrines, Unciv, Gd5, Gd4, TempleOS };

struct Toast { std::string text; bool error = false; double until = 0; };

struct App {
    // ---- lifetime ----
    void init(int argc, char** argv);
    void frame();
    void shutdown();
    bool wantsQuit = false;

    // ---- navigation ----
    Tab tab = Tab::Play;
    Shelf shelf = Shelf::OpenDoctrines;
    float time = 0;
    int hero = 0;
    double heroAt = 0;

    // ---- data, refreshed by jobs ----
    std::vector<Install> installs;
    std::vector<Release> releases;
    std::vector<NewsItem> news;
    JobPtr releasesJob, newsJob, installJob, updateJob, toolJob;
    std::string selectedTag;           // version to play
    std::string pageInstallTag;        // the version the Worlds/Mods/Servers/Settings pages act on
    void reloadInstalls();
    /** Apply Settings' accent, or the selected version's when it follows the game. */
    void applyAccent();
    bool selectedInstall(Install& out);
    bool pageInstall(Install& out);
    std::vector<std::string> allDataDirs() const;

    // ---- the running game ----
    std::unique_ptr<ulaunch::Running> running;
    std::unique_ptr<uproc::Child> other;     // Unciv, GD5, TempleOS
    std::string otherName;
    double otherStartedAt = 0;
    bool hidden = false;
    void play(const std::string& extraArg = {});
    void stop();
    void pollRunning();

    // ---- console ----
    bool consoleOpen = false;
    std::vector<std::string> console;
    size_t consoleFrom = 0, otherFrom = 0;
    float consoleScroll = 0;
    bool consoleFollow = true;
    void saveLog();

    // ---- modal & toasts ----
    std::function<bool()> modal;      // drawn above everything; returns false to close
    Rectangle modalRect{};
    std::vector<Toast> toasts;
    void toast(const std::string& text, bool error = false);
    void toggleFullscreen();
    /** The "Report a problem" form, as a modal. */
    void openFeedback(bool bug = true);
    void confirm(const std::string& title, const std::string& body, const std::string& yes,
                 std::function<void()> onYes, bool danger = false);

    // ---- self-update ----
    uupdate::Available update;
    bool updateKnown = false;

    // ---- the hidden shelf ----
    int versionClicks = 0;

    // ---- --screenshots ----
    std::string shotDir;
    void screenshotStep();

    // ---- layout, recomputed every frame ----
    Rectangle content{};
};

// Pages. Each draws into `r` and handles its own input.
void drawPlay(App& a, Rectangle r);
void drawInstalls(App& a, Rectangle r);
void drawWorlds(App& a, Rectangle r);
void drawMods(App& a, Rectangle r);
void drawAchievements(App& a, Rectangle r);
void drawServers(App& a, Rectangle r);
void drawAccount(App& a, Rectangle r);
void drawSettings(App& a, Rectangle r);
void drawTools(App& a, Rectangle r);
void drawAdmin(App& a, Rectangle r);
struct Sanction;
/** Bans, timeouts and restrictions as cards (AdminTab.cpp; the Account page uses it too). */
void drawSanctionHistory(const std::vector<Sanction>& list, float x, float& y, float w);
void drawOtherGame(App& a, Rectangle r);
void drawConsole(App& a, Rectangle r);

/** The version picker every per-installation page shares. Returns false if nothing is installed. */
bool installPicker(App& a, Rectangle r, Install& out);
