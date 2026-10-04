// Worlds: the saves of one installation, managed without starting the game.
#include "App.h"
#include "core/FileDialog.h"
#include "core/Fs.h"
#include "od/Worlds.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

#include <ctime>

namespace {
ui::Scroll g_scroll;
std::vector<World> g_worlds;
std::string g_forDir;
double g_loadedAt = 0;
int g_renaming = -1;
std::string g_newName;

bool gameUsing(App& a, const Install& i) {
    return a.running && a.running->child && a.running->child->running() && a.running->install.dataDir == i.dataDir;
}
}  // namespace

void drawWorlds(App& a, Rectangle r) {
    const float x = r.x + 40, w = r.width - 80;
    float y = r.y + 28;
    ui::heading(T("Worlds"), x, y, 30);
    Install inst;
    if (!installPicker(a, {x + w - 320, y, 320, 38}, inst)) return;
    y += 50;
    if (g_forDir != inst.dataDir || GetTime() - g_loadedAt > 5) {
        g_worlds = uworlds::list(inst.dataDir);
        g_forDir = inst.dataDir;
        g_loadedAt = GetTime();
    }
    const bool locked = gameUsing(a, inst);
    if (locked) {
        utext::draw(T("This version is running. Worlds are read-only until it closes."), x, y, 15, theme::gold);
        y += 28;
    }
    // Whole-state actions.
    float bx = x;
    auto top = [&](float bw, const char* label) { bool c = ui::button({bx, y, bw, 36}, label, ui::Style::Secondary, locked); bx += bw + 10; return c; };
    if (top(150, T("Import world..."))) {
        std::string f = udialog::openFile(T("Choose a world (.odsv)"), "odsv");
        std::string err;
        if (!f.empty()) { if (uworlds::importFile(f, inst.dataDir, &err)) { a.toast(T("World imported.")); g_loadedAt = 0; } else a.toast(err, true); }
    }
    if (top(170, T("Export .odstate..."))) {
        char name[64];
        std::time_t t = std::time(nullptr);
        std::strftime(name, sizeof name, "OpenDoctrines-%Y%m%d-%H%M.odstate", std::localtime(&t));
        std::string dest = udialog::saveFile(T("Save everything as a .odstate"), name);
        int n = 0;
        std::string err;
        if (!dest.empty()) {
            if (uworlds::exportState(inst.dataDir, dest, &n, &err)) a.toast(TextFormat(T("Saved %d files."), n));
            else a.toast(err, true);
        }
    }
    if (top(170, T("Import .odstate..."))) {
        std::string f = udialog::openFile(T("Choose a .odstate"), "odstate");
        if (!f.empty()) {
            a.confirm(T("Restore this .odstate?"),
                      T("Its files are written over this version's. Achievements in it are only offered: the game checks each one's signature and keeps the genuine ones."),
                      T("Restore"), [&a, f, inst] {
                          int n = 0;
                          std::string err;
                          if (uworlds::importState(inst.dataDir, f, &n, &err)) a.toast(TextFormat(T("Restored %d files."), n) + (err.empty() ? "" : "  " + err));
                          else a.toast(err, true);
                          g_loadedAt = 0;
                      });
        }
    }
    if (ui::button({bx, y, 130, 36}, T("Open folder"), ui::Style::Ghost)) uproc::revealInFileManager(inst.dataDir + "/saves");
    y += 56;

    Rectangle list{r.x, y, r.width, r.y + r.height - y};
    ui::beginScroll(list, g_scroll);
    float yy = y - g_scroll.y;
    if (g_worlds.empty()) utext::draw(T("No worlds yet. Start one in the game and it appears here."), x, yy + 10, 15, theme::faint);
    for (size_t k = 0; k < g_worlds.size(); ++k) {
        const World& wd = g_worlds[k];
        Rectangle card{x, yy, w, 78};
        ui::card(card);
        if ((int)k == g_renaming) {
            ui::textField({x + 20, yy + 16, w * 0.45f, 38}, g_newName, 3000);
            if (ui::button({x + 30 + w * 0.45f, yy + 18, 90, 34}, T("Save"), ui::Style::Primary) || IsKeyPressed(KEY_ENTER)) {
                std::string err;
                if (!uworlds::rename(wd, g_newName, &err)) a.toast(err, true);
                g_renaming = -1;
                g_loadedAt = 0;
            }
            if (ui::button({x + 130 + w * 0.45f, yy + 18, 90, 34}, T("Cancel"), ui::Style::Ghost)) g_renaming = -1;
        } else {
            utext::draw(utext::ellipsize(wd.name, w * 0.45f, 19), x + 20, yy + 14, 19, theme::ink, utext::Semi);
            std::string sub = TextFormat(T("Turn %d"), wd.turns) + std::string("  ·  ") + ufs::humanBytes(wd.size);
            if (!wd.version.empty()) sub += "  ·  " + wd.version;
            if (wd.multiplayer) sub += std::string("  ·  ") + T("multiplayer");
            if (!wd.lastPlayed.empty()) sub += "  ·  " + wd.lastPlayed.substr(0, 16);
            utext::draw(utext::ellipsize(sub, w * 0.5f, 14), x + 20, yy + 46, 14, theme::muted);
        }
        float bx2 = x + w - 16;
        auto rb = [&](float bw, const char* label, ui::Style s = ui::Style::Ghost) {
            bx2 -= bw; bool c = ui::button({bx2, yy + 20, bw, 36}, label, s, locked && s != ui::Style::Primary); bx2 -= 6; return c; };
        if (rb(84, T("Delete"), ui::Style::Danger)) {
            World copy = wd;
            a.confirm(T("Delete this world?"), TextFormat(T("\"%s\" is deleted from this computer. This cannot be undone."), wd.name.c_str()),
                      T("Delete"), [&a, copy] { std::string e; if (!uworlds::remove(copy, &e)) a.toast(e, true); g_loadedAt = 0; }, true);
        }
        if (rb(84, T("Export"))) {
            std::string dest = udialog::saveFile(T("Export world"), wd.file);
            std::string err;
            if (!dest.empty()) { if (uworlds::exportTo(wd, dest, &err)) a.toast(T("Exported.")); else a.toast(err, true); }
        }
        if (a.installs.size() > 1 && rb(96, T("Copy to..."))) {
            World copy = wd;
            std::string from = inst.tag;
            a.modal = [&a, copy, from]() -> bool {
                Rectangle m{(ui::W() - 460) / 2.0f, (ui::H() - 360) / 2.0f, 460, 360};
                a.modalRect = m;
                ui::card(m);
                ui::heading(T("Copy to which version?"), m.x + 24, m.y + 20, 22);
                float yy2 = m.y + 66;
                bool keep = true;
                for (auto& other : a.installs) {
                    if (other.tag == from) continue;
                    if (ui::button({m.x + 24, yy2, m.width - 48, 38}, other.label().c_str())) {
                        std::string e;
                        if (uworlds::copyTo(copy, other.dataDir, &e)) a.toast(TextFormat(T("Copied to %s."), other.version.c_str()));
                        else a.toast(e, true);
                        keep = false;
                    }
                    yy2 += 46;
                    if (yy2 > m.y + m.height - 60) break;
                }
                if (ui::button({m.x + m.width - 124, m.y + m.height - 54, 100, 36}, T("Cancel"), ui::Style::Ghost) || IsKeyPressed(KEY_ESCAPE)) keep = false;
                return keep;
            };
        }
        if (rb(100, T("Duplicate"))) { std::string e; if (!uworlds::duplicate(wd, &e)) a.toast(e, true); g_loadedAt = 0; }
        if (rb(90, T("Rename"))) { g_renaming = (int)k; g_newName = wd.name; }
        if (rb(84, T("Play"), ui::Style::Primary)) { a.selectedTag = inst.tag; a.play(wd.path); }
        yy += 88;
    }
    ui::endScroll(list, g_scroll, yy + g_scroll.y - y + 20);
}
