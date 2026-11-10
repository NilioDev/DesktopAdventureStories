// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests against the player's original game files. They run only when the
// environment variable DAS_GAMEDATA_DIR points to a folder that contains the
// games (directly or one folder deep); otherwise they are skipped. Game files
// are never part of this repository.
#include "test.hpp"

#include "das/data/character.hpp"
#include "das/data/game_data.hpp"
#include "das/data/game_files.hpp"
#include "das/data/palette.hpp"
#include "das/pack/import.hpp"
#include "das/pack/pack.hpp"
#include "das/world/world.hpp"

#include <algorithm>
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

DAS_TEST("every character entry decodes and each game has a hero") {
    const auto games = installedGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        const GameData data = parseGameData(readFile(game.dataFile));
        for (const auto& entry : data.characters) {
            const auto character = parseCharacter(entry.body, data.layout());
            DAS_CHECK(writeCharacter(character, data.layout(), entry.body) == entry.body);
        }
        const auto hero = findHero(data);
        DAS_CHECK(hero.has_value());
        if (hero) {
            DAS_CHECK(hero->frame(0, Facing::Down) < data.tiles.size());
        }
    }
}

DAS_TEST("original data survives the trip through our own pack format") {
    const auto games = installedGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        const Bytes original = readFile(game.dataFile);
        const auto source = das::pack::openSource(game.dataFile.parent_path());
        DAS_CHECK(source != nullptr);
        const auto packs = das::pack::importGames(*source);
        const auto it = std::find_if(packs.begin(), packs.end(),
                                     [&](const auto& p) { return p.game == das::pack::gameId(game.game); });
        DAS_CHECK(it != packs.end());
        if (it == packs.end()) {
            continue;
        }
        const das::pack::Pack& pack = *it;
        DAS_CHECK(!pack.sounds.empty());
        const das::pack::Files files = das::pack::encode(pack);
        const das::pack::Pack back = das::pack::decode(files);
        DAS_CHECK(back.game == pack.game);
        DAS_CHECK(back.palette.colors == pack.palette.colors);
        DAS_CHECK(back.sounds == pack.sounds);
        DAS_CHECK(writeGameData(back.data) == original);
        // Encoding is stable, so packs diff cleanly.
        DAS_CHECK(das::pack::encode(back) == files);
    }
}

DAS_TEST("every zone of both games can be entered with its scripts running") {
    const auto games = installedGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        const GameData data = parseGameData(readFile(game.dataFile));
        int speeches = 0;
        for (std::size_t z = 0; z < data.zones.size(); ++z) {
            das::world::World w(data);
            w.start(static_cast<int>(z));
            for (int guard = 0; guard < 200; ++guard) {
                for (const auto& e : w.takeEffects()) {
                    speeches += e.kind == das::world::Effect::Kind::Speech ? 1 : 0;
                }
                if (!w.paused()) {
                    break;
                }
                w.resume();
            }
            DAS_CHECK(!w.paused());
        }
        std::printf("    %s: %d speech bubbles on entering every zone\n", std::string(layoutName(game.game)).c_str(),
                    speeches);
    }
}
