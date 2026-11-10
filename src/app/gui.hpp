// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small immediate-mode toolkit for the editor: buttons, checkboxes,
// steppers, text fields and scrolling lists, drawn with the ui helpers. Every
// widget can explain itself: the text of the widget under the mouse (or with
// the keyboard focus) is shown in the editor's help bar.
//
// Widgets are declared while drawing a frame; what was pressed is answered
// right away. Keyboard and gamepad move a focus through the widgets in the
// order they were declared.
#pragma once

#include "ui.hpp"

#include <SDL3/SDL.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace das::app::gui {

inline constexpr float kRow = 14;  // a widget's height
inline constexpr float kGap = 4;

// What the player did since the last frame.
struct Input {
    float mouseX = -1;
    float mouseY = -1;
    bool mouseDown = false;    // held
    bool mousePressed = false; // went down this frame
    bool mouseReleased = false;
    bool rightPressed = false;
    float wheel = 0; // positive: up
    std::string typed; // text typed this frame
    bool backspace = false;
    bool deleteKey = false;
    bool enter = false;
    bool escape = false;
    bool left = false; // cursor keys for text fields
    bool right = false;
    bool home = false;
    bool end = false;
    bool ctrl = false;
    bool shift = false;
    char shortcut = 0; // a letter pressed with Ctrl, lower case
    // Moving the focus (keyboard or gamepad), activating and going back.
    int navY = 0;
    int navX = 0;
    bool activate = false;
    bool back = false;
};

// Collects SDL events into an Input. Mouse positions must already be in
// interface units.
class InputCollector {
public:
    void event(const SDL_Event& e);
    // The input of this frame; the per-frame parts are cleared afterwards.
    Input take();

private:
    Input input_;
};

class Gui {
public:
    void begin(SDL_Renderer* renderer, const Input& input);
    void end();

    SDL_Renderer* renderer() const { return renderer_; }
    const Input& input() const { return input_; }

    // Widgets. `id` must be unique in the frame and the same every frame.
    bool button(std::string_view id, const SDL_FRect& r, std::string_view label, std::string_view help,
                bool selected = false, bool enabled = true);
    bool checkbox(std::string_view id, const SDL_FRect& r, std::string_view label, bool& value,
                  std::string_view help);
    // "label    < value >": left and right change `value` within min..max.
    bool stepper(std::string_view id, const SDL_FRect& r, std::string_view label, int& value, int min, int max,
                 std::string_view shown, std::string_view help, int step = 1);
    // A one-line text field; returns true when the text changed.
    bool textField(std::string_view id, const SDL_FRect& r, std::string& text, std::string_view help,
                   std::size_t maxLength = 200, std::string_view placeholder = {});
    void label(float x, float y, std::string_view text, ui::Color c = ui::color::text);
    // Text wrapped to a width; returns the height used.
    float paragraph(float x, float y, float width, std::string_view text, ui::Color c = ui::color::dim,
                    int maxLines = 99);

    // A free area that takes clicks (a canvas, a tile in a list). Registers
    // for the focus too; `help` explains it.
    bool area(std::string_view id, const SDL_FRect& r, std::string_view help);
    bool hovered(const SDL_FRect& r) const;
    bool focused(std::string_view id) const { return focus_ == id; }
    void setFocus(std::string_view id) { focus_ = std::string(id); }
    bool typing() const { return !editing_.empty(); }

    // Scrolling: content drawn between beginScroll and endScroll is moved
    // up by `scroll` and clipped to `r`. The wheel over `r` changes it.
    void beginScroll(const SDL_FRect& r, float& scroll, float contentHeight);
    void endScroll();
    // Inside a scroll area: whether a rectangle can be seen at all.
    bool visible(const SDL_FRect& r) const;

    // The explanation for the help bar: of the widget under the mouse, or
    // else of the focused one.
    const std::string& help() const { return help_; }
    void setHelp(std::string_view text) { help_ = std::string(text); }

private:
    bool interact(std::string_view id, const SDL_FRect& r, std::string_view help);
    void focusRing(std::string_view id, const SDL_FRect& r);

    SDL_Renderer* renderer_ = nullptr;
    Input input_;
    std::string help_;
    std::string focusHelp_;
    std::string focus_;
    std::string pressedOn_; // the widget the mouse went down on
    std::vector<std::string> order_;     // focusable widgets of this frame
    std::vector<std::string> lastOrder_; // and of the last one
    std::string editing_; // the text field being typed in
    std::size_t caret_ = 0;
    struct Scroll {
        SDL_FRect rect;
        float offset;
    };
    std::vector<Scroll> scrolls_;
};

// Wraps text into lines of at most `columns` characters.
std::vector<std::string> wrap(std::string_view text, std::size_t columns);

} // namespace das::app::gui
