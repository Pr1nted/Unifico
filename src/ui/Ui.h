#pragma once
// Immediate-mode widgets in the website's look: a lamplit map table, warm ink
// on a near-black ground, one gold accent. The colours are site.css's tokens,
// named the same way, so the launcher and opendoctrines.pages.dev agree.
#include "raylib.h"
#include "ui/Text.h"
#include <string>
#include <vector>

namespace theme {
inline const Color ground{12, 15, 22, 255};    // --ground
inline const Color raise{20, 24, 34, 255};     // --raise
inline const Color sunken{9, 11, 17, 255};     // --sunken
inline const Color ink{232, 228, 218, 255};    // --ink
inline const Color muted{168, 174, 188, 255};  // --muted, likewise
inline const Color faint{124, 131, 146, 255};  // --faint, lifted for small text on the launcher's darker ground
inline const Color rule{35, 41, 54, 255};      // --rule
inline const Color ruleFirm{51, 59, 75, 255};  // --rule-firm
// The accent. --gold on the website and the default here, but the person can
// pick another (Settings -> Appearance), exactly as the game lets them -- so
// these two are the only theme colours that are not constants.
inline Color gold{201, 162, 39, 255};          // --gold
inline Color goldDim{140, 115, 32, 255};       // --gold-dim
inline Color goldHover{222, 182, 52, 255};
/** Set the accent from 0xRRGGBB; the dim and hover shades follow. */
void setAccent(int rgb);
/** The accent with a given alpha. */
inline Color accentA(unsigned char a) { return Color{gold.r, gold.g, gold.b, a}; }
/** The presets offered: the website's gold first, then the game's own list. */
inline const int kAccentPresets[] = {0xC9A227, 0xFFD700, 0x0096FF, 0x00FF64, 0xFF3232, 0xC832FF,
                                     0xFF9600, 0xFF6496, 0x64FFFF, 0xFFFFFF, 0xC8C8C8};
inline const Color sea{22, 32, 46, 255};       // --sea
inline const Color danger{196, 72, 64, 255};
inline const Color ok{94, 170, 110, 255};
}  // namespace theme

enum class Icon { Play, Box, Globe, Puzzle, Trophy, Server, User, Gear, Tools, Terminal, News,
                  Folder, Trash, Download, Link, Stop, Check, Cross, Map, Cpu, Lock, Star };

namespace ui {
void beginFrame();
void endFrame();

Vector2 mouse();               // in logical units
/** Interface scale: logical units -> window points. */
void setScale(float s);
float scale();
float W();                     // window size in logical units
float H();
/** BeginScissorMode, in logical units. */
void scissor(int x, int y, int w, int h);
bool hovered(Rectangle r);
bool clicked(Rectangle r);          // released over r, pressed over r too
/** True while a modal is open: everything outside it ignores the mouse. */
void setModalRect(const Rectangle* r);
void setInput(bool on);
bool inputOn();

enum class Style { Primary, Secondary, Ghost, Danger };
bool button(Rectangle r, const char* label, Style s = Style::Secondary, bool disabled = false, Icon* icon = nullptr);
bool iconButton(Rectangle r, Icon icon, const char* tooltip = nullptr, bool active = false);
bool toggle(Rectangle r, const char* label, bool* value, const char* hint = nullptr);
bool slider(Rectangle r, float* value, float min, float max, const char* fmt);
/** Single-line text field. `id` keeps focus stable across frames. */
bool textField(Rectangle r, std::string& text, int id, const char* placeholder = nullptr, bool secret = false);
/** Drop-down; returns the selected index, possibly changed. */
int dropdown(Rectangle r, const std::vector<std::string>& items, int selected, int id);
void progress(Rectangle r, float p, const char* label = nullptr);
void card(Rectangle r, bool raised = true);
void divider(float x, float y, float w);
void icon(Icon i, float cx, float cy, float size, Color c);
void tooltip(const char* text);
void spinner(float cx, float cy, float r);

/** A vertical scroll region; content drawn between begin and end is clipped. */
struct Scroll { float y = 0, target = 0, content = 0; };
void beginScroll(Rectangle r, Scroll& s);
void endScroll(Rectangle r, Scroll& s, float contentHeight);

void label(const std::string& s, float x, float y, float size, Color c = theme::ink, utext::Face f = utext::Sans);
float heading(const std::string& s, float x, float y, float size = 30);

/** Drawn last, above everything: dropdown lists and tooltips. */
void deferredOverlays();
int focusedField();
void clearFocus();
}  // namespace ui
