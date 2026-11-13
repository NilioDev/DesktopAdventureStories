// SPDX-License-Identifier: GPL-3.0-or-later
//
// Platform-independent player input.
//
// The platform layer translates keyboard, mouse, touch and gamepad events into
// abstract actions; the engine only ever sees actions. That keeps controls
// rebindable and makes every input device a first-class citizen.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace das {

enum class Action : std::uint8_t {
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Use,           // talk, pick up, use the selected item
    Attack,        // use the equipped weapon
    InventoryPrev,
    InventoryNext,
    Map,           // open the world map / locator
    Menu,          // pause menu
    Push,          // held while walking into a block to push it (Shift in the originals)
    Count,
};

inline constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::Count);

std::string_view actionName(Action action);

// Which actions are held, and which changed this frame.
//
// Several devices can hold the same action at once (for example the d-pad and
// the left stick both pushing up), so every action keeps a counter of sources.
class ActionState {
public:
    // Marks one input source as holding or releasing an action.
    void press(Action action);
    void release(Action action);

    bool isDown(Action action) const;
    // True only in the frame in which the action went from up to down.
    bool wasPressed(Action action) const;

    // Call once at the end of every frame.
    void endFrame();

    // Releases everything, for example when the window loses focus.
    void clear();

private:
    static std::size_t index(Action action) { return static_cast<std::size_t>(action); }

    std::array<std::uint8_t, kActionCount> holders_{};
    std::array<bool, kActionCount> pressedThisFrame_{};
};

struct Step {
    int dx = 0;
    int dy = 0;
    friend bool operator==(const Step&, const Step&) = default;
};

// The direction the held movement actions point to, or nothing when no
// direction (or two opposite ones) is held. Diagonals are not allowed in the
// original games, so vertical input wins over horizontal input.
std::optional<Step> heldDirection(const ActionState& state);

// Turns a held direction into tile steps: one step immediately, then after an
// initial delay one step per repeat interval for as long as it is held.
class MoveRepeater {
public:
    MoveRepeater(std::uint32_t initialDelayMs = 220, std::uint32_t repeatMs = 140)
        : initialDelayMs_(initialDelayMs), repeatMs_(repeatMs) {}

    std::optional<Step> update(std::optional<Step> held, std::uint32_t elapsedMs);

private:
    std::uint32_t initialDelayMs_;
    std::uint32_t repeatMs_;
    std::optional<Step> current_;
    std::uint32_t untilNextMs_ = 0;
};

} // namespace das
