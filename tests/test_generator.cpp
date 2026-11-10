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

// Every site is reachable from the town over neighbouring cells.
bool connected(const world::Map& map) {
    int total = 0;
    int sx = -1;
    int sy = -1;
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            if (map.at(x, y).kind != world::SiteKind::None) {
                ++total;
                if (map.at(x, y).kind == world::SiteKind::Town || sx < 0) {
                    sx = x;
                    sy = y;
                }
            }
        }
    }
    std::set<std::pair<int, int>> seen;
    std::vector<std::pair<int, int>> todo{{sx, sy}};
    while (!todo.empty()) {
        const auto [x, y] = todo.back();
        todo.pop_back();
        if (!map.inside(x, y) || map.at(x, y).kind == world::SiteKind::None || !seen.insert({x, y}).second) {
            continue;
        }
        todo.insert(todo.end(), {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}});
    }
    return static_cast<int>(seen.size()) == total;
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
        const auto puzzles = data::parsePuzzles(game);
        std::set<int> goalsSeen;
        for (std::uint32_t seed = 1; seed <= 60; ++seed) {
            world::GeneratorOptions options;
            options.seed = seed;
            options.size = static_cast<world::WorldSize>(seed % 3);
            const auto map = world::generateWorld(game, options);
            goalsSeen.insert(map.goalPuzzle);
            DAS_CHECK(puzzles.at(static_cast<std::size_t>(map.goalPuzzle)).kind() == data::PuzzleType::Goal);
            DAS_CHECK(connected(map.world));
            DAS_CHECK(map.startZone >= 0);

            // Every required item is handed out somewhere, and each item is
            // handed out once.
            std::multiset<std::uint16_t> found;
            std::set<std::uint16_t> needed;
            std::set<int> zones;
            int puzzleSites = 0;
            for (const auto& s : map.world.sites) {
                for (const int z : s.zones) {
                    DAS_CHECK(zones.insert(z).second); // no zone twice
                }
                if (s.findItem != data::kNoTile) {
                    found.insert(s.findItem);
                    DAS_CHECK(s.offerPuzzle >= 0);
                    DAS_CHECK(puzzles[static_cast<std::size_t>(s.offerPuzzle)].item1 == s.findItem);
                }
                for (const auto item : {s.requiredItem, s.requiredItem2}) {
                    if (item != data::kNoTile) {
                        needed.insert(item);
                    }
                }
                if (s.kind == world::SiteKind::Puzzle) {
                    ++puzzleSites;
                    const auto& z = game.zones.at(static_cast<std::size_t>(s.zone));
                    DAS_CHECK(z.type == 15 || z.type == 16 || z.type == 17);
                }
            }
            DAS_CHECK(puzzleSites == map.puzzleCount);
            DAS_CHECK(map.puzzleCount >= 3);
            for (const auto item : needed) {
                DAS_CHECK(found.count(item) == 1);
            }
            DAS_CHECK(found.size() == needed.size());
        }
        std::printf("       %s: %zu different goals in 60 worlds\n",
                    std::string(data::layoutName(game.layout())).c_str(), goalsSeen.size());
        DAS_CHECK(goalsSeen.size() >= 5);
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
