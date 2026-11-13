// SPDX-License-Identifier: GPL-3.0-or-later
//
// The world generator: picks a mission, chains puzzles back from the goal
// to items the player can find, and lays the zones out on a map. Our own
// algorithm, following the rules the data sets (which zone can ask for or
// hand out which item); see docs/world-generator.md.
#pragma once

#include "das/data/game_data.hpp"
#include "das/data/puzzle.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace das::world {

// Zone types (Zone::type).
enum class ZoneType : std::uint32_t {
    Empty = 1,
    BlockadeNorth = 2,
    BlockadeSouth = 3,
    BlockadeEast = 4,
    BlockadeWest = 5,
    TravelStart = 6,
    TravelEnd = 7,
    Room = 8,
    Load = 9,
    Goal = 10,
    Town = 11,
    Win = 13,
    Lose = 14,
    Use = 15,   // use an item on something, get an item
    Trade = 16, // give an item to someone, get an item
    Find = 17,  // an item to find
    FindWeapon = 18,
};

enum class WorldSize { Small, Medium, Large };

struct GeneratorOptions {
    std::uint32_t seed = 1;
    WorldSize size = WorldSize::Medium;
    // Yoda Stories: 1..3; -1: any. Indy has a single planet.
    int planet = -1;
    // A goal puzzle index; -1: any.
    int goal = -1;
    // Yoda Stories keeps two goals back until the player has won games, and
    // avoids the goals of the last games on a planet (oldest first).
    int gamesWon = 0;
    std::vector<int> recentGoals;
    // Indy only: zones the last game used, which this game leaves out.
    std::vector<int> recentZones;
};

enum class SiteKind { None, Empty, Town, Puzzle, Goal, Dagobah };

// A cell of a map: an outdoor zone, the rooms behind its doors, and the
// puzzle the generator gave it.
struct Site {
    SiteKind kind = SiteKind::None;
    // What the zone does here (a ZoneType value): a blockade, a trade, ...
    std::uint32_t role = 0;
    int zone = -1;
    std::vector<int> zones; // zone and its rooms
    std::uint16_t requiredItem = data::kNoTile;
    std::uint16_t requiredItem2 = data::kNoTile;
    std::uint16_t findItem = data::kNoTile;
    std::uint16_t findItem2 = data::kNoTile;
    int requestPuzzle = -1; // its Request text asks for requiredItem
    int offerPuzzle = -1;   // its Offer text offers findItem
    // A character that stands in the zone and trades.
    std::uint16_t npc = data::kNoTile;
    int npcZone = -1;
    int npcSpot = -1; // hotspot index in npcZone
    // Where findItem lies, when no script hands it out.
    int itemZone = -1;
    int itemSpot = -1;
};

// The sites the player must solve to win: the trades and uses of the
// puzzle chain, and the goal. Blockades, places where an item lies and
// vehicles are puzzle cells too, but no script marks them solved, so they
// cannot count towards the player's progress.
inline bool isChainPuzzle(const Site& s) {
    return s.kind == SiteKind::Goal ||
           (s.kind == SiteKind::Puzzle && (s.role == static_cast<std::uint32_t>(ZoneType::Use) ||
                                           s.role == static_cast<std::uint32_t>(ZoneType::Trade)));
}

struct Map {
    int width = 0;
    int height = 0;
    std::vector<Site> sites; // row by row

    Site& at(int x, int y) { return sites[static_cast<std::size_t>(y * width + x)]; }
    const Site& at(int x, int y) const { return sites[static_cast<std::size_t>(y * width + x)]; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
};

// A hotspot whose argument the generator set (an item, a character), which
// also turns it on.
struct HotspotChange {
    int zone = -1;
    int spot = -1;
    std::uint16_t argument = data::kNoTile;
};

struct WorldMap {
    std::uint32_t seed = 0; // the seed the world was made from
    int planet = 0;
    int goalPuzzle = -1;
    Map world;   // the planet, 10 x 10
    Map dagobah; // Yoda Stories only: where a game starts, 2 x 2
    int startZone = -1;
    int puzzleCount = 0; // puzzle sites, the goal not counted
    // Indy only: the item the mission giver in the town hands over with the
    // briefing, which the first puzzle of the chain asks for.
    std::uint16_t startItem = data::kNoTile;
    std::vector<HotspotChange> hotspots;
    std::vector<int> recentGoals; // GeneratorOptions::recentGoals for the next game
    std::vector<int> recentZones; // Indy: GeneratorOptions::recentZones for the next game

    // Finds the map cell of an outdoor zone. Returns false when the zone is
    // not on a map (rooms, unused zones).
    bool locate(int zone, const Map*& map, int& x, int& y) const;
};

class GeneratorError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

WorldMap generateWorld(const data::GameData& data, const GeneratorOptions& options);

// Yoda Stories: the planet of a new game, as the original picks it. It always
// differs from the last one; coin (a random bit) picks between the other
// two. After 5, 10 and 15 won games the snow planet (2) is skipped.
int nextPlanet(int last, int gamesWon, bool coin);

// The zone and every room reachable through its doors (DoorIn hotspots).
std::vector<int> zoneCluster(const data::GameData& data, int zone);

} // namespace das::world
