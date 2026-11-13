// SPDX-License-Identifier: GPL-3.0-or-later
//
// World generator tests. They need the original game files (see
// test_game_files.cpp) and are skipped without them.
#include "test.hpp"

#include "das/data/game_data.hpp"
#include "das/data/game_files.hpp"
#include "das/data/puzzle.hpp"
#include "das/world/generator.hpp"
#include "das/world/world.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>

using namespace das;

namespace {

std::vector<data::GameData> loadGames() {
    const char* dir = std::getenv("DAS_GAMEDATA_DIR");
    if (!dir || !*dir) {
        return {};
    }
    std::vector<data::GameData> out;
    for (const auto& game : data::findGameFiles(dir)) {
        out.push_back(data::parseGameData(data::readFile(game.dataFile)));
    }
    return out;
}


} // namespace

DAS_TEST("puzzle entries round-trip") {
    const auto games = loadGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        int goals = 0;
        for (const auto& entry : game.puzzles) {
            const auto p = data::parsePuzzle(entry.body, game.layout());
            DAS_CHECK(data::writePuzzle(p, game.layout()) == entry.body);
            goals += p.kind() == data::PuzzleType::Goal ? 1 : 0;
        }
        DAS_CHECK(goals == 15);
    }
}

DAS_TEST("the generator chains puzzles to the goal") {
    const auto games = loadGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        const bool yoda = game.layout() == data::Layout::Yoda;
        const auto puzzles = data::parsePuzzles(game);
        std::set<int> goalsSeen;
        for (std::uint32_t seed = 1; seed <= 60; ++seed) {
            world::GeneratorOptions options;
            options.seed = seed;
            options.size = static_cast<world::WorldSize>(seed % 3);
            const auto map = world::generateWorld(game, options);
            goalsSeen.insert(map.goalPuzzle);
            DAS_CHECK(puzzles.at(static_cast<std::size_t>(map.goalPuzzle)).kind() == data::PuzzleType::Goal);
            DAS_CHECK(map.startZone >= 0);
            // Both games leave islands that only a vehicle reaches, so the
            // map need not be connected.

            // Every required item is handed out somewhere, and each item is
            // handed out once.
            std::multiset<std::uint16_t> found;
            std::set<std::uint16_t> needed;
            std::set<int> zones;
            int puzzleSites = 0;
            for (const auto* m : {&map.world, &map.dagobah}) {
                for (const auto& s : m->sites) {
                    if (s.zone < 0) {
                        continue;
                    }
                    DAS_CHECK(zones.insert(s.zone).second); // no zone twice
                    for (const auto item : {s.findItem, s.findItem2}) {
                        if (item != data::kNoTile) {
                            found.insert(item);
                        }
                    }
                    for (const auto item : {s.requiredItem, s.requiredItem2}) {
                        if (item != data::kNoTile) {
                            needed.insert(item);
                        }
                    }
                    puzzleSites += m == &map.world && (s.role == 10 || s.role == 15 || s.role == 16) ? 1 : 0;
                    // The puzzle counter the player sees counts exactly these.
                    if (m == &map.world) {
                        DAS_CHECK(world::isChainPuzzle(s) == (s.role == 10 || s.role == 15 || s.role == 16));
                    }
                    if (s.role >= 2 && s.role <= 18 && m == &map.world) {
                        const auto& z = game.zones.at(static_cast<std::size_t>(s.zone));
                        DAS_CHECK(z.type == s.role || (s.role == 17 && z.type == 18));
                    }
                }
            }
            // Indy: the mission giver in the town hands out the first item.
            DAS_CHECK(yoda == (map.startItem == data::kNoTile));
            if (map.startItem != data::kNoTile) {
                found.insert(map.startItem);
            }
            DAS_CHECK(puzzleSites == map.puzzleCount + 1);
            DAS_CHECK(map.puzzleCount >= (yoda ? 3 : 1)); // a small Indy world can have a single puzzle
            for (const auto item : needed) {
                DAS_CHECK(found.count(item) == 1);
            }
        }
        std::printf("       %s: %zu different goals in 60 worlds\n",
                    std::string(data::layoutName(game.layout())).c_str(), goalsSeen.size());
        DAS_CHECK(goalsSeen.size() >= 5);
    }
}

DAS_TEST("a Yoda Stories seed always gives the same world") {
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Yoda) {
            continue;
        }
        world::GeneratorOptions options;
        options.seed = 7;
        options.planet = 1;
        options.size = world::WorldSize::Medium;
        const auto a = world::generateWorld(game, options);
        const auto b = world::generateWorld(game, options);
        std::string zones;
        for (const auto& s : a.world.sites) {
            zones += std::to_string(s.zone) + " ";
        }
        std::string again;
        for (const auto& s : b.world.sites) {
            again += std::to_string(s.zone) + " ";
        }
        DAS_CHECK(zones == again);
        // Pinned so that a change to the port shows up; see
        // docs/world-generator.md.
        DAS_CHECK(a.goalPuzzle == 85);
        DAS_CHECK(a.world.at(4, 5).zone == 2); // the town
        DAS_CHECK(a.world.at(6, 9).zone == 277); // the goal zone
        DAS_CHECK(a.world.at(4, 0).zone == 83);
        DAS_CHECK(a.world.at(0, 9).zone == 106);
    }
}

DAS_TEST("an Indiana Jones seed always gives the same world") {
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Indy) {
            continue;
        }
        world::GeneratorOptions options;
        options.seed = 7;
        options.size = world::WorldSize::Medium;
        const auto a = world::generateWorld(game, options);
        const auto b = world::generateWorld(game, options);
        for (std::size_t i = 0; i < a.world.sites.size(); ++i) {
            DAS_CHECK(a.world.sites[i].zone == b.world.sites[i].zone);
        }
        // Pinned so that a change to the port shows up; see
        // docs/world-generator.md.
        DAS_CHECK(a.goalPuzzle == 125);
        DAS_CHECK(a.world.at(4, 5).zone == 120); // the town
        DAS_CHECK(a.world.at(7, 1).zone == 290); // the goal zone
        DAS_CHECK(a.world.at(0, 0).zone == 195);
        DAS_CHECK(a.world.at(9, 8).zone == 201);
    }
}

DAS_TEST("a generated world trades with its characters and walks from zone to zone") {
    const auto games = loadGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        int trades = 0;
        int edges = 0;
        for (std::uint32_t seed = 1; seed <= 20; ++seed) {
            world::GeneratorOptions options;
            options.seed = seed;
            const auto map = world::generateWorld(game, options);
            world::World w(game);
            w.setMap(map);
            w.begin();
            DAS_CHECK(w.zoneIndex() == map.startZone);

            for (int y = 0; y < map.world.height; ++y) {
                for (int x = 0; x < map.world.width; ++x) {
                    const auto& s = map.world.at(x, y);
                    // Walking east off the zone leads into the next one.
                    if (s.zone >= 0 && map.world.inside(x + 1, y) && map.world.at(x + 1, y).zone >= 0) {
                        const auto& z = game.zones[static_cast<std::size_t>(s.zone)];
                        for (int row = 0; row < z.height; ++row) {
                            w.enter(s.zone, {z.width - 1, row});
                            (void)w.takeEffects();
                            if (w.paused() || w.zoneIndex() != s.zone || w.player().x != z.width - 1 ||
                                !w.walkable(w.player())) {
                                continue;
                            }
                            if (w.move({1, 0}) == world::MoveResult::ChangedZone &&
                                w.zoneIndex() == map.world.at(x + 1, y).zone) {
                                ++edges;
                                break;
                            }
                        }
                    }
                    if (s.npc == data::kNoTile || s.requiredItem == data::kNoTile) {
                        continue;
                    }
                    const auto& z = game.zones[static_cast<std::size_t>(s.npcZone)];
                    const auto& spot = (*z.hotspots)[static_cast<std::size_t>(s.npcSpot)];
                    w.enter(s.npcZone, {0, 0});
                    while (w.paused()) {
                        (void)w.takeEffects();
                        w.resume();
                    }
                    (void)w.takeEffects();
                    const world::Position at{spot.x, spot.y};
                    if (w.tile(at, 1) != s.npc) {
                        continue; // a script took the character away
                    }
                    DAS_CHECK(w.goal(s.npcZone).findItem == s.findItem);
                    w.addItem(s.requiredItem);
                    if (w.useItem(s.requiredItem, at)) {
                        bool gotIt = false;
                        for (const auto& e : w.takeEffects()) {
                            gotIt = gotIt || (e.kind == world::Effect::Kind::ItemFound && e.value == s.findItem);
                        }
                        DAS_CHECK(gotIt);
                        DAS_CHECK(w.hasItem(s.findItem));
                        DAS_CHECK(!w.hasItem(s.requiredItem));
                        DAS_CHECK(w.goal(s.zone).solved);
                        ++trades;
                    }
                    while (w.paused()) {
                        (void)w.takeEffects();
                        w.resume();
                    }
                }
            }
        }
        std::printf("       %s: %d trades, %d zone edges crossed in 20 worlds\n",
                    std::string(data::layoutName(game.layout())).c_str(), trades, edges);
        DAS_CHECK(trades > 10);
        DAS_CHECK(edges > 100);
    }
}

DAS_TEST("Indy worlds match the original program seed for seed") {
    // Zone of every map cell, row by row, as DESKADV.EXE made them for this
    // seed, size and history (read back from its save and settings files).
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Indy) {
            continue;
        }
        struct Case {
            std::uint32_t seed;
            world::WorldSize size;
            std::vector<int> goals, zones, expected;
        };
        const std::vector<Case> cases = {
            {0x2fac76f7u,
             world::WorldSize::Medium,
             {134, 105, 56, 15},
             {13, 17, 259, 196, 195, 204, 96, 212, 153, 60, 344, 345, 105, 150, 119},
             {10, 43, 32, 201, -1, -1, 329, 171, 38, 167, -1, -1, -1, -1, -1, -1, 71, 31, 21, 125,
              -1, -1, -1, -1, -1, 143, 44, -1, 6, -1, 146, 327, 185, 42, 346, 47, 106, -1, -1, -1,
              350, 89, -1, 24, 67, 16, 139, -1, -1, 74, -1, -1, -1, 20, 120, 334, 1, 200, -1, 41,
              -1, -1, -1, 137, 260, 54, 98, 73, -1, 356, -1, -1, -1, 75, 116, -1, -1, -1, -1, 102,
              14, 152, 50, 77, 68, -1, -1, -1, -1, -1, -1, 164, -1, 357, 252, -1, -1, -1, -1, -1}},
            {0x73cee779u,
             world::WorldSize::Small,
             {134, 105, 56, 15, 106, 49, 16, 133},
             {299, 300, 13, 17, 256, 157, 84, 195, 48, 83, 344, 322, 69, 105, 106, 174},
             {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
              -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 31, 91, -1, -1, -1, -1,
              49, 352, 73, 320, 361, 356, 77, 176, -1, -1, -1, -1, -1, -1, 33, 120, 35, -1, -1, -1,
              -1, -1, -1, -1, 145, -1, 23, 169, -1, -1, -1, -1, -1, -1, -1, -1, 290, -1, -1, -1,
              -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 74, 53, 36, 126, -1, -1, -1, -1, -1, -1}},
        };
        for (const auto& c : cases) {
            world::GeneratorOptions options;
            options.seed = c.seed;
            options.size = c.size;
            options.recentGoals = c.goals;
            options.recentZones = c.zones;
            const auto map = world::generateWorld(game, options);
            DAS_CHECK(map.seed == c.seed);
            for (int i = 0; i < 100; ++i) {
                DAS_CHECK(map.world.sites[static_cast<std::size_t>(i)].zone == c.expected[static_cast<std::size_t>(i)]);
            }
        }
    }
}

DAS_TEST("Indy's mission giver briefs the hero and hands over the first item once") {
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Indy) {
            continue;
        }
        world::GeneratorOptions options;
        options.seed = 7;
        const auto map = world::generateWorld(game, options);
        DAS_CHECK(map.startItem != data::kNoTile);
        world::World w(game);
        w.setMap(map);
        w.begin();
        DAS_CHECK(w.inventory().empty()); // the original starts with nothing
        // Marcus sits at his desk in a room of the town; walk into him from
        // the cell below.
        int room = -1;
        world::Position desk{};
        for (const int z : world::zoneCluster(game, map.startZone)) {
            const auto& zone = game.zones[static_cast<std::size_t>(z)];
            for (int y = 0; y < zone.height; ++y) {
                for (int x = 0; x < zone.width; ++x) {
                    if (zone.at(x, y).layers[1] == 794) {
                        room = z;
                        desk = {x, y};
                    }
                }
            }
        }
        DAS_CHECK(room >= 0);
        if (room < 0) {
            continue;
        }
        const auto talk = [&] {
            w.enter(room, {desk.x, desk.y + 1});
            while (w.paused()) {
                (void)w.takeEffects();
                w.resume();
            }
            (void)w.takeEffects();
            (void)w.move({0, -1});
            bool spoke = false;
            int items = 0;
            for (const auto& e : w.takeEffects()) {
                spoke = spoke || e.kind == world::Effect::Kind::Speech;
                items += e.kind == world::Effect::Kind::ItemDrop && e.value == map.startItem ? 1 : 0;
            }
            while (w.paused()) {
                (void)w.takeEffects();
                w.resume();
            }
            // It blinks on the desk until the player takes it.
            if (items > 0) {
                DAS_CHECK(w.drop() && w.drop()->at == desk);
                DAS_CHECK(!w.hasItem(map.startItem));
                w.pickUp();
            }
            DAS_CHECK(spoke);
            return items;
        };
        DAS_CHECK(talk() == 1);
        DAS_CHECK(w.hasItem(map.startItem));
        DAS_CHECK(talk() == 0);
    }
}

DAS_TEST("a trading character only stands on a free cell, like the originals") {
    // Rooms that draw their trader themselves keep the spawn spot in a wall
    // or under a table (Yoda zones 150, 247, 368, ...). The original puts
    // the character only on a cell with an empty object layer.
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Yoda) {
            continue;
        }
        int occupied = 0;
        for (std::uint32_t seed = 1; seed <= 40; ++seed) {
            world::GeneratorOptions options;
            options.seed = seed;
            const auto map = world::generateWorld(game, options);
            for (const auto& s : map.world.sites) {
                if (s.npc == data::kNoTile || s.npcZone < 0) {
                    continue;
                }
                const auto& z = game.zones[static_cast<std::size_t>(s.npcZone)];
                const auto& spot = (*z.hotspots)[static_cast<std::size_t>(s.npcSpot)];
                const auto drawn = z.cells[static_cast<std::size_t>(spot.y * z.width + spot.x)].layers[1];
                if (drawn == data::kNoTile || drawn == s.npc) {
                    continue;
                }
                ++occupied;
                world::World w(game);
                w.setMap(map);
                w.begin();
                w.enter(s.npcZone, {0, 0});
                while (w.paused()) {
                    (void)w.takeEffects();
                    w.resume();
                }
                DAS_CHECK(w.tile({spot.x, spot.y}, 1) != s.npc);
            }
        }
        DAS_CHECK(occupied > 0);
    }
}

DAS_TEST("Yoda on Dagobah hands over the first item of the second chain") {
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Yoda) {
            continue;
        }
        int handed = 0;
        int helpers = 0;
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            world::GeneratorOptions options;
            options.seed = seed;
            const auto map = world::generateWorld(game, options);
            for (const auto& s : map.dagobah.sites) {
                if (s.npc == data::kNoTile || s.findItem == data::kNoTile) {
                    continue;
                }
                ++helpers;
                world::World w(game);
                w.setMap(map);
                w.begin();
                const auto& z = game.zones[static_cast<std::size_t>(s.npcZone)];
                const auto& spot = (*z.hotspots)[static_cast<std::size_t>(s.npcSpot)];
                // Walk into the helper from a free neighbouring cell.
                for (const das::Step step : {das::Step{0, -1}, das::Step{0, 1}, das::Step{1, 0}, das::Step{-1, 0}}) {
                    const world::Position from{spot.x - step.dx, spot.y - step.dy};
                    w.enter(s.npcZone, from);
                    while (w.paused()) {
                        (void)w.takeEffects();
                        w.resume();
                    }
                    (void)w.takeEffects();
                    if (!w.inside(from) || !w.walkable(from) || !(w.player() == from)) {
                        continue;
                    }
                    (void)w.move(step);
                    while (w.paused()) {
                        (void)w.takeEffects();
                        w.resume();
                    }
                    if (w.hasItem(s.findItem)) {
                        ++handed;
                        break;
                    }
                }
            }
        }
        std::printf("       Yoda handed over the item in %d of %d worlds\n", handed, helpers);
        DAS_CHECK(helpers > 0);
        DAS_CHECK(handed == helpers);
    }
}

DAS_TEST("after the landing on Dagobah the hero can walk away from the ship") {
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Yoda) {
            continue;
        }
        for (std::uint32_t seed = 1; seed <= 5; ++seed) {
            world::GeneratorOptions options;
            options.seed = seed;
            world::World w(game);
            w.setMap(world::generateWorld(game, options));
            w.begin();
            // Play the landing and what is said after it. Let a few game
            // ticks pass too: Yoda Stories looks at the hero's cell on each.
            for (int guard = 0; guard < 256; ++guard) {
                (void)w.takeEffects();
                if (w.paused()) {
                    w.resume();
                } else if (guard < 32) {
                    w.update(100);
                } else {
                    break;
                }
            }
            const world::Position start = w.player();
            bool moved = false;
            for (const das::Step step : {das::Step{0, -1}, das::Step{0, 1}, das::Step{1, 0}, das::Step{-1, 0}}) {
                if (w.move(step) == world::MoveResult::Moved) {
                    moved = true;
                    break;
                }
            }
            DAS_CHECK(moved);
            DAS_CHECK(!(w.player() == start));
        }
    }
}

DAS_TEST("standing on a ship or vehicle spot travels, like the originals") {
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Yoda) {
            continue;
        }
        const auto settle = [](world::World& w) {
            for (int i = 0; i < 64; ++i) {
                (void)w.takeEffects();
                if (w.paused()) {
                    w.resume();
                } else {
                    w.update(100);
                }
            }
        };
        world::GeneratorOptions options;
        options.seed = 4023434935u; // planet 2, town 151, a vehicle from 465 to 466
        const auto map = world::generateWorld(game, options);
        {
            // On Dagobah, bumping into the X-wing plays the take-off, which
            // ends with the hero on the ship's spot; the next tick flies.
            world::World w(game);
            w.setMap(map);
            w.begin();
            settle(w);
            (void)w.move({0, -1});
            settle(w);
            (void)w.move({0, -1});
            settle(w);
            DAS_CHECK(w.zoneIndex() == 151);
        }
        {
            // The key starts the vehicle; its script puts the hero on the
            // vehicle's spot, and the next tick takes him to the island.
            world::World w(game);
            w.setMap(map);
            w.begin();
            settle(w);
            w.enter(465, {10, 8});
            settle(w);
            w.addItem(1240);
            DAS_CHECK(w.useItem(1240, {10, 7}));
            settle(w);
            DAS_CHECK(w.zoneIndex() == 466);
        }
    }
}

DAS_TEST("yoda: the next planet follows the original's rule") {
    using world::nextPlanet;
    for (const bool coin : {false, true}) {
        for (int last = 1; last <= 3; ++last) {
            DAS_CHECK(nextPlanet(last, 0, coin) != last);
        }
    }
    DAS_CHECK(nextPlanet(1, 0, false) == 3);
    DAS_CHECK(nextPlanet(1, 0, true) == 2);
    DAS_CHECK(nextPlanet(2, 3, false) == 3);
    DAS_CHECK(nextPlanet(2, 3, true) == 1);
    DAS_CHECK(nextPlanet(3, 1, false) == 1);
    DAS_CHECK(nextPlanet(3, 1, true) == 2);
    // After 5, 10 and 15 won games the snow planet is left out.
    for (const int won : {5, 10, 15}) {
        for (const bool coin : {false, true}) {
            DAS_CHECK(nextPlanet(1, won, coin) == 3);
            DAS_CHECK(nextPlanet(3, won, coin) == 1);
        }
        DAS_CHECK(nextPlanet(2, won, false) == 3);
        DAS_CHECK(nextPlanet(2, won, true) == 1);
    }
}

DAS_TEST("indy: a game starts in front of the mission giver's house") {
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Indy) {
            continue;
        }
        world::GeneratorOptions options;
        options.seed = 12345;
        world::World w(game);
        w.setMap(world::generateWorld(game, options));
        w.begin();
        DAS_CHECK(w.zoneIndex() == 120);
        // Below the door at (11,4), to the office with the mission giver.
        DAS_CHECK(w.player() == (world::Position{11, 5}));
    }
}
