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
        DAS_CHECK(a.goalPuzzle == 15);
        DAS_CHECK(a.puzzleCount == 4);
        DAS_CHECK(a.world.at(4, 5).zone == 120); // the town
        DAS_CHECK(a.world.at(5, 9).zone == 259); // the goal zone
        DAS_CHECK(a.world.at(1, 0).zone == 141);
        DAS_CHECK(a.world.at(0, 9).zone == 106);
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
                items += e.kind == world::Effect::Kind::ItemFound && e.value == map.startItem ? 1 : 0;
            }
            while (w.paused()) {
                (void)w.takeEffects();
                w.resume();
            }
            DAS_CHECK(spoke);
            return items;
        };
        DAS_CHECK(talk() == 1);
        DAS_CHECK(w.hasItem(map.startItem));
        DAS_CHECK(talk() == 0);
    }
}
