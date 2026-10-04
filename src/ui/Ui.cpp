#include "ui/Ui.h"
#include "ui/Strings.h"

#include <algorithm>
#include <cmath>
#include <functional>

void theme::setAccent(int rgb) {
    const unsigned char r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    gold = {r, g, b, 255};
    goldDim = {(unsigned char)(r * 0.7f), (unsigned char)(g * 0.7f), (unsigned char)(b * 0.7f), 255};
    goldHover = {(unsigned char)std::min(255, r + 24), (unsigned char)std::min(255, g + 22), (unsigned char)std::min(255, b + 18), 255};
}

namespace ui {
namespace {
const Rectangle* g_modal = nullptr;
Rectangle g_modalRect{};
bool g_hasModal = false;
bool g_pressInside = false;
Vector2 g_pressPos{};
int g_focus = -1;
int g_openDropdown = -1;
std::string g_tooltip;
std::vector<std::function<void()>> g_overlays;
float g_caret = 0;
bool g_escUsed = false;
struct Clip { int x, y, w, h; };
std::vector<Clip> g_clips;   // see scissor() below   // a field took this frame's Esc; nothing else may act on it

// While a modal is up, the screen behind it is drawn with input off and the
// modal with input on -- App toggles this around the two passes.
bool g_input = true;
bool allowed(Rectangle) { return g_input; }

Color mix(Color a, Color b, float t) {
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
            (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t)};
}
}  // namespace

void beginFrame() {
    g_escUsed = false;
    g_clips.clear();
    g_tooltip.clear();
    g_overlays.clear();
    g_caret += GetFrameTime();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_pressPos = mouse();
}

void endFrame() {}

void setModalRect(const Rectangle* r) {
    g_hasModal = r != nullptr;
    if (r) g_modalRect = *r;
}

void setInput(bool on) { g_input = on; }
bool inputOn() { return g_input; }

// ---- scale: the whole interface is laid out in logical units and drawn
// through a camera zoom, so a fullscreen window shows the same layout bigger
// rather than the same small text spread over more pixels.
float g_scale = 1.0f;

void setScale(float s) { g_scale = s < 0.5f ? 0.5f : s; }
float scale() { return g_scale; }
float W() { return GetScreenWidth() / g_scale; }
float H() { return GetScreenHeight() / g_scale; }
// A STACK, because clips nest: a text field inside a scrolling page clips to
// itself and must then put the PAGE's clip back. raylib keeps one rectangle,
// and EndScissorMode turns clipping off entirely -- which is how everything
// below a text field once drew over the top bar.
void applyClip() {
    if (g_clips.empty()) { EndScissorMode(); return; }
    // The intersection of every clip on the stack.
    int x0 = -100000, y0 = -100000, x1 = 100000, y1 = 100000;
    for (auto& c : g_clips) { x0 = std::max(x0, c.x); y0 = std::max(y0, c.y); x1 = std::min(x1, c.x + c.w); y1 = std::min(y1, c.y + c.h); }
    BeginScissorMode((int)(x0 * g_scale), (int)(y0 * g_scale), (int)(std::max(0, x1 - x0) * g_scale + 0.5f),
                     (int)(std::max(0, y1 - y0) * g_scale + 0.5f));
}
void scissor(int x, int y, int w, int h) { g_clips.push_back({x, y, w, h}); applyClip(); }
void endScissor() { if (!g_clips.empty()) g_clips.pop_back(); applyClip(); }
Vector2 mouse() { Vector2 m = GetMousePosition(); return {m.x / g_scale, m.y / g_scale}; }

bool hovered(Rectangle r) {
    if (!allowed(r)) return false;
    return CheckCollisionPointRec(mouse(), r);
}

bool clicked(Rectangle r) {
    return hovered(r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(g_pressPos, r);
}

/** An ellipse (or an arc of one) drawn at the same stroke width as the other icons. */
static void strokeEllipse(float cx, float cy, float rx, float ry, float t, Color c, float a0 = 0, float a1 = 360) {
    const int n = 40;
    Vector2 prev{};
    for (int k = 0; k <= n; ++k) {
        const float a = (a0 + (a1 - a0) * k / n) * DEG2RAD;
        const Vector2 p{cx + cosf(a) * rx, cy + sinf(a) * ry};
        if (k) DrawLineEx(prev, p, t, c);
        prev = p;
    }
}

void icon(Icon i, float cx, float cy, float s, Color c) {
    const float h = s / 2, t = std::max(1.5f, s / 11.0f);
    switch (i) {
        case Icon::Play:
            DrawTriangle({cx - h * 0.6f, cy - h * 0.8f}, {cx - h * 0.6f, cy + h * 0.8f}, {cx + h * 0.85f, cy}, c);
            break;
        case Icon::Stop:
            DrawRectangleRounded({cx - h * 0.7f, cy - h * 0.7f, h * 1.4f, h * 1.4f}, 0.2f, 4, c);
            break;
        case Icon::Box:
            DrawRectangleLinesEx({cx - h * 0.8f, cy - h * 0.5f, h * 1.6f, h * 1.3f}, t, c);
            DrawLineEx({cx - h * 0.95f, cy - h * 0.5f}, {cx, cy - h * 0.95f}, t, c);
            DrawLineEx({cx + h * 0.95f, cy - h * 0.5f}, {cx, cy - h * 0.95f}, t, c);
            break;
        case Icon::Globe: {
            // Every line at the same weight: an outline, a meridian, the
            // equator and two parallels, each chord ending on the outline.
            const float R = h * 0.82f;
            DrawRing({cx, cy}, R - t, R, 0, 360, 40, c);
            strokeEllipse(cx, cy, R * 0.42f, R - t / 2, t, c);
            DrawLineEx({cx - R + t / 2, cy}, {cx + R - t / 2, cy}, t, c);
            for (float f : {-0.5f, 0.5f}) {
                const float yy = cy + f * R, half = sqrtf(R * R - f * R * f * R) - t * 0.7f;
                DrawLineEx({cx - half, yy}, {cx + half, yy}, t, c);
            }
            break;
        }
        case Icon::Puzzle:
            DrawRectangleLinesEx({cx - h * 0.7f, cy - h * 0.5f, h * 1.3f, h * 1.2f}, t, c);
            DrawCircle((int)(cx - h * 0.05f), (int)(cy - h * 0.6f), h * 0.25f, c);
            DrawCircle((int)(cx + h * 0.72f), (int)(cy + h * 0.1f), h * 0.25f, c);
            break;
        case Icon::Trophy:
            DrawRectangleRounded({cx - h * 0.55f, cy - h * 0.8f, h * 1.1f, h * 0.9f}, 0.5f, 6, c);
            DrawRing({cx - h * 0.55f, cy - h * 0.4f}, h * 0.2f, h * 0.2f + t, 90, 270, 12, c);
            DrawRing({cx + h * 0.55f, cy - h * 0.4f}, h * 0.2f, h * 0.2f + t, -90, 90, 12, c);
            DrawRectangle((int)(cx - t), (int)(cy + h * 0.1f), (int)(t * 2), (int)(h * 0.4f), c);
            DrawRectangle((int)(cx - h * 0.45f), (int)(cy + h * 0.5f), (int)(h * 0.9f), (int)(t * 1.6f), c);
            break;
        case Icon::Server:
            for (int k = 0; k < 3; ++k) {
                Rectangle r{cx - h * 0.8f, cy - h * 0.8f + k * h * 0.58f, h * 1.6f, h * 0.45f};
                DrawRectangleLinesEx(r, t, c);
                DrawCircle((int)(r.x + r.width - h * 0.25f), (int)(r.y + r.height / 2), t, c);
            }
            break;
        case Icon::User:
            DrawCircle((int)cx, (int)(cy - h * 0.35f), h * 0.35f, c);
            DrawRectangleRounded({cx - h * 0.7f, cy + h * 0.1f, h * 1.4f, h * 0.75f}, 0.8f, 8, c);
            break;
        case Icon::Gear:
            DrawRing({cx, cy}, h * 0.3f, h * 0.62f, 0, 360, 24, c);
            for (int k = 0; k < 8; ++k) {
                float a = k * PI / 4;
                DrawLineEx({cx + cosf(a) * h * 0.5f, cy + sinf(a) * h * 0.5f},
                           {cx + cosf(a) * h * 0.9f, cy + sinf(a) * h * 0.9f}, t * 1.8f, c);
            }
            break;
        case Icon::Tools:
            DrawLineEx({cx - h * 0.7f, cy + h * 0.7f}, {cx + h * 0.4f, cy - h * 0.4f}, t * 1.8f, c);
            DrawRing({cx + h * 0.5f, cy - h * 0.5f}, h * 0.15f, h * 0.38f, 200, 520, 16, c);
            break;
        case Icon::Terminal:
            DrawRectangleLinesEx({cx - h * 0.9f, cy - h * 0.7f, h * 1.8f, h * 1.4f}, t, c);
            DrawLineEx({cx - h * 0.55f, cy - h * 0.25f}, {cx - h * 0.2f, cy}, t, c);
            DrawLineEx({cx - h * 0.2f, cy}, {cx - h * 0.55f, cy + h * 0.25f}, t, c);
            DrawLineEx({cx, cy + h * 0.3f}, {cx + h * 0.5f, cy + h * 0.3f}, t, c);
            break;
        case Icon::News:
            DrawRectangleLinesEx({cx - h * 0.8f, cy - h * 0.7f, h * 1.6f, h * 1.4f}, t, c);
            for (int k = 0; k < 3; ++k)
                DrawLineEx({cx - h * 0.5f, cy - h * 0.3f + k * h * 0.3f}, {cx + h * 0.5f, cy - h * 0.3f + k * h * 0.3f}, t * 0.8f, c);
            break;
        case Icon::Folder:
            DrawRectangleRounded({cx - h * 0.85f, cy - h * 0.45f, h * 1.7f, h * 1.15f}, 0.15f, 4, c);
            DrawRectangleRounded({cx - h * 0.85f, cy - h * 0.65f, h * 0.7f, h * 0.4f}, 0.3f, 4, c);
            break;
        case Icon::Trash:
            DrawRectangleLinesEx({cx - h * 0.55f, cy - h * 0.45f, h * 1.1f, h * 1.25f}, t, c);
            DrawLineEx({cx - h * 0.8f, cy - h * 0.55f}, {cx + h * 0.8f, cy - h * 0.55f}, t, c);
            DrawLineEx({cx - h * 0.2f, cy - h * 0.8f}, {cx + h * 0.2f, cy - h * 0.8f}, t, c);
            break;
        case Icon::Download:
            DrawLineEx({cx, cy - h * 0.8f}, {cx, cy + h * 0.3f}, t * 1.4f, c);
            DrawTriangle({cx - h * 0.5f, cy}, {cx, cy + h * 0.5f}, {cx + h * 0.5f, cy}, c);
            DrawLineEx({cx - h * 0.8f, cy + h * 0.8f}, {cx + h * 0.8f, cy + h * 0.8f}, t * 1.4f, c);
            break;
        case Icon::Link:
            DrawRing({cx - h * 0.3f, cy + h * 0.3f}, h * 0.25f, h * 0.25f + t, 0, 360, 16, c);
            DrawRing({cx + h * 0.3f, cy - h * 0.3f}, h * 0.25f, h * 0.25f + t, 0, 360, 16, c);
            DrawLineEx({cx - h * 0.2f, cy + h * 0.2f}, {cx + h * 0.2f, cy - h * 0.2f}, t, c);
            break;
        case Icon::Check:
            DrawLineEx({cx - h * 0.6f, cy}, {cx - h * 0.15f, cy + h * 0.5f}, t * 1.6f, c);
            DrawLineEx({cx - h * 0.15f, cy + h * 0.5f}, {cx + h * 0.7f, cy - h * 0.5f}, t * 1.6f, c);
            break;
        case Icon::Cross:
            DrawLineEx({cx - h * 0.55f, cy - h * 0.55f}, {cx + h * 0.55f, cy + h * 0.55f}, t * 1.5f, c);
            DrawLineEx({cx + h * 0.55f, cy - h * 0.55f}, {cx - h * 0.55f, cy + h * 0.55f}, t * 1.5f, c);
            break;
        case Icon::Map:
            DrawTriangle({cx - h * 0.9f, cy - h * 0.6f}, {cx - h * 0.9f, cy + h * 0.8f}, {cx - h * 0.3f, cy + h * 0.6f}, c);
            DrawTriangle({cx - h * 0.9f, cy - h * 0.6f}, {cx - h * 0.3f, cy + h * 0.6f}, {cx - h * 0.3f, cy - h * 0.8f}, c);
            DrawRectangleLinesEx({cx - h * 0.3f, cy - h * 0.8f, h * 1.2f, h * 1.4f}, t, c);
            break;
        case Icon::Cpu:
            DrawRectangleLinesEx({cx - h * 0.55f, cy - h * 0.55f, h * 1.1f, h * 1.1f}, t, c);
            for (int k = -1; k <= 1; ++k) {
                DrawLineEx({cx + k * h * 0.3f, cy - h * 0.55f}, {cx + k * h * 0.3f, cy - h * 0.85f}, t, c);
                DrawLineEx({cx + k * h * 0.3f, cy + h * 0.55f}, {cx + k * h * 0.3f, cy + h * 0.85f}, t, c);
            }
            break;
        case Icon::Lock:
            DrawRectangleRounded({cx - h * 0.6f, cy - h * 0.1f, h * 1.2f, h * 0.9f}, 0.2f, 4, c);
            DrawRing({cx, cy - h * 0.15f}, h * 0.3f, h * 0.3f + t, 180, 360, 16, c);
            break;
        case Icon::Bug: {
            // Outlined like the rest of the set: a shell split down the middle,
            // a head, two antennae and three legs a side.
            const float bodyY = cy + h * 0.18f, rx = h * 0.42f, ry = h * 0.55f;
            strokeEllipse(cx, bodyY, rx, ry, t, c);
            DrawLineEx({cx, bodyY - ry + t}, {cx, bodyY + ry - t}, t, c);
            DrawCircleSector({cx, cy - h * 0.38f}, h * 0.27f, 180, 360, 16, c);
            DrawLineEx({cx - h * 0.16f, cy - h * 0.58f}, {cx - h * 0.3f, cy - h * 0.82f}, t, c);
            DrawLineEx({cx + h * 0.16f, cy - h * 0.58f}, {cx + h * 0.3f, cy - h * 0.82f}, t, c);
            const float off[3] = {-0.12f, 0.2f, 0.52f}, slope[3] = {-0.22f, 0.0f, 0.22f};
            for (int k = 0; k < 3; ++k)
                for (float side : {-1.0f, 1.0f}) {
                    const float yy = bodyY + (off[k] - 0.18f) * h * 1.1f;
                    DrawLineEx({cx + side * rx * 0.92f, yy}, {cx + side * h * 0.74f, yy + slope[k] * h * 0.8f}, t, c);
                }
            break;
        }
        case Icon::Shield: {
            const Vector2 p[] = {{cx - h * 0.68f, cy - h * 0.72f}, {cx, cy - h * 0.9f}, {cx + h * 0.68f, cy - h * 0.72f},
                                 {cx + h * 0.68f, cy - h * 0.05f}, {cx + h * 0.45f, cy + h * 0.5f}, {cx, cy + h * 0.88f},
                                 {cx - h * 0.45f, cy + h * 0.5f}, {cx - h * 0.68f, cy - h * 0.05f}};
            for (int k = 0; k < 8; ++k) { DrawLineEx(p[k], p[(k + 1) % 8], t, c); DrawCircleV(p[k], t / 2, c); }
            DrawLineEx({cx - h * 0.3f, cy - h * 0.02f}, {cx - h * 0.05f, cy + h * 0.28f}, t * 1.3f, c);
            DrawLineEx({cx - h * 0.05f, cy + h * 0.28f}, {cx + h * 0.35f, cy - h * 0.3f}, t * 1.3f, c);
            break;
        }
        case Icon::Heart:
            DrawCircle((int)(cx - h * 0.32f), (int)(cy - h * 0.18f), h * 0.36f, c);
            DrawCircle((int)(cx + h * 0.32f), (int)(cy - h * 0.18f), h * 0.36f, c);
            DrawTriangle({cx - h * 0.66f, cy - h * 0.05f}, {cx, cy + h * 0.72f}, {cx + h * 0.66f, cy - h * 0.05f}, c);
            break;
        case Icon::Star: {
            Vector2 pts[10];
            for (int k = 0; k < 10; ++k) {
                float a = -PI / 2 + k * PI / 5;
                float r = (k % 2 == 0) ? h * 0.9f : h * 0.38f;
                pts[k] = {cx + cosf(a) * r, cy + sinf(a) * r};
            }
            for (int k = 0; k < 10; ++k) DrawTriangle({cx, cy}, pts[(k + 1) % 10], pts[k], c);
            break;
        }
    }
}

bool button(Rectangle r, const char* label, Style s, bool disabled, Icon* ic) {
    const bool hov = !disabled && hovered(r);
    const bool down = hov && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Color bg, fg, border;
    switch (s) {
        case Style::Primary:
            bg = hov ? theme::goldHover : theme::gold;
            fg = theme::ground; border = bg; break;
        case Style::Danger:
            bg = hov ? Color{170, 60, 54, 255} : Color{60, 28, 30, 255};
            fg = hov ? theme::ink : Color{232, 150, 140, 255}; border = theme::danger; break;
        case Style::Ghost:
            bg = hov ? Color{255, 255, 255, 14} : BLANK;
            fg = hov ? theme::ink : theme::muted; border = hov ? theme::ruleFirm : BLANK; break;
        default:
            bg = hov ? theme::ruleFirm : theme::raise;
            fg = theme::ink; border = hov ? theme::goldDim : theme::ruleFirm; break;
    }
    if (disabled) { bg = mix(bg, theme::ground, 0.6f); fg = theme::faint; border = theme::rule; }
    Rectangle rr = r;
    if (down) { rr.y += 1; }
    DrawRectangleRounded(rr, 0.22f, 8, bg);
    if (border.a) DrawRectangleRoundedLinesEx(rr, 0.22f, 8, 1.0f, border);
    const float size = std::min(18.0f, r.height * 0.45f);
    const utext::Face f = s == Style::Primary ? utext::Semi : utext::Sans;
    Vector2 m = utext::measure(label, size, f);
    float x = rr.x + (rr.width - m.x) / 2;
    if (ic) {
        x += size * 0.7f;
        icon(*ic, x - size * 0.95f, rr.y + rr.height / 2, size, fg);
    }
    utext::draw(label, x, rr.y + (rr.height - size) / 2 - 1, size, fg, f);
    if (hov) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    return !disabled && clicked(r);
}

bool iconButton(Rectangle r, Icon ic, const char* tip, bool active) {
    const bool hov = hovered(r);
    DrawRectangleRounded(r, 0.25f, 8, active ? theme::accentA(40) : (hov ? Color{255, 255, 255, 18} : BLANK));
    icon(ic, r.x + r.width / 2, r.y + r.height / 2, std::min(r.width, r.height) * 0.5f,
         active ? theme::gold : (hov ? theme::ink : theme::muted));
    if (hov && tip) tooltip(tip);
    if (hov) SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    return clicked(r);
}

bool toggle(Rectangle r, const char* lbl, bool* v, const char* hint) {
    const Rectangle sw{r.x + r.width - 46, r.y + (r.height - 24) / 2, 46, 24};
    const bool hov = hovered(r);
    DrawRectangleRounded(sw, 1.0f, 16, *v ? theme::gold : theme::ruleFirm);
    DrawCircle((int)(*v ? sw.x + sw.width - 12 : sw.x + 12), (int)(sw.y + 12), 9, *v ? theme::ground : theme::muted);
    utext::draw(lbl, r.x, r.y + (hint ? 4 : (r.height - 17) / 2), 17, hov ? theme::ink : Color{215, 211, 202, 255});
    if (hint) utext::draw(utext::ellipsize(hint, r.width - 70, 14), r.x, r.y + 26, 14, theme::faint);
    if (clicked(r)) { *v = !*v; return true; }
    return false;
}

bool slider(Rectangle r, float* v, float mn, float mx, const char* fmt) {
    const Rectangle track{r.x, r.y + r.height / 2 - 3, r.width - 90, 6};
    const float t = (mx > mn) ? (*v - mn) / (mx - mn) : 0;
    DrawRectangleRounded(track, 1, 8, theme::ruleFirm);
    DrawRectangleRounded({track.x, track.y, track.width * t, track.height}, 1, 8, theme::goldDim);
    DrawCircle((int)(track.x + track.width * t), (int)(track.y + 3), 9, theme::gold);
    char buf[64];
    snprintf(buf, sizeof buf, fmt, *v);
    utext::draw(buf, r.x + r.width - 80, r.y + (r.height - 16) / 2, 16, theme::muted);
    const Rectangle hit{track.x - 10, r.y, track.width + 20, r.height};
    if (hovered(hit) && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(g_pressPos, hit)) {
        float nt = std::clamp((mouse().x - track.x) / track.width, 0.0f, 1.0f);
        float nv = mn + nt * (mx - mn);
        if (nv != *v) { *v = nv; return true; }
    }
    return false;
}

bool textField(Rectangle r, std::string& text, int id, const char* placeholder, bool secret) {
    const bool hov = hovered(r);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g_input) {
        if (hov) g_focus = id;
        else if (g_focus == id) g_focus = -1;
    }
    const bool focus = g_focus == id;
    bool changed = false;
    if (focus) {
        int ch;
        while ((ch = GetCharPressed()) > 0) {
            int len = 0;
            const char* utf8 = CodepointToUTF8(ch, &len);
            text.append(utf8, (size_t)len);
            changed = true;
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text.empty()) {
            size_t k = text.size() - 1;
            while (k > 0 && ((unsigned char)text[k] & 0xC0) == 0x80) --k;
            text.erase(k);
            changed = true;
        }
        const bool cmd = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER) || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (cmd && IsKeyPressed(KEY_V)) {
            if (const char* clip = GetClipboardText()) { text += clip; changed = true; }
        }
        if (IsKeyPressed(KEY_ESCAPE)) g_escUsed = true;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) g_focus = -1;
    }
    DrawRectangleRounded(r, 0.2f, 8, theme::sunken);
    DrawRectangleRoundedLinesEx(r, 0.2f, 8, 1.0f, focus ? theme::gold : (hov ? theme::ruleFirm : theme::rule));
    std::string shown = secret ? std::string(text.size(), '*') : text;
    const float size = 17;
    ui::scissor((int)r.x + 8, (int)r.y, (int)r.width - 16, (int)r.height);
    float w = utext::measure(shown, size).x;
    float x = r.x + 10;
    if (w > r.width - 24) x -= w - (r.width - 24);
    if (shown.empty() && placeholder && !focus) utext::draw(placeholder, r.x + 10, r.y + (r.height - size) / 2 - 1, size, theme::faint);
    else utext::draw(shown, x, r.y + (r.height - size) / 2 - 1, size, theme::ink);
    if (focus && fmodf(g_caret, 1.0f) < 0.55f)
        DrawRectangle((int)(x + w + 1), (int)(r.y + 8), 2, (int)(r.height - 16), theme::gold);
    endScissor();
    if (hov) SetMouseCursor(MOUSE_CURSOR_IBEAM);
    return changed;
}

bool textArea(Rectangle r, std::string& text, int id, const char* placeholder) {
    const bool hov = hovered(r);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && g_input) {
        if (hov) g_focus = id;
        else if (g_focus == id) g_focus = -1;
    }
    const bool focus = g_focus == id;
    bool changed = false;
    if (focus) {
        int ch;
        while ((ch = GetCharPressed()) > 0) {
            int len = 0;
            const char* u = CodepointToUTF8(ch, &len);
            text.append(u, (size_t)len);
            changed = true;
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressedRepeat(KEY_ENTER)) { text += '\n'; changed = true; }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text.empty()) {
            size_t k = text.size() - 1;
            while (k > 0 && ((unsigned char)text[k] & 0xC0) == 0x80) --k;
            text.erase(k);
            changed = true;
        }
        const bool cmd = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER) || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        if (cmd && IsKeyPressed(KEY_V)) if (const char* clip = GetClipboardText()) { text += clip; changed = true; }
        if (IsKeyPressed(KEY_ESCAPE)) { g_focus = -1; g_escUsed = true; }
    }
    DrawRectangleRounded(r, 0.05f, 8, theme::sunken);
    DrawRectangleRoundedLinesEx(r, 0.05f, 8, 1.0f, focus ? theme::gold : (hov ? theme::ruleFirm : theme::rule));
    scissor((int)r.x + 6, (int)r.y + 4, (int)r.width - 12, (int)r.height - 8);
    const Rectangle inner{r.x + 10, r.y + 8, r.width - 20, 0};
    const float h = utext::drawWrapped(text, inner, 16, theme::ink, utext::Sans, false);
    const float off = std::max(0.0f, h - (r.height - 20));   // keep the end of the text in view
    if (text.empty() && placeholder && !focus) utext::draw(placeholder, r.x + 10, r.y + 8, 16, theme::faint);
    else utext::drawWrapped(text, {inner.x, inner.y - off, inner.width, 0}, 16, theme::ink);
    if (focus && fmodf(g_caret, 1.0f) < 0.55f) DrawRectangle((int)(r.x + 10), (int)(r.y + 8 + h - off - 2), 2, 2, theme::gold);
    endScissor();
    if (hov) SetMouseCursor(MOUSE_CURSOR_IBEAM);
    return changed;
}

bool searchField(Rectangle r, std::string& text, int id) {
    const bool changed = textField({r.x, r.y, r.width, r.height}, text, id, T("Search"));
    // A magnifier over the left padding, and a clear button when there is text.
    if (text.empty() && focusedField() != id) {
        const float cx = r.x + r.width - 22, cy = r.y + r.height / 2;
        DrawRing({cx - 2, cy - 2}, 5, 7, 0, 360, 16, theme::faint);
        DrawLineEx({cx + 3, cy + 3}, {cx + 8, cy + 8}, 2, theme::faint);
    } else if (!text.empty()) {
        Rectangle x{r.x + r.width - 30, r.y + (r.height - 22) / 2, 22, 22};
        icon(Icon::Cross, x.x + 11, x.y + 11, 12, hovered(x) ? theme::ink : theme::faint);
        if (clicked(x)) { text.clear(); return true; }
    }
    return changed;
}

bool matches(const std::string& haystack, const std::string& query) {
    if (query.empty()) return true;
    // Case-insensitive for ASCII; other scripts match as typed, which is what
    // a person typing in them expects.
    auto low = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    return low(haystack).find(low(query)) != std::string::npos;
}

int dropdown(Rectangle r, const std::vector<std::string>& items, int selected, int id) {
    const bool hov = hovered(r);
    DrawRectangleRounded(r, 0.2f, 8, hov ? theme::ruleFirm : theme::raise);
    DrawRectangleRoundedLinesEx(r, 0.2f, 8, 1.0f, g_openDropdown == id ? theme::gold : theme::ruleFirm);
    std::string cur = (selected >= 0 && selected < (int)items.size()) ? items[selected] : "";
    utext::draw(utext::ellipsize(cur, r.width - 40, 17), r.x + 12, r.y + (r.height - 17) / 2 - 1, 17, theme::ink);
    DrawTriangle({r.x + r.width - 22, r.y + r.height / 2 - 3}, {r.x + r.width - 16, r.y + r.height / 2 + 4},
                 {r.x + r.width - 10, r.y + r.height / 2 - 3}, theme::muted);
    int result = selected;
    if (clicked(r)) g_openDropdown = g_openDropdown == id ? -1 : id;
    if (g_openDropdown == id) {
        const float rowH = 32;
        const int shown = std::min((int)items.size(), 12);
        Rectangle list{r.x, r.y + r.height + 4, r.width, rowH * shown + 8};
        if (list.y + list.height > ui::H() - 8) list.y = r.y - list.height - 4;
        // Clicks are resolved now, drawing is deferred so the list sits above
        // whatever is drawn after this widget.
        const Vector2 m = mouse();
        static float scroll[64] = {0};
        float& sc = scroll[id & 63];
        if (CheckCollisionPointRec(m, list)) {
            sc -= GetMouseWheelMove() * rowH;
            sc = std::clamp(sc, 0.0f, std::max(0.0f, (float)items.size() * rowH - shown * rowH));
        }
        int hoverRow = -1;
        if (CheckCollisionPointRec(m, list)) hoverRow = (int)((m.y - list.y - 4 + sc) / rowH);
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(g_pressPos, r)) {
            if (hoverRow >= 0 && hoverRow < (int)items.size() && CheckCollisionPointRec(m, list)) result = hoverRow;
            g_openDropdown = -1;
        }
        g_overlays.push_back([list, items, selected, hoverRow, rowH, sc] {
            DrawRectangleRounded(list, 0.06f, 8, theme::raise);
            DrawRectangleRoundedLinesEx(list, 0.06f, 8, 1.0f, theme::ruleFirm);
            ui::scissor((int)list.x, (int)list.y + 4, (int)list.width, (int)list.height - 8);
            for (int i = 0; i < (int)items.size(); ++i) {
                float y = list.y + 4 + i * rowH - sc;
                if (y + rowH < list.y || y > list.y + list.height) continue;
                if (i == hoverRow) DrawRectangle((int)list.x + 4, (int)y, (int)list.width - 8, (int)rowH, theme::ruleFirm);
                utext::draw(utext::ellipsize(items[i], list.width - 24, 16), list.x + 12, y + 7, 16,
                            i == selected ? theme::gold : theme::ink);
            }
            endScissor();
        });
    }
    return result;
}

void progress(Rectangle r, float p, const char* lbl) {
    DrawRectangleRounded(r, 1.0f, 8, theme::rule);
    if (p < 0) {
        // Indeterminate: a gold segment travelling along the bar.
        const float t = fmodf((float)GetTime() * 0.7f, 1.0f);
        const float w = r.width * 0.25f;
        float x = r.x + (r.width + w) * t - w;
        float x0 = std::max(r.x, x), x1 = std::min(r.x + r.width, x + w);
        if (x1 > x0) DrawRectangleRounded({x0, r.y, x1 - x0, r.height}, 1.0f, 8, theme::gold);
    } else if (p > 0) {
        DrawRectangleRounded({r.x, r.y, r.width * std::clamp(p, 0.0f, 1.0f), r.height}, 1.0f, 8, theme::gold);
    }
    if (lbl) utext::draw(utext::ellipsize(lbl, r.width, 14), r.x, r.y + r.height + 6, 14, theme::muted);
}

void card(Rectangle r, bool raised) {
    DrawRectangleRounded(r, 0.04f, 8, raised ? theme::raise : theme::sunken);
    DrawRectangleRoundedLinesEx(r, 0.04f, 8, 1.0f, theme::rule);
}

void divider(float x, float y, float w) { DrawRectangle((int)x, (int)y, (int)w, 1, theme::rule); }

void tooltip(const char* text) { g_tooltip = text ? text : ""; }

void spinner(float cx, float cy, float r) {
    const float a = (float)GetTime() * 300.0f;
    DrawRing({cx, cy}, r - 3, r, a, a + 270, 24, theme::gold);
}

void beginScroll(Rectangle r, Scroll& s) {
    if (CheckCollisionPointRec(mouse(), r) && !g_hasModal) s.target -= GetMouseWheelMove() * 60.0f;
    s.target = std::clamp(s.target, 0.0f, std::max(0.0f, s.content - r.height));
    s.y += (s.target - s.y) * std::min(1.0f, GetFrameTime() * 14.0f);
    ui::scissor((int)r.x, (int)r.y, (int)r.width, (int)r.height);
}

void endScroll(Rectangle r, Scroll& s, float contentHeight) {
    endScissor();
    s.content = contentHeight;
    if (contentHeight > r.height) {
        const float frac = r.height / contentHeight;
        const float barH = std::max(30.0f, r.height * frac);
        const float t = s.y / std::max(1.0f, contentHeight - r.height);
        DrawRectangleRounded({r.x + r.width - 5, r.y + (r.height - barH) * t, 4, barH}, 1.0f, 4, theme::ruleFirm);
    }
}

void label(const std::string& s, float x, float y, float size, Color c, utext::Face f) { utext::draw(s, x, y, size, c, f); }

float heading(const std::string& s, float x, float y, float size) {
    utext::draw(s, x, y, size, theme::ink, utext::Serif);
    return size * 1.3f;
}

void deferredOverlays() {
    for (auto& o : g_overlays) o();
    g_overlays.clear();
    if (!g_tooltip.empty()) {
        Vector2 m = mouse();
        Vector2 sz = utext::measure(g_tooltip, 15);
        Rectangle r{m.x + 14, m.y + 18, sz.x + 18, 28};
        if (r.x + r.width > ui::W() - 4) r.x = ui::W() - r.width - 4;
        DrawRectangleRounded(r, 0.3f, 6, Color{30, 35, 47, 245});
        DrawRectangleRoundedLinesEx(r, 0.3f, 6, 1.0f, theme::ruleFirm);
        utext::draw(g_tooltip, r.x + 9, r.y + 6, 15, theme::ink);
    }
}

int focusedField() { return g_focus; }
bool escapePressed() { return IsKeyPressed(KEY_ESCAPE) && !g_escUsed && g_focus < 0; }
void clearFocus() { g_focus = -1; g_openDropdown = -1; }
}  // namespace ui
