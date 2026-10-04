#include "ui/Art.h"
#include "ui/Ui.h"
#include "core/Assets.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace art {
namespace {
std::vector<Texture2D> g_heroes;
Texture2D g_logo{}, g_odLogo{}, g_icon{}, g_ach{}, g_temple{}, g_library{};
RenderTexture2D g_contours{};
int g_contoursW = 0, g_contoursH = 0;
int g_lastHero = 0, g_fromHero = 0;
float g_fade = 1.0f;

Texture2D load(const char* path) {
    const std::string* bytes = uassets::get(path);
    if (!bytes) return {};
    const char* ext = std::string(path).find(".jpg") != std::string::npos ? ".jpg" : ".png";
    Image im = LoadImageFromMemory(ext, (const unsigned char*)bytes->data(), (int)bytes->size());
    Texture2D t = LoadTextureFromImage(im);
    UnloadImage(im);
    GenTextureMipmaps(&t);
    SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
    return t;
}

// Value noise, smooth enough for contour lines and cheap enough to sample
// once per cell when the contour texture is (re)built.
float hash(int x, int y) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)(h & 0xFFFF) / 65535.0f;
}
float noise(float x, float y) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float a = hash(xi, yi), b = hash(xi + 1, yi), c = hash(xi, yi + 1), d = hash(xi + 1, yi + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
float terrain(float x, float y) {
    return noise(x * 0.004f, y * 0.004f) * 0.6f + noise(x * 0.011f, y * 0.011f) * 0.3f +
           noise(x * 0.03f, y * 0.03f) * 0.1f;
}

// Marching squares into a render texture, once per window size. Drawing this
// every frame would be the most expensive thing the launcher does; drawn once
// and panned, it costs one textured quad.
void buildContours(int w, int h) {
    if (g_contours.id) UnloadRenderTexture(g_contours);
    w += 400; h += 300;   // margin for the drift
    g_contours = LoadRenderTexture(w, h);
    g_contoursW = w; g_contoursH = h;
    BeginTextureMode(g_contours);
    ClearBackground(BLANK);
    const int cell = 9;
    const int cols = w / cell + 2, rows = h / cell + 2;
    std::vector<float> v((size_t)cols * rows);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x) v[(size_t)y * cols + x] = terrain((float)x * cell, (float)y * cell);
    for (int level = 1; level < 14; ++level) {
        const float iso = level / 14.0f;
        const bool major = level % 4 == 0;
        const Color c = major ? theme::accentA(34) : Color{154, 160, 174, 16};
        for (int y = 0; y + 1 < rows; ++y)
            for (int x = 0; x + 1 < cols; ++x) {
                float a = v[(size_t)y * cols + x], b = v[(size_t)y * cols + x + 1];
                float c2 = v[(size_t)(y + 1) * cols + x + 1], d = v[(size_t)(y + 1) * cols + x];
                int idx = (a > iso) | ((b > iso) << 1) | ((c2 > iso) << 2) | ((d > iso) << 3);
                if (idx == 0 || idx == 15) continue;
                auto lerp = [&](float p, float q) { return (iso - p) / (q - p + 1e-6f); };
                Vector2 top{(x + lerp(a, b)) * cell, (float)y * cell};
                Vector2 right{(float)(x + 1) * cell, (y + lerp(b, c2)) * cell};
                Vector2 bottom{(x + lerp(d, c2)) * cell, (float)(y + 1) * cell};
                Vector2 left{(float)x * cell, (y + lerp(a, d)) * cell};
                Vector2 p[4];
                int n = 0;
                switch (idx) {
                    case 1: case 14: p[0] = left; p[1] = top; n = 2; break;
                    case 2: case 13: p[0] = top; p[1] = right; n = 2; break;
                    case 3: case 12: p[0] = left; p[1] = right; n = 2; break;
                    case 4: case 11: p[0] = right; p[1] = bottom; n = 2; break;
                    case 6: case 9: p[0] = top; p[1] = bottom; n = 2; break;
                    case 7: case 8: p[0] = left; p[1] = bottom; n = 2; break;
                    case 5: p[0] = left; p[1] = top; p[2] = right; p[3] = bottom; n = 4; break;
                    case 10: p[0] = top; p[1] = right; p[2] = bottom; p[3] = left; n = 4; break;
                }
                for (int k = 0; k + 1 < n; k += 2) DrawLineEx(p[k], p[k + 1], major ? 1.4f : 1.0f, c);
            }
    }
    // A graticule: the map table's meridians and parallels.
    for (int x = 0; x < w; x += 120) DrawLine(x, 0, x, h, Color{154, 160, 174, 10});
    for (int y = 0; y < h; y += 120) DrawLine(0, y, w, y, Color{154, 160, 174, 10});
    EndTextureMode();
    SetTextureFilter(g_contours.texture, TEXTURE_FILTER_BILINEAR);
}

void vignette(Rectangle r, float strength) {
    const Color dark{12, 15, 22, (unsigned char)(255 * strength)};
    DrawRectangleGradientH((int)r.x, (int)r.y, (int)(r.width * 0.25f), (int)r.height, dark, BLANK);
    DrawRectangleGradientH((int)(r.x + r.width * 0.75f), (int)r.y, (int)(r.width * 0.25f) + 1, (int)r.height, BLANK, dark);
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)(r.height * 0.3f), dark, BLANK);
    DrawRectangleGradientV((int)r.x, (int)(r.y + r.height * 0.55f), (int)r.width, (int)(r.height * 0.45f) + 1, BLANK,
                           Color{12, 15, 22, 255});
}

// Something crossing the picture: a ship's silhouette at sea level, a gull.
void ship(float x, float y, float s, Color c) {
    DrawTriangle({x - s, y}, {x + s * 1.2f, y}, {x + s, y + s * 0.35f}, c);
    DrawTriangle({x - s, y}, {x + s, y + s * 0.35f}, {x - s * 0.8f, y + s * 0.35f}, c);
    DrawRectangle((int)(x - s * 0.1f), (int)(y - s * 1.2f), (int)std::max(1.0f, s * 0.08f), (int)(s * 1.2f), c);
    DrawTriangle({x, y - s * 1.15f}, {x, y - s * 0.15f}, {x + s * 0.7f, y - s * 0.2f}, c);
}
}  // namespace

void init() {
    for (int i = 0;; ++i) {
        char path[32];
        snprintf(path, sizeof path, "art/hero-%d.jpg", i);
        if (!uassets::get(path)) break;
        g_heroes.push_back(load(path));
    }
    g_logo = load("art/logo.png");
    g_odLogo = load("art/od-logo.png");
    g_icon = load("art/icon.png");
    g_ach = load("achievements.png");
    g_temple = load("art/templeos.jpg");
    g_library = load("art/library.jpg");
}

void shutdown() {
    for (auto& t : g_heroes) UnloadTexture(t);
    for (Texture2D* t : {&g_logo, &g_odLogo, &g_icon, &g_ach, &g_temple, &g_library}) if (t->id) UnloadTexture(*t);
    if (g_contours.id) UnloadRenderTexture(g_contours);
}

void accentChanged() { g_contoursW = g_contoursH = 0; }

int heroCount() { return (int)g_heroes.size(); }
Texture2D appIcon() { return g_icon; }
Texture2D achievementAtlas() { return g_ach; }
Texture2D templeosShot() { return g_temple; }
Texture2D libraryArt() { return g_library; }

static void drawPan(const Texture2D& t, Rectangle r, float phase, float alpha) {
    if (!t.id) return;
    // Cover the rectangle, then drift: a slow zoom and pan, never a jump.
    const float scaleCover = std::max(r.width / t.width, r.height / t.height);
    const float zoom = scaleCover * (1.08f + 0.04f * sinf(phase * 0.05f));
    const float w = t.width * zoom, h = t.height * zoom;
    const float ox = (w - r.width) * (0.5f + 0.45f * sinf(phase * 0.031f));
    const float oy = (h - r.height) * (0.5f + 0.40f * cosf(phase * 0.023f));
    const Rectangle src{ox / zoom, oy / zoom, r.width / zoom, r.height / zoom};
    DrawTexturePro(t, src, r, {0, 0}, 0, Color{255, 255, 255, (unsigned char)(255 * alpha)});
}

void hero(Rectangle r, int which, float t) {
    DrawRectangleRec(r, theme::sea);
    if (!g_heroes.empty()) {
        which = ((which % (int)g_heroes.size()) + (int)g_heroes.size()) % (int)g_heroes.size();
        if (which != g_lastHero) { g_fromHero = g_lastHero; g_lastHero = which; g_fade = 0; }
        g_fade = std::min(1.0f, g_fade + GetFrameTime() * 0.8f);
        ui::scissor((int)r.x, (int)r.y, (int)r.width, (int)r.height);
        if (g_fade < 1.0f) drawPan(g_heroes[g_fromHero], r, t, 1.0f);
        drawPan(g_heroes[which], r, t, g_fade);
        // Weather and traffic over the picture.
        for (int k = 0; k < 3; ++k) {
            float speed = 9.0f + k * 4.0f;
            float x = fmodf(t * speed + k * 523.0f, r.width + 200) - 100 + r.x;
            ship(x, r.y + r.height * (0.62f + 0.07f * k), 7.0f + k * 2, Color{12, 15, 22, 150});
        }
        // Cloud shadow: a soft band of light drifting across, as if the lamp
        // above the table were swinging very slightly.
        {
            const float cx = r.x + fmodf(t * 18.0f, r.width + 800) - 400;
            DrawCircleGradient((int)cx, (int)(r.y + r.height * 0.45f), r.height * 0.9f, Color{232, 228, 218, 12}, BLANK);
        }
        vignette(r, 0.85f);
        ui::endScissor();
    }
}

void backdrop(Rectangle r, float t) {
    DrawRectangleRec(r, theme::ground);
    if (g_contoursW < (int)r.width + 400 || g_contoursH < (int)r.height + 300) buildContours((int)r.width, (int)r.height);
    const float ox = 200 + 180 * sinf(t * 0.013f), oy = 150 + 130 * cosf(t * 0.011f);
    DrawTextureRec(g_contours.texture, {ox, oy, r.width, -r.height}, {r.x, r.y}, WHITE);
    // A lamp above the table: warm in the middle, falling off at the edges.
    DrawCircleGradient((int)(r.x + r.width * 0.55f), (int)(r.y + r.height * 0.35f), r.width * 0.6f,
                       theme::accentA((unsigned char)(10 + 3 * sinf(t * 1.7f))), BLANK);
}

void banner(Rectangle r, int seed, float t) {
    DrawRectangleRec(r, theme::sunken);
    ui::scissor((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    // Contours seen at a different place on the table for each section.
    if (g_contours.id) {
        const float ox = fmodf(seed * 337.0f + t * 4.0f, std::max(1.0f, (float)g_contoursW - r.width));
        const float oy = fmodf(seed * 211.0f, std::max(1.0f, (float)g_contoursH - r.height));
        DrawTextureRec(g_contours.texture, {ox, oy, r.width, -r.height}, {r.x, r.y}, Color{255, 255, 255, 220});
    }
    compass(r.x + r.width - r.height * 0.6f, r.y + r.height * 0.5f, r.height * 0.42f, t + seed, theme::accentA(70));
    DrawRectangleGradientH((int)r.x, (int)r.y, (int)(r.width * 0.6f), (int)r.height, Color{9, 11, 17, 230}, BLANK);
    DrawRectangle((int)r.x, (int)(r.y + r.height - 1), (int)r.width, 1, theme::rule);
    ui::endScissor();
}

void compass(float cx, float cy, float rad, float t, Color c) {
    const float spin = sinf(t * 0.2f) * 4.0f;   // a needle settling, never still
    DrawRing({cx, cy}, rad * 0.93f, rad, 0, 360, 48, c);
    DrawRing({cx, cy}, rad * 0.62f, rad * 0.64f, 0, 360, 48, c);
    for (int k = 0; k < 32; ++k) {
        float a = k * PI / 16;
        float in = (k % 4 == 0) ? 0.8f : 0.88f;
        DrawLineEx({cx + cosf(a) * rad * in, cy + sinf(a) * rad * in}, {cx + cosf(a) * rad * 0.93f, cy + sinf(a) * rad * 0.93f}, 1, c);
    }
    for (int k = 0; k < 8; ++k) {
        float a = (k * 45 + spin) * DEG2RAD - PI / 2;
        float len = (k % 2 == 0) ? rad * 0.85f : rad * 0.5f;
        float w = rad * 0.09f;
        Vector2 tip{cx + cosf(a) * len, cy + sinf(a) * len};
        Vector2 l{cx + cosf(a + PI / 2) * w, cy + sinf(a + PI / 2) * w};
        Vector2 rr{cx + cosf(a - PI / 2) * w, cy + sinf(a - PI / 2) * w};
        DrawTriangle(l, rr, tip, c);
        DrawTriangle(rr, l, tip, c);
    }
}

void logo(float x, float y, float h, Color tint) {
    if (!g_logo.id) return;
    const float w = g_logo.width * h / g_logo.height;
    DrawTexturePro(g_logo, {0, 0, (float)g_logo.width, (float)g_logo.height}, {x, y, w, h}, {0, 0}, 0, tint);
}

void gameLogo(float x, float y, float h, Color tint) {
    if (!g_odLogo.id) return;
    const float w = g_odLogo.width * h / g_odLogo.height;
    DrawTexturePro(g_odLogo, {0, 0, (float)g_odLogo.width, (float)g_odLogo.height}, {x, y, w, h}, {0, 0}, 0, tint);
}

void gameCard(Rectangle r, Game g, bool active, float t) {
    DrawRectangleRounded(r, 0.12f, 8, theme::sunken);
    ui::scissor((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    switch (g) {
        case Game::OpenDoctrines:
            if (g_library.id) {
                const float s = std::max(r.width / g_library.width, r.height / g_library.height);
                DrawTexturePro(g_library, {(g_library.width - r.width / s) / 2, (g_library.height - r.height / s) / 2, r.width / s, r.height / s},
                               r, {0, 0}, 0, WHITE);
            }
            break;
        case Game::Unciv: {
            // Hexes, the Unciv grid, coloured as terrain.
            const float hs = r.width / 6.0f;
            for (int row = -1; row < (int)(r.height / (hs * 0.86f)) + 2; ++row)
                for (int col = -1; col < 8; ++col) {
                    float cx = r.x + col * hs * 1.0f + (row % 2 ? hs * 0.5f : 0);
                    float cy = r.y + row * hs * 0.86f;
                    float n = noise(col * 0.9f + 3, row * 0.9f + t * 0.05f);
                    Color c = n < 0.35f ? Color{30, 60, 96, 255} : n < 0.55f ? Color{72, 112, 60, 255}
                            : n < 0.75f ? Color{120, 132, 70, 255} : Color{130, 120, 110, 255};
                    DrawPoly({cx, cy}, 6, hs * 0.55f, 30, c);
                    DrawPolyLinesEx({cx, cy}, 6, hs * 0.55f, 30, 1, Color{0, 0, 0, 60});
                }
            break;
        }
        case Game::GreaterDiplomacy5:
        case Game::GreaterDiplomacy4: {
            // Pixel provinces: big blocky regions in strong colours.
            const int px = (int)std::max(4.0f, r.width / 18);
            for (int y = 0; y < (int)r.height; y += px)
                for (int x = 0; x < (int)r.width; x += px) {
                    float n = noise(x * 0.03f + (g == Game::GreaterDiplomacy4 ? 40 : 0), y * 0.03f);
                    int region = (int)(noise(x * 0.012f, y * 0.012f + 7) * 7);
                    Color pal[7] = {{176, 70, 60, 255}, {70, 110, 170, 255}, {200, 170, 70, 255}, {90, 150, 90, 255},
                                    {140, 90, 160, 255}, {200, 120, 60, 255}, {60, 140, 150, 255}};
                    Color c = n < 0.38f ? Color{20, 34, 56, 255} : pal[region];
                    DrawRectangle((int)r.x + x, (int)r.y + y, px, px, c);
                }
            break;
        }
        case Game::TempleOS:
            if (g_temple.id) DrawTexturePro(g_temple, {0, 0, (float)g_temple.width, (float)g_temple.height}, r, {0, 0}, 0, WHITE);
            break;
    }
    DrawRectangleGradientV((int)r.x, (int)(r.y + r.height * 0.45f), (int)r.width, (int)(r.height * 0.55f) + 1, BLANK, Color{9, 11, 17, 235});
    ui::endScissor();
    DrawRectangleRoundedLinesEx(r, 0.12f, 8, active ? 2.0f : 1.0f, active ? theme::gold : theme::rule);
}
}  // namespace art
