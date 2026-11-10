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
};

enum class SiteKind { None, Empty, Town, Puzzle, Goal, Dagobah };

// A cell of a map: an outdoor zone, the rooms behind its doors, and the
// puzzle the generator gave it.
struct Site {
    SiteKind kind = SiteKind::None;
    int zone = -1;
    std::vector<int> zones; // zone and its rooms
    std::uint16_t requiredItem = data::kNoTile;
    std::uint16_t requiredItem2 = data::kNoTile;
    std::uint16_t findItem = data::kNoTile;
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

struct Map {
    int width = 0;
    int height = 0;
    std::vector<Site> sites; // row by row

    Site& at(int x, int y) { return sites[static_cast<std::size_t>(y * width + x)]; }
    const Site& at(int x, int y) const { return sites[static_cast<std::size_t>(y * width + x)]; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }
};

struct WorldMap {
    int planet = 0;
    int goalPuzzle = -1;
    Map world;   // the planet, 10 x 10
    Map dagobah; // Yoda Stories only: where a game starts, 2 x 2
    int startZone = -1;
    int puzzleCount = 0; // puzzle sites, the goal not counted

    // Finds the map cell of an outdoor zone. Returns false when the zone is
    // not on a map (rooms, unused zones).
    bool locate(int zone, const Map*& map, int& x, int& y) const;
};

class GeneratorError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

WorldMap generateWorld(const data::GameData& data, const GeneratorOptions& options);

// The zone and every room reachable through its doors (DoorIn hotspots).
std::vector<int> zoneCluster(const data::GameData& data, int zone);

} // namespace das::world
