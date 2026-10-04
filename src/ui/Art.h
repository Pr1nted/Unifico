#pragma once
// The launcher's pictures. Two kinds:
//
//   KEY ART   the game's own Steam artwork and screenshots, embedded as JPEG,
//             shown as a slow drifting pan behind the Play page -- the Minecraft
//             launcher's big picture, in this game's colours.
//   DRAWN     everything else is drawn, not stored, which is how a launcher can
//             have a great deal of art and stay small: an animated topographic
//             map-table ground, a compass rose, per-section banners, the cards
//             for the other games, ships and clouds crossing the hero.
#include "raylib.h"

namespace art {
void init();
/** The accent changed: redraw what was drawn in it once and cached. */
void accentChanged();
void shutdown();

/** The Play page's picture. `which` picks the key art; it cross-fades on change. */
void hero(Rectangle r, int which, float t);
int heroCount();

/** The whole-window ground: contour lines drifting under a lamplit vignette. */
void backdrop(Rectangle r, float t);

/** A section header strip, different per section (seeded by `seed`). */
void banner(Rectangle r, int seed, float t);

void compass(float cx, float cy, float radius, float t, Color c);
/** Unifico's own wordmark ("UNIFICO", pixel face, matching the game's O.D.). */
void logo(float x, float y, float height, Color tint);
/** The game's O.D. mark, for the Play page's key art. */
void gameLogo(float x, float y, float height, Color tint);
Texture2D appIcon();
Texture2D achievementAtlas();
Texture2D templeosShot();
Texture2D libraryArt();

/** Cards for the game shelf. */
enum class Game { OpenDoctrines, Unciv, GreaterDiplomacy5, GreaterDiplomacy4, TempleOS };
void gameCard(Rectangle r, Game g, bool active, float t);
}  // namespace art
