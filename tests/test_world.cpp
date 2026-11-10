// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "das/data/tile_flags.hpp"
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
    put(outside, 2, 3, kBlock);
    put(outside, 2, 0, kWall); // the door itself is drawn as an object
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

DAS_TEST("player pushes a draggable block onto free floor only") {
    const auto data = makeWorld();
    world::World w(data);
    w.enter(0, {2, 2});
    DAS_CHECK(w.move({0, 1}) == MoveResult::Pushed);
    DAS_CHECK(w.player() == (Position{2, 3}));
    DAS_CHECK(w.tile({2, 4}, 1) == kBlock);
    DAS_CHECK(w.tile({2, 3}, 1) == data::kNoTile);
    DAS_CHECK(w.move({0, 1}) == MoveResult::Blocked); // block is against the edge now
    // The original zone data is untouched.
    DAS_CHECK(data.zones[0].cells[3 * 5 + 2].layers[1] == kBlock);
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

DAS_TEST("start puts the player on the exit door or a free cell") {
    auto data = makeWorld();
    world::World w(data);
    w.start(1);
    DAS_CHECK(w.player() == (Position{1, 2}));
    put(data.zones[0], 2, 2, kWall); // middle blocked
    w.start(0);
    DAS_CHECK(w.walkable(w.player()));
}
