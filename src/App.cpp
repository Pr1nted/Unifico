#include "App.h"
#include "BuildInfo.h"
#include "core/FileDialog.h"
#include "core/Fs.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/Window.h"
#include "od/Account.h"
#include "od/Admin.h"
#include "od/Achievements.h"
#include "od/Analytics.h"
#include "od/Feedback.h"
#include "od/Support.h"
#include "od/Discord.h"
#include "od/Playtime.h"
#include "tools/OtherGames.h"
#include "ui/Art.h"
#include "ui/Strings.h"
#include "ui/Text.h"
#include "ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace {
const float kRail = 236;     // left column: logo, games, account
const float kTop = 64;       // the tab bar

struct TabDef { Tab tab; const char* label; Icon icon; };
const TabDef kTabs[] = {
    {Tab::Play, "Play", Icon::Play},
    {Tab::Installs, "Installations", Icon::Box},
    {Tab::Worlds, "Worlds", Icon::Map},
    {Tab::Mods, "Mods", Icon::Puzzle},
    {Tab::Achievements, "Achievements", Icon::Trophy},
    {Tab::Servers, "Servers", Icon::Server},
    {Tab::Tools, "Tools", Icon::Tools},
};

const char* tabName(Tab t) {
    switch (t) {
        case Tab::Play: return "play"; case Tab::Installs: return "installs"; case Tab::Worlds: return "worlds";
        case Tab::Mods: return "mods"; case Tab::Achievements: return "achievements"; case Tab::Servers: return "servers";
        case Tab::Account: return "account"; case Tab::Settings: return "settings"; case Tab::Admin: return "admin";
        default: return "tools";
    }
}
}  // namespace

// ---------------------------------------------------------------- lifetime

void App::init(int argc, char** argv) {
    Settings::get().load();
    ustr::setLanguage(Settings::get().language);
    Account::get().init(UNIFICO_ACCOUNT_ISSUER);
    uanalytics::init(UNIFICO_ACCOUNT_ISSUER);
    udiscord::init(UNIFICO_DISCORD_APP_ID);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(1220, 760, "Unifico");
    SetWindowMinSize(960, 600);
    // Restored natively on macOS; elsewhere borderless, with Esc as the way out.
    if (Settings::get().fullscreen) uwindow::toggleFullscreen();
    // For checking layouts at other sizes (e.g. with --screenshots): WxH.
    if (const char* ws = std::getenv("UNIFICO_WINDOW")) {
        int w = 0, h = 0;
        if (std::sscanf(ws, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) SetWindowSize(w, h);
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    art::init();
    if (Texture2D icon = art::appIcon(); icon.id) {
        Image im = LoadImageFromTexture(icon);
        SetWindowIcon(im);
        UnloadImage(im);
    }
    utext::init();

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--updated") toast(TextFormat(T("Unifico was updated to %s."), UNIFICO_VERSION));
        // --screenshots <dir>: render every page once and write a PNG of each,
        // for the website, the itch page and reviewing a change by eye.
        if (std::string(argv[i]) == "--screenshots" && i + 1 < argc) shotDir = argv[i + 1];
    }

    reloadInstalls();
    applyAccent();
    releases = ureleases::cached();
    news = unews::cached();
    selectedTag = Settings::get().selectedVersion;
    releasesJob = ujobs::run("releases", [](Job& j) { std::string e; bool ok = ureleases::refresh(&e); if (!ok) j.setStatus(e); return ok; });
    newsJob = ujobs::run("news", [](Job&) { return unews::refresh(UNIFICO_ACCOUNT_ISSUER); });
    Account::get().bootstrap();
    uach::refresh(allDataDirs());
    uach::retryPending();
    if (Settings::get().autoUpdateLauncher) {
        updateJob = ujobs::run("update check", [this](Job&) {
            uupdate::Available av;
            if (uupdate::check(av, nullptr)) { update = av; updateKnown = true; }
            return true;
        });
    }
    uanalytics::event("launcher_open", {{"launcher_version", UNIFICO_VERSION}, {"platform", upaths::platformTag()},
                                        {"language", ustr::current()}});
    ulog::info(std::string("Unifico ") + UNIFICO_VERSION + " on " + upaths::platformTag() + ", home " + upaths::home());
}

void App::shutdown() {
    uanalytics::flush();
    if (running && running->child && running->child->running() && !Settings::get().closeOnLaunch) {
        // Closing the launcher does not stop a game the person is playing.
        ulog::info("launcher closed while the game runs; leaving it running");
        running->child.release();   // detach: the destructor would stop it
    }
    utext::shutdown();
    art::shutdown();
    CloseWindow();
}

// ---------------------------------------------------------------- data

void App::reloadInstalls() {
    installs = uinstalls::list();
    if (pageInstallTag.empty() && !installs.empty()) pageInstallTag = installs.front().tag;
}

void App::applyAccent() {
    int rgb = Settings::get().accent;
    if (Settings::get().accentFromGame) {
        // The game keeps its accent in config.json as accentColor, 0xRRGGBB.
        Install i;
        if (selectedInstall(i)) {
            json c = ujson::load(i.dataDir + "/config.json");
            if (c.contains("accentColor") && c["accentColor"].is_number_integer()) rgb = c["accentColor"].get<int>() & 0xFFFFFF;
        }
    }
    theme::setAccent(rgb);
    art::accentChanged();
}

bool App::selectedInstall(Install& out) {
    for (auto& i : installs) if (i.tag == selectedTag) { out = i; return true; }
    if (installs.empty()) return false;
    out = installs.front();
    return true;
}

bool App::pageInstall(Install& out) {
    for (auto& i : installs) if (i.tag == pageInstallTag) { out = i; return true; }
    if (installs.empty()) return false;
    out = installs.front();
    pageInstallTag = out.tag;
    return true;
}

std::vector<std::string> App::allDataDirs() const {
    std::vector<std::string> d;
    for (auto& i : installs) d.push_back(i.dataDir);
    return d;
}

void App::toggleFullscreen() {
    // Borderless windowed rather than exclusive fullscreen: instant, no mode
    // switch, and the game can still be alt-tabbed to.
    uwindow::toggleFullscreen();
    // The native macOS transition is animated and reports its new state a
    // moment later, so the setting stores the state we asked for.
    Settings::get().fullscreen = !Settings::get().fullscreen;
    Settings::get().save();
}

void App::openFeedback(bool bug) {
    struct Form { bool bug; int cat = 7; std::string title, body; bool diag = true; JobPtr job; };
    auto f = std::make_shared<Form>();
    f->bug = bug;
    modal = [this, f]() -> bool {
        const float mw = 640, mh = 540;
        Rectangle m{(ui::W() - mw) / 2, (ui::H() - mh) / 2, mw, mh};
        modalRect = m;
        ui::card(m);
        ui::heading(f->bug ? T("Report a problem") : T("Suggest something"), m.x + 26, m.y + 20, 24);
        float y = m.y + 64;
        if (Account::get().state() != Account::State::SignedIn) {
            utext::drawWrapped(T("Please sign in to send this. Reports are published, and signed with your nickname."),
                               {m.x + 26, y, mw - 52, 60}, 16, theme::muted);
            bool keep = true;
            if (ui::button({m.x + 26, y + 60, 160, 40}, T("Sign in"), ui::Style::Primary)) { tab = Tab::Account; shelf = Shelf::OpenDoctrines; keep = false; }
            if (ui::button({m.x + mw - 146, m.y + mh - 62, 120, 40}, T("Cancel"), ui::Style::Ghost) || ui::escapePressed()) keep = false;
            return keep;
        }
        // Bug or suggestion.
        if (ui::button({m.x + 26, y, 150, 34}, T("Bug"), f->bug ? ui::Style::Primary : ui::Style::Ghost)) f->bug = true;
        if (ui::button({m.x + 186, y, 150, 34}, T("Suggestion"), !f->bug ? ui::Style::Primary : ui::Style::Ghost)) f->bug = false;
        std::vector<std::string> cats;
        for (int i = 0; i < 8; ++i) cats.push_back(T(ufeedback::categoryLabel(i)));
        f->cat = ui::dropdown({m.x + mw - 246, y, 220, 34}, cats, f->cat, 960);
        y += 50;
        utext::draw(T("Title"), m.x + 26, y, 14, theme::faint, utext::Semi);
        ui::textField({m.x + 26, y + 20, mw - 52, 38}, f->title, 9100);
        y += 70;
        utext::draw(T("What happened, and what you expected"), m.x + 26, y, 14, theme::faint, utext::Semi);
        ui::textArea({m.x + 26, y + 20, mw - 52, 150}, f->body, 9101);
        y += 186;
        ui::toggle({m.x + 26, y, mw - 52, 48}, T("Attach the launcher log and the game console"), &f->diag,
                   T("Helps find the cause. You can read it first with Download a log."));
        y += 56;
        if (f->job && f->job->done) {
            if (f->job->ok) { toast(T(f->job->status().c_str())); return false; }
            utext::draw(utext::ellipsize(f->job->status(), mw - 52, 14), m.x + 26, y, 14, theme::danger);
        }
        bool keep = true;
        const bool sending = f->job && !f->job->done;
        if (ui::button({m.x + mw - 26 - 150, m.y + mh - 62, 150, 40}, sending ? T("Sending...") : T("Send"), ui::Style::Primary,
                       sending || f->title.size() < 4 || f->body.size() < 10)) {
            std::string diag;
            if (f->diag) {
                diag = std::string("Unifico ") + UNIFICO_VERSION + " on " + upaths::platformTag() + "\n--- launcher ---\n";
                for (auto& l : ulog::tail(150)) diag += l + "\n";
                diag += "--- game console ---\n";
                const size_t from = console.size() > 150 ? console.size() - 150 : 0;
                for (size_t i = from; i < console.size(); ++i) diag += console[i] + "\n";
            }
            f->job = ufeedback::send(f->bug, f->cat, f->title, f->body, diag);
        }
        // Esc leaves a text field first; only an Esc with nothing focused closes.
        if (ui::button({m.x + mw - 26 - 150 - 12 - 120, m.y + mh - 62, 120, 40}, T("Cancel"), ui::Style::Ghost) ||
            ui::escapePressed()) keep = false;
        return keep;
    };
}

void App::toast(const std::string& text, bool error) {
    toasts.push_back({text, error, GetTime() + (error ? 7.0 : 4.5)});
}

void App::confirm(const std::string& title, const std::string& body, const std::string& yes,
                  std::function<void()> onYes, bool danger) {
    modal = [this, title, body, yes, onYes, danger]() -> bool {
        const float w = 520, h = 236;
        Rectangle r{(ui::W() - w) / 2, (ui::H() - h) / 2, w, h};
        modalRect = r;
        ui::card(r);
        ui::heading(title, r.x + 28, r.y + 22, 26);
        utext::drawWrapped(body, {r.x + 28, r.y + 66, r.width - 56, 100}, 16, theme::muted);
        bool keep = true;
        if (ui::button({r.x + r.width - 28 - 150, r.y + r.height - 64, 150, 40}, yes.c_str(),
                       danger ? ui::Style::Danger : ui::Style::Primary)) { onYes(); keep = false; }
        if (ui::button({r.x + r.width - 28 - 150 - 12 - 120, r.y + r.height - 64, 120, 40}, T("Cancel"), ui::Style::Ghost)) keep = false;
        if (ui::escapePressed()) keep = false;
        return keep;
    };
}

// ---------------------------------------------------------------- running

void App::play(const std::string& extraArg) {
    if (running && running->child && running->child->running()) return;
    Install i;
    if (!selectedInstall(i)) { tab = Tab::Installs; toast(T("Install a version first."), true); return; }
    // Never start what this computer cannot run: the binary's own header
    // decides, not its file name (see od/Support.h).
    if (Verdict v = usupport::binary(i.exe); !v.ok) { toast(verdictText(v), true); return; }
    std::string err;
    running = ulaunch::start(i, extraArg, &err);
    if (!running) { toast(std::string(T("The game could not be started: ")) + err, true); return; }
    running->startedAt = GetTime();
    console.clear();
    consoleFrom = 0;
    const LaunchOptions& lo = Settings::get().optionsFor(i.tag);
    if (lo.console || Settings::get().showConsoleOnLaunch) consoleOpen = true;
    uanalytics::event("game_launch", {{"game_version", i.version}, {"platform", upaths::platformTag()},
                                      {"source", extraArg.empty() ? "play" : (extraArg.rfind("opendoctrines://", 0) == 0 ? "join" : "world")}});
    if (Settings::get().closeOnLaunch) {
        running->child.release();
        wantsQuit = true;
        return;
    }
    if (Settings::get().hideWhileRunning && !consoleOpen) {
        SetWindowState(FLAG_WINDOW_HIDDEN);
        hidden = true;
    }
}

void App::stop() {
    if (running && running->child) running->child->stop();
    if (other) other->stop();
}

void App::pollRunning() {
    if (running && running->child) {
        for (auto& l : running->child->linesSince(consoleFrom)) console.push_back(l);
        if (!running->child->running()) {
            const int code = running->child->exitCode();
            const double minutes = (GetTime() - running->startedAt) / 60.0;
            uplay::add("od:" + running->install.tag, minutes * 60.0);
            const char* bucket = minutes < 1 ? "lt1" : minutes < 5 ? "1-5" : minutes < 15 ? "5-15" : minutes < 60 ? "15-60" : "gt60";
            uanalytics::event("game_exit", {{"game_version", running->install.version}, {"minutes_bucket", bucket},
                                            {"exit_kind", code == 0 ? "clean" : (running->child->killedForMemory() ? "memory" : "error")}});
            if (code != 0 && !running->child->killedForMemory())
                toast(TextFormat(T("The game exited with code %d. The console has its last words."), code), true);
            if (running->child->killedForMemory()) toast(T("The game was stopped at its memory limit."), true);
            Account::get().takeBackFrom(running->install.dataDir);
            if (Settings::get().accentFromGame) applyAccent();   // it may have been changed in the game
            uach::refresh(allDataDirs());
            running.reset();
            if (hidden) { ClearWindowState(FLAG_WINDOW_HIDDEN); hidden = false; }
            if (code != 0) consoleOpen = true;
        }
    }
    if (other) {
        for (auto& l : other->linesSince(otherFrom)) console.push_back("[" + otherName + "] " + l);
        if (!other->running()) {
            const std::string key = otherName == "Unciv" ? "unciv" : otherName == "TempleOS" ? "templeos" : "gd5";
            uplay::add(key, GetTime() - otherStartedAt);
            other.reset();
            otherFrom = 0;
        }
    }
    if (console.size() > 8000) console.erase(console.begin(), console.begin() + 2000);
}

void App::saveLog() {
    char stamp[40];
    std::time_t t = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "unifico-%Y%m%d-%H%M%S.log", std::localtime(&t));
    std::string dest = udialog::saveFile(T("Save the log"), stamp);
    if (dest.empty()) dest = upaths::downloadsDir() + "/" + stamp;
    std::string text = std::string("Unifico ") + UNIFICO_VERSION + " on " + upaths::platformTag() + "\n\n--- game ---\n";
    for (auto& l : console) text += l + "\n";
    text += "\n--- launcher ---\n";
    for (auto& l : ulog::tail(600)) text += l + "\n";
    if (ufs::writeFileAtomic(dest, text)) toast(std::string(T("Log saved to ")) + dest);
    else toast(T("The log could not be saved."), true);
}

// ---------------------------------------------------------------- chrome

static void drawRail(App& a, Rectangle r) {
    DrawRectangleRec(r, Color{9, 11, 17, 235});
    DrawRectangle((int)(r.x + r.width - 1), (int)r.y, 1, (int)r.height, theme::rule);
    if (Texture2D ic = art::appIcon(); ic.id)
        DrawTexturePro(ic, {0, 0, (float)ic.width, (float)ic.height}, {r.x + 20, r.y + 22, 46, 46}, {0, 0}, 0, WHITE);
    art::logo(r.x + 76, r.y + 32, 16, theme::ink);
    utext::draw(T("Open Doctrines launcher"), r.x + 77, r.y + 54, 12, theme::gold, utext::Semi);

    float y = r.y + 100;
    utext::draw(T("Games"), r.x + 24, y, 13, theme::faint, utext::Semi);
    y += 24;
    struct G { Shelf s; art::Game g; const char* name; };
    std::vector<G> games = {{Shelf::OpenDoctrines, art::Game::OpenDoctrines, "Open Doctrines"}};
    if (Settings::get().otherGamesUnlocked) {
        games.push_back({Shelf::Unciv, art::Game::Unciv, "Unciv"});
        games.push_back({Shelf::Gd5, art::Game::GreaterDiplomacy5, "Greater Diplomacy 5"});
        games.push_back({Shelf::Gd4, art::Game::GreaterDiplomacy4, "Greater Diplomacy 4"});
    }
    games.push_back({Shelf::TempleOS, art::Game::TempleOS, "TempleOS edition"});
    for (auto& g : games) {
        Rectangle row{r.x + 14, y, r.width - 28, 64};
        const bool active = a.shelf == g.s;
        if (ui::hovered(row) || active) DrawRectangleRounded(row, 0.18f, 8, active ? theme::accentA(26) : Color{255, 255, 255, 10});
        art::gameCard({row.x + 8, row.y + 8, 48, 48}, g.g, active, a.time);
        utext::draw(utext::ellipsize(g.name, row.width - 76, 16), row.x + 68, row.y + 13, 16, active ? theme::ink : theme::muted, utext::Semi);
        const char* sub = g.s == Shelf::OpenDoctrines ? T("Grand strategy") : g.s == Shelf::TempleOS ? T("In an emulator")
                        : T("Another open game");
        if ((g.s == Shelf::Unciv && !uother::support(uother::Game::Unciv).ok) ||
            (g.s == Shelf::Gd5 && !uother::support(uother::Game::Gd5).ok))
            sub = T("Not on this computer");
        utext::draw(sub, row.x + 68, row.y + 35, 13, theme::faint);
        if (ui::clicked(row)) {
            a.shelf = g.s;
            if (g.s == Shelf::OpenDoctrines && a.tab == Tab::Tools) a.tab = Tab::Play;
        }
        y += 70;
    }

    // Account chip at the bottom.
    Rectangle chip{r.x + 14, r.y + r.height - 118, r.width - 28, 52};
    const bool hov = ui::hovered(chip);
    DrawRectangleRounded(chip, 0.25f, 8, hov || a.tab == Tab::Account ? Color{255, 255, 255, 14} : BLANK);
    ui::icon(Icon::User, chip.x + 26, chip.y + 26, 22, Account::get().state() == Account::State::SignedIn ? theme::gold : theme::muted);
    const bool signedIn = Account::get().state() == Account::State::SignedIn;
    utext::draw(utext::ellipsize(signedIn ? Account::get().nickname() : std::string(T("Sign in")), chip.width - 60, 16),
                chip.x + 48, chip.y + 8, 16, theme::ink, utext::Semi);
    utext::draw(signedIn ? T("Open Doctrines account") : T("Achievements, multiplayer"), chip.x + 48, chip.y + 29, 13, theme::faint);
    if (ui::clicked(chip)) { a.tab = Tab::Account; a.shelf = Shelf::OpenDoctrines; }

    Rectangle gear{r.x + 14, r.y + r.height - 58, r.width - 28, 40};
    if (ui::hovered(gear) || a.tab == Tab::Settings) DrawRectangleRounded(gear, 0.25f, 8, Color{255, 255, 255, 12});
    ui::icon(Icon::Gear, gear.x + 26, gear.y + 20, 20, a.tab == Tab::Settings ? theme::gold : theme::muted);
    utext::draw(T("Settings"), gear.x + 48, gear.y + 11, 16, theme::ink);
    if (ui::clicked(gear)) { a.tab = Tab::Settings; a.shelf = Shelf::OpenDoctrines; }
}

static void drawTopBar(App& a, Rectangle r) {
    DrawRectangleRec(r, Color{12, 15, 22, 225});
    DrawRectangle((int)r.x, (int)(r.y + r.height - 1), (int)r.width, 1, theme::rule);
    float x = r.x + 26;
    if (a.shelf == Shelf::OpenDoctrines) {
        std::vector<TabDef> tabs(std::begin(kTabs), std::end(kTabs));
        if (uadmin::isAdmin()) tabs.push_back({Tab::Admin, "Admin", Icon::Shield});
        for (auto& t : tabs) {
            const char* label = T(t.label);
            const float w = utext::measure(label, 16).x;
            Rectangle hit{x - 8, r.y, w + 16, r.height};
            const bool active = a.tab == t.tab;
            const bool hov = ui::hovered(hit);
            utext::draw(label, x, r.y + (r.height - 16) / 2 - 2, 16, active ? theme::gold : (hov ? theme::ink : theme::muted),
                        active ? utext::Semi : utext::Sans);
            if (active) DrawRectangle((int)x, (int)(r.y + r.height - 3), (int)w, 2, theme::gold);
            else if (hov) DrawRectangle((int)x, (int)(r.y + r.height - 3), (int)w, 1, theme::goldDim);
            if (ui::clicked(hit) && a.tab != t.tab) {
                a.tab = t.tab;
                uanalytics::event("tab_view", {{"tab", tabName(t.tab)}});
                if (t.tab == Tab::Achievements) uach::refresh(a.allDataDirs());
            }
            x += w + 30;
        }
    } else {
        const char* title = a.shelf == Shelf::Unciv ? "Unciv" : a.shelf == Shelf::Gd5 ? "Greater Diplomacy 5"
                          : a.shelf == Shelf::Gd4 ? "Greater Diplomacy 4" : T("Open Doctrines on TempleOS");
        utext::draw(title, x, r.y + (r.height - 22) / 2 - 2, 22, theme::ink, utext::Serif);
    }
    // Links, right-aligned: the website and friends.
    struct L { const char* tip; Icon icon; const char* url; };
    const L links[] = {
        {"Website", Icon::Globe, "https://opendoctrines.pages.dev"},
        {"Wiki", Icon::News, "https://github.com/Pr1nted/Open-Doctrines/wiki"},
        {"Source code", Icon::Link, "https://github.com/Pr1nted/Open-Doctrines"},
        {"itch.io", Icon::Star, "https://pr1nted.itch.io/open-doctrines"},
    };
    float lx = r.x + r.width - 16;
    if (uwindow::needsOwnControls()) {
        // No title bar in borderless fullscreen: draw the two controls it took.
        if (ui::iconButton({lx - 40, r.y + 12, 40, 40}, Icon::Cross, T("Quit"))) a.wantsQuit = true;
        lx -= 44;
        if (ui::iconButton({lx - 40, r.y + 12, 40, 40}, Icon::Box, T("Leave fullscreen (Esc)"))) a.toggleFullscreen();
        lx -= 52;
    }
    Rectangle con{lx - 40, r.y + 12, 40, 40};
    if (ui::iconButton(con, Icon::Terminal, T("Console"), a.consoleOpen)) a.consoleOpen = !a.consoleOpen;
    lx -= 44;
    if (ui::iconButton({lx - 40, r.y + 12, 40, 40}, Icon::Bug, T("Report a problem"))) a.openFeedback(true);
    lx -= 44;
    // Support, on Ko-fi. One quiet icon among the others: no badge, no
    // animation, never a pop-up. It is there for whoever goes looking.
    if (ui::iconButton({lx - 40, r.y + 12, 40, 40}, Icon::Heart, T("Support Open Doctrines on Ko-fi"))) uproc::openUrl("https://ko-fi.com/pr1nted");
    lx -= 48;
    for (auto& l : links) {
        Rectangle b{lx - 40, r.y + 12, 40, 40};
        if (ui::iconButton(b, l.icon, T(l.tip))) uproc::openUrl(l.url);
        lx -= 44;
    }
}

static void drawToasts(App& a) {
    const double now = GetTime();
    a.toasts.erase(std::remove_if(a.toasts.begin(), a.toasts.end(), [&](const Toast& t) { return t.until < now; }), a.toasts.end());
    float y = ui::H() - 24.0f - (a.consoleOpen ? 260.0f : 0.0f);
    for (auto it = a.toasts.rbegin(); it != a.toasts.rend(); ++it) {
        const float w = std::min(560.0f, utext::measure(it->text, 15).x + 44);
        Rectangle r{ui::W() - w - 24, y - 46, w, 42};
        DrawRectangleRounded(r, 0.3f, 8, Color{24, 29, 40, 245});
        DrawRectangleRoundedLinesEx(r, 0.3f, 8, 1, it->error ? theme::danger : theme::goldDim);
        ui::icon(it->error ? Icon::Cross : Icon::Check, r.x + 20, r.y + 21, 14, it->error ? theme::danger : theme::gold);
        utext::draw(utext::ellipsize(it->text, w - 44, 15), r.x + 34, r.y + 12, 15, theme::ink);
        y -= 50;
    }
}

static void consentModal(App& a) {
    // Asked once. The default is no, and both buttons are the same size.
    a.modal = [&a]() -> bool {
        const float w = 580, h = 330;
        Rectangle r{(ui::W() - w) / 2, (ui::H() - h) / 2, w, h};
        a.modalRect = r;
        ui::card(r);
        ui::heading(T("May Unifico send usage statistics?"), r.x + 28, r.y + 24, 24);
        utext::drawWrapped(T("If you agree, the launcher reports which pages you open, which game version you start and roughly how long it ran, "
                             "through the Open Doctrines account service to Google Analytics. Nothing you typed or named is ever sent, Google never "
                             "receives your address, and you can change your mind in Settings at any time. Saying no changes nothing else."),
                           {r.x + 28, r.y + 72, r.width - 56, 170}, 16, theme::muted);
        bool keep = true;
        if (ui::button({r.x + 28, r.y + r.height - 68, 160, 42}, T("No, thanks"), ui::Style::Secondary)) { uanalytics::setConsent(false); keep = false; }
        if (ui::button({r.x + r.width - 28 - 160, r.y + r.height - 68, 160, 42}, T("Allow"), ui::Style::Secondary)) {
            uanalytics::setConsent(true);
            uanalytics::event("launcher_open", {{"launcher_version", UNIFICO_VERSION}, {"platform", upaths::platformTag()}, {"language", ustr::current()}});
            keep = false;
        }
        if (ui::button({r.x + r.width / 2 - 70, r.y + r.height - 68, 140, 42}, T("Privacy policy"), ui::Style::Ghost))
            uproc::openUrl(std::string(UNIFICO_ACCOUNT_ISSUER) + "/privacy");
        return keep;
    };
}

void App::frame() {
    time += GetFrameTime();
    utext::frame();
    pollRunning();
    // Discord: the launcher steps aside while Open Doctrines runs (the game
    // publishes its own presence); for the other games it speaks for them.
    {
        static long long otherSince = 0;
        if (running && running->child && running->child->running()) udiscord::clear();
        else if (other) {
            if (!otherSince) otherSince = (long long)std::time(nullptr);
            udiscord::set(std::string(T("Playing")) + " " + otherName, T("via Unifico"), otherSince);
        } else {
            otherSince = 0;
            const char* where = tab == Tab::Achievements ? T("Looking at achievements") : tab == Tab::Installs ? T("Choosing a version")
                              : tab == Tab::Worlds ? T("Looking through worlds") : tab == Tab::Mods ? T("Browsing mods") : T("In the launcher");
            udiscord::set(where, "Open Doctrines", 0);
        }
        udiscord::tick(GetTime());
    }
    if (!updateKnown && updateJob && updateJob->done) updateJob.reset();
    if (releasesJob && releasesJob->done) { releases = ureleases::cached(); releasesJob.reset(); }
    if (newsJob && newsJob->done) { news = unews::cached(); newsJob.reset(); }
    static double lastFlush = 0;
    if (GetTime() - lastFlush > 20) { uanalytics::flush(); lastFlush = GetTime(); }
    static bool asked = false;
    if (!asked && !modal && Settings::get().analyticsConsent.empty() && time > 1.5f) { asked = true; consentModal(*this); }

    // Hidden while the game runs: keep watching it, draw nothing, use nothing.
    if (hidden) {
        SetTargetFPS(4);
        BeginDrawing();
        EndDrawing();
        return;
    }
    SetTargetFPS(IsWindowFocused() ? 60 : 24);

    // ── SIZE ──
    // F11 (or the Settings switch) for fullscreen. The interface zoom follows
    // the window: laid out against 1220x780, drawn bigger when the window is,
    // so fullscreen on a large display is the same launcher at a readable size
    // rather than small text in a sea of space. A fixed size in Settings wins.
    if (IsKeyPressed(KEY_F11)) toggleFullscreen();
    // Esc always leaves a borderless fullscreen: with no title bar it is the
    // one exit a person will try. (Text fields and modals use Esc first.)
    if (IsKeyPressed(KEY_ESCAPE) && uwindow::needsOwnControls() && !modal && ui::focusedField() < 0) toggleFullscreen();
    {
        float s;
        const std::string& pref = Settings::get().uiScale;
        if (pref.empty() || pref == "auto") {
            const float raw = std::min(GetScreenWidth() / 1220.0f, GetScreenHeight() / 780.0f);
            s = raw <= 1.0f ? 1.0f : 1.0f + (raw - 1.0f) * 0.9f;
            s = std::min(s, 3.0f);
        } else {
            s = std::clamp((float)std::atof(pref.c_str()), 0.75f, 3.0f);
        }
        // Never so big that the minimum layout no longer fits.
        s = std::min(s, std::max(0.75f, std::min(GetScreenWidth() / 960.0f, GetScreenHeight() / 600.0f)));
        ui::setScale(s);
        utext::setScale(s);
    }
    const float W = (float)ui::W(), H = (float)ui::H();
    SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    BeginDrawing();
    ClearBackground(theme::ground);
    Camera2D cam{};
    cam.zoom = ui::scale();
    BeginMode2D(cam);
    ui::beginFrame();
    // --screenshots must not be steered by whoever's mouse is over the window.
    ui::setInput(!modal && shotDir.empty());
    art::backdrop({0, 0, W, H}, time);

    const float consoleH = consoleOpen ? 260.0f : 0.0f;
    drawRail(*this, {0, 0, kRail, H});
    drawTopBar(*this, {kRail, 0, W - kRail, kTop});

    float top = kTop;
    if (updateKnown && !updateJob) {
        Rectangle b{kRail, kTop, W - kRail, 44};
        DrawRectangleRec(b, theme::accentA(30));
        utext::draw(TextFormat(T("Unifico %s is available."), update.version.c_str()), b.x + 26, b.y + 13, 16, theme::ink);
        if (ui::button({b.x + b.width - 190, b.y + 6, 170, 32}, T("Update and restart"), ui::Style::Primary)) {
            updateJob = uupdate::apply(update);
            uanalytics::event("launcher_update", {{"from_version", UNIFICO_VERSION}, {"to_version", update.version}});
        }
        top += 44;
    }
    if (updateJob && updateKnown) {
        Rectangle b{kRail, kTop, W - kRail, 44};
        DrawRectangleRec(b, theme::accentA(30));
        ui::progress({b.x + 26, b.y + 20, b.width * 0.5f, 6}, updateJob->progress.load());
        utext::draw(updateJob->status(), b.x + 26 + b.width * 0.5f + 16, b.y + 13, 15, theme::ink);
        if (updateJob->done) {
            if (updateJob->ok) wantsQuit = true;     // the new version is starting
            else { toast(updateJob->status(), true); updateJob.reset(); updateKnown = false; }
        }
        top += 44;
    }

    content = {kRail, top, W - kRail, H - top - consoleH};
    ui::scissor((int)content.x, (int)content.y, (int)content.width, (int)content.height);
    if (shelf != Shelf::OpenDoctrines) drawOtherGame(*this, content);
    else switch (tab) {
        case Tab::Play: drawPlay(*this, content); break;
        case Tab::Installs: drawInstalls(*this, content); break;
        case Tab::Worlds: drawWorlds(*this, content); break;
        case Tab::Mods: drawMods(*this, content); break;
        case Tab::Achievements: drawAchievements(*this, content); break;
        case Tab::Servers: drawServers(*this, content); break;
        case Tab::Account: drawAccount(*this, content); break;
        case Tab::Settings: drawSettings(*this, content); break;
        case Tab::Tools: drawTools(*this, content); break;
        case Tab::Admin: drawAdmin(*this, content); break;
    }
    ui::endScissor();
    if (consoleOpen) drawConsole(*this, {kRail, H - consoleH, W - kRail, consoleH});

    ui::deferredOverlays();
    if (modal) {
        DrawRectangle(0, 0, (int)W, (int)H, Color{5, 7, 10, 170});
        ui::setInput(true);
        ui::beginFrame();
        if (!modal()) { modal = nullptr; ui::clearFocus(); }
        ui::deferredOverlays();
    }
    drawToasts(*this);
    EndMode2D();
    EndDrawing();
    if (!shotDir.empty()) screenshotStep();
}

void App::screenshotStep() {
    // Each page gets a couple of seconds to settle (fonts, art, the release
    // list) before its picture is taken.
    struct S { Tab t; Shelf s; const char* name; };
    static const S pages[] = {
        {Tab::Play, Shelf::OpenDoctrines, "play"}, {Tab::Installs, Shelf::OpenDoctrines, "installations"},
        {Tab::Worlds, Shelf::OpenDoctrines, "worlds"}, {Tab::Mods, Shelf::OpenDoctrines, "mods"},
        {Tab::Achievements, Shelf::OpenDoctrines, "achievements"}, {Tab::Servers, Shelf::OpenDoctrines, "servers"},
        {Tab::Account, Shelf::OpenDoctrines, "account"},
        {Tab::Admin, Shelf::OpenDoctrines, "admin"}, {Tab::Settings, Shelf::OpenDoctrines, "settings"},
        {Tab::Tools, Shelf::OpenDoctrines, "tools"}, {Tab::Play, Shelf::TempleOS, "templeos"},
        {Tab::Play, Shelf::Unciv, "unciv"},
    };
    static int page = -1;
    static double at = 0;
    modal = nullptr;
    Settings::get().otherGamesUnlocked = true;
    if (page < 0 || GetTime() - at > 2.5) {
        if (page >= 0) {
            Image im = LoadImageFromScreen();
            ExportImage(im, (shotDir + "/" + pages[page].name + ".png").c_str());
            UnloadImage(im);
        }
        ++page;
        if (page >= (int)(sizeof pages / sizeof pages[0])) { wantsQuit = true; return; }
        tab = pages[page].t;
        shelf = pages[page].s;
        at = GetTime();
    }
}
