// Tools: the map translator, logs, and the TempleOS edition's front door.
#include "App.h"
#include "core/FileDialog.h"
#include "core/Fs.h"
#include "core/Paths.h"
#include "od/Achievements.h"
#include "od/Analytics.h"
#include "tools/OtherGames.h"
#include "tools/Translate.h"
#include "ui/Art.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace {
ui::Scroll g_scroll;
std::string g_in, g_kind;
JobPtr g_job;
std::string g_lastOut;
}

void drawTools(App& a, Rectangle r) {
    ui::beginScroll(r, g_scroll);
    const float x = r.x + 40, w = r.width - 80;
    float y = r.y + 28 - g_scroll.y;
    ui::heading(T("Tools"), x, y, 30);
    y += 56;

    // ---- map translator ----
    ui::card({x, y, w, 300});
    ui::icon(Icon::Map, x + 34, y + 34, 26, theme::gold);
    utext::draw(T("Map translator"), x + 62, y + 20, 20, theme::ink, utext::Serif);
    utext::draw(TextFormat(T("open-dragoman %s: Open Doctrines, Greater Diplomacy 5 and Unciv maps, both ways."), utranslate::version().c_str()),
                x + 62, y + 48, 14, theme::muted);
    float iy = y + 84;
    if (!utranslate::available()) {
        utext::draw(T("This build of the launcher has no translator."), x + 24, iy, 15, theme::faint);
    } else {
        utext::draw(T("Map to translate"), x + 24, iy, 14, theme::faint, utext::Semi);
        Rectangle field{x + 24, iy + 20, w - 330, 38};
        DrawRectangleRounded(field, 0.2f, 8, theme::sunken);
        utext::draw(utext::ellipsize(g_in.empty() ? std::string(T("No map chosen")) : g_in, field.width - 20, 15), field.x + 10, field.y + 10, 15,
                    g_in.empty() ? theme::faint : theme::ink);
        if (ui::button({x + w - 296, iy + 21, 130, 36}, T("Choose file..."), ui::Style::Secondary)) {
            std::string f = udialog::openFile(T("A .odmap, a Unciv map, or a GD5 map's map.json"));
            if (!f.empty()) {
                // A GD5 map is a folder; picking any file inside it means the folder.
                g_in = f;
                g_kind = utranslate::detect(g_in);
                if (g_kind.empty()) { g_in = fs::path(f).parent_path().string(); g_kind = utranslate::detect(g_in); }
            }
        }
        if (ui::button({x + w - 156, iy + 21, 132, 36}, T("Choose folder..."), ui::Style::Ghost)) {
            std::string f = udialog::openFolder(T("A Greater Diplomacy 5 map folder"));
            if (!f.empty()) { g_in = f; g_kind = utranslate::detect(f); }
        }
        iy += 70;
        if (!g_in.empty()) utext::draw(g_kind.empty() ? T("Not a map this translator recognises.") : (std::string(T("Detected:")) + " " + T(g_kind.c_str())),
                                       x + 24, iy, 14, g_kind.empty() ? theme::danger : theme::gold);
        iy += 30;
        const bool busy = g_job && !g_job->done;
        auto go = [&](utranslate::Target t, const std::string& defaultName, const std::string& intoDir) {
            std::string out;
            if (!intoDir.empty()) out = intoDir + "/" + defaultName;
            else if (t == utranslate::Target::Gd5) { std::string d = udialog::openFolder(T("Where to put the Greater Diplomacy 5 map")); if (!d.empty()) out = d + "/" + defaultName; }
            else out = udialog::saveFile(T("Save the translated map"), defaultName);
            if (out.empty()) return;
            if (ufs::exists(out)) { a.toast(T("Something already exists there; nothing was overwritten."), true); return; }
            g_lastOut = out;
            g_job = utranslate::convert(g_in, out, t);
            uanalytics::event("map_translate", {{"direction", t == utranslate::Target::Gd5 ? "to_gd5" : t == utranslate::Target::Unciv ? "to_unciv" : "to_od"}});
        };
        const std::string stem = fs::path(g_in).stem().string();
        const bool isOd = g_kind == "Open Doctrines map";
        float bx = x + 24;
        auto tb = [&](const char* label, bool enabled) { bool c = ui::button({bx, iy, 210, 38}, label, ui::Style::Secondary, !enabled || busy); bx += 220; return c; };
        if (tb(T("To Greater Diplomacy 5"), isOd)) go(utranslate::Target::Gd5, stem, uother::installed(uother::Game::Gd5) ? uother::mapsDir(uother::Game::Gd5) : "");
        if (tb(T("To Unciv"), isOd)) go(utranslate::Target::Unciv, stem + ".json", uother::installed(uother::Game::Unciv) ? uother::mapsDir(uother::Game::Unciv) : "");
        if (tb(T("To Open Doctrines"), !g_kind.empty() && !isOd)) go(utranslate::Target::OdMap, stem + ".odmap", "");
        iy += 52;
        if (busy) { ui::progress({x + 24, iy, w - 48, 6}, -1); }
        else if (g_job && g_job->done) {
            if (g_job->ok) {
                utext::draw(utext::ellipsize(g_job->status(), w - 200, 14), x + 24, iy, 14, theme::ok);
                if (ui::button({x + w - 150, iy - 8, 126, 30}, T("Show it"), ui::Style::Ghost)) uproc::revealInFileManager(g_lastOut);
                static std::string claimed;
                if (claimed != g_lastOut) { claimed = g_lastOut; uach::claim("translator"); }
            } else {
                utext::draw(utext::ellipsize(g_job->status(), w - 48, 14), x + 24, iy, 14, theme::danger);
            }
        }
    }
    y += 316;
    // Translator notes from the last run.
    if (g_job && g_job->done) {
        auto notes = utranslate::lastNotes();
        if (!notes.empty()) {
            utext::draw(T("What could not cross exactly:"), x, y, 15, theme::muted, utext::Semi);
            y += 26;
            for (size_t i = 0; i < notes.size() && i < 12; ++i) y += utext::drawWrapped("• " + notes[i], {x + 10, y, w - 20, 0}, 14, theme::faint) + 2;
            y += 12;
        }
    }

    // ---- logs ----
    ui::card({x, y, w, 120});
    ui::icon(Icon::Terminal, x + 34, y + 34, 24, theme::gold);
    utext::draw(T("Logs"), x + 62, y + 20, 20, theme::ink, utext::Serif);
    utext::draw(T("Every launch writes its own log. Attach one when you report a problem."), x + 62, y + 48, 14, theme::muted);
    if (ui::button({x + 24, y + 74, 160, 34}, T("Open the console"), ui::Style::Secondary)) a.consoleOpen = true;
    if (ui::button({x + 194, y + 74, 170, 34}, T("Download a log..."), ui::Style::Secondary)) a.saveLog();
    if (ui::button({x + 374, y + 74, 160, 34}, T("Logs folder"), ui::Style::Ghost)) uproc::revealInFileManager(upaths::logsDir());
    y += 136;

    // ---- TempleOS ----
    Rectangle tcard{x, y, w, 150};
    ui::card(tcard);
    art::gameCard({x + 16, y + 16, 168, 118}, art::Game::TempleOS, false, a.time);
    utext::draw(T("Open Doctrines on TempleOS"), x + 204, y + 22, 20, theme::ink, utext::Serif);
    utext::drawWrapped(T("The game, rewritten in HolyC for Terry Davis's operating system, run in an emulator with one click."),
                       {x + 204, y + 52, w - 230, 50}, 14, theme::muted);
    if (ui::button({x + 204, y + 100, 150, 34}, T("Open"), ui::Style::Primary)) a.shelf = Shelf::TempleOS;
    y += 170;
    ui::endScroll(r, g_scroll, y + g_scroll.y - r.y);
}
