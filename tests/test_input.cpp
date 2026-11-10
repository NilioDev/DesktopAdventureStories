// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "das/input.hpp"

using das::Action;
using das::ActionState;
using das::MoveRepeater;
using das::Step;

DAS_TEST("an action is pressed only in the frame it goes down") {
    ActionState state;
    state.press(Action::Use);
    DAS_CHECK(state.isDown(Action::Use));
    DAS_CHECK(state.wasPressed(Action::Use));
    state.endFrame();
    DAS_CHECK(state.isDown(Action::Use));
    DAS_CHECK(!state.wasPressed(Action::Use));
}

DAS_TEST("an action stays down while any device still holds it") {
    ActionState state;
    state.press(Action::MoveUp); // d-pad
    state.press(Action::MoveUp); // left stick
    state.release(Action::MoveUp);
    DAS_CHECK(state.isDown(Action::MoveUp));
    state.release(Action::MoveUp);
    DAS_CHECK(!state.isDown(Action::MoveUp));
    state.release(Action::MoveUp); // extra release is harmless
    DAS_CHECK(!state.isDown(Action::MoveUp));
}

DAS_TEST("clear releases everything") {
    ActionState state;
    state.press(Action::Attack);
    state.clear();
    DAS_CHECK(!state.isDown(Action::Attack));
    DAS_CHECK(!state.wasPressed(Action::Attack));
}

DAS_TEST("held direction prefers vertical and cancels opposites") {
    ActionState state;
    DAS_CHECK(!das::heldDirection(state));

    state.press(Action::MoveLeft);
    DAS_CHECK(das::heldDirection(state) == Step{-1, 0});

    state.press(Action::MoveDown);
    DAS_CHECK(das::heldDirection(state) == Step{0, 1});

    state.press(Action::MoveUp);
    DAS_CHECK(das::heldDirection(state) == Step{-1, 0});

    state.press(Action::MoveRight);
    DAS_CHECK(!das::heldDirection(state));
}

DAS_TEST("move repeater steps at once, then after the delay, then per interval") {
    MoveRepeater repeater(200, 100);
    const Step right{1, 0};

    DAS_CHECK(repeater.update(right, 16) == right);
    DAS_CHECK(!repeater.update(right, 150));
    DAS_CHECK(repeater.update(right, 60) == right);
    DAS_CHECK(!repeater.update(right, 50));
    DAS_CHECK(repeater.update(right, 60) == right);
}

DAS_TEST("move repeater restarts on release and on a new direction") {
    MoveRepeater repeater(200, 100);
    const Step up{0, -1};
    const Step left{-1, 0};

    DAS_CHECK(repeater.update(up, 16) == up);
    DAS_CHECK(!repeater.update(std::nullopt, 16));
    DAS_CHECK(repeater.update(up, 16) == up);
    DAS_CHECK(repeater.update(left, 16) == left);
}

DAS_TEST("move repeater takes at most one step per update") {
    MoveRepeater repeater(200, 100);
    const Step down{0, 1};
    DAS_CHECK(repeater.update(down, 16) == down);
    DAS_CHECK(repeater.update(down, 5000) == down);
    DAS_CHECK(!repeater.update(down, 16));
}

DAS_TEST("every action has a name") {
    for (std::size_t i = 0; i < das::kActionCount; ++i) {
        DAS_CHECK(das::actionName(static_cast<Action>(i)) != "unknown");
    }
}
