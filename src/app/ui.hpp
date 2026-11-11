// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small drawing helpers for our own interface around the play field: one
// colour scheme, panels, text and the screen layout.
//
// Everything is drawn in interface units. One unit is a whole number of
// screen pixels (the interface scale), so the panels stay sharp on every
// screen. Text uses Fira Sans, and the speech bubbles during play the pixel
// font PxPlus IBM CGA (both built into the program), drawn at the screen's own
// resolution.
#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <string_view>
#include <vector>

namespace das::app::ui {

struct Color {
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    Uint8 a = 255;
};

namespace color {
inline constexpr Color background{18, 20, 26};
inline constexpr Color panel{28, 31, 40};
inline constexpr Color panelLight{38, 42, 54};
inline constexpr Color border{56, 61, 76};
inline constexpr Color accent{236, 196, 92};
inline constexpr Color text{226, 229, 236};
inline constexpr Color dim{146, 151, 164};
inline constexpr Color faint{96, 101, 114};
inline constexpr Color good{112, 190, 112};
inline constexpr Color danger{222, 86, 74};
inline constexpr Color shade{0, 0, 0, 150};
} // namespace color

// The height of a line of text in units: capitals fill it, the tails of g
// and y hang below. Centre a line in a row with (row height - kFont) / 2.
inline constexpr float kFont = 8;

// Where things go on a screen of a given size.
struct Layout {
    float scale = 1;  // screen pixels per unit
    float width = 0;  // the screen, in units
    float height = 0;
    SDL_FRect field{}; // the square play field
    SDL_FRect panel{}; // the side (or bottom) panel
    bool portrait = false;
};

// The play field is 288 game pixels square. `pixelPerfect` keeps it at a
// whole multiple of that on screen. On a tall screen `bottomRoom` units are
// kept free under the panel (for the touch pad).
Layout layout(int pixelWidth, int pixelHeight, bool pixelPerfect, float bottomRoom = 0);
// The whole screen at a given scale, without a play field (the editor).
Layout plainLayout(int pixelWidth, int pixelHeight, float scale);

enum class Font {
    Interface, // Fira Sans: menus, panels, the editor
    Story,     // PxPlus IBM CGA, 8x8 pixels: what characters say during play
};

// Loads the fonts for this renderer. Without it (it should not happen) text
// falls back to SDL's small built-in font.
void init(SDL_Renderer* renderer);
// Frees the font, before the renderer goes.
void shutdown();

// Starts drawing a frame in units.
void begin(SDL_Renderer* renderer, const Layout& layout);

void fill(SDL_Renderer* renderer, const SDL_FRect& rect, Color c);
void outline(SDL_Renderer* renderer, const SDL_FRect& rect, Color c);
// A panel: filled, with a border and a soft shadow.
void panel(SDL_Renderer* renderer, const SDL_FRect& rect, Color fillColor = color::panel);
// One line of text (UTF-8) with its top at y, `size` times as large.
void text(SDL_Renderer* renderer, float x, float y, std::string_view line, Color c, float size = 1,
          Font font = Font::Interface);
// How wide a line is, in units.
float textWidth(std::string_view line, float size = 1, Font font = Font::Interface);
// The line itself when it fits in `width`, otherwise its start and "…".
std::string fit(std::string_view line, float width, float size = 1, Font font = Font::Interface);
// Text split at spaces into lines that fit in `width`. A newline starts a new
// line; a word longer than a line (a file path) is cut.
std::vector<std::string> wrap(std::string_view text, float width, float size = 1, Font font = Font::Interface);
void textRight(SDL_Renderer* renderer, float right, float y, std::string_view line, Color c, float size = 1);
void textCentered(SDL_Renderer* renderer, float centerX, float y, std::string_view line, Color c, float size = 1);
// A bar filled for `ratio` (0 to 1).
void bar(SDL_Renderer* renderer, const SDL_FRect& rect, float ratio, Color c);

bool contains(const SDL_FRect& rect, float x, float y);

} // namespace das::app::ui
