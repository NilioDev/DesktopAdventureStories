// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "das/data/tile_flags.hpp"
#include "das/util/zip.hpp"
#include "das/world/savegame.hpp"
#include "das/world/snapshot.hpp"
#include "das/world/world.hpp"

using namespace das;
using das::world::MoveResult;
using das::world::Position;

namespace {

constexpr std::uint16_t kFloor = 0;
constexpr std::uint16_t kWall = 1;
constexpr std::uint16_t kBlock = 2;

data::Zone makeZone(int size) {
    data::Zone zone;
    zone.width = static_cast<std::uint16_t>(size);
    zone.height = static_cast<std::uint16_t>(size);
    zone.cells.resize(static_cast<std::size_t>(size * size));
    for (auto& cell : zone.cells) {
        cell.layers[0] = kFloor;
    }
    zone.hotspots.emplace();
    return zone;
}

void put(data::Zone& zone, int x, int y, std::uint16_t tile) {
    zone.cells[static_cast<std::size_t>(y * zone.width + x)].layers[1] = tile;
}

data::GameData makeWorld() {
    namespace f = data::tile_flags;
    data::GameData data;
    data.tiles.resize(3);
    data.tiles[kFloor].flags = f::kFloor;
    data.tiles[kWall].flags = f::kObject;
    data.tiles[kBlock].flags = f::kObject | f::kDraggable | f::kTransparent;

    // Zone 0: 5x5 outdoors, a wall at (3,2), a block at (2,3), a door at (2,0) to zone 1.
    data::Zone outside = makeZone(5);
    put(outside, 3, 2, kWall);
    put(outside, 3, 1, kWall); // walls beside it: no way around
    put(outside, 3, 3, kWall);
    put(outside, 2, 3, kBlock);
    outside.hotspots->push_back({9, 2, 0, 1, 1});

    // Zone 1: 3x3 room with its way out at (1,2).
    data::Zone room = makeZone(3);
    room.hotspots->push_back({10, 1, 2, 1, 0xffff});

    data.zones = {outside, room};
    return data;
}

} // namespace

DAS_TEST("player walks, is stopped by walls and the zone edge") {
    const auto data = makeWorld();
    world::World w(data);
    w.enter(0, {2, 2});
    DAS_CHECK(w.move({1, 0}) == MoveResult::Blocked); // wall at (3,2)
    DAS_CHECK(w.facing() == data::Facing::Right);
    DAS_CHECK(w.player() == (Position{2, 2}));
    DAS_CHECK(w.move({-1, 0}) == MoveResult::Moved);
    DAS_CHECK(w.move({-1, 0}) == MoveResult::Moved);
    DAS_CHECK(w.player() == (Position{0, 2}));
    DAS_CHECK(w.move({-1, 0}) == MoveResult::Blocked); // edge
}

DAS_TEST("player steps around an obstacle when a cell beside it is free, like the originals") {
    const auto data = makeWorld();
    world::World w(data);
    // Up from (3,4) the wall at (3,3) is in the way; (2,3) holds the block,
    // so the hero goes up-right to (4,3).
    w.enter(0, {3, 4});
    DAS_CHECK(w.move({0, -1}) == MoveResult::Moved);
    DAS_CHECK(w.player() == (Position{4, 3}));
    DAS_CHECK(w.facing() == data::Facing::Up);
    // Left from (4,2): the wall at (3,2) with walls above and below it.
    w.enter(0, {4, 2});
    DAS_CHECK(w.move({-1, 0}) == MoveResult::Blocked);
    DAS_CHECK(w.player() == (Position{4, 2}));
    // A diagonal step into the wall keeps the part that is free.
    w.enter(0, {4, 1});
    DAS_CHECK(w.move({-1, 1}) == MoveResult::Moved);
    DAS_CHECK(w.player() == (Position{4, 2}));
}

DAS_TEST("player pushes a draggable block onto free floor only") {
    const auto data = makeWorld();
    world::World w(data);
    w.enter(0, {2, 2});
    // Like the originals, walking into it without the push control does not move it.
    DAS_CHECK(w.move({0, 1}) != MoveResult::Pushed);
    DAS_CHECK(w.tile({2, 3}, 1) == kBlock);
    w.enter(0, {2, 2});
    DAS_CHECK(w.move({0, 1}, true) == MoveResult::Pushed);
    DAS_CHECK(w.player() == (Position{2, 3}));
    DAS_CHECK(w.tile({2, 4}, 1) == kBlock);
    DAS_CHECK(w.tile({2, 3}, 1) == data::kNoTile);
    DAS_CHECK(w.move({0, 1}, true) != MoveResult::Pushed); // block is against the edge now
    DAS_CHECK(w.tile({2, 4}, 1) == kBlock);
    // The original zone data is untouched.
    DAS_CHECK(data.zones[0].cells[3 * 5 + 2].layers[1] == kBlock);
}

DAS_TEST("a door under an object stays shut until a script removes it") {
    auto data = makeWorld();
    put(data.zones[0], 2, 0, kWall); // a closed door drawn as an object
    world::World w(data);
    w.enter(0, {2, 1});
    DAS_CHECK(w.move({0, -1}) != MoveResult::ChangedZone);
    DAS_CHECK(w.zoneIndex() == 0);
}

DAS_TEST("a door to a room without a way out does not open") {
    auto data = makeWorld();
    data.zones[1].hotspots->clear();
    world::World w(data);
    w.enter(0, {2, 1});
    DAS_CHECK(w.move({0, -1}) != MoveResult::ChangedZone);
    DAS_CHECK(w.zoneIndex() == 0);
}

DAS_TEST("doors lead into a room and back out to the same door") {
    const auto data = makeWorld();
    world::World w(data);
    w.enter(0, {2, 1});
    DAS_CHECK(w.move({0, -1}) == MoveResult::ChangedZone);
    DAS_CHECK(w.zoneIndex() == 1);
    DAS_CHECK(w.player() == (Position{1, 2}));
    DAS_CHECK(w.doorDepth() == 1);
    DAS_CHECK(w.move({0, -1}) == MoveResult::Moved);
    DAS_CHECK(w.move({0, 1}) == MoveResult::ChangedZone);
    DAS_CHECK(w.zoneIndex() == 0);
    DAS_CHECK(w.player() == (Position{2, 0}));
    DAS_CHECK(w.doorDepth() == 0);
}

DAS_TEST("an item taken from where it lay at the start stays gone after loading") {
    namespace f = data::tile_flags;
    auto data = makeWorld();
    data.tiles.push_back({});
    const auto item = static_cast<std::uint16_t>(data.tiles.size() - 1);
    data.tiles[item].flags = f::kObject | f::kItem;
    data.zones[0].hotspots->push_back({6, 0, 4, 1, item}); // lies at (0,4)
    world::World w(data);
    w.enter(0, {1, 4});
    DAS_CHECK(w.tile({0, 4}, 1) == item);
    DAS_CHECK(w.move({-1, 0}) == MoveResult::Blocked); // takes it
    DAS_CHECK(w.hasItem(item));
    DAS_CHECK(w.tile({0, 4}, 1) == data::kNoTile);
    world::World back(data);
    back.loadState(w.saveState());
    back.enter(0, {1, 4});
    DAS_CHECK(back.tile({0, 4}, 1) == data::kNoTile);
}

DAS_TEST("start puts the player on the exit door or a free cell") {
    auto data = makeWorld();
    world::World w(data);
    w.start(1);
    DAS_CHECK(w.player() == (Position{1, 2}));
    put(data.zones[0], 2, 2, kWall); // middle blocked
    w.start(0);
    DAS_CHECK(w.walkable(w.player()));
}

DAS_TEST("a bug snapshot holds the report, the state and the files it was given") {
    auto data = makeWorld();
    data.tileNames.push_back({kBlock, "Blue crate"});
    world::World w(data);
    w.enter(0, {2, 2});
    w.addItem(kBlock);
    const std::string text = world::describeWorld(w, data);
    DAS_CHECK(text.find("Zone: 0 (5x5") != std::string::npos);
    DAS_CHECK(text.find("Hero: at 2,2") != std::string::npos);
    DAS_CHECK(text.find("Items: Blue crate (tile 2)") != std::string::npos);

    world::SnapshotInfo info;
    info.kind = "Stuck";
    info.note = "the block won't move";
    info.version = "0.0.1";
    info.game = "yoda";
    info.options = world::GeneratorOptions{};
    info.inputs = "1.000 Use\n";
    info.log = "a log line\n";
    info.screenshot = {1, 2, 3};
    const data::Bytes snapshot = world::writeSnapshot(&w, &data, info);
    std::vector<std::string> names;
    std::string report;
    for (const auto& e : zip::read(snapshot)) {
        names.push_back(e.name);
        if (e.name == "report.txt") {
            report.assign(e.content.begin(), e.content.end());
        }
    }
    DAS_CHECK((names == std::vector<std::string>{"report.txt", "save.json", "screenshot.png", "inputs.txt", "log.txt"}));
    DAS_CHECK(report.find("Note: the block won't move") != std::string::npos);
    DAS_CHECK(report.find("Hero: at 2,2") != std::string::npos);
    const auto save = world::snapshotSave(snapshot);
    DAS_CHECK(save.has_value());
    DAS_CHECK(world::readSaveGame(*save).game == "yoda");

    // A single zone has no save game, only its state.
    info.options.reset();
    const auto zoneOnly = zip::read(world::writeSnapshot(&w, &data, info));
    DAS_CHECK(zoneOnly.size() == 5 && zoneOnly[1].name == "state.json");

    // On the title screen there is no world: only the report.
    const data::Bytes title = world::writeSnapshot(nullptr, nullptr, world::SnapshotInfo{});
    DAS_CHECK(zip::read(title).size() == 1);
    DAS_CHECK(!world::snapshotSave(title).has_value());
}

DAS_TEST("indy: the cliff lip at either end of a ladder does not block") {
    namespace f = data::tile_flags;
    constexpr std::uint16_t kLadder = 44;
    constexpr std::uint16_t kLip = 36;
    data::GameData data;
    data.container.layout = data::Layout::Indy;
    data.tiles.resize(46);
    data.tiles[kFloor].flags = f::kFloor;
    data.tiles[kWall].flags = f::kObject;
    data.tiles[kLip].flags = f::kObject | f::kTransparent;
    data.tiles[kLadder].flags = f::kFloor;
    // A 5x5 zone: a cliff along row 2 with a ladder at (2,2) and lips on
    // (2,1) and (2,3), the cells at both ends of it.
    data::Zone zone = makeZone(5);
    for (int x = 0; x < 5; ++x) {
        put(zone, x, 2, kWall);
    }
    zone.cells[2 * 5 + 2].layers = {kLadder, data::kNoTile, data::kNoTile};
    put(zone, 2, 1, kLip);
    put(zone, 2, 3, kLip);
    put(zone, 3, 3, kLip); // not at a ladder: still blocks
    data.zones = {zone};

    world::World w(data);
    w.enter(0, {2, 4});
    DAS_CHECK(w.move({0, -1}) == MoveResult::Moved);
    DAS_CHECK(w.move({0, -1}) == MoveResult::Moved);
    DAS_CHECK(w.move({0, -1}) == MoveResult::Moved);
    DAS_CHECK(w.player() == (Position{2, 1}));
    w.enter(0, {3, 4});
    w.move({0, -1});
    DAS_CHECK(w.player() != (Position{3, 3}));

    // Yoda Stories has no such ladders.
    data.container.layout = data::Layout::Yoda;
    world::World yoda(data);
    yoda.enter(0, {2, 4});
    yoda.move({0, -1});
    DAS_CHECK(yoda.player() != (Position{2, 3}));
}
