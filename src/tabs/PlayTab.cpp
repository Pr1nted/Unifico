// Play: the big picture, the version, the button. Then the news.
#include "App.h"
#include "core/Settings.h"
#include "od/Account.h"
#include "od/Achievements.h"
#include "od/Support.h"
#include "od/Playtime.h"
#include "ui/Art.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace {
ui::Scroll g_scroll;
}

bool installPicker(App& a, Rectangle r, Install& out) {
    if (a.installs.empty()) {
        // Laid out INSIDE the picker's own rectangle, right-aligned: the
        // button where the dropdown would be, the sentence to its left. It used
        // to start at r.x and run a button past the right edge of the page.
        const float bw = std::min(170.0f, r.width * 0.5f);
        const Rectangle btn{r.x + r.width - bw, r.y, bw, r.height};
        const char* msg = T("No version is installed yet.");
        utext::draw(msg, btn.x - 16 - utext::measure(msg, 16).x, r.y + (r.height - 16) / 2 - 1, 16, theme::muted);
        if (ui::button(btn, T("Install one"), ui::Style::Primary)) a.tab = Tab::Installs;
        return false;
    }
    std::vector<std::string> labels;
    int sel = 0;
    for (size_t i = 0; i < a.installs.size(); ++i) {
        labels.push_back(a.installs[i].label());
        if (a.installs[i].tag == a.pageInstallTag) sel = (int)i;
    }
    int n = ui::dropdown(r, labels, sel, 901);
    if (n != sel) a.pageInstallTag = a.installs[(size_t)n].tag;
    out = a.installs[(size_t)n];
    return true;
}

void drawPlay(App& a, Rectangle r) {
    const float heroH = std::max(320.0f, r.height * 0.62f);
    Rectangle hero{r.x, r.y, r.width, heroH};
    if (GetTime() - a.heroAt > 14) { a.hero++; a.heroAt = GetTime(); }
    art::hero(hero, a.hero, a.time);
    art::compass(hero.x + hero.width - 90, hero.y + 90, 56, a.time, Color{232, 228, 218, 60});

    // Title over the picture.
    art::gameLogo(hero.x + 48, hero.y + 46, 54, theme::ink);
    utext::draw(T("A grand strategy game about the whole world at once."), hero.x + 50, hero.y + 114, 19, Color{232, 228, 218, 220});
    const int got = uach::grantedCount();
    if (got > 0) {
        Rectangle chip{hero.x + 50, hero.y + 150, 210, 30};
        DrawRectangleRounded(chip, 0.5f, 8, Color{9, 11, 17, 170});
        ui::icon(Icon::Trophy, chip.x + 18, chip.y + 15, 16, theme::gold);
        utext::draw(TextFormat(T("%d of %d achievements"), got, odach::kCatalogCount), chip.x + 34, chip.y + 7, 15, theme::ink);
        if (ui::clicked(chip)) a.tab = Tab::Achievements;
    }

    // The bar along the bottom of the picture: version, PLAY, who.
    Rectangle bar{r.x, hero.y + heroH - 96, r.width, 96};
    DrawRectangleGradientV((int)bar.x, (int)bar.y, (int)bar.width, (int)bar.height, Color{12, 15, 22, 0}, Color{12, 15, 22, 240});
    const float playW = 300, playH = 64;
    Rectangle playR{r.x + (r.width - playW) / 2, bar.y + 16, playW, playH};
    const bool runningNow = a.running && a.running->child && a.running->child->running();

    // Version picker, left.
    Install sel;
    const bool have = a.selectedInstall(sel);
    std::vector<std::string> labels;
    int idx = 0;
    for (size_t i = 0; i < a.installs.size(); ++i) {
        labels.push_back(a.installs[i].label());
        if (have && a.installs[i].tag == sel.tag) idx = (int)i;
    }
    if (!labels.empty()) {
        utext::draw(T("Version"), r.x + 40, bar.y + 18, 13, theme::faint, utext::Semi);
        int n = ui::dropdown({r.x + 40, bar.y + 38, 240, 38}, labels, idx, 902);
        if (n != idx) { a.selectedTag = a.installs[(size_t)n].tag; Settings::get().selectedVersion = a.selectedTag; Settings::get().save(); a.applyAccent(); }
    }

    if (runningNow) {
        const double secs = GetTime() - a.running->startedAt;
        Icon stop = Icon::Stop;
        if (ui::button(playR, T("STOP"), ui::Style::Danger, false, &stop)) a.stop();
        utext::draw(TextFormat(T("Running %d:%02d"), (int)secs / 60, (int)secs % 60), playR.x + playR.width + 22, playR.y + 22, 16, theme::muted);
    } else if (!have) {
        Icon dl = Icon::Download;
        if (ui::button(playR, T("INSTALL"), ui::Style::Primary, false, &dl)) a.tab = Tab::Installs;
    } else if (Verdict runs = usupport::binary(sel.exe); !runs.ok) {
        ui::button(playR, T("PLAY"), ui::Style::Primary, true);
        utext::drawWrapped(verdictText(runs).c_str(), {playR.x - 60, playR.y + playH + 6, playW + 120, 40}, 13, theme::danger);
    } else {
        // The button glows: a slow gold breath, the launcher's one flourish.
        const float g = 0.5f + 0.5f * sinf(a.time * 2.2f);
        DrawRectangleRounded({playR.x - 6, playR.y - 6, playR.width + 12, playR.height + 12}, 0.3f, 10,
                             theme::accentA((unsigned char)(25 + 35 * g)));
        Icon pl = Icon::Play;
        if (ui::button(playR, T("PLAY"), ui::Style::Primary, false, &pl) || (IsKeyPressed(KEY_ENTER) && ui::focusedField() < 0)) a.play();
        utext::draw(sel.version, playR.x + (playR.width - utext::measure(sel.version, 13).x) / 2, playR.y + playH + 6, 13, theme::faint);
    }

    // Who is playing, right.
    const bool signedIn = Account::get().state() == Account::State::SignedIn;
    const std::string who = signedIn ? Account::get().nickname() : std::string(T("Not signed in"));
    const float ww = utext::measure(who, 16, utext::Semi).x;
    utext::draw(signedIn ? T("Playing as") : T("Account"), r.x + r.width - 40 - std::max(ww, 90.0f), bar.y + 18, 13, theme::faint, utext::Semi);
    utext::draw(who, r.x + r.width - 40 - std::max(ww, 90.0f), bar.y + 44, 16, theme::ink, utext::Semi);
    // Hours played, under the version picker: the number Steam would show.
    if (const double pt = uplay::openDoctrines(a.allDataDirs()); pt >= 60)
        utext::draw(std::string(T("Played")) + " " + uplay::human(pt), r.x + 40, bar.y + 80, 13, theme::muted);

    // Below the picture: news and what changed.
    Rectangle below{r.x, hero.y + heroH, r.width, r.height - heroH};
    ui::beginScroll(below, g_scroll);
    float y = below.y + 22 - g_scroll.y;
    const float colW = (below.width - 40 * 2 - 24) / 2;
    float leftH = 0, rightH = 0;
    {
        float x = below.x + 40, yy = y;
        ui::heading(T("News"), x, yy, 22);
        yy += 36;
        if (a.news.empty()) {
            utext::draw(T("Nothing posted right now."), x, yy, 15, theme::faint);
            yy += 30;
        }
        for (auto& n : a.news) {
            const float bodyH = utext::drawWrapped(n.body, {x + 18, 0, colW - 36, 0}, 15, theme::muted, utext::Sans, false);
            Rectangle card{x, yy, colW, 54 + bodyH + (n.buttonAction.empty() ? 0 : 44)};
            ui::card(card);
            DrawRectangle((int)card.x, (int)card.y + 10, 3, (int)card.height - 20, theme::gold);
            utext::draw(n.title, x + 18, yy + 14, 18, theme::ink, utext::Semi);
            utext::drawWrapped(n.body, {x + 18, yy + 42, colW - 36, bodyH}, 15, theme::muted);
            if (!n.buttonAction.empty()) {
                if (ui::button({x + 18, card.y + card.height - 46, 180, 34}, n.buttonLabel.c_str(), ui::Style::Secondary)) {
                    if (n.buttonAction == "join") a.play("opendoctrines://join/" + n.buttonParam);
                    else uproc::openUrl("https://opendoctrines.pages.dev/#community");
                }
            }
            yy += card.height + 14;
        }
        leftH = yy - y;
    }
    {
        float x = below.x + 40 + colW + 24, yy = y;
        ui::heading(T("What's new"), x, yy, 22);
        yy += 36;
        const Release* rel = nullptr;
        for (auto& rr : a.releases) if (!have || rr.tag == sel.tag || (sel.external && rr.version == sel.version)) { rel = &rr; break; }
        if (!rel && !a.releases.empty()) rel = &a.releases.front();
        if (rel) {
            utext::draw(rel->name, x, yy, 17, theme::gold, utext::Semi);
            yy += 28;
            std::string notes = rel->notes;
            // Markdown, read as text: headings and bullets keep their line breaks.
            for (const char* strip : {"## ", "### ", "**"}) {
                size_t p;
                while ((p = notes.find(strip)) != std::string::npos) notes.erase(p, std::string(strip).size());
            }
            if (notes.size() > 2400) notes = notes.substr(0, 2400) + "…";
            yy += utext::drawWrapped(notes, {x, yy, colW, 0}, 15, theme::muted);
        } else {
            utext::draw(T("Release notes appear here once the list has loaded."), x, yy, 15, theme::faint);
            yy += 30;
        }
        rightH = yy - y;
    }
    ui::endScroll(below, g_scroll, std::max(leftH, rightH) + 40);
}
