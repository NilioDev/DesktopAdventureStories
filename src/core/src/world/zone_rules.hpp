// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal: what the world generators of both games share (see
// docs/world-generator.md). Yoda Stories grew out of Indiana Jones and His
// Desktop Adventures, and most of how a zone is checked and set up for its
// part in a world is the same in both. The game specific parts (choosing
// puzzles, the goal, the order of the whole run) live in yoda_generator.cpp
// and indy_generator.cpp.
#pragma once

#include "yoda_layout.hpp"

#include "das/data/puzzle.hpp"
#include "das/world/generator.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace das::world::rules {

using yoda::Cell;
using yoda::kCells;
using yoda::kSide;
using yoda::Layout;
using yoda::ring;
using yoda::Rng;

constexpr std::int16_t kNone = -1;
constexpr int kAnyGoal = 9999; // choosing a puzzle: a goal

// Hotspot types the generators look at.
enum Spot : std::uint32_t {
    ItemSpot = 0,
    NpcSpot = 1,
    WeaponSpot = 2,
    VehicleTo = 3,
    LocatorSpot = 5,
    CrateItem = 6,
    CrateWeapon = 8,
    DoorIn = 9,
    Lock = 12,
    Teleporter = 13,
};

struct Entry {
    std::int16_t item = kNone;
    std::int16_t ring = 0; // how far out the item may be found
};

struct CellInfo {
    std::int16_t zone = kNone;
    std::int32_t type = -1;
    std::int16_t indexA = kNone; // chain index of the cell's puzzle
    std::int16_t indexB = kNone; // Yoda goal only: index in the second chain
    std::int16_t required = kNone;
    std::int16_t required2 = kNone;
    std::int16_t find = kNone;
    std::int16_t find2 = kNone;
    std::int16_t npc = kNone;
    bool chainA = false;
};

// What the zone setups report back (the originals keep these in the game
// object and copy them into a map cell).
struct Report {
    std::int32_t type = -1;
    std::int16_t find = kNone;
    std::int16_t find2 = kNone;
    std::int16_t required = kNone;
    std::int16_t required2 = kNone;
    std::int16_t npc = kNone;
    std::int16_t indexA = kNone;
    std::int16_t indexB = kNone;
};

class Failed {}; // this seed gives no world; the caller tries another one

class Builder {
public:
    Builder(const data::GameData& data) : data_(data), puzzles_(data::parsePuzzles(data)) {}
    virtual ~Builder() = default;
    Builder(const Builder&) = delete;
    Builder& operator=(const Builder&) = delete;

protected:
    // --- the zone data -----------------------------------------------------

    const data::Zone* zone(int z) const {
        return z >= 0 && static_cast<std::size_t>(z) < data_.zones.size() ? &data_.zones[static_cast<std::size_t>(z)]
                                                                           : nullptr;
    }
    static const std::vector<data::Hotspot>& spots(const data::Zone& z);
    static std::int16_t arg(const data::Hotspot& h) { return static_cast<std::int16_t>(h.argument); }
    static bool has(const std::vector<std::uint16_t>& list, std::int16_t item);
    const std::vector<std::uint16_t>& required(const data::Zone& z, bool goalList) const {
        return goalList ? z.goalItems : z.requiredItems;
    }
    const data::Puzzle* puzzle(int p) const {
        return p >= 0 && static_cast<std::size_t>(p) < puzzles_.size() ? &puzzles_[static_cast<std::size_t>(p)] : nullptr;
    }
    std::int16_t item1(int p) const {
        const auto* q = puzzle(p);
        return q != nullptr ? static_cast<std::int16_t>(q->item1) : kNone;
    }
    std::uint32_t tileFlags(std::int16_t tile) const {
        const auto t = static_cast<std::uint16_t>(tile);
        return t < data_.tiles.size() ? data_.tiles[t].flags : 0;
    }
    void setSpot(int z, int spot, std::int16_t value) { changes_[{z, spot}] = static_cast<std::uint16_t>(value); }

    // --- bookkeeping lists -------------------------------------------------

    bool zoneUsed(int z) const;
    void markZoneUsed(int z) { usedZones_.push_back(static_cast<std::int16_t>(z)); }
    bool puzzleUsed(int p) const;
    bool itemUsed(std::int16_t item) const;
    void markItemUsed(std::int16_t item, int ring);
    void unmarkItem(std::int16_t item);
    void addRequired(std::int16_t item, int ring) {
        required_.insert(required_.begin(), Entry{item, static_cast<std::int16_t>(ring)});
    }
    void removeRequired(std::int16_t item);

    // --- zone checks (each also looks into the rooms behind the doors) -----

    bool zoneItemsFree(int z) const;
    void markZoneItems(int z);
    bool provides(int z, std::int16_t item) const;
    bool asks(int z, std::int16_t item, bool goalList = false) const;
    bool hasNpc(int z, std::int16_t npc) const;
    std::int16_t pickNpc(int z);
    std::int16_t placeOn(int z, std::int16_t value, bool npc);
    std::int16_t findRequired(int z, std::int16_t item, bool goalList = false) const;
    bool placeLock(int z, int ring, std::int16_t item, bool goalList = false);
    bool placeReward(int z, int ring, std::int16_t item);

    // --- zone setups -------------------------------------------------------

    bool setupBlockade(int z, int ring, bool goalList = false);
    bool setupFind(int z, std::int16_t item, int ring);
    // A chain of puzzles: the cell with chain index i asks for the item of
    // chain[i] and hands out the item of chain[i + 1].
    bool setupUse(int z, int index, int ring, const std::vector<std::int16_t>& chain);
    bool setupTrade(int z, int index, int ring, const std::vector<std::int16_t>& chain);

    // --- the map -----------------------------------------------------------

    // A free zone of a type for a cell that is no puzzle (travel, blockade,
    // town, empty), set up for its role.
    virtual int findPlainZone(int type, int ring) = 0;
    // The puzzle chain a cell's chain index points into.
    virtual const std::vector<std::int16_t>& chainOf(const CellInfo& cell) const = 0;

    void reset();
    void resetReport() {
        report_ = Report{};
        npcAt_ = {-1, -1};
        itemAt_ = {-1, -1};
    }
    void store(int cell, int z, bool full);
    bool findCell(int ring, const Layout& layout, int& outX, int& outY);
    void placeTravel(Layout& layout);
    void placeOthers(const Layout& layout);
    // The map cells, puzzle texts and hotspot changes of the world.
    void writeSites(WorldMap& out) const;

    const data::GameData& data_;
    std::vector<data::Puzzle> puzzles_;
    Rng rng_;

    std::vector<std::int16_t> usedZones_;
    std::vector<std::int16_t> usedPuzzles_;
    std::vector<Entry> usedItems_;
    std::vector<Entry> required_;
    std::vector<std::int16_t> singleProvided_;
    bool emptyWithoutHotspots_ = false;

    Report report_;
    std::pair<int, int> npcAt_{-1, -1};
    std::pair<int, int> itemAt_{-1, -1};
    std::array<CellInfo, kCells> cells_{};
    std::array<bool, kCells> grid_{};
    std::array<std::pair<int, int>, kCells> cellNpcAt_{};
    std::array<std::pair<int, int>, kCells> cellItemAt_{};
    std::map<std::pair<int, int>, std::uint16_t> changes_;
};

// The next seed to try when a seed gives no world. The originals take a new
// seed from the clock; we mix one from the old seed the same way, so the
// result stays repeatable.
std::uint32_t nextSeed(std::uint32_t seed);

} // namespace das::world::rules
