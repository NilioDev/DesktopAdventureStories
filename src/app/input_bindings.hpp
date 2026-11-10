// SPDX-License-Identifier: GPL-3.0-or-later
//
// Translates SDL keyboard and gamepad events into engine actions.
#pragma once

#include "das/input.hpp"

#include <SDL3/SDL.h>

#include <unordered_map>

namespace das::app {

class InputBindings {
public:
    // Feeds one SDL event; returns true when the event was input we handled.
    bool handleEvent(const SDL_Event& event, ActionState& state);

    // Releases all held input, for example when the window loses focus.
    void reset(ActionState& state);

private:
    // An analog stick acts like a d-pad: each axis is -1, 0 or +1.
    struct StickState {
        int x = 0;
        int y = 0;
    };

    void updateStickAxis(int& current, int next, Action negative, Action positive, ActionState& state);

    std::unordered_map<SDL_JoystickID, StickState> sticks_;
};

} // namespace das::app
