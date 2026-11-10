// SPDX-License-Identifier: GPL-3.0-or-later
#include "input_bindings.hpp"

#include <optional>

namespace das::app {

namespace {

// Stick deflection (of 32767) needed to count as pushed, and the lower value
// it has to fall back under to count as released again. The gap stops the
// player from jittering when the stick rests near the threshold.
constexpr int kStickPress = 16000;
constexpr int kStickRelease = 11000;

std::optional<Action> keyAction(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_W: return Action::MoveUp;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_S: return Action::MoveDown;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A: return Action::MoveLeft;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D: return Action::MoveRight;
    case SDL_SCANCODE_SPACE:
    case SDL_SCANCODE_RETURN: return Action::Use;
    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:
    case SDL_SCANCODE_X: return Action::Attack;
    case SDL_SCANCODE_Q: return Action::InventoryPrev;
    case SDL_SCANCODE_E: return Action::InventoryNext;
    case SDL_SCANCODE_M:
    case SDL_SCANCODE_TAB: return Action::Map;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_AC_BACK: return Action::Menu; // AC_BACK = Android back button
    default: return std::nullopt;
    }
}

std::optional<Action> gamepadAction(Uint8 button) {
    switch (static_cast<SDL_GamepadButton>(button)) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return Action::MoveUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return Action::MoveDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return Action::MoveLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return Action::MoveRight;
    case SDL_GAMEPAD_BUTTON_SOUTH: return Action::Use;    // Xbox A, PlayStation cross
    case SDL_GAMEPAD_BUTTON_EAST: return Action::Attack;  // Xbox B, PlayStation circle
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return Action::InventoryPrev;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return Action::InventoryNext;
    case SDL_GAMEPAD_BUTTON_BACK: return Action::Map;
    case SDL_GAMEPAD_BUTTON_START: return Action::Menu;
    default: return std::nullopt;
    }
}

int stickDirection(int current, Sint16 value) {
    const int threshold = current == 0 ? kStickPress : kStickRelease;
    if (value >= threshold) {
        return 1;
    }
    if (value <= -threshold) {
        return -1;
    }
    return 0;
}

} // namespace

bool InputBindings::handleEvent(const SDL_Event& event, ActionState& state) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        if (event.key.repeat) {
            return true; // we do our own key repeat for movement
        }
        const auto action = keyAction(event.key.scancode);
        if (!action) {
            return false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN) {
            state.press(*action);
        } else {
            state.release(*action);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const auto action = gamepadAction(event.gbutton.button);
        if (!action) {
            return false;
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            state.press(*action);
        } else {
            state.release(*action);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        auto& stick = sticks_[event.gaxis.which];
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            updateStickAxis(stick.x, stickDirection(stick.x, event.gaxis.value), Action::MoveLeft,
                            Action::MoveRight, state);
            return true;
        }
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            updateStickAxis(stick.y, stickDirection(stick.y, event.gaxis.value), Action::MoveUp,
                            Action::MoveDown, state);
            return true;
        }
        return false;
    }
    case SDL_EVENT_GAMEPAD_REMOVED: {
        // Release whatever the removed pad's stick was holding.
        if (auto it = sticks_.find(event.gdevice.which); it != sticks_.end()) {
            updateStickAxis(it->second.x, 0, Action::MoveLeft, Action::MoveRight, state);
            updateStickAxis(it->second.y, 0, Action::MoveUp, Action::MoveDown, state);
            sticks_.erase(it);
        }
        return false; // the app also wants to see this event
    }
    default: return false;
    }
}

void InputBindings::updateStickAxis(int& current, int next, Action negative, Action positive,
                                    ActionState& state) {
    if (next == current) {
        return;
    }
    if (current < 0) {
        state.release(negative);
    } else if (current > 0) {
        state.release(positive);
    }
    if (next < 0) {
        state.press(negative);
    } else if (next > 0) {
        state.press(positive);
    }
    current = next;
}

void InputBindings::reset(ActionState& state) {
    sticks_.clear();
    state.clear();
}

} // namespace das::app
