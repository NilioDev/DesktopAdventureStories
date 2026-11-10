// SPDX-License-Identifier: GPL-3.0-or-later
//
// Translates SDL keyboard and gamepad events into engine actions, with
// controls the player can change (kept in the settings).
#pragma once

#include "das/input.hpp"

#include "settings.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace das::app {

class InputBindings {
public:
    InputBindings() { configure(Settings{}); }

    // Takes the controls of the settings; actions they leave out keep their
    // defaults.
    void configure(const Settings& settings);

    // Feeds one SDL event; returns true when the event was input we handled.
    bool handleEvent(const SDL_Event& event, ActionState& state);

    // Releases all held input, for example when the window loses focus.
    void reset(ActionState& state);

    std::vector<SDL_Scancode> keysFor(Action action) const;
    std::vector<SDL_GamepadButton> buttonsFor(Action action) const;
    // Short names for the menu, such as "Up, W" or "A".
    std::string keyNames(Action action) const;
    std::string buttonNames(Action action) const;

    // Makes a key or button the only one for an action, and takes it away
    // from any other action. Writes all controls into the settings.
    void bindKey(Settings& settings, Action action, SDL_Scancode key) const;
    void bindButton(Settings& settings, Action action, SDL_GamepadButton button) const;

private:
    // An analog stick acts like a d-pad: each axis is -1, 0 or +1.
    struct StickState {
        int x = 0;
        int y = 0;
    };

    void updateStickAxis(int& current, int next, Action negative, Action positive, ActionState& state);
    void writeAll(Settings& settings) const;

    std::unordered_map<int, Action> keys_;    // by scancode
    std::unordered_map<int, Action> buttons_; // by gamepad button
    std::unordered_map<SDL_JoystickID, StickState> sticks_;
};

} // namespace das::app
