// SPDX-License-Identifier: GPL-3.0-or-later
//
// Combat tests (docs/combat.md). They need the original game files (see
// test_game_files.cpp) and are skipped without them.
#include "test.hpp"

#include "das/data/game_data.hpp"
#include "das/data/game_files.hpp"
#include "das/world/savegame.hpp"
#include "das/world/world.hpp"

#include <cstdlib>
#include <optional>

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

// A zone with an enemy that can be hurt and does harm, and a free cell
// beside it for the hero.
struct Arena {
    int zone = -1;
    std::size_t monster = 0;
    world::Position hero;
    das::Step toward{0, 0};
};

Arena findArena(const data::GameData& game, world::World& w) {
    for (std::size_t z = 0; z < game.zones.size(); ++z) {
        const auto& zone = game.zones[z];
        if (zone.actions && !zone.actions->empty()) {
            continue; // keep scripts out of the way
        }
        for (std::size_t i = 0; i < zone.monsters.size(); ++i) {
            const auto& m = zone.monsters[i];
            const auto& f = w.fighters().at(m.character);
            if (f.character.type != 2 || f.health <= 0 || f.damage <= 0 || f.weapon >= 0) {
                continue;
            }
            for (const das::Step s : {das::Step{1, 0}, das::Step{-1, 0}, das::Step{0, 1}, das::Step{0, -1}}) {
                const world::Position at{m.x - s.dx, m.y - s.dy};
                w.enter(static_cast<int>(z), at);
                if (w.inside(at) && w.walkable(at)) {
                    return {static_cast<int>(z), i, at, s};
                }
            }
        }
    }
    return {};
}

} // namespace

DAS_TEST("weapons are found from their inventory tiles") {
    for (const auto& game : loadGames()) {
        world::World w(game);
        int weapons = 0;
        for (const auto& f : w.fighters()) {
            if (f.character.type == 4 && f.item != 0xffff) {
                DAS_CHECK(w.isWeapon(f.item));
                ++weapons;
            }
        }
        DAS_CHECK(weapons >= 4);
    }
}

DAS_TEST("a monster next to the hero hurts him until he is lost") {
    const auto games = loadGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        world::World w(game);
        const Arena a = findArena(game, w);
        DAS_CHECK(a.zone >= 0);
        if (a.zone < 0) {
            continue;
        }
        w.enter(a.zone, a.hero);
        const int before = w.health();
        for (int t = 0; t < 600 && w.outcome() == world::Outcome::Playing; ++t) {
            w.update(100);
            // Stay next to it: it may have moved.
            (void)w.takeEffects();
        }
        DAS_CHECK(w.health() < before);
    }
}

DAS_TEST("the hero's weapon kills a monster") {
    const auto games = loadGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        world::World w(game);
        // The strongest melee weapon the hero can carry.
        std::uint16_t best = data::kNoTile;
        int damage = 0;
        for (const auto& f : w.fighters()) {
            if (f.character.type == 4 && f.item != 0xffff && f.damage > damage &&
                f.character.frame(0, data::Facing::Up) == 0xffff) {
                best = f.item;
                damage = f.damage;
            }
        }
        DAS_CHECK(best != data::kNoTile);
        w.addItem(best);
        DAS_CHECK(w.equip(best));
        DAS_CHECK(w.weapon() == best);
        const Arena a = findArena(game, w);
        if (a.zone < 0) {
            continue;
        }
        w.enter(a.zone, a.hero);
        (void)w.takeEffects();
        bool dead = false;
        for (int t = 0; t < 400 && !dead && w.outcome() == world::Outcome::Playing; ++t) {
            // Face the monster wherever it went and strike.
            for (const auto& m : w.monsters()) {
                if (m.index != a.monster) {
                    continue;
                }
                const int dx = m.at.x - w.player().x;
                const int dy = m.at.y - w.player().y;
                if (std::abs(dx) + std::abs(dy) == 1) {
                    (void)w.move({dx, dy}); // blocked by the monster: just turns
                    (void)w.attack();
                }
            }
            w.update(100);
            (void)w.takeEffects();
            dead = true;
            for (const auto& m : w.monsters()) {
                dead = dead && m.index != a.monster;
            }
        }
        DAS_CHECK(dead);
    }
}

DAS_TEST("a save game brings back the same world and state") {
    const auto games = loadGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        world::GeneratorOptions options;
        options.seed = 12;
        world::World w(game);
        w.setMap(world::generateWorld(game, options));
        w.begin();
        // Play a little: walk around, let time pass, fight.
        const das::Step steps[] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        for (int i = 0; i < 400; ++i) {
            while (w.paused()) {
                (void)w.takeEffects();
                w.resume();
            }
            (void)w.move(steps[(i / 7) % 4]);
            (void)w.attack();
            w.update(120);
            (void)w.takeEffects();
            w.setHealth(world::kMaxHealth);
        }
        while (w.paused()) {
            (void)w.takeEffects();
            w.resume();
        }
        DAS_CHECK(w.canSave());
        const std::string text = world::writeSaveGame(w, options, "test");
        const auto save = world::readSaveGame(text);
        DAS_CHECK(save.game == "test");
        DAS_CHECK(save.options.seed == options.seed);
        world::World back(game);
        world::restoreGame(back, game, save);
        DAS_CHECK(back.zoneIndex() == w.zoneIndex());
        DAS_CHECK(back.player() == w.player());
        DAS_CHECK(back.inventory() == w.inventory());
        DAS_CHECK(back.health() == w.health());
        const std::string a = json::write(w.saveState());
        const std::string b = json::write(back.saveState());
        if (a != b) {
            std::size_t i = 0;
            while (i < a.size() && i < b.size() && a[i] == b[i]) {
                ++i;
            }
            std::printf("       differs at %zu:\n%s\n---\n%s\n", i, a.substr(i > 200 ? i - 200 : 0, 300).c_str(),
                        b.substr(i > 200 ? i - 200 : 0, 300).c_str());
        }
        DAS_CHECK(a == b);
        const auto& z = w.zone();
        bool same = true;
        for (int y = 0; y < z.height; ++y) {
            for (int x = 0; x < z.width; ++x) {
                for (int layer = 0; layer < 3; ++layer) {
                    same = same && back.tile({x, y}, layer) == w.tile({x, y}, layer);
                }
            }
        }
        DAS_CHECK(same);
    }
}

DAS_TEST("a damaged save game is refused") {
    bool refused = false;
    try {
        (void)world::readSaveGame("{\"format\": \"something else\"}");
    } catch (const json::Error&) {
        refused = true;
    }
    DAS_CHECK(refused);
}

namespace {

// Lets time pass in game ticks, answering speech and keeping the hero alive.
void play(world::World& w, int ticks) {
    for (int t = 0; t < ticks && w.outcome() == world::Outcome::Playing; ++t) {
        while (w.paused()) {
            (void)w.takeEffects();
            w.resume();
        }
        w.update(140);
        (void)w.takeEffects();
        w.setHealth(world::kMaxHealth);
    }
}

std::optional<world::Position> monsterPosition(const world::World& w, std::size_t index) {
    for (const auto& m : w.monsters()) {
        if (m.index == index) {
            return m.at;
        }
    }
    return std::nullopt;
}

} // namespace

DAS_TEST("monsters take at most one step per game tick and sitting ones never move") {
    const auto games = loadGames();
    if (games.empty()) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : games) {
        world::World w(game);
        int zonesSeen = 0;
        int moves = 0;
        for (std::size_t z = 0; z < game.zones.size() && zonesSeen < 40; ++z) {
            const auto& zone = game.zones[z];
            if (zone.monsters.empty() || (zone.actions && !zone.actions->empty())) {
                continue;
            }
            ++zonesSeen;
            w.enter(static_cast<int>(z), {0, 0});
            for (int t = 0; t < 60; ++t) {
                std::vector<world::MonsterView> before = w.monsters();
                play(w, 1);
                for (const auto& b : before) {
                    const auto now = monsterPosition(w, b.index);
                    if (!now) {
                        continue;
                    }
                    const int movement = w.fighters().at(zone.monsters[b.index].character).character.movement;
                    const int dx = std::abs(now->x - b.at.x);
                    const int dy = std::abs(now->y - b.at.y);
                    if (movement == 5) {
                        continue; // Indy's rock jumps back to its start
                    }
                    DAS_CHECK(dx <= 1 && dy <= 1);
                    if (movement == 4 || movement == 12) {
                        DAS_CHECK(dx == 0 && dy == 0);
                    }
                    moves += dx + dy > 0 ? 1 : 0;
                }
            }
        }
        DAS_CHECK(zonesSeen > 0);
        DAS_CHECK(moves > 0);
    }
}

DAS_TEST("Yoda Stories patrols walk between their four waypoints") {
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Yoda) {
            continue;
        }
        world::World w(game);
        bool tested = false;
        for (std::size_t z = 0; z < game.zones.size() && !tested; ++z) {
            const auto& zone = game.zones[z];
            for (std::size_t i = 0; i < zone.monsters.size() && !tested; ++i) {
                const auto& m = zone.monsters[i];
                if (w.fighters().at(m.character).character.movement != 10 || m.waypoints[4] == m.waypoints[0]) {
                    continue;
                }
                // The waypoints are four pairs of 32-bit x and y.
                int lowX = 99, highX = -1, lowY = 99, highY = -1;
                for (int k = 0; k < 4; ++k) {
                    lowX = std::min<int>(lowX, m.waypoints[static_cast<std::size_t>(k * 4)]);
                    highX = std::max<int>(highX, m.waypoints[static_cast<std::size_t>(k * 4)]);
                    lowY = std::min<int>(lowY, m.waypoints[static_cast<std::size_t>(k * 4 + 2)]);
                    highY = std::max<int>(highY, m.waypoints[static_cast<std::size_t>(k * 4 + 2)]);
                }
                if (m.x < lowX || m.x > highX || m.y < lowY || m.y > highY) {
                    continue;
                }
                // Keep the hero out of its way, in a corner.
                w.enter(static_cast<int>(z), {zone.width - 1, zone.height - 1});
                if (!w.monsterEnabled(i)) {
                    continue;
                }
                tested = true;
                bool left = false;
                bool reachedSecond = false;
                for (int t = 0; t < 200; ++t) {
                    play(w, 1);
                    const auto at = monsterPosition(w, i);
                    if (!at) {
                        break;
                    }
                    left = left || at->x < lowX || at->x > highX || at->y < lowY || at->y > highY;
                    reachedSecond = reachedSecond || (at->x == m.waypoints[4] && at->y == m.waypoints[6]);
                }
                DAS_CHECK(!left);
                DAS_CHECK(reachedSecond);
            }
        }
        DAS_CHECK(tested);
    }
}

DAS_TEST("Indy's rock rolls down and starts again at the top") {
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Indy) {
            continue;
        }
        world::World w(game);
        bool tested = false;
        for (std::size_t z = 0; z < game.zones.size() && !tested; ++z) {
            const auto& zone = game.zones[z];
            for (std::size_t i = 0; i < zone.monsters.size() && !tested; ++i) {
                const auto& m = zone.monsters[i];
                if (w.fighters().at(m.character).character.movement != 5) {
                    continue;
                }
                tested = true;
                w.enter(static_cast<int>(z), {0, 0});
                int lowest = m.y;
                bool back = false;
                for (int t = 0; t < 400; ++t) {
                    play(w, 1);
                    if (const auto at = monsterPosition(w, i)) {
                        DAS_CHECK(at->x == m.x);
                        DAS_CHECK(at->y >= m.y);
                        back = back || (lowest > m.y && at->y == m.y);
                        lowest = std::max(lowest, at->y);
                    }
                }
                DAS_CHECK(lowest > m.y);
                DAS_CHECK(back);
            }
        }
        DAS_CHECK(tested);
    }
}

DAS_TEST("a shooting monster's shot flies three cells at most and hurts the hero") {
    const auto games = loadGames();
    for (const auto& game : games) {
        world::World w(game);
        bool tested = false;
        for (std::size_t z = 0; z < game.zones.size() && !tested; ++z) {
            const auto& zone = game.zones[z];
            for (std::size_t i = 0; i < zone.monsters.size() && !tested; ++i) {
                const auto& m = zone.monsters[i];
                const auto& f = w.fighters().at(m.character);
                if (f.weapon < 0 || f.character.movement != 4 || f.damage <= 0) {
                    continue;
                }
                // A free cell three below or above it, with nothing in between.
                for (const int dir : {1, -1}) {
                    const world::Position hero{m.x, m.y + 3 * dir};
                    bool clear = w.inside(hero);
                    w.enter(static_cast<int>(z), hero);
                    for (int k = 1; k <= 3 && clear; ++k) {
                        clear = w.tile({m.x, m.y + k * dir}, 1) == 0xffff;
                    }
                    if (!clear || !w.walkable(hero) || !w.monsterEnabled(i) || w.zoneIndex() != static_cast<int>(z)) {
                        continue;
                    }
                    tested = true;
                    const int before = w.health();
                    bool shotSeen = false;
                    for (int t = 0; t < 400 && w.health() == before; ++t) {
                        while (w.paused()) {
                            (void)w.takeEffects();
                            w.resume();
                        }
                        w.update(140);
                        (void)w.takeEffects();
                        for (const auto& s : w.shots()) {
                            shotSeen = true;
                            DAS_CHECK(s.at.x == m.x);
                            DAS_CHECK(std::abs(s.at.y - m.y) <= 3);
                        }
                    }
                    DAS_CHECK(shotSeen);
                    DAS_CHECK(w.health() < before);
                    break;
                }
            }
        }
        DAS_CHECK(tested);
    }
}

DAS_TEST("Yoda Stories monsters drop the tile before their item number") {
    const auto games = loadGames();
    for (const auto& game : games) {
        if (game.layout() != data::Layout::Yoda) {
            continue;
        }
        world::World w(game);
        w.setDifficulty(0); // ten times the damage: one blow is enough
        std::uint16_t best = data::kNoTile;
        int damage = 0;
        for (const auto& f : w.fighters()) {
            if (f.character.type == 4 && f.item != 0xffff && f.damage > damage &&
                f.character.frame(0, data::Facing::Up) == 0xffff) {
                best = f.item;
                damage = f.damage;
            }
        }
        w.addItem(best);
        DAS_CHECK(w.equip(best));
        bool tested = false;
        for (std::size_t z = 0; z < game.zones.size() && !tested; ++z) {
            const auto& zone = game.zones[z];
            if (zone.actions && !zone.actions->empty()) {
                continue;
            }
            for (std::size_t i = 0; i < zone.monsters.size() && !tested; ++i) {
                const auto& m = zone.monsters[i];
                const auto& f = w.fighters().at(m.character);
                if (m.unknown == 0 || m.item < 2 || m.item == 0xffff || f.invulnerable ||
                    f.health > damage * 10 || f.character.movement != 4) {
                    continue;
                }
                for (const das::Step s : {das::Step{0, 1}, das::Step{0, -1}, das::Step{1, 0}, das::Step{-1, 0}}) {
                    const world::Position hero{m.x - s.dx, m.y - s.dy};
                    w.enter(static_cast<int>(z), hero);
                    if (!w.inside(hero) || !w.walkable(hero) || !w.monsterEnabled(i)) {
                        continue;
                    }
                    tested = true;
                    (void)w.move(s); // turns to it
                    DAS_CHECK(w.attack());
                    DAS_CHECK(!monsterPosition(w, i));
                    const std::uint16_t dropped = w.tile({m.x, m.y}, 1);
                    DAS_CHECK(dropped == m.item - 1);
                    break;
                }
            }
        }
        DAS_CHECK(tested);
    }
}
