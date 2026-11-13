// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "das/data/tile_flags.hpp"
#include "das/script/opcodes.hpp"
#include "das/world/world.hpp"

using namespace das;
using script::Condition;
using script::Instruction;
using world::Effect;
using world::Position;

namespace {

constexpr std::uint16_t kFloor = 0;
constexpr std::uint16_t kWall = 1;
constexpr std::uint16_t kStatue = 2;
constexpr std::uint16_t kKey = 3;
constexpr std::uint16_t kApple = 4;
constexpr std::uint16_t kGem = 5;

data::ScriptLine line(std::uint16_t op, std::array<std::int16_t, 5> args = {}, std::string text = {}) {
    data::ScriptLine l;
    l.opcode = op;
    l.args = args;
    l.text = std::move(text);
    return l;
}

std::uint16_t c(Condition op) { return static_cast<std::uint16_t>(op); }
std::uint16_t i(Instruction op) { return static_cast<std::uint16_t>(op); }

data::Zone makeZone(int size) {
    data::Zone zone;
    zone.width = static_cast<std::uint16_t>(size);
    zone.height = static_cast<std::uint16_t>(size);
    zone.cells.resize(static_cast<std::size_t>(size * size));
    for (auto& cell : zone.cells) {
        cell.layers[0] = kFloor;
    }
    zone.hotspots.emplace();
    zone.actions.emplace();
    return zone;
}

data::GameData makeGame() {
    namespace f = data::tile_flags;
    data::GameData data; // Yoda Stories numbering
    data.tiles.resize(6);
    data.tiles[kFloor].flags = f::kFloor;
    data.tiles[kWall].flags = f::kObject;
    data.tiles[kStatue].flags = f::kObject;
    data.tiles[kKey].flags = f::kItem;
    data.tiles[kApple].flags = f::kItem;
    data.tiles[kGem].flags = f::kItem;
    data.tileNames = {{kKey, "Key"}, {kApple, "Apple"}};

    // Zone 0: 5x5 with a statue at (3,2) and a door at (2,0) to zone 1.
    data::Zone outside = makeZone(5);
    outside.cells[2 * 5 + 3].layers[1] = kStatue;
    outside.cells[0 * 5 + 2].layers[1] = kWall;
    outside.hotspots->push_back({9, 2, 0, 1, 1});
    auto& a = *outside.actions;
    // 0: greet once, in two bubbles.
    a.push_back({{line(c(Condition::FirstEnter))},
                 {line(i(Instruction::SpeakHero), {}, "Hello."),
                  line(i(Instruction::SpeakNpc), {1, 1}, "Bring me a \xa2, please.\r\nThanks."),
                  line(i(Instruction::SetCounter), {1})}});
    // 1: bumping the statue rolls a die and moves it away once.
    a.push_back({{line(c(Condition::Bump), {3, 2, kStatue})},
                 {line(i(Instruction::MoveTile), {3, 2, 1, 4, 2}), line(i(Instruction::AddToCounter), {5}),
                  line(i(Instruction::DisableAction))}});
    // 2: the right item for the statue spot.
    a.push_back({{line(c(Condition::PlaceItem), {4, 2, 1, kStatue, -1})},
                 {line(i(Instruction::RemoveItem), {-1}), line(i(Instruction::AddItem), {kGem}),
                  line(i(Instruction::MarkAsSolved))}});
    // 3: a wrong item.
    a.push_back({{line(c(Condition::PlaceItemIsNot), {4, 2, 1, kStatue, -1})},
                 {line(i(Instruction::SpeakHero), {}, "That does not fit.")}});
    // 4: standing on (0,4) wins once the zone is solved.
    a.push_back({{line(c(Condition::StandingOn), {0, 4, kFloor}), line(c(Condition::ZoneIsSolved))},
                 {line(i(Instruction::WinGame))}});

    data::Zone room = makeZone(3);
    room.hotspots->push_back({10, 1, 2, 1, 0xffff});

    // Zone 2: 3x3 with a cabinet in the wall at (1,0); standing in front of
    // it drops the key into it.
    data::Zone cabinet = makeZone(3);
    cabinet.cells[1].layers[1] = kWall;
    cabinet.actions->push_back({{line(c(Condition::StandingOn), {1, 1, kFloor})},
                                {line(i(Instruction::DropItem), {kKey, 1, 0}), line(i(Instruction::DisableAction))}});

    data.zones = {outside, room, cabinet};
    return data;
}

std::vector<Effect> drain(world::World& w, std::vector<std::string>* spoken = nullptr) {
    std::vector<Effect> all;
    for (int guard = 0; guard < 100; ++guard) {
        auto effects = w.takeEffects();
        for (const auto& e : effects) {
            if (spoken && e.kind == Effect::Kind::Speech) {
                spoken->push_back(e.text);
            }
        }
        all.insert(all.end(), effects.begin(), effects.end());
        if (!w.paused()) {
            break;
        }
        w.resume();
    }
    return all;
}

} // namespace

DAS_TEST("first enter speaks once, pausing for each bubble") {
    const auto data = makeGame();
    world::World w(data);
    w.setGoal(0, {kApple, data::kNoTile, kGem, false});
    w.start(0);

    auto effects = w.takeEffects();
    DAS_CHECK(effects.size() == 1);
    DAS_CHECK(effects[0].kind == Effect::Kind::Speech);
    DAS_CHECK(effects[0].text == "Hello.");
    DAS_CHECK(!effects[0].at.has_value());
    DAS_CHECK(w.paused());
    DAS_CHECK(w.move({1, 0}) == world::MoveResult::Blocked); // no walking while someone talks

    w.resume();
    effects = w.takeEffects();
    DAS_CHECK(effects.size() == 1);
    DAS_CHECK(effects[0].text == "Bring me an Apple, please.\nThanks.");
    DAS_CHECK((effects[0].at == Position{1, 1}));
    w.resume();
    DAS_CHECK(!w.paused());
    DAS_CHECK(w.counter() == 1);

    // Through the door and back: no greeting the second time.
    w.enter(1, {1, 1});
    w.enter(0, {2, 2});
    DAS_CHECK(w.takeEffects().empty());
    DAS_CHECK(!w.paused());
}

DAS_TEST("bumping runs a script that moves a tile and disables itself") {
    const auto data = makeGame();
    world::World w(data);
    w.start(0);
    drain(w);
    w.enter(0, {2, 2});
    DAS_CHECK(w.player() == (Position{2, 2}));
    DAS_CHECK(w.move({1, 0}) == world::MoveResult::Blocked); // bump the statue
    DAS_CHECK(w.tile({3, 2}, 1) == data::kNoTile);
    DAS_CHECK(w.tile({4, 2}, 1) == kStatue);
    DAS_CHECK(w.counter() == 6);
    DAS_CHECK(w.actionDisabled(1));
}

DAS_TEST("using items runs the matching scripts") {
    const auto data = makeGame();
    world::World w(data);
    w.setGoal(0, {kApple, data::kNoTile, kGem, false});
    w.start(0);
    drain(w);
    w.enter(0, {2, 2});
    w.move({1, 0});
    w.addItem(kKey);
    w.addItem(kApple);

    std::vector<std::string> spoken;
    DAS_CHECK(w.useItem(kKey, {4, 2}));
    drain(w, &spoken);
    DAS_CHECK(spoken == std::vector<std::string>{"That does not fit."});
    DAS_CHECK(w.hasItem(kKey));

    DAS_CHECK(w.useItem(kApple, {4, 2}));
    DAS_CHECK(!w.hasItem(kApple));
    DAS_CHECK(w.hasItem(kGem));
    DAS_CHECK(w.goal(0).solved);
    DAS_CHECK(!w.useItem(kKey, {0, 0}));

    // Walk to (0,4): solved, so the game is won.
    w.enter(0, {0, 3});
    drain(w);
    w.move({0, 1});
    DAS_CHECK(w.outcome() == world::Outcome::Won);
}

DAS_TEST("indy opcodes translate both ways") {
    for (std::uint16_t op = 0; op < 23; ++op) {
        const auto cond = script::condition(data::Layout::Indy, op);
        if (cond != Condition::Unknown) {
            DAS_CHECK(script::opcode(data::Layout::Indy, cond) == op);
        }
    }
    for (std::uint16_t op = 0; op < 36; ++op) {
        const auto ins = script::instruction(data::Layout::Indy, op);
        DAS_CHECK(ins != Instruction::Unknown);
        if (ins != Instruction::Nothing) {
            DAS_CHECK(script::opcode(data::Layout::Indy, ins) == op);
        }
    }
    DAS_CHECK(script::condition(data::Layout::Indy, 2) == Condition::Bump);
    DAS_CHECK(script::instruction(data::Layout::Indy, 5) == Instruction::SpeakHero);
    DAS_CHECK(script::instruction(data::Layout::Yoda, 5) == Instruction::SpeakNpc);
    DAS_CHECK(script::condition(data::Layout::Yoda, 36) == Condition::Unknown);
    DAS_CHECK(script::name(Instruction::ChangeZone) == "ChangeZone");
    DAS_CHECK(script::name(Condition::PlaceItemIsNot) == "PlaceItemIsNot");
}

DAS_TEST("an item dropped into a cabinet is found by bumping it, and the cabinet stays") {
    const auto data = makeGame();
    world::World w(data);
    w.start(2);
    w.enter(2, {1, 2});
    drain(w);
    DAS_CHECK(w.move({0, -1}) == world::MoveResult::Moved); // onto (1,1): the key drops
    drain(w);
    DAS_CHECK(w.tile({1, 0}, 1) == kWall);
    DAS_CHECK(!w.hasItem(kKey));
    w.move({0, -1}); // bump the cabinet
    drain(w);
    DAS_CHECK(w.hasItem(kKey));
    DAS_CHECK(w.tile({1, 0}, 1) == kWall);
}

namespace {

// Yoda Stories behaviour checked against the original program; see
// docs/formats/scripts.md.
data::GameData makeFlowGame() {
    data::GameData data = makeGame();
    // Zone 3: bumping the wall at (1,0) sends the hero to zone 1, and the
    // action switches itself off after the ChangeZone. Entering counts the
    // Update passes; standing on (2,2) sets the counter to 50. The door at
    // (0,1) leads to zone 4.
    data::Zone z = makeZone(3);
    z.cells[1].layers[1] = kWall;
    z.hotspots->push_back({9, 0, 1, 1, 4});
    z.actions->push_back({{line(c(Condition::Bump), {1, 0, kWall})},
                          {line(i(Instruction::ChangeZone), {1, 1, 1}), line(i(Instruction::DisableAction)),
                           line(i(Instruction::SetSharedCounter), {7})}});
    z.actions->push_back({{line(c(Condition::Update))}, {line(i(Instruction::AddToCounter), {1})}});
    z.actions->push_back({{line(c(Condition::StandingOn), {2, 2, kFloor})}, {line(i(Instruction::SetCounter), {50})}});
    data::Zone room = makeZone(3);
    room.hotspots->push_back({10, 1, 2, 1, 0xffff});
    data.zones.push_back(z);
    data.zones.push_back(room);
    return data;
}

} // namespace

DAS_TEST("a ChangeZone lets the rest of the pass run first") {
    const auto data = makeFlowGame();
    world::World w(data);
    w.start(3);
    w.enter(3, {1, 1});
    drain(w);
    DAS_CHECK(w.move({0, -1}) == world::MoveResult::ChangedZone);
    DAS_CHECK(w.zoneIndex() == 1);
    // SetSharedCounter after the ChangeZone ran in zone 3 and went along.
    DAS_CHECK(w.sharedCounter() == 7);
    w.enter(3, {1, 1});
    drain(w);
    // DisableAction after the ChangeZone ran too: no second trip.
    DAS_CHECK(w.move({0, -1}) == world::MoveResult::Blocked);
    DAS_CHECK(w.zoneIndex() == 3);
}

DAS_TEST("Yoda zones start their counters at -1 and run Update twice on entering") {
    const auto data = makeFlowGame();
    world::World w(data);
    w.start(3);
    drain(w);
    DAS_CHECK(w.counter() == 2);
    DAS_CHECK(w.random() == -1);
    DAS_CHECK(w.sharedCounter() == -1);
}

DAS_TEST("the shared counter goes through a door into a room") {
    const auto data = makeFlowGame();
    world::World w(data);
    w.start(3);
    w.enter(3, {1, 1});
    drain(w);
    DAS_CHECK(w.move({-1, 0}) == world::MoveResult::ChangedZone); // the door at (0,1), still -1
    DAS_CHECK(w.zoneIndex() == 4);
    DAS_CHECK(w.sharedCounter() == -1);
}

DAS_TEST("a game tick notices the hero standing on a cell") {
    const auto data = makeFlowGame();
    world::World w(data);
    w.start(3);
    w.enter(3, {2, 2});
    drain(w);
    DAS_CHECK(w.counter() != 50);
    w.update(150);
    drain(w);
    DAS_CHECK(w.counter() == 50);
}
