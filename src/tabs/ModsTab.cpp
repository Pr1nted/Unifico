// Mods: what an installation has, switched on or off, and the directory.
#include "App.h"
#include "core/FileDialog.h"
#include "core/Fs.h"
#include "od/Account.h"
#include "od/Analytics.h"
#include "od/Mods.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

namespace {
ui::Scroll g_scroll;
std::vector<DirectoryMod> g_dir;
JobPtr g_dirJob, g_modJob;
std::string g_dirError;
std::shared_ptr<std::vector<DirectoryMod>> g_dirPending;
}

void drawMods(App& a, Rectangle r) {
    const float x = r.x + 40, w = r.width - 80;
    float y0 = r.y + 28;
    ui::heading(T("Mods"), x, y0, 30);
    Install inst;
    if (!installPicker(a, {x + w - 320, y0, 320, 38}, inst)) return;
    if (!g_dirJob) {
        auto out = std::make_shared<std::vector<DirectoryMod>>();
        g_dirPending = out;
        g_dirJob = ujobs::run("mod directory", [out](Job& j) {
            std::string e;
            *out = umods::browse(Account::get().issuer(), &e);
            if (!e.empty()) j.setStatus(e);
            return e.empty();
        });
    }
    if (g_dirJob->done && g_dirPending) { g_dir = *g_dirPending; g_dirError = g_dirJob->ok ? "" : g_dirJob->status(); g_dirPending.reset(); }
    if (g_modJob && g_modJob->done) { a.toast(g_modJob->status(), !g_modJob->ok); g_modJob.reset(); }

    Rectangle body{r.x, y0 + 54, r.width, r.height - 82};
    ui::beginScroll(body, g_scroll);
    float y = body.y - g_scroll.y;
    ui::heading(T("Installed"), x, y, 20);
    if (ui::button({x + w - 170, y - 4, 170, 34}, T("Install from file..."), ui::Style::Secondary)) {
        std::string f = udialog::openFile(T("Choose a mod (.odmod)"), "odmod");
        std::string err;
        if (!f.empty()) {
            if (umods::installFile(inst.dataDir, f, &err)) { a.toast(T("Mod installed. Switch it on below.")); uanalytics::event("mod_install", {{"source", "file"}}); }
            else a.toast(err, true);
        }
    }
    y += 40;
    auto mods = umods::installed(inst.dataDir);
    if (mods.empty()) { utext::draw(T("No mods in this version."), x, y, 15, theme::faint); y += 30; }
    for (auto& m : mods) {
        ui::card({x, y, w, 58});
        utext::draw(m.id, x + 20, y + 10, 17, theme::ink, utext::Semi);
        utext::draw(ufs::humanBytes(m.size) + (m.known ? "" : std::string("  ·  ") + T("not yet loaded by the game")), x + 20, y + 33, 13, theme::faint);
        bool on = m.enabled;
        if (ui::toggle({x + w - 330, y + 9, 180, 40}, T("Enabled"), &on)) umods::setEnabled(inst.dataDir, m.id, on);
        if (ui::button({x + w - 120, y + 12, 100, 34}, T("Remove"), ui::Style::Danger)) {
            InstalledMod copy = m;
            std::string dir = inst.dataDir;
            a.confirm(T("Remove this mod?"), copy.file, T("Remove"), [&a, copy, dir] { std::string e; if (!umods::remove(dir, copy, &e)) a.toast(e, true); }, true);
        }
        y += 66;
    }
    y += 16;
    ui::heading(T("Directory"), x, y, 20);
    y += 32;
    utext::drawWrapped(T("Published by players and reviewed before listing. Each download is checked against the checksum its author declared."),
                       {x, y, w, 40}, 14, theme::faint);
    y += 34;
    if (!g_dirJob->done) { ui::spinner(x + 12, y + 14, 10); y += 40; }
    else if (!g_dirError.empty()) { utext::draw(g_dirError, x, y, 15, theme::danger); y += 30; }
    const float colW = (w - 16) / 2;
    int k = 0;
    for (auto& d : g_dir) {
        const float cx = x + (k % 2) * (colW + 16);
        if (k % 2 == 0 && k) y += 132;
        Rectangle card{cx, y, colW, 120};
        ui::card(card);
        utext::draw(utext::ellipsize(d.name, colW - 150, 17), cx + 18, y + 14, 17, theme::ink, utext::Semi);
        utext::draw(utext::ellipsize(d.version + "  ·  " + d.by + (d.sideLabel.empty() ? "" : "  ·  " + d.sideLabel), colW - 36, 13), cx + 18, y + 38, 13, theme::faint);
        utext::drawWrapped(utext::ellipsize(d.summary, (colW - 36) * 2, 14), {cx + 18, y + 60, colW - 36, 40}, 14, theme::muted);
        if (ui::button({cx + colW - 120, y + 10, 104, 32}, T("Install"), ui::Style::Secondary, (bool)g_modJob)) {
            g_modJob = umods::installFromDirectory(Account::get().issuer(), d, inst.dataDir);
            uanalytics::event("mod_install", {{"source", "directory"}});
        }
        ++k;
    }
    if (k) y += 132;
    ui::endScroll(body, g_scroll, y + g_scroll.y - body.y + 20);
}
