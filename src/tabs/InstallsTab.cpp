// Installations: every released version, what is on disk, and what each
// costs in space. Launch options per version.
#include "App.h"
#include "core/FileDialog.h"
#include "core/Fs.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "od/Analytics.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

#include <map>

namespace {
ui::Scroll g_scroll;
std::string g_expanded;            // tag whose launch options are open
std::map<std::string, DiskUsage> g_usage;
JobPtr g_usageJob;
std::shared_ptr<std::map<std::string, DiskUsage>> g_pending;
std::vector<std::string> g_detected;
bool g_detectedDone = false;

void refreshUsage(App& a) {
    auto installs = a.installs;
    auto out = std::make_shared<std::map<std::string, DiskUsage>>();
    g_pending = out;
    // Walking a game folder takes a moment; the worker fills its own map and
    // the page takes it over only once the job is done.
    g_usageJob = ujobs::run("disk usage", [installs, out](Job&) {
        for (auto& i : installs) (*out)[i.tag] = uinstalls::usage(i);
        return true;
    });
}
}  // namespace

void drawInstalls(App& a, Rectangle r) {
    if (!g_detectedDone) { g_detected = uinstalls::detectExisting(); g_detectedDone = true; }
    if (!g_usageJob) refreshUsage(a);
    if (g_usageJob && g_usageJob->done && g_pending) { g_usage = *g_pending; g_pending.reset(); }
    if (a.installJob && a.installJob->done) {
        if (a.installJob->ok) { a.toast(a.installJob->status()); a.reloadInstalls(); refreshUsage(a); }
        else a.toast(a.installJob->status(), true);
        a.installJob.reset();
    }

    ui::beginScroll(r, g_scroll);
    const float x = r.x + 40, w = r.width - 80;
    float y = r.y + 28 - g_scroll.y;
    ui::heading(T("Installations"), x, y, 30);
    y += 44;

    // The disk report.
    uint64_t total = 0;
    for (auto& [t, u] : g_usage) total += u.total;
    const uint64_t freeB = ufs::freeSpace(upaths::versionsDir());
    utext::draw(TextFormat(T("Game versions use %s. %s free on this disk."), ufs::humanBytes(total).c_str(), ufs::humanBytes(freeB).c_str()),
                x, y, 16, theme::muted);
    if (ui::button({x + w - 150, y - 8, 150, 34}, T("Refresh"), ui::Style::Ghost)) {
        a.releasesJob = ujobs::run("releases", [](Job&) { return ureleases::refresh(nullptr); });
        a.reloadInstalls();
        refreshUsage(a);
    }
    y += 40;

    if (a.installJob) {
        ui::card({x, y, w, 70});
        utext::draw(a.installJob->status(), x + 20, y + 14, 16, theme::ink);
        ui::progress({x + 20, y + 44, w - 160, 8}, a.installJob->progress.load());
        if (ui::button({x + w - 120, y + 18, 100, 34}, T("Cancel"), ui::Style::Ghost)) a.installJob->cancel = true;
        y += 86;
    }

    // Installed first, then what can be installed.
    ui::heading(T("On this computer"), x, y, 20);
    y += 34;
    if (a.installs.empty()) { utext::draw(T("Nothing yet. Pick a version below."), x, y, 15, theme::faint); y += 32; }
    for (auto& i : a.installs) {
        const bool open = g_expanded == i.tag;
        const float h = open ? 300 : 76;
        Rectangle card{x, y, w, h};
        ui::card(card);
        utext::draw(i.version, x + 20, y + 14, 20, theme::ink, utext::Semi);
        const DiskUsage u = g_usage.count(i.tag) ? g_usage[i.tag] : DiskUsage{};
        std::string line = i.external ? std::string(T("Existing installation")) + "  ·  " + i.root
                                      : TextFormat(T("%s  ·  worlds %s  ·  mods %s"), ufs::humanBytes(u.total).c_str(),
                                                   ufs::humanBytes(u.saves).c_str(), ufs::humanBytes(u.mods).c_str());
        utext::draw(utext::ellipsize(line, w - 480, 14), x + 20, y + 44, 14, theme::muted);
        float bx = x + w - 20;
        auto btn = [&](float bw, const char* label, ui::Style s) { bx -= bw; bool c = ui::button({bx, y + 20, bw, 36}, label, s); bx -= 8; return c; };
        if (btn(110, i.external ? T("Forget") : T("Uninstall"), ui::Style::Danger)) {
            Install copy = i;
            a.confirm(TextFormat(T("Remove %s?"), i.version.c_str()),
                      i.external ? T("The launcher stops listing it. Nothing on disk is deleted.")
                                 : T("The game files are deleted. Its worlds are first copied to the launcher's kept-worlds folder."),
                      i.external ? T("Forget") : T("Uninstall"), [&a, copy] {
                          std::string err;
                          if (uinstalls::uninstall(copy, true, &err)) {
                              a.toast(TextFormat(T("%s removed."), copy.version.c_str()));
                              uanalytics::event("version_uninstall", {{"game_version", copy.version}});
                          } else a.toast(err, true);
                          a.reloadInstalls();
                          refreshUsage(a);
                      }, true);
        }
        if (btn(130, open ? T("Close options") : T("Launch options"), ui::Style::Ghost)) g_expanded = open ? "" : i.tag;
        if (btn(100, T("Folder"), ui::Style::Ghost)) uproc::revealInFileManager(i.root);
        if (btn(100, T("Play"), ui::Style::Primary)) { a.selectedTag = i.tag; a.play(); }

        if (open) {
            LaunchOptions& lo = Settings::get().optionsFor(i.tag);
            float oy = y + 88;
            utext::draw(T("Extra arguments"), x + 20, oy, 14, theme::faint, utext::Semi);
            if (ui::textField({x + 20, oy + 20, w / 2 - 30, 36}, lo.args, 1000 + (int)(std::hash<std::string>{}(i.tag) % 1000),
                              "--resource-limit 80")) Settings::get().save();
            utext::draw(T("Environment (KEY=value, one per line)"), x + w / 2 + 10, oy, 14, theme::faint, utext::Semi);
            std::string envOne = lo.env;
            for (char& c : envOne) if (c == '\n') c = ';';
            if (ui::textField({x + w / 2 + 10, oy + 20, w / 2 - 30, 36}, envOne, 2000 + (int)(std::hash<std::string>{}(i.tag) % 1000),
                              "OD_DATA_DIR=/path")) {
                for (char& c : envOne) if (c == ';') c = '\n';
                lo.env = envOne;
                Settings::get().save();
            }
            oy += 74;
            utext::draw(T("Memory limit"), x + 20, oy, 14, theme::faint, utext::Semi);
            float mb = (float)lo.memoryLimitMB;
            if (ui::slider({x + 20, oy + 18, w / 2 - 30, 30}, &mb, 0, 16384, mb < 1 ? T("none") : "%.0f MB")) {
                lo.memoryLimitMB = mb < 256 ? 0 : (int)(mb / 256) * 256;
                Settings::get().save();
            }
            utext::draw(
#if defined(__APPLE__)
                T("macOS cannot enforce a hard limit; the launcher stops the game if it goes over."),
#else
                T("The game cannot allocate past this; leave it at none unless you need to."),
#endif
                x + 20, oy + 54, 13, theme::faint);
            bool con = lo.console;
            if (ui::toggle({x + w / 2 + 10, oy + 12, w / 2 - 30, 40}, T("Open the console when this version starts"), &con)) {
                lo.console = con;
                Settings::get().save();
            }
        }
        y += h + 12;
    }

    // Existing copies elsewhere on the machine: the seamless switch.
    y += 8;
    ui::heading(T("Already have the game?"), x, y, 20);
    y += 32;
    utext::drawWrapped(T("Point the launcher at a copy you installed before. It plays that copy where it is, with its worlds, and never deletes it."),
                       {x, y, w - 200, 40}, 15, theme::muted);
    if (ui::button({x + w - 180, y - 4, 180, 36}, T("Choose a folder..."), ui::Style::Secondary)) {
        std::string p = udialog::openFolder(T("The folder holding Open Doctrines"));
        if (!p.empty()) {
            std::string err;
            if (uinstalls::adopt(p, &err)) { a.reloadInstalls(); a.toast(T("Added.")); } else a.toast(err, true);
        }
    }
    y += 48;
    for (auto& d : g_detected) {
        ui::card({x, y, w, 52});
        utext::draw(utext::ellipsize(d, w - 200, 15), x + 20, y + 16, 15, theme::ink);
        if (ui::button({x + w - 140, y + 9, 120, 34}, T("Use this"), ui::Style::Primary)) {
            std::string err;
            if (uinstalls::adopt(d, &err)) { a.reloadInstalls(); g_detectedDone = false; } else a.toast(err, true);
        }
        y += 60;
    }

    // Everything released.
    y += 14;
    ui::heading(T("Available versions"), x, y, 20);
    bool pre = Settings::get().showPrereleases;
    if (ui::toggle({x + w - 300, y - 4, 300, 36}, T("Show pre-releases"), &pre)) { Settings::get().showPrereleases = pre; Settings::get().save(); }
    y += 40;
    if (a.releases.empty()) {
        utext::draw(a.releasesJob ? T("Loading the release list...") : T("The release list could not be loaded."), x, y, 15, theme::faint);
        y += 30;
    }
    const std::string asset = ureleases::gameAssetName();
    for (auto& rel : a.releases) {
        if (rel.prerelease && !pre) continue;
        bool have = false;
        for (auto& i : a.installs) if (i.tag == rel.tag) have = true;
        const ReleaseAsset* as = rel.assetFor(asset);
        Rectangle card{x, y, w, 60};
        ui::card(card, false);
        utext::draw(rel.version, x + 20, y + 10, 18, theme::ink, utext::Semi);
        std::string sub = rel.publishedAt.substr(0, 10);
        if (rel.prerelease) sub += std::string("  ·  ") + T("pre-release");
        if (as) sub += "  ·  " + ufs::humanBytes(as->size);
        else sub += std::string("  ·  ") + T("no build for this computer");
        utext::draw(sub, x + 20, y + 34, 13, theme::faint);
        if (have) utext::draw(T("Installed"), x + w - 120, y + 20, 15, theme::ok);
        else if (as && !a.installJob) {
            if (ui::button({x + w - 140, y + 12, 120, 36}, T("Install"), ui::Style::Secondary)) {
                a.installJob = uinstalls::install(rel);
                uanalytics::event("version_install", {{"game_version", rel.version}, {"platform", upaths::platformTag()}});
            }
        }
        y += 68;
    }
    ui::endScroll(r, g_scroll, y + g_scroll.y - r.y + 30);
}
