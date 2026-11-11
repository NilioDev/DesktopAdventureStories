// SPDX-License-Identifier: GPL-3.0-or-later
#include "touch_controls.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace das::app {

namespace {

constexpr float kDeadZone = 8; // units from the pad's middle that hold nothing
constexpr ui::Color kFace{230, 232, 240, 45};
constexpr ui::Color kEdge{255, 255, 255, 80};

// A round button or stick part: a circle in `rect`'s square.
void circle(SDL_Renderer* r, const SDL_FRect& rect, ui::Color fill, ui::Color edge) {
    ui::fillRounded(r, rect, rect.w / 2, fill);
    ui::outlineRounded(r, rect, rect.w / 2, edge);
}

} // namespace

void TouchControls::place(const ui::Layout& layout) {
    const SDL_FRect& field = layout.field;
    const float size = std::clamp(field.w / 3.6f, 64.0f, 120.0f);
    padRadius_ = size / 2;
    const float button = std::floor(size * 0.42f);
    const float leftRoom = layout.left.w > 0 ? layout.left.x : field.x;
    // In play the cards and the item bar take the room around the field.
    const SDL_FRect panel = ui::hudArea(layout);
    const float rightRoom = layout.width - (panel.x + panel.w);
    if (!layout.portrait && leftRoom >= size + 12 && rightRoom >= 2 * button + 20) {
        // In the empty strips left and right.
        padCenter_ = {std::floor(leftRoom / 2), field.y + field.h - padRadius_ - 6};
        const float rx = panel.x + panel.w + std::floor((rightRoom - 2 * button - 8) / 2);
        const float big = std::floor(button * 1.2f);
        attack_ = {rx, field.y + field.h - 6 - button, button, button};
        use_ = {rx + button + 8, attack_.y - std::floor(button * 0.6f) - (big - button), big, big};
        item_ = {use_.x + (big - button) / 2, use_.y - button - 10, button, button};
        return;
    }
    const float below = layout.height - (panel.y + panel.h);
    if (layout.portrait && below >= kBottomRoom - 8) {
        // In the strip under the panel.
        padRadius_ = std::floor((below - 12) / 2);
        const float b = std::floor(padRadius_ * 0.85f);
        padCenter_ = {panel.x + padRadius_ + 4, layout.height - 6 - padRadius_};
        use_ = {panel.x + panel.w - b - 4, layout.height - 6 - 2 * padRadius_, b, b};
        attack_ = {use_.x - b - 10, layout.height - 6 - b, b, b};
        const float small = std::floor(b * 0.8f);
        item_ = {attack_.x - small - 10, use_.y, small, small};
        return;
    }
    padCenter_ = {field.x + 6 + padRadius_, field.y + field.h - 6 - padRadius_};
    use_ = {field.x + field.w - 6 - button, field.y + field.h - 6 - button - std::floor(button * 0.6f), button, button};
    attack_ = {use_.x - button - 8, use_.y + std::floor(button * 0.6f), button, button};
    item_ = {use_.x, use_.y - button - 8, button, button};
}

void TouchControls::setVisible(bool visible, ActionState& state) {
    if (visible == visible_) {
        return;
    }
    visible_ = visible;
    if (!visible_) {
        for (auto& [finger, action] : fingers_) {
            if (action) {
                state.release(*action);
            }
        }
        fingers_.clear();
    }
}

std::optional<Action> TouchControls::actionAt(float x, float y, bool fromPad) const {
    const float dx = x - padCenter_.x;
    const float dy = y - padCenter_.y;
    // A finger that started on the pad keeps steering from anywhere.
    if (fromPad || dx * dx + dy * dy <= padRadius_ * padRadius_) {
        if (std::abs(dx) < kDeadZone && std::abs(dy) < kDeadZone) {
            return std::nullopt;
        }
        if (std::abs(dy) >= std::abs(dx)) {
            return dy < 0 ? Action::MoveUp : Action::MoveDown;
        }
        return dx < 0 ? Action::MoveLeft : Action::MoveRight;
    }
    const auto grown = [](const SDL_FRect& r) { return SDL_FRect{r.x - 6, r.y - 6, r.w + 12, r.h + 12}; };
    if (ui::contains(grown(use_), x, y)) {
        return Action::Use;
    }
    if (ui::contains(grown(attack_), x, y)) {
        return Action::Attack;
    }
    if (ui::contains(grown(item_), x, y)) {
        return Action::InventoryNext;
    }
    return std::nullopt;
}

void TouchControls::hold(SDL_FingerID finger, std::optional<Action> action, ActionState& state) {
    auto& held = fingers_[finger];
    if (held == action) {
        return;
    }
    if (held) {
        state.release(*held);
    }
    if (action) {
        state.press(*action);
    }
    held = action;
}

bool TouchControls::fingerDown(SDL_FingerID finger, float x, float y, ActionState& state) {
    if (!visible_) {
        return false;
    }
    const float dx = x - padCenter_.x;
    const float dy = y - padCenter_.y;
    const bool onPad = dx * dx + dy * dy <= padRadius_ * padRadius_;
    const auto action = actionAt(x, y, false);
    if (!onPad && !action) {
        return false;
    }
    hold(finger, action, state);
    if (onPad) {
        padFingers_.insert(finger);
    }
    return true;
}

bool TouchControls::fingerMoved(SDL_FingerID finger, float x, float y, ActionState& state) {
    const auto it = fingers_.find(finger);
    if (it == fingers_.end()) {
        return false;
    }
    if (padFingers_.contains(finger)) {
        hold(finger, actionAt(x, y, true), state);
    }
    return true;
}

bool TouchControls::fingerUp(SDL_FingerID finger, ActionState& state) {
    const auto it = fingers_.find(finger);
    if (it == fingers_.end()) {
        return false;
    }
    hold(finger, std::nullopt, state);
    fingers_.erase(finger);
    padFingers_.erase(finger);
    return true;
}

void TouchControls::render(SDL_Renderer* renderer) const {
    if (!visible_) {
        return;
    }
    const auto held = [&](Action a) {
        return std::any_of(fingers_.begin(), fingers_.end(), [&](const auto& f) { return f.second == a; });
    };
    // The stick: a round base, and a knob that leans the way it is held.
    const float r = padRadius_;
    const float cx = padCenter_.x;
    const float cy = padCenter_.y;
    circle(renderer, {cx - r, cy - r, 2 * r, 2 * r}, kFace, kEdge);
    float kx = 0;
    float ky = 0;
    if (held(Action::MoveUp)) {
        ky = -1;
    } else if (held(Action::MoveDown)) {
        ky = 1;
    } else if (held(Action::MoveLeft)) {
        kx = -1;
    } else if (held(Action::MoveRight)) {
        kx = 1;
    }
    const float knob = std::floor(r * 0.42f);
    const float lean = std::floor(r * 0.45f);
    const ui::Color knobColor = kx != 0 || ky != 0 ? ui::Color{ui::color::accent.r, ui::color::accent.g, ui::color::accent.b, 170}
                                                   : ui::Color{255, 255, 255, 90};
    circle(renderer, {cx + kx * lean - knob, cy + ky * lean - knob, 2 * knob, 2 * knob}, knobColor,
           {255, 255, 255, 130});

    // The buttons: Use in the game's colour, Hit and the next item lighter.
    const auto accent = [](Uint8 a) { return ui::Color{ui::color::accent.r, ui::color::accent.g, ui::color::accent.b, a}; };
    circle(renderer, use_, accent(held(Action::Use) ? 240 : 190), kEdge);
    ui::textCentered(renderer, use_.x + use_.w / 2, use_.y + (use_.h - ui::kFont) / 2, "Use", ui::color::accentInk);
    for (const auto& [rect, action, label] :
         {std::tuple{attack_, Action::Attack, "Hit"}, std::tuple{item_, Action::InventoryNext, "Item"}}) {
        circle(renderer, rect, held(action) ? accent(150) : kFace, kEdge);
        ui::textCentered(renderer, rect.x + rect.w / 2, rect.y + (rect.h - ui::kFont) / 2, label, {255, 255, 255, 230});
    }
}

} // namespace das::app
