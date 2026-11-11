// SPDX-License-Identifier: GPL-3.0-or-later
#include "touch_controls.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace das::app {

namespace {

constexpr float kDeadZone = 8; // units from the pad's middle that hold nothing
constexpr ui::Color kFace{230, 232, 240, 70};
constexpr ui::Color kPressed{236, 196, 92, 150};
constexpr ui::Color kEdge{255, 255, 255, 90};

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
        attack_ = {rx, field.y + field.h - 6 - button, button, button};
        use_ = {rx + button + 8, attack_.y - std::floor(button * 0.6f), button, button};
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
        return;
    }
    padCenter_ = {field.x + 6 + padRadius_, field.y + field.h - 6 - padRadius_};
    use_ = {field.x + field.w - 6 - button, field.y + field.h - 6 - button - std::floor(button * 0.6f), button, button};
    attack_ = {use_.x - button - 8, use_.y + std::floor(button * 0.6f), button, button};
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
    // The pad: a cross of four arms around a middle.
    const float r = padRadius_;
    const float arm = std::floor(r * 0.62f);
    const float half = std::floor(arm / 2);
    const float cx = padCenter_.x;
    const float cy = padCenter_.y;
    const struct {
        Action action;
        SDL_FRect rect;
    } arms[] = {
        {Action::MoveUp, {cx - half, cy - r, arm, r - half}},
        {Action::MoveDown, {cx - half, cy + half, arm, r - half}},
        {Action::MoveLeft, {cx - r, cy - half, r - half, arm}},
        {Action::MoveRight, {cx + half, cy - half, r - half, arm}},
    };
    for (const auto& a : arms) {
        ui::fill(renderer, a.rect, held(a.action) ? kPressed : kFace);
        ui::outline(renderer, a.rect, kEdge);
    }
    ui::fill(renderer, {cx - half, cy - half, arm, arm}, kFace);
    for (const auto& [rect, action, label] :
         {std::tuple{use_, Action::Use, "Use"}, std::tuple{attack_, Action::Attack, "Hit"}}) {
        ui::fill(renderer, rect, held(action) ? kPressed : kFace);
        ui::outline(renderer, rect, kEdge);
        ui::textCentered(renderer, rect.x + rect.w / 2, rect.y + (rect.h - ui::kFont) / 2, label, {255, 255, 255, 220});
    }
}

} // namespace das::app
