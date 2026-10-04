// Settings: the launcher's, the game's (config.json of a version), privacy,
// storage, about -- and removing everything.
#include "App.h"
#include "BuildInfo.h"
#include "core/Fs.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/Window.h"
#include "od/Analytics.h"
#include "od/GameConfig.h"
#include "tools/OtherGames.h"
#include "ui/Strings.h"
#include "ui/Text.h"
#include "ui/Ui.h"
#include "update/Uninstall.h"

#include <map>

namespace {
ui::Scroll g_scroll;
json g_cfg;
std::string g_cfgDir;
std::map<std::string, std::string> g_edit;   // text being typed into number/text fields
JobPtr g_storageJob;
std::shared_ptr<std::vector<std::pair<std::string, uint64_t>>> g_storagePending;
std::vector<std::pair<std::string, uint64_t>> g_storage;

void section(float x, float& y, float w, const char* title) {
    y += 18;
    ui::heading(title, x, y, 21);
    y += 34;
    ui::divider(x, y - 6, w);
    y += 6;
}

void startStorage(App& a) {
    auto installs = a.installs;
    auto out = std::make_shared<std::vector<std::pair<std::string, uint64_t>>>();
    g_storagePending = out;
    g_storageJob = ujobs::run("storage", [installs, out](Job&) {
        for (auto& i : installs) out->push_back({std::string("Open Doctrines ") + i.version, ufs::dirSize(i.root)});
        for (auto g : {uother::Game::Unciv, uother::Game::Gd5}) {
            uint64_t s = ufs::dirSize(uother::dir(g));
            if (s) out->push_back({uother::name(g), s});
        }
        uint64_t t = ufs::dirSize(upaths::gamesDir() + "/templeos");
        if (t) out->push_back({"TempleOS", t});
        out->push_back({N_("Launcher cache"), ufs::dirSize(upaths::cacheDir())});
        out->push_back({N_("Logs"), ufs::dirSize(upaths::logsDir())});
        out->push_back({N_("Kept worlds"), ufs::dirSize(upaths::home() + "/kept-worlds")});
        return true;
    });
}
}  // namespace

void drawSettings(App& a, Rectangle r) {
    Settings& s = Settings::get();
    ui::beginScroll(r, g_scroll);
    const float x = r.x + 40, w = std::min(860.0f, r.width - 80);
    float y = r.y + 28 - g_scroll.y;
    ui::heading(T("Settings"), x, y, 30);
    y += 40;

    // ---- launcher ----
    section(x, y, w, T("Launcher"));
    {
        utext::draw(T("Language"), x, y + 10, 16, theme::ink);
        std::vector<std::string> names{T("Same as the system")};
        int sel = 0;
        const auto& langs = ustr::languages();
        for (size_t i = 0; i < langs.size(); ++i) {
            names.push_back(langs[i].name);
            if (langs[i].code == s.language) sel = (int)i + 1;
        }
        int n = ui::dropdown({x + w - 300, y, 300, 38}, names, sel, 950);
        if (n != sel) {
            s.language = n == 0 ? "" : langs[(size_t)n - 1].code;
            s.save();
            ustr::setLanguage(s.language);
            utext::rebuild();
            uanalytics::event("language_change", {{"language", ustr::current()}});
        }
        y += 52;
    }
    // ---- appearance: the accent, as in the game ----
    {
        utext::draw(T("Accent colour"), x, y + 6, 16, theme::ink);
        float sx = x + 220;
        for (int rgb : theme::kAccentPresets) {
            const Color c{(unsigned char)(rgb >> 16), (unsigned char)(rgb >> 8), (unsigned char)rgb, 255};
            Rectangle sw{sx, y, 30, 30};
            const bool on = !s.accentFromGame && s.accent == rgb;
            DrawRectangleRounded(sw, 0.3f, 6, c);
            if (on) DrawRectangleRoundedLinesEx({sw.x - 4, sw.y - 4, sw.width + 8, sw.height + 8}, 0.3f, 6, 2, theme::ink);
            else if (ui::hovered(sw)) DrawRectangleRoundedLinesEx({sw.x - 3, sw.y - 3, sw.width + 6, sw.height + 6}, 0.3f, 6, 1, theme::muted);
            if (ui::clicked(sw)) { s.accent = rgb; s.accentFromGame = false; s.save(); a.applyAccent(); }
            sx += 40;
        }
        y += 44;
        static std::string hex;
        static int shown = -1;
        if (shown != s.accent) { char b[8]; snprintf(b, sizeof b, "%06X", s.accent); hex = b; shown = s.accent; }
        utext::draw(T("Custom (hex)"), x + 220, y + 10, 14, theme::faint);
        if (ui::textField({x + 330, y, 120, 36}, hex, 8800, "C9A227")) {
            std::string h = hex;
            if (!h.empty() && h[0] == '#') h.erase(0, 1);
            if (h.size() == 6 && h.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
                s.accent = (int)std::stoul(h, nullptr, 16);
                shown = s.accent;
                s.accentFromGame = false;
                s.save();
                a.applyAccent();
            }
        }
        y += 50;
        bool follow = s.accentFromGame;
        if (ui::toggle({x, y, w, 48}, T("Use the game's accent colour"), &follow,
                       T("Takes the accent you chose inside the selected version of Open Doctrines."))) {
            s.accentFromGame = follow;
            s.save();
            a.applyAccent();
        }
        y += 60;
    }

    // ---- interface size ----
    {
        utext::draw(T("Interface size"), x, y + 10, 16, theme::ink);
        static const char* vals[] = {"auto", "0.9", "1", "1.25", "1.5", "1.75", "2"};
        std::vector<std::string> names{T("Automatic (follows the window)"), "90%", "100%", "125%", "150%", "175%", "200%"};
        int sel = 0;
        for (int i = 0; i < 7; ++i) if (s.uiScale == vals[i]) sel = i;
        int n = ui::dropdown({x + w - 300, y, 300, 38}, names, sel, 951);
        if (n != sel) { s.uiScale = vals[n]; s.save(); }
        y += 52;
        bool fs = uwindow::isFullscreen();
        if (ui::toggle({x, y, w, 48}, T("Fullscreen"), &fs, T("F11 switches it from anywhere in the launcher."))) a.toggleFullscreen();
        y += 60;
    }

    auto tog = [&](const char* label, const char* hint, bool& v) {
        if (ui::toggle({x, y, w, 48}, label, &v, hint)) s.save();
        y += 56;
    };
    tog(T("Hide the launcher while the game runs"), T("Otherwise it stays open with a Stop button and the console."), s.hideWhileRunning);
    tog(T("Close the launcher when the game starts"), T("The game keeps running; nothing watches it."), s.closeOnLaunch);
    tog(T("Open the console whenever a game starts"), nullptr, s.showConsoleOnLaunch);
    tog(T("Keep a log file of every launch"), T("In the launcher's logs folder, for reporting problems."), s.keepLogs);
    tog(T("Update the launcher automatically"), T("Checks at start; asks before restarting."), s.autoUpdateLauncher);

    // ---- the game's own settings ----
    section(x, y, w, T("Game settings"));
    Install inst;
    if (installPicker(a, {x + w - 320, y - 46, 320, 38}, inst)) {
        if (g_cfgDir != inst.dataDir) { g_cfg = uconfig::load(inst.dataDir); g_cfgDir = inst.dataDir; g_edit.clear(); }
        const bool locked = a.running && a.running->child && a.running->child->running() && a.running->install.dataDir == inst.dataDir;
        if (locked) { utext::draw(T("This version is running; it would overwrite changes made now."), x, y, 15, theme::gold); y += 30; }
        if (!g_cfg.is_object() || g_cfg.empty()) {
            utext::draw(T("This version has no settings file yet. Start it once and its settings appear here."), x, y, 15, theme::faint);
            y += 30;
        }
        std::string group;
        bool changed = false;
        int id = 6000;
        for (auto& f : uconfig::fields(g_cfg)) {
            if (f.group != group) { group = f.group; utext::draw(T(group.c_str()), x, y + 6, 14, theme::gold, utext::Semi); y += 30; }
            ++id;
            if (f.kind == ConfigField::Bool) {
                bool v = g_cfg[f.key].get<bool>();
                if (ui::toggle({x, y, w, 40}, T(f.label.c_str()), &v)) { g_cfg[f.key] = v; changed = true; }
                y += 44;
            } else {
                utext::draw(T(f.label.c_str()), x, y + 10, 16, theme::ink);
                auto it = g_edit.find(f.key);
                if (it == g_edit.end()) {
                    std::string cur = f.kind == ConfigField::Number ? g_cfg[f.key].dump() : g_cfg[f.key].get<std::string>();
                    it = g_edit.emplace(f.key, cur).first;
                }
                if (ui::textField({x + w - 300, y, 300, 38}, it->second, id)) {
                    if (f.kind == ConfigField::Number) {
                        json parsed = ujson::parse(it->second);
                        if (parsed.is_number()) { g_cfg[f.key] = parsed; changed = true; }
                    } else { g_cfg[f.key] = it->second; changed = true; }
                }
                y += 46;
            }
        }
        if (changed && !locked) uconfig::save(inst.dataDir, g_cfg);
    } else {
        utext::draw(T("Each installed version's own settings appear here, ready to edit."), x, y, 15, theme::faint);
        y += 34;
    }

    // ---- privacy ----
    section(x, y, w, T("Privacy"));
    {
        bool on = s.analyticsConsent == "yes";
        if (ui::toggle({x, y, w, 48}, T("Send usage statistics"),
                       &on, T("Pages opened and versions started, through the account service to Google Analytics. Never anything you typed."))) {
            uanalytics::setConsent(on);
        }
        y += 56;
        if (on) {
            utext::draw(TextFormat(T("This installation's random identifier: %s"), s.analyticsClientId.c_str()), x, y, 13, theme::faint);
            if (ui::button({x + w - 120, y - 8, 120, 30}, T("Copy"), ui::Style::Ghost)) SetClipboardText(s.analyticsClientId.c_str());
            y += 30;
        }
        if (ui::button({x, y, 180, 34}, T("Privacy policy"), ui::Style::Ghost)) uproc::openUrl(std::string(UNIFICO_ACCOUNT_ISSUER) + "/privacy");
        y += 46;
    }

    // ---- storage ----
    section(x, y, w, T("Storage"));
    {
        if (!g_storageJob) startStorage(a);
        if (g_storageJob->done && g_storagePending) { g_storage = *g_storagePending; g_storagePending.reset(); }
        uint64_t total = 0;
        for (auto& [n, b] : g_storage) total += b;
        utext::draw(TextFormat(T("Everything the launcher manages: %s. Free on this disk: %s."), ufs::humanBytes(total).c_str(),
                               ufs::humanBytes(ufs::freeSpace(upaths::home())).c_str()), x, y, 15, theme::muted);
        if (ui::button({x + w - 120, y - 8, 120, 32}, T("Recount"), ui::Style::Ghost)) g_storageJob.reset();
        y += 32;
        for (auto& [n, b] : g_storage) {
            const float frac = total ? (float)b / (float)total : 0;
            utext::draw(T(n.c_str()), x, y, 15, theme::ink);
            DrawRectangleRounded({x + 260, y + 5, (w - 400), 8}, 1, 8, theme::rule);
            DrawRectangleRounded({x + 260, y + 5, (w - 400) * frac, 8}, 1, 8, theme::goldDim);
            utext::draw(ufs::humanBytes(b), x + w - 120, y, 15, theme::muted);
            y += 28;
        }
        if (ui::button({x, y + 4, 200, 34}, T("Open launcher folder"), ui::Style::Ghost)) uproc::revealInFileManager(upaths::home());
        if (ui::button({x + 210, y + 4, 160, 34}, T("Clear cache"), ui::Style::Ghost)) { ufs::removeAll(upaths::cacheDir()); g_storageJob.reset(); }
        y += 52;
    }

    // ---- about ----
    section(x, y, w, T("About"));
    {
        Rectangle ver{x, y, 360, 26};
        utext::draw(TextFormat(T("Unifico %s for %s"), UNIFICO_VERSION, upaths::platformTag().c_str()), x, y, 16, theme::ink);
        // Seven clicks on the version, and the shelf of other games appears.
        if (ui::clicked(ver) && !s.otherGamesUnlocked) {
            if (++a.versionClicks >= 7) {
                s.otherGamesUnlocked = true;
                s.save();
                a.toast(T("Other games unlocked: look in the list on the left."));
            } else if (a.versionClicks >= 3) {
                a.toast(TextFormat(T("%d more..."), 7 - a.versionClicks));
            }
        }
        y += 30;
        utext::drawWrapped(T("The launcher for Open Doctrines. Released under the same licence as the game. Map translation by open-dragoman."),
                           {x, y, w, 40}, 14, theme::faint);
        utext::drawWrapped(T("Open Doctrines is free and made by one person. If you want to help keep it going, there is a Ko-fi page; it changes nothing in the game."),
                           {x, y + 22, w, 40}, 14, theme::faint);
        y += 22;
        y += 44;
        if (ui::button({x, y, 200, 34}, T("Check for updates"), ui::Style::Secondary)) {
            a.updateJob = ujobs::run("update check", [&a](Job&) {
                uupdate::Available av;
                if (uupdate::check(av, nullptr)) { a.update = av; a.updateKnown = true; }
                return true;
            });
            a.toast(T("Checking..."));
        }
        {
            Icon heart = Icon::Heart;
            if (ui::button({x + 420, y, 230, 34}, T("Support on Ko-fi"), ui::Style::Ghost, false, &heart))
                uproc::openUrl("https://ko-fi.com/pr1nted");
        }
        if (s.otherGamesUnlocked && ui::button({x + 210, y, 200, 34}, T("Hide other games"), ui::Style::Ghost)) {
            s.otherGamesUnlocked = false; s.save(); a.versionClicks = 0;
        }
        y += 52;
    }

    // ---- danger ----
    section(x, y, w, T("Remove everything"));
    {
        utext::drawWrapped(T("Deletes every version this launcher installed, their worlds, the other games, the launcher's data, and finally the launcher itself. Copies you installed yourself and pointed the launcher at are left alone."),
                           {x, y, w, 60}, 15, theme::muted);
        y += 60;
        if (ui::button({x, y, 260, 40}, T("Uninstall everything..."), ui::Style::Danger)) {
            auto plan = uuninstall::plan(true);
            std::string body = TextFormat(T("%s will be deleted, including all worlds in launcher-installed versions. This cannot be undone."),
                                          ufs::humanBytes(plan.bytes).c_str());
            a.confirm(T("Uninstall everything?"), body, T("Delete it all"), [&a, plan] {
                std::string err;
                if (uuninstall::run(plan, true, &err)) a.wantsQuit = true;
                else a.toast(err, true);
            }, true);
        }
        y += 60;
    }
    ui::endScroll(r, g_scroll, y + g_scroll.y - r.y + 20);
}
