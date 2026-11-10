// SPDX-License-Identifier: GPL-3.0-or-later
#include "input_bindings.hpp"

#include <algorithm>

namespace das::app {

namespace {

// Stick deflection (of 32767) needed to count as pushed, and the lower value
// it has to fall back under to count as released again. The gap stops the
// player from jittering when the stick rests near the threshold.
constexpr int kStickPress = 16000;
constexpr int kStickRelease = 11000;

std::vector<SDL_Scancode> defaultKeys(Action action) {
    switch (action) {
    case Action::MoveUp: return {SDL_SCANCODE_UP, SDL_SCANCODE_W};
    case Action::MoveDown: return {SDL_SCANCODE_DOWN, SDL_SCANCODE_S};
    case Action::MoveLeft: return {SDL_SCANCODE_LEFT, SDL_SCANCODE_A};
    case Action::MoveRight: return {SDL_SCANCODE_RIGHT, SDL_SCANCODE_D};
    case Action::Use: return {SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN};
    case Action::Attack: return {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL, SDL_SCANCODE_X};
    case Action::InventoryPrev: return {SDL_SCANCODE_Q};
    case Action::InventoryNext: return {SDL_SCANCODE_E};
    case Action::Map: return {SDL_SCANCODE_M, SDL_SCANCODE_TAB};
    case Action::Menu: return {SDL_SCANCODE_ESCAPE};
    case Action::Count: break;
    }
    return {};
}

std::vector<SDL_GamepadButton> defaultButtons(Action action) {
    switch (action) {
    case Action::MoveUp: return {SDL_GAMEPAD_BUTTON_DPAD_UP};
    case Action::MoveDown: return {SDL_GAMEPAD_BUTTON_DPAD_DOWN};
    case Action::MoveLeft: return {SDL_GAMEPAD_BUTTON_DPAD_LEFT};
    case Action::MoveRight: return {SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
    case Action::Use: return {SDL_GAMEPAD_BUTTON_SOUTH};   // Xbox A, PlayStation cross
    case Action::Attack: return {SDL_GAMEPAD_BUTTON_EAST}; // Xbox B, PlayStation circle
    case Action::InventoryPrev: return {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER};
    case Action::InventoryNext: return {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
    case Action::Map: return {SDL_GAMEPAD_BUTTON_BACK};
    case Action::Menu: return {SDL_GAMEPAD_BUTTON_START};
    case Action::Count: break;
    }
    return {};
}

Action actionAt(std::size_t i) { return static_cast<Action>(i); }

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

std::string buttonName(SDL_GamepadButton button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A";
    case SDL_GAMEPAD_BUTTON_EAST: return "B";
    case SDL_GAMEPAD_BUTTON_WEST: return "X";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3";
    case SDL_GAMEPAD_BUTTON_BACK: return "Back";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-right";
    default: {
        const char* name = SDL_GetGamepadStringForButton(button);
        return name ? name : "?";
    }
    }
}

} // namespace

void InputBindings::configure(const Settings& settings) {
    keys_.clear();
    buttons_.clear();
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const Action action = actionAt(i);
        const std::string id(actionName(action));
        if (const auto it = settings.keys.find(id); it != settings.keys.end()) {
            for (const auto& name : it->second) {
                const SDL_Scancode key = SDL_GetScancodeFromName(name.c_str());
                if (key != SDL_SCANCODE_UNKNOWN) {
                    keys_[key] = action;
                }
            }
        } else {
            for (const SDL_Scancode key : defaultKeys(action)) {
                keys_[key] = action;
            }
        }
        if (const auto it = settings.buttons.find(id); it != settings.buttons.end()) {
            for (const auto& name : it->second) {
                const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name.c_str());
                if (button != SDL_GAMEPAD_BUTTON_INVALID) {
                    buttons_[button] = action;
                }
            }
        } else {
            for (const SDL_GamepadButton button : defaultButtons(action)) {
                buttons_[button] = action;
            }
        }
    }
}

std::vector<SDL_Scancode> InputBindings::keysFor(Action action) const {
    std::vector<SDL_Scancode> out;
    for (const auto& [key, a] : keys_) {
        if (a == action) {
            out.push_back(static_cast<SDL_Scancode>(key));
        }
    }
    // Keep the order stable: the defaults' order first, then by code.
    const auto defaults = defaultKeys(action);
    std::sort(out.begin(), out.end(), [&](SDL_Scancode x, SDL_Scancode y) {
        const auto px = std::find(defaults.begin(), defaults.end(), x) - defaults.begin();
        const auto py = std::find(defaults.begin(), defaults.end(), y) - defaults.begin();
        return px != py ? px < py : x < y;
    });
    return out;
}

std::vector<SDL_GamepadButton> InputBindings::buttonsFor(Action action) const {
    std::vector<SDL_GamepadButton> out;
    for (const auto& [button, a] : buttons_) {
        if (a == action) {
            out.push_back(static_cast<SDL_GamepadButton>(button));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string InputBindings::keyNames(Action action) const {
    std::string out;
    for (const SDL_Scancode key : keysFor(action)) {
        std::string name = SDL_GetScancodeName(key);
        if (name.rfind("Left ", 0) == 0) {
            name = "L" + name.substr(5); // "Left Ctrl" -> "LCtrl"
        } else if (name.rfind("Right ", 0) == 0) {
            name = "R" + name.substr(6);
        }
        out += (out.empty() ? "" : ", ") + name;
    }
    return out.empty() ? "-" : out;
}

std::string InputBindings::buttonNames(Action action) const {
    std::string out;
    for (const SDL_GamepadButton button : buttonsFor(action)) {
        out += (out.empty() ? "" : ", ") + buttonName(button);
    }
    return out.empty() ? "-" : out;
}

void InputBindings::writeAll(Settings& settings) const {
    settings.keys.clear();
    settings.buttons.clear();
    for (std::size_t i = 0; i < kActionCount; ++i) {
        const Action action = actionAt(i);
        const std::string id(actionName(action));
        auto& keys = settings.keys[id];
        for (const SDL_Scancode key : keysFor(action)) {
            keys.emplace_back(SDL_GetScancodeName(key));
        }
        auto& buttons = settings.buttons[id];
        for (const SDL_GamepadButton button : buttonsFor(action)) {
            buttons.emplace_back(SDL_GetGamepadStringForButton(button));
        }
    }
}

void InputBindings::bindKey(Settings& settings, Action action, SDL_Scancode key) const {
    writeAll(settings);
    for (auto& [id, names] : settings.keys) {
        std::erase(names, std::string(SDL_GetScancodeName(key)));
    }
    settings.keys[std::string(actionName(action))] = {SDL_GetScancodeName(key)};
}

void InputBindings::bindButton(Settings& settings, Action action, SDL_GamepadButton button) const {
    writeAll(settings);
    for (auto& [id, names] : settings.buttons) {
        std::erase(names, std::string(SDL_GetGamepadStringForButton(button)));
    }
    settings.buttons[std::string(actionName(action))] = {SDL_GetGamepadStringForButton(button)};
}

bool InputBindings::handleEvent(const SDL_Event& event, ActionState& state) {
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        if (event.key.repeat) {
            return true; // we do our own key repeat for movement
        }
        std::optional<Action> action;
        if (event.key.scancode == SDL_SCANCODE_AC_BACK) {
            action = Action::Menu; // the Android back button, always
        } else if (const auto it = keys_.find(event.key.scancode); it != keys_.end()) {
            action = it->second;
        }
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
        const auto it = buttons_.find(event.gbutton.button);
        if (it == buttons_.end()) {
            return false;
        }
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            state.press(it->second);
        } else {
            state.release(it->second);
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
