// Achievements: the same collection the game shows, verified here too.
#include "App.h"
#include "od/Achievements.h"
#include "ui/Art.h"
#include "ui/Strings.h"
#include "ui/Ui.h"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace {
ui::Scroll g_scroll;
int g_filter = 0;
std::string g_query;
const char* catLabel(const char* c) {
    if (!std::strcmp(c, "meta")) return T("Head of State");
    if (!std::strcmp(c, "war")) return T("Warfare");
    if (!std::strcmp(c, "artillery")) return T("Artillery");
    if (!std::strcmp(c, "diplomacy")) return T("Diplomacy");
    if (!std::strcmp(c, "economy")) return T("Economy");
    if (!std::strcmp(c, "empire")) return T("Empire");
    if (!std::strcmp(c, "science")) return T("Science");
    if (!std::strcmp(c, "society")) return T("Society");
    if (!std::strcmp(c, "navy")) return T("Navy");
    if (!std::strcmp(c, "social")) return T("Multiplayer");
    if (!std::strcmp(c, "creator")) return T("Creator");
    return c;
}
}  // namespace

void drawAchievements(App& a, Rectangle r) {
    (void)a;
    const float x = r.x + 40, w = r.width - 80;
    float y = r.y + 28;
    art::banner({r.x, r.y, r.width, 120}, 5, a.time);
    ui::heading(T("Achievements"), x, y, 30);
    ui::searchField({x + w - 340, y + 2, 300, 38}, g_query, 4700);
    auto rows = uach::view();
    int got = 0;
    for (auto& v : rows) got += v.granted;
    utext::draw(TextFormat(T("%d of %d confirmed"), got, odach::kCatalogCount), x, y + 46, 17, theme::gold);
    ui::progress({x, y + 76, w, 6}, (float)got / odach::kCatalogCount);
    y += 104;
    std::string st = uach::status();
    if (!st.empty()) { utext::draw(T(st.c_str()), x, y, 14, theme::faint); y += 26; }

    // Closest to unlocking: the counted ones the player is furthest along.
    {
        std::vector<const AchView*> near;
        for (auto& v : rows)
            if (!v.earned && !v.def->hidden && v.def->gte > 1 && v.progress > 0) near.push_back(&v);
        std::sort(near.begin(), near.end(), [](const AchView* p, const AchView* q) {
            return p->progress / p->def->gte > q->progress / q->def->gte;
        });
        if (near.size() > 3) near.resize(3);
        if (!near.empty()) {
            utext::draw(T("Closest to unlocking"), x, y, 17, theme::gold, utext::Semi);
            y += 28;
            const float gap = 12, cw = (w - gap * 2) / 3;
            Texture2D at = art::achievementAtlas();
            for (size_t i = 0; i < near.size(); ++i) {
                const AchView& v = *near[i];
                const float cx = x + i * (cw + gap);
                Rectangle card{cx, y, cw, 74};
                DrawRectangleRounded(card, 0.15f, 8, Color{22, 32, 46, 235});
                DrawRectangleRoundedLinesEx(card, 0.15f, 8, 1, theme::goldDim);
                if (at.id) {
                    const float t = (float)odach::kIconTilePx;
                    DrawTexturePro(at, {v.def->icon * t, t, t, t}, {cx + 12, y + 12, 50, 50}, {0, 0}, 0, WHITE);
                }
                utext::draw(utext::ellipsize(T(v.def->name), cw - 86, 16), cx + 74, y + 10, 16, theme::ink, utext::Semi);
                const float p = (float)std::min(1.0, v.progress / v.def->gte);
                ui::progress({cx + 74, y + 38, cw - 90, 6}, p);
                utext::draw(TextFormat(T("%.0f of %.0f  ·  %d%%"), std::min(v.progress, v.def->gte), v.def->gte, (int)(p * 100)),
                            cx + 74, y + 50, 13, theme::muted);
            }
            y += 90;
        }
    }

    // Category chips.
    float cx = x;
    for (int i = -1; i < odach::kCategoryCount; ++i) {
        const char* label = i < 0 ? T("All") : catLabel(odach::kCategories[i]);
        const float cw = utext::measure(label, 15).x + 26;
        if (cx + cw > x + w) { cx = x; y += 36; }
        Rectangle chip{cx, y, cw, 30};
        const bool on = g_filter == i + 1;
        DrawRectangleRounded(chip, 0.5f, 8, on ? theme::gold : (ui::hovered(chip) ? theme::ruleFirm : theme::raise));
        utext::draw(label, cx + 13, y + 6, 15, on ? theme::ground : theme::ink);
        if (ui::clicked(chip)) { g_filter = i + 1; g_scroll = {}; }
        cx += cw + 8;
    }
    y += 46;

    Rectangle grid{r.x, y, r.width, r.y + r.height - y};
    ui::beginScroll(grid, g_scroll);
    const int cols = r.width > 1150 ? 3 : 2;
    const float gap = 14, cw = (w - gap * (cols - 1)) / cols, ch = 96;
    Texture2D atlas = art::achievementAtlas();
    int k = 0;
    for (auto& v : rows) {
        if (g_filter > 0 && std::strcmp(v.def->cat, odach::kCategories[g_filter - 1]) != 0) continue;
        // Searching hidden ones by their real name would be a spoiler.
        if (!g_query.empty() && (v.def->hidden && !v.granted ? true : !ui::matches(std::string(T(v.def->name)) + " " + T(v.def->desc), g_query))) continue;
        const float gx = x + (k % cols) * (cw + gap), gy = y + (k / cols) * (ch + gap) - g_scroll.y;
        ++k;
        if (gy > grid.y + grid.height || gy + ch < grid.y) continue;
        Rectangle card{gx, gy, cw, ch};
        DrawRectangleRounded(card, 0.1f, 8, v.granted ? Color{22, 32, 46, 240} : Color{14, 17, 25, 230});
        DrawRectangleRoundedLinesEx(card, 0.1f, 8, 1, v.granted ? theme::gold : theme::rule);
        if (atlas.id) {
            const float t = (float)odach::kIconTilePx;
            DrawTexturePro(atlas, {v.def->icon * t, v.granted ? 0.0f : t, t, t}, {gx + 14, gy + 16, 64, 64}, {0, 0}, 0, WHITE);
        }
        const bool secret = v.def->hidden && !v.granted;
        utext::draw(utext::ellipsize(secret ? std::string(T("Hidden achievement")) : std::string(T(v.def->name)), cw - 104, 17),
                    gx + 92, gy + 14, 17, v.granted ? theme::ink : theme::muted, utext::Semi);
        utext::drawWrapped(secret ? T("Keep playing. Or keep doing something you probably shouldn't.") : T(v.def->desc),
                           {gx + 92, gy + 40, cw - 106, 40}, 14, v.granted ? theme::muted : theme::faint);
        if (!v.granted && !secret && v.def->gte > 1) {
            // Progress on every counted card, as in the game.
            const float p = (float)std::min(1.0, v.progress / v.def->gte);
            const float bw = 120;
            DrawRectangle((int)(gx + cw - bw - 12), (int)(gy + ch - 14), (int)bw, 4, theme::rule);
            DrawRectangle((int)(gx + cw - bw - 12), (int)(gy + ch - 14), (int)(bw * p), 4, theme::goldDim);
            const char* nums = TextFormat("%.0f / %.0f", std::min(v.progress, v.def->gte), v.def->gte);
            utext::draw(nums, gx + cw - bw - 20 - utext::measure(nums, 12).x, gy + ch - 21, 12, theme::faint);
        } else if (!v.granted && v.earned) {
            const char* w8 = T("Waiting for confirmation");
            utext::draw(w8, gx + cw - utext::measure(w8, 12).x - 12, gy + ch - 20, 12, theme::goldDim);
        }
        if (v.granted && v.when) {
            char d[32];
            std::time_t t = (std::time_t)v.when;
            std::strftime(d, sizeof d, "%Y-%m-%d", std::localtime(&t));
            std::string s = std::string(d) + (v.modded ? std::string("  ·  ") + T("modified rules") : "");
            utext::draw(s, gx + cw - utext::measure(s, 12).x - 12, gy + ch - 20, 12, theme::goldDim);
        }
    }
    ui::endScroll(grid, g_scroll, ((k + cols - 1) / cols) * (ch + gap) + 20);
}
