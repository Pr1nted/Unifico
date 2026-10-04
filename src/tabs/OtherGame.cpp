// The page for each game on the shelf other than Open Doctrines itself.
#include "App.h"
#include "core/Fs.h"
#include "od/Achievements.h"
#include "od/Analytics.h"
#include "od/Playtime.h"
#include "tools/OtherGames.h"
#include "tools/TempleOS.h"
#include "ui/Art.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

namespace {
JobPtr g_job;          // downloading and building the disk
JobPtr g_auto;         // typing the way into the game
bool g_bootWhenReady = false, g_showSteps = false;
bool g_templeClaimed = false;

void bootTemple(App& a) {
    std::string err;
    a.other = utemple::boot(&err);
    a.otherName = "TempleOS";
    a.otherStartedAt = GetTime();
    if (!a.other) { a.toast(err, true); return; }
    uanalytics::event("templeos_launch", {{"mode", "auto"}});
    if (!g_templeClaimed) { g_templeClaimed = true; uach::claim("templeos"); }
    g_auto = utemple::autoStart();
}

void templePage(App& a, Rectangle r) {
    const float x = r.x + 40, w = r.width - 80;
    float y = r.y + 28;
    Rectangle shot{x, y, 420, 315};
    art::gameCard(shot, art::Game::TempleOS, true, a.time);
    const float tx = x + 450, tw = w - 450;
    utext::draw(T("Open Doctrines on TempleOS"), tx, y, 26, theme::ink, utext::Serif);
    if (const double pt = uplay::seconds("templeos"); pt >= 60)
        utext::draw(std::string(T("played")) + " " + uplay::human(pt), tx + tw - 160, y + 8, 14, theme::faint);
    utext::drawWrapped(T("The game written again in HolyC, running on TempleOS inside QEMU. Press Play: the launcher downloads the TempleOS live CD from templeos.org and the game's TempleOS release, puts the game on a small disk, boots the emulator and types its way into the game for you."),
                       {tx, y + 42, tw, 120}, 15, theme::muted);
    float yy = y + 150;

    // ---- QEMU, found and working, before anything else ----
    const utemple::QemuStatus qs = utemple::qemuStatus();
    if (!qs.ok) {
        ui::card({tx, yy, tw, 132});
        utext::draw(T("QEMU is needed first."), tx + 20, yy + 16, 17, theme::gold, utext::Semi);
        utext::drawWrapped(qs.problem, {tx + 20, yy + 44, tw - 40, 40}, 14, theme::muted);
        if (ui::button({tx + 20, yy + 84, 160, 34}, T("Get QEMU"), ui::Style::Primary)) uproc::openUrl("https://www.qemu.org/download/");
#if defined(__APPLE__)
        if (ui::button({tx + 190, yy + 84, 200, 34}, T("Copy the command"), ui::Style::Secondary)) {
            SetClipboardText("brew install qemu");
            a.toast(T("Copied. Paste it into Terminal."));
        }
#endif
        if (ui::button({tx + tw - 160, yy + 84, 140, 34}, T("Check again"), ui::Style::Ghost)) utemple::qemuStatus();
        return;
    }

    if (g_job && g_job->done) {
        const bool ok = g_job->ok;
        if (!ok) a.toast(g_job->status(), true);
        g_job.reset();
        if (ok && g_bootWhenReady) bootTemple(a);
        g_bootWhenReady = false;
    }
    if (g_job) {
        utext::draw(g_job->status(), tx, yy, 15, theme::ink);
        ui::progress({tx, yy + 28, tw, 6}, g_job->progress.load());
        return;
    }

    const bool on = a.other && a.otherName == "TempleOS";
    if (!on && g_auto) { g_auto->cancel = true; g_auto.reset(); }
    if (!on) {
        if (ui::button({tx, yy, 200, 48}, T("Play"), ui::Style::Primary)) {
            if (utemple::ready()) bootTemple(a);
            else { g_bootWhenReady = true; g_job = utemple::prepare(); }
        }
        utext::draw(utemple::ready() ? T("About a minute to boot. The emulator opens in its own window.")
                                     : T("The first start downloads about 20 MB."),
                    tx + 216, yy + 15, 14, theme::faint);
        return;
    }

    if (ui::button({tx, yy, 140, 38}, T("Stop"), ui::Style::Danger)) { a.other->stop(); if (g_auto) g_auto->cancel = true; }
    if (g_auto) {
        const bool done = g_auto->done.load();
        utext::draw(T(g_auto->status().c_str()), tx + 160, yy + 4, 15, done && !g_auto->ok ? theme::danger : theme::ink);
        if (!done) ui::progress({tx + 160, yy + 28, tw - 160, 6}, g_auto->progress.load());
    }
    yy += 60;

    // ---- the fallback: every step as a button ----
    ui::toggle({tx, yy, tw, 32}, T("Do it by hand"), &g_showSteps,
               T("If the automatic start gets stuck, type each step yourself."));
    yy += 44;
    if (!g_showSteps) return;
    if (g_auto && !g_auto->done) {
        if (ui::button({tx, yy, 220, 34}, T("Stop typing for me"), ui::Style::Ghost)) g_auto->cancel = true;
        yy += 44;
    }
    int n = 1;
    for (auto& st : utemple::steps()) {
        utext::draw(TextFormat("%d.", n++), tx, yy + 8, 15, theme::gold, utext::Semi);
        utext::drawWrapped(T(st.what), {tx + 28, yy + 8, tw - 190, 30}, 14, theme::ink);
        if (ui::button({tx + tw - 150, yy, 150, 32}, T("Type it for me"), ui::Style::Secondary, g_auto && !g_auto->done)) {
            bool ok = st.text.empty() ? utemple::sendKeys(st.keys) : utemple::typeText(st.text);
            if (!ok) a.toast(T("Could not reach the emulator's monitor."), true);
        }
        yy += 40;
    }
}
}  // namespace

void drawOtherGame(App& a, Rectangle r) {
    if (a.shelf == Shelf::TempleOS) { templePage(a, r); return; }
    const uother::Game g = a.shelf == Shelf::Unciv ? uother::Game::Unciv : a.shelf == Shelf::Gd5 ? uother::Game::Gd5 : uother::Game::Gd4;
    const art::Game ag = g == uother::Game::Unciv ? art::Game::Unciv : g == uother::Game::Gd5 ? art::Game::GreaterDiplomacy5 : art::Game::GreaterDiplomacy4;
    const float x = r.x + 40, w = r.width - 80;
    float y = r.y + 28;
    art::gameCard({x, y, 360, 240}, ag, true, a.time);
    const float tx = x + 390, tw = w - 390;
    utext::draw(uother::name(g), tx, y, 28, theme::ink, utext::Serif);
    {
        std::string meta = TextFormat(T("Licence: %s"), uother::licence(g));
        const char* key = g == uother::Game::Unciv ? "unciv" : g == uother::Game::Gd5 ? "gd5" : "gd4";
        if (const double pt = uplay::seconds(key); pt >= 60) meta += std::string("  ·  ") + T("played") + " " + uplay::human(pt);
        utext::draw(meta, tx, y + 42, 14, theme::faint);
    }
    if (ui::button({tx, y + 66, 160, 30}, T("Its own page"), ui::Style::Ghost)) uproc::openUrl(uother::source(g));
    const char* blurb = g == uother::Game::Unciv
        ? T("An open-source remake of Civilization V. The launcher installs the official release; translated Open Doctrines maps go straight into its maps folder.")
        : g == uother::Game::Gd5 ? T("An open-source grand strategy game from 1910 to 2010. The launcher runs it from source in its own Python environment.")
        : T("The browser game that came before Greater Diplomacy 5. It plays on itch.io; the launcher opens it there.");
    utext::drawWrapped(blurb, {tx, y + 110, tw, 80}, 15, theme::muted);
    float yy = y + 200;
    if (Verdict sv = uother::support(g); !sv.ok) {
        // Shown, explained, and not startable: there is no build of it here.
        ui::icon(Icon::Lock, tx + 10, yy + 12, 18, theme::danger);
        utext::drawWrapped(std::string(T("Not available on this computer.")) + " " + verdictText(sv), {tx + 30, yy, tw - 30, 60}, 15, theme::muted);
        return;
    }
    if (g_job && g_job->done) { a.toast(g_job->ok ? std::string(T("Installed.")) : g_job->status(), !g_job->ok); g_job.reset(); }
    if (g_job) { utext::draw(g_job->status(), tx, yy, 15, theme::ink); ui::progress({tx, yy + 28, tw, 6}, g_job->progress.load()); return; }
    const std::string pre = uother::prerequisite(g);
    if (!pre.empty()) { utext::draw(std::string(T("Needs:")) + " " + pre, tx, yy, 15, theme::gold); yy += 34; }
    const bool on = a.other && a.otherName == uother::name(g);
    if (!uother::installed(g)) {
        if (ui::button({tx, yy, 200, 44}, T("Install"), ui::Style::Primary, !pre.empty())) g_job = uother::install(g);
    } else if (on) {
        if (ui::button({tx, yy, 160, 44}, T("Stop"), ui::Style::Danger)) a.other->stop();
    } else {
        if (ui::button({tx, yy, 160, 44}, T("Play"), ui::Style::Primary)) {
            std::string err;
            auto c = uother::launch(g, &err);
            if (c) { a.other = std::move(c); a.otherName = uother::name(g); a.otherStartedAt = GetTime(); }
            else if (!err.empty()) a.toast(err, true);
            uanalytics::event("other_game_launch", {{"game", g == uother::Game::Unciv ? "unciv" : g == uother::Game::Gd5 ? "gd5" : "gd4"}});
        }
        if (g != uother::Game::Gd4 && ui::button({tx + 172, yy, 140, 44}, T("Uninstall"), ui::Style::Ghost)) {
            a.confirm(TextFormat(T("Remove %s?"), uother::name(g)), T("Its files in the launcher's games folder are deleted."), T("Remove"),
                      [&a, g] { std::string e; if (!uother::uninstall(g, &e)) a.toast(e, true); }, true);
        }
    }
}
