// The page for each game on the shelf other than Open Doctrines itself.
#include "App.h"
#include "core/Fs.h"
#include "od/Achievements.h"
#include "od/Analytics.h"
#include "tools/OtherGames.h"
#include "tools/TempleOS.h"
#include "ui/Art.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

namespace {
JobPtr g_job;
std::string g_drive = "T";
bool g_templeClaimed = false;

void templePage(App& a, Rectangle r) {
    const float x = r.x + 40, w = r.width - 80;
    float y = r.y + 28;
    Rectangle shot{x, y, 420, 315};
    art::gameCard(shot, art::Game::TempleOS, true, a.time);
    const float tx = x + 450, tw = w - 450;
    utext::draw(T("Open Doctrines on TempleOS"), tx, y, 26, theme::ink, utext::Serif);
    utext::drawWrapped(T("The game written again in HolyC, running on TempleOS inside QEMU. The launcher downloads the TempleOS live CD from templeos.org and the game's TempleOS release, puts the game on a second CD, boots the emulator, and can type the commands for you."),
                       {tx, y + 42, tw, 120}, 15, theme::muted);
    float yy = y + 150;
    const std::string q = utemple::qemu();
    if (q.empty()) {
        utext::drawWrapped(std::string(T("QEMU is needed first.")) + " " + utemple::qemuHelp(), {tx, yy, tw, 60}, 15, theme::gold);
        if (ui::button({tx, yy + 48, 200, 36}, T("Get QEMU"), ui::Style::Secondary)) uproc::openUrl("https://www.qemu.org/download/");
        return;
    }
    if (g_job && g_job->done) { if (!g_job->ok) a.toast(g_job->status(), true); g_job.reset(); }
    if (g_job) {
        utext::draw(g_job->status(), tx, yy, 15, theme::ink);
        ui::progress({tx, yy + 28, tw, 6}, g_job->progress.load());
        return;
    }
    if (!utemple::ready()) {
        if (ui::button({tx, yy, 240, 44}, T("Download and prepare"), ui::Style::Primary)) g_job = utemple::prepare();
        return;
    }
    const bool on = a.other && a.otherName == "TempleOS";
    if (!on) {
        if (ui::button({tx, yy, 200, 48}, T("Boot TempleOS"), ui::Style::Primary)) {
            std::string err;
            a.other = utemple::boot(&err);
            a.otherName = "TempleOS";
            if (!a.other) a.toast(err, true);
            else {
                uanalytics::event("templeos_launch", {{"mode", "live"}});
                if (!g_templeClaimed) { g_templeClaimed = true; uach::claim("templeos"); }
            }
        }
        return;
    }
    if (ui::button({tx, yy, 140, 38}, T("Stop"), ui::Style::Danger)) a.other->stop();
    yy += 56;
    utext::draw(T("Game CD drive letter"), tx, yy + 10, 14, theme::faint);
    ui::textField({tx + 200, yy, 60, 38}, g_drive, 7000);
    yy += 50;
    int n = 1;
    for (auto& st : utemple::steps(g_drive)) {
        utext::draw(TextFormat("%d.", n++), tx, yy + 10, 16, theme::gold, utext::Semi);
        utext::drawWrapped(T(st.what), {tx + 28, yy + 9, tw - 190, 40}, 15, theme::ink);
        if (ui::button({tx + tw - 150, yy, 150, 36}, T("Type it for me"), ui::Style::Secondary)) {
            bool ok = st.text.empty() ? utemple::sendKeys(st.keys) : utemple::typeText(st.text);
            if (!ok) a.toast(T("Could not reach the emulator's monitor."), true);
        }
        yy += 48;
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
    utext::draw(TextFormat(T("Licence: %s"), uother::licence(g)), tx, y + 42, 14, theme::faint);
    if (ui::button({tx, y + 66, 160, 30}, T("Its own page"), ui::Style::Ghost)) uproc::openUrl(uother::source(g));
    const char* blurb = g == uother::Game::Unciv
        ? T("An open-source remake of Civilization V. The launcher installs the official release; translated Open Doctrines maps go straight into its maps folder.")
        : g == uother::Game::Gd5 ? T("An open-source grand strategy game from 1910 to 2010. The launcher runs it from source in its own Python environment.")
        : T("The browser game that came before Greater Diplomacy 5. It plays on itch.io; the launcher opens it there.");
    utext::drawWrapped(blurb, {tx, y + 110, tw, 80}, 15, theme::muted);
    float yy = y + 200;
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
            if (c) { a.other = std::move(c); a.otherName = uother::name(g); }
            else if (!err.empty()) a.toast(err, true);
            uanalytics::event("other_game_launch", {{"game", g == uother::Game::Unciv ? "unciv" : g == uother::Game::Gd5 ? "gd5" : "gd4"}});
        }
        if (g != uother::Game::Gd4 && ui::button({tx + 172, yy, 140, 44}, T("Uninstall"), ui::Style::Ghost)) {
            a.confirm(TextFormat(T("Remove %s?"), uother::name(g)), T("Its files in the launcher's games folder are deleted."), T("Remove"),
                      [&a, g] { std::string e; if (!uother::uninstall(g, &e)) a.toast(e, true); }, true);
        }
    }
}
