#pragma once
// Drawing text in every language the game speaks.
//
// Three embedded faces in the website's type: Source Sans 3 (regular and
// semibold) for the interface and Spectral for headings. They cover Latin,
// Cyrillic, Greek and Vietnamese. Everything else -- Arabic, Urdu, Armenian,
// Georgian, Devanagari, Chinese, Japanese, Korean -- comes from a FALLBACK
// face found on the system (or downloaded once into the cache when the system
// has none), rasterised only for the codepoints the current language uses.
// That is what keeps the binary small: no CJK font is shipped.
//
// Arabic and Urdu are shaped (joining forms, right-to-left) with the game's
// own shaper, src/ui/Arabic.cpp. Devanagari is drawn unshaped: correct
// letters, imperfect conjuncts; shaping it properly needs HarfBuzz, which
// would double the launcher's size.
#include "raylib.h"
#include <string>

namespace utext {
enum Face { Sans = 0, Semi = 1, Serif = 2 };

void init();
/** After a language change: rebuild atlases for its codepoints. */
void rebuild();
/** Call once a frame: picks up codepoints first seen last frame. */
void frame();
void shutdown();
/** The interface zoom changed (window resized, fullscreen, setting). */
void setScale(float s);

Vector2 measure(const std::string& s, float size, Face f = Sans);
float draw(const std::string& s, float x, float y, float size, Color c, Face f = Sans);
/** Wrapped into width; returns the height used. `draw` false only measures. */
float drawWrapped(const std::string& s, Rectangle box, float size, Color c, Face f = Sans,
                  bool doDraw = true, float lineGap = 1.25f);
/** Truncate with an ellipsis to fit width. */
std::string ellipsize(const std::string& s, float width, float size, Face f = Sans);
}  // namespace utext
