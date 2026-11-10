// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests against the player's original game files. They run only when the
// environment variable DAS_GAMEDATA_DIR points to a folder that contains the
// games (directly or one folder deep); otherwise they are skipped. Game files
// are never part of this repository.
#include "test.hpp"

#include "das/data/game_data.hpp"
#include "das/data/game_files.hpp"
#include "das/data/palette.hpp"

#include <cstdlib>
#include <cstdio>

using namespace das::data;

namespace {

std::vector<GameFiles> installedGames() {
    const char* dir = std::getenv("DAS_GAMEDATA_DIR");
    if (!dir || !*dir) {
        return {};
    }
    return findGameFiles(dir);
}

} // namespace

DAS_TEST("original data files round-trip byte for byte") {
    const auto games = installedGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        const Bytes original = readFile(game.dataFile);
        const Container container = readContainer(original);
        DAS_CHECK(container.layout == game.game);
        DAS_CHECK(writeContainer(container) == original);

        const GameData data = parseGameData(original);
        DAS_CHECK(writeGameData(data) == original);
        std::size_t hotspots = 0;
        std::size_t actions = 0;
        std::size_t monsters = 0;
        for (const auto& zone : data.zones) {
            hotspots += zone.hotspots ? zone.hotspots->size() : 0;
            actions += zone.actions ? zone.actions->size() : 0;
            monsters += zone.monsters.size();
        }
        std::printf("       %s: %zu tiles, %zu zones, %zu hotspots, %zu actions, %zu placed characters,\n"
                    "       %zu puzzles, %zu characters, %zu sounds, %zu tile names\n",
                    std::string(layoutName(data.layout())).c_str(), data.tiles.size(), data.zones.size(), hotspots,
                    actions, monsters, data.puzzles.size(), data.characters.size(), data.sounds.size(),
                    data.tileNames.size());
    }
}

DAS_TEST("zones only reference existing tiles") {
    const auto games = installedGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        const GameData data = parseGameData(readFile(game.dataFile));
        DAS_CHECK(!data.zones.empty());
        std::size_t bad = 0;
        for (const auto& zone : data.zones) {
            for (const auto& cell : zone.cells) {
                for (const auto tile : cell.layers) {
                    bad += tile != kNoTile && tile >= data.tiles.size() ? 1 : 0;
                }
            }
        }
        DAS_CHECK(bad == 0);
    }
}

DAS_TEST("palette is found in the original executables") {
    const auto games = installedGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        if (game.executable.empty()) {
            continue;
        }
        DAS_CHECK(findPalette(readFile(game.executable)).has_value());
    }
}
