// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/input.hpp"

namespace das {

std::string_view actionName(Action action) {
    switch (action) {
    case Action::MoveUp: return "move_up";
    case Action::MoveDown: return "move_down";
    case Action::MoveLeft: return "move_left";
    case Action::MoveRight: return "move_right";
    case Action::Use: return "use";
    case Action::Attack: return "attack";
    case Action::InventoryPrev: return "inventory_prev";
    case Action::InventoryNext: return "inventory_next";
    case Action::Map: return "map";
    case Action::Menu: return "menu";
    case Action::Count: break;
    }
    return "unknown";
}

void ActionState::press(Action action) {
    auto& holders = holders_[index(action)];
    if (holders == 0) {
        pressedThisFrame_[index(action)] = true;
    }
    if (holders < UINT8_MAX) {
        ++holders;
    }
}

void ActionState::release(Action action) {
    auto& holders = holders_[index(action)];
    if (holders > 0) {
        --holders;
    }
}

bool ActionState::isDown(Action action) const { return holders_[index(action)] > 0; }

bool ActionState::wasPressed(Action action) const { return pressedThisFrame_[index(action)]; }

void ActionState::endFrame() { pressedThisFrame_.fill(false); }

void ActionState::clear() {
    holders_.fill(0);
    pressedThisFrame_.fill(false);
}

std::optional<Step> heldDirection(const ActionState& state) {
    const int dy = (state.isDown(Action::MoveDown) ? 1 : 0) - (state.isDown(Action::MoveUp) ? 1 : 0);
    if (dy != 0) {
        return Step{0, dy};
    }
    const int dx = (state.isDown(Action::MoveRight) ? 1 : 0) - (state.isDown(Action::MoveLeft) ? 1 : 0);
    if (dx != 0) {
        return Step{dx, 0};
    }
    return std::nullopt;
}

std::optional<Step> MoveRepeater::update(std::optional<Step> held, std::uint32_t elapsedMs) {
    if (!held) {
        current_.reset();
        return std::nullopt;
    }
    if (held != current_) {
        // New direction: step at once, then wait for the initial delay.
        current_ = held;
        untilNextMs_ = initialDelayMs_;
        return held;
    }
    if (elapsedMs < untilNextMs_) {
        untilNextMs_ -= elapsedMs;
        return std::nullopt;
    }
    // At most one step per update: a long frame must not teleport the player.
    untilNextMs_ = repeatMs_;
    return held;
}

} // namespace das
