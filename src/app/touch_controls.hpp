// SPDX-License-Identifier: GPL-3.0-or-later
//
// On-screen controls for touch screens: a direction pad and Use and Attack
// buttons over the bottom corners of the play field. Several fingers work at
// once, so the hero can walk and attack together.
#pragma once

#include "das/input.hpp"

#include "ui.hpp"

#include <SDL3/SDL.h>

#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace das::app {

class TouchControls {
public:
    // Places the controls beside the play field when the screen has room
    // there (a phone held sideways), otherwise over its bottom corners: a
    // round stick on the left, and on the right a large Use button, Hit and
    // a button for the next item.
    void place(const ui::Layout& layout);
    // Room the pad wants under the panel on a tall screen.
    static constexpr float kBottomRoom = 112;
    void setVisible(bool visible, ActionState& state);
    bool visible() const { return visible_; }

    // A finger in interface units. Returns true when it is on a control (or
    // was put down on one), so that it is not also a tap on the game.
    bool fingerDown(SDL_FingerID finger, float x, float y, ActionState& state);
    bool fingerMoved(SDL_FingerID finger, float x, float y, ActionState& state);
    bool fingerUp(SDL_FingerID finger, ActionState& state);

    void render(SDL_Renderer* renderer) const;

private:
    std::optional<Action> actionAt(float x, float y, bool fromPad) const;
    void hold(SDL_FingerID finger, std::optional<Action> action, ActionState& state);

    bool visible_ = false;
    SDL_FPoint padCenter_{};
    float padRadius_ = 40;
    SDL_FRect use_{};
    SDL_FRect attack_{};
    SDL_FRect item_{}; // the next item
    // What each finger on a control holds; nullopt while it rests in the
    // pad's middle.
    std::unordered_map<SDL_FingerID, std::optional<Action>> fingers_;
    std::unordered_set<SDL_FingerID> padFingers_; // fingers that started on the pad
};

} // namespace das::app
