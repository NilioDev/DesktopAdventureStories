// SPDX-License-Identifier: GPL-3.0-or-later
//
// The world generator of Indiana Jones and His Desktop Adventures, as
// specified in docs/world-generator.md. The map layout is the one Yoda
// Stories took over (yoda_layout.cpp); on it the generator picks the goal,
// chains puzzles back from it in one chain, and gives every map cell a zone.
// It draws from the game's random number sequence in the same order as the
// original, so a seed and a size give the same world.
#include "indy_generator.hpp"

#include "zone_rules.hpp"

#include <algorithm>

namespace das::world::indy {

namespace {

using namespace rules;

constexpr int kAttempts = 64; // seeds tried before we give up

// A goal the original keeps back: never in a first game, and later only once
// the player has won ten games.
constexpr int kLateGoal = 134;
constexpr int kLateGoalWins = 10;
constexpr std::size_t kGoalHistory = 10;

// Items every world hides: one near the town, and one of four further out.
constexpr std::int16_t kFixedItemNear = 443;
constexpr std::int16_t kFixedItemsFar[] = {450, 450, 515, 454, 438}; // by a draw of 0..4

// Items the first puzzle of the chain may not ask for.
constexpr std::int16_t kNotFirst[] = {540, 542, 543};

class Generator : public Builder {
public:
    Generator(const data::GameData& data, const GeneratorOptions& options) : Builder(data), options_(options) {}

    WorldMap run(std::uint32_t seed);

private:
    // A free item the zone asks for, picked at random; the first puzzle of
    // the chain leaves a few items out.
    std::int16_t pickItem(int z, bool first) {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return kNone;
        }
        std::vector<std::int16_t> free;
        for (const auto entry : zz->requiredItems) {
            const auto item = static_cast<std::int16_t>(entry);
            if (itemUsed(item)) {
                continue;
            }
            if (first && std::find(std::begin(kNotFirst), std::end(kNotFirst), item) != std::end(kNotFirst)) {
                continue;
            }
            free.push_back(item);
        }
        std::int16_t out = kNone;
        if (!free.empty()) {
            out = free[static_cast<std::size_t>(rng_.next() % static_cast<int>(free.size()))];
        }
        if (out < 0) {
            for (const auto& h : spots(*zz)) {
                if (h.type == DoorIn) {
                    out = pickItem(arg(h), first);
                    if (out >= 0) {
                        break;
                    }
                }
            }
        }
        return out;
    }

    // The goal zone: asks for the item of the chain's last puzzle and hands
    // out the goal's item.
    bool setupGoal(int z, int index, int ring) {
        const auto* zz = zone(z);
        if (zz == nullptr || zz->type != static_cast<std::uint32_t>(ZoneType::Goal)) {
            return false;
        }
        const std::int16_t wanted = item1(chain_[static_cast<std::size_t>(index)]);
        const std::int16_t reward = item1(chain_[static_cast<std::size_t>(index) + 1]);
        if (!zoneItemsFree(z)) {
            return false;
        }
        bool withNpc = false;
        const std::int16_t npc = pickNpc(z);
        if (npc >= 0) {
            withNpc = hasNpc(z, npc);
        }
        if (!asks(z, wanted) || !provides(z, reward)) {
            return false;
        }
        const auto fill = [&](std::int16_t who) {
            report_.npc = who;
            report_.find = reward;
            report_.required = wanted;
            report_.indexA = static_cast<std::int16_t>(index);
        };
        if (withNpc) {
            if (placeOn(z, npc, true) < 0) {
                return false;
            }
            fill(npc);
            markZoneItems(z);
            return true;
        }
        const std::int16_t found = findRequired(z, wanted);
        const std::int16_t placed = placeOn(z, reward, false);
        if (found < 0 || placed < 0) {
            return false;
        }
        fill(kNone);
        markZoneItems(z);
        markItemUsed(wanted, ring);
        markItemUsed(reward, ring);
        return true;
    }

    // Shuffles in place the way the original does (not a uniform shuffle).
    void shuffle(std::vector<std::int16_t>& list) {
        const int n = static_cast<int>(list.size());
        if (n <= 0) {
            return;
        }
        std::vector<std::int16_t> out(list.size(), kNone);
        for (int i = 0; i < n; ++i) {
            const int j = rng_.next() % n;
            if (out[static_cast<std::size_t>(j)] == kNone) {
                out[static_cast<std::size_t>(j)] = list[static_cast<std::size_t>(i)];
                list[static_cast<std::size_t>(i)] = kNone;
            }
        }
        // Unlike Yoda Stories, only what is still left over is placed.
        for (int i = n - 1; i >= 0; --i) {
            if (list[static_cast<std::size_t>(i)] == kNone) {
                continue;
            }
            for (;;) {
                const int j = rng_.next() % n;
                if (out[static_cast<std::size_t>(j)] == kNone) {
                    out[static_cast<std::size_t>(j)] = list[static_cast<std::size_t>(i)];
                    list[static_cast<std::size_t>(i)] = kNone;
                    break;
                }
            }
        }
        list = std::move(out);
    }

    // A free puzzle of a kind whose item1 is the item (any goal for goals).
    int choosePuzzle(std::int16_t item, int kind) {
        std::uint32_t wantedType = 0;
        switch (kind) {
        case 10: wantedType = 2; break;
        case 15: wantedType = 1; break;
        case 16: wantedType = 0; break;
        case kAnyGoal: wantedType = 3; break;
        default: return -1;
        }
        std::vector<std::int16_t> list;
        for (std::size_t i = 0; i < puzzles_.size() && i < 0x8000; ++i) {
            const int p = static_cast<int>(i);
            if (puzzles_[i].type != wantedType) {
                continue;
            }
            if (kind == kAnyGoal) {
                const bool recent = std::find(recentGoals_.begin(), recentGoals_.end(), p) != recentGoals_.end();
                if (recent && options_.goal < 0) {
                    continue;
                }
                if (p == kLateGoal && options_.gamesWon < kLateGoalWins) {
                    continue;
                }
            } else if (puzzleUsed(p)) {
                continue;
            }
            list.push_back(static_cast<std::int16_t>(p));
        }
        if (list.empty()) {
            return -1;
        }
        shuffle(list);
        for (const auto p : list) {
            const auto* q = puzzle(p);
            if (puzzleUsed(p) || q == nullptr || q->type != wantedType) {
                continue;
            }
            if (kind == kAnyGoal || q->item1 == static_cast<std::uint16_t>(item)) {
                return p;
            }
        }
        return -1;
    }

    // A free zone of a type for a map cell; sets it up for its role.
    // index: the chain index of a puzzle cell; item: what the zone hands out
    // (puzzles) or holds (items to find); ring: the cell's ring.
    int findZone(int type, int index, std::int16_t item, int ring) {
        const bool first = index == 0;
        std::vector<std::int16_t> list;
        for (std::size_t i = 0; i < data_.zones.size() && i < 0x8000; ++i) {
            const auto t = data_.zones[i].type;
            const bool fits = type == 17 ? (t == 17 || t == 18)
                                         : ((type >= 1 && type <= 7) || type == 10 || type == 11 || type == 15 ||
                                            type == 16) &&
                                               t == static_cast<std::uint32_t>(type);
            if (fits) {
                list.push_back(static_cast<std::int16_t>(i));
            }
        }
        if (list.empty()) {
            return -1;
        }
        shuffle(list);
        for (const auto entry : list) {
            const int z = entry;
            if (zoneUsed(z) && !(type == 10 && options_.goal > 0)) {
                continue;
            }
            const auto* zz = zone(z);
            if (zz == nullptr) {
                continue;
            }
            switch (type) {
            case 1:
                if (!emptyWithoutHotspots_ || spots(*zz).empty()) {
                    return z;
                }
                break;
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
                if (setupBlockade(z, ring)) {
                    return z;
                }
                break;
            case 10:
            case 15:
            case 16: {
                if (!provides(z, item)) {
                    break;
                }
                const std::int16_t wanted = pickItem(z, first);
                if (wanted < 0) {
                    break;
                }
                const int p = choosePuzzle(wanted, type);
                if (p < 0) {
                    break;
                }
                chain_[static_cast<std::size_t>(index)] = static_cast<std::int16_t>(p);
                const bool ok = type == 10   ? setupGoal(z, index, ring)
                                : type == 15 ? setupUse(z, index, ring, chain_)
                                             : setupTrade(z, index, ring, chain_);
                if (ok) {
                    usedPuzzles_.push_back(static_cast<std::int16_t>(p));
                    markItemUsed(wanted, ring);
                    return z;
                }
                if (type == 10) {
                    chain_[static_cast<std::size_t>(index)] = kNone;
                }
                break;
            }
            case 11: return z;
            case 17:
                if (setupFind(z, item, ring)) {
                    return z;
                }
                break;
            default: break;
            }
        }
        return -1;
    }

    int findPlainZone(int type, int ring) override { return findZone(type, -1, kNone, ring); }
    const std::vector<std::int16_t>& chainOf(const CellInfo&) const override { return chain_; }

    void placeChain(const Layout& layout, int count);
    void fillRest(Layout& layout);

    const GeneratorOptions& options_;
    std::vector<std::int16_t> chain_;
    std::vector<int> recentGoals_;
    int goal_ = -1;
};

// The chain from the goal back to the first puzzle; the puzzle numbers of
// the layout say where each one goes.
void Generator::placeChain(const Layout& layout, int count) {
    for (int index = count; index > 0; --index) {
        resetReport();
        const std::int16_t item = item1(chain_[static_cast<std::size_t>(index)]);
        int cell = -1;
        for (int i = 0; i < kCells && cell < 0; ++i) {
            if (layout.puzzle[static_cast<std::size_t>(i)] == index - 1) {
                cell = i;
            }
        }
        if (cell < 0) {
            throw Failed{};
        }
        const int r = ring(cell % kSide, cell / kSide);
        int type = 10;
        int z = -1;
        if (index == count) {
            z = findZone(10, index - 1, item, r);
        } else {
            type = rng_.next() % 2 == 0 ? 15 : 16;
            z = findZone(type, index - 1, item, r);
            if (z < 0) {
                type = type == 15 ? 16 : 15;
                z = findZone(type, index - 1, item, r);
            }
        }
        if (z < 0) {
            throw Failed{};
        }
        report_.type = type;
        report_.indexA = static_cast<std::int16_t>(index - 1);
        markZoneUsed(z);
        store(cell, z, false);
    }
}

// The items to find, then empty zones on all cells still free.
void Generator::fillRest(Layout& layout) {
    const std::int16_t far = kFixedItemsFar[rng_.next() % 5];
    addRequired(far, 2);
    addRequired(kFixedItemNear, 1);
    const std::size_t count = required_.size();
    for (std::size_t i = 0; i < count; ++i) {
        const Entry e = required_[i];
        int x = 0;
        int y = 0;
        if (!findCell(e.ring, layout, x, y)) {
            throw Failed{};
        }
        itemAt_ = {-1, -1};
        const int z = findZone(17, -1, e.item, e.ring);
        if (z < 0) {
            throw Failed{};
        }
        const int cell = y * kSide + x;
        CellInfo& c = cells_[static_cast<std::size_t>(cell)];
        c.type = 17;
        c.find = report_.find;
        grid_[static_cast<std::size_t>(cell)] = true;
        c.zone = static_cast<std::int16_t>(z);
        cellItemAt_[static_cast<std::size_t>(cell)] = itemAt_;
        layout.at(x, y) = Cell::PuzzleSlot;
        markZoneUsed(z);
    }
    required_.clear();
    singleProvided_.clear();

    // Empty zones. A teleporter needs a second one to lead anywhere.
    int teleporters = 0;
    int lastX = 0;
    int lastY = 0;
    const auto setEmpty = [&](int cell, int z) {
        CellInfo& c = cells_[static_cast<std::size_t>(cell)];
        c.type = 1;
        grid_[static_cast<std::size_t>(cell)] = true;
        c.zone = static_cast<std::int16_t>(z);
        c.required = kNone;
        c.indexA = kNone;
        c.find = kNone;
        c.npc = kNone;
        markZoneUsed(z);
    };
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const Cell v = layout.at(x, y);
            if (v != Cell::Plain && v != Cell::Behind && v != Cell::Island) {
                continue;
            }
            const int r = ring(x, y);
            emptyWithoutHotspots_ = v == Cell::Island || r < 2;
            const int z = findZone(1, -1, kNone, r);
            if (z < 0) {
                continue; // the cell stays empty
            }
            if (!emptyWithoutHotspots_) {
                const auto& hs = spots(data_.zones[static_cast<std::size_t>(z)]);
                if (std::any_of(hs.begin(), hs.end(), [](const data::Hotspot& h) { return h.type == Teleporter; })) {
                    ++teleporters;
                    lastX = x;
                    lastY = y;
                }
            }
            setEmpty(y * kSide + x, z);
        }
    }
    // A lone teleporter leads nowhere: swap its zone for one without hotspots.
    if (teleporters == 1) {
        emptyWithoutHotspots_ = true;
        const int z = findZone(1, -1, kNone, ring(lastX, lastY));
        if (z >= 0) {
            setEmpty(lastY * kSide + lastX, z);
        }
    }
}

WorldMap Generator::run(std::uint32_t seed) {
    reset();
    goal_ = -1;

    // The original seeds with 16 bits: the low half of the seed, negated
    // when the whole seed is negative.
    auto low = static_cast<std::uint16_t>(seed & 0xffffu);
    if (static_cast<std::int32_t>(seed) < 0) {
        low = static_cast<std::uint16_t>(-low);
    }
    rng_.seed(low);
    const int maxTravel = rng_.next() % 3;
    const int maxBlockades = rng_.next() % 4;
    rng_.next();
    const int townX = 4 + rng_.next() % 2;
    const int townY = 4 + rng_.next() % 2;

    // As in Yoda Stories, the four middle cells start out plain.
    Layout layout(rng_);
    for (const auto& [x, y] : {std::pair{4, 4}, std::pair{5, 4}, std::pair{4, 5}, std::pair{5, 5}}) {
        layout.at(x, y) = Cell::Plain;
    }
    layout.at(townX, townY) = Cell::Town;

    // Ring sizes: {low, high} for the inner ring, the middle ring, the
    // outer ring and the edge.
    struct Range {
        int lo, hi;
    };
    Range inner{6, 12}, middle{6, 12}, outer{6, 11}, edge{4, 11};
    if (options_.size == WorldSize::Small) {
        inner = {5, 8}, middle = {4, 6}, outer = {1, 1}, edge = {1, 1};
    } else if (options_.size == WorldSize::Large) {
        inner = {7, 12}, middle = {7, 14}, outer = {8, 17}, edge = {5, 12};
    }
    const auto draw = [&](Range r) { return r.lo + rng_.next() % (r.hi - r.lo + 1); };
    const int n1 = std::min(12, draw(inner) + maxBlockades + maxTravel);
    layout.placeRing(2, n1, maxTravel, maxBlockades);
    layout.placeRing(3, draw(middle), maxTravel, maxBlockades);
    layout.placeRing(4, draw(outer), maxTravel, maxBlockades);
    layout.placeEdge(draw(edge));
    layout.placeIslands(layout.travels());
    const int count = layout.assignPuzzles();
    chain_.assign(static_cast<std::size_t>(count) + 1, 0);

    // The zones and goals of the last games are kept out of this one; with
    // no goals yet, the late goal counts as one.
    std::size_t history = 0;
    recentGoals_.clear();
    if (options_.goal < 0) {
        for (const int z : options_.recentZones) {
            usedZones_.push_back(static_cast<std::int16_t>(z));
        }
        history = usedZones_.size();
        recentGoals_ = options_.recentGoals;
        if (recentGoals_.size() > kGoalHistory) {
            recentGoals_.erase(recentGoals_.begin());
        }
    }
    if (recentGoals_.empty()) {
        recentGoals_.push_back(kLateGoal);
    }
    if (recentGoals_.size() > kGoalHistory) {
        recentGoals_.erase(recentGoals_.begin());
    }
    goal_ = options_.goal >= 0 ? options_.goal : choosePuzzle(kNone, kAnyGoal);
    if (goal_ < 0) {
        throw Failed{};
    }
    chain_[static_cast<std::size_t>(count)] = static_cast<std::int16_t>(goal_);
    usedPuzzles_.push_back(static_cast<std::int16_t>(goal_));
    recentGoals_.push_back(goal_);

    placeTravel(layout);
    placeChain(layout, count);
    placeOthers(layout);
    fillRest(layout);

    WorldMap out;
    out.seed = seed;
    out.planet = 0;
    out.goalPuzzle = goal_;
    out.puzzleCount = count - 1;
    out.recentGoals = recentGoals_;
    out.startItem = static_cast<std::uint16_t>(item1(chain_.front()));
    // Remembered for the next game: the zones this world uses, except the
    // empty ones.
    for (std::size_t i = history; i < usedZones_.size(); ++i) {
        const auto* zz = zone(usedZones_[i]);
        if (zz != nullptr && zz->type != static_cast<std::uint32_t>(ZoneType::Empty)) {
            out.recentZones.push_back(usedZones_[i]);
        }
    }
    writeSites(out);
    out.startZone = out.world.at(townX, townY).zone;
    return out;
}

} // namespace

WorldMap generate(const data::GameData& data, const GeneratorOptions& options) {
    Generator generator(data, options);
    std::uint32_t seed = options.seed;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        try {
            return generator.run(seed);
        } catch (const Failed&) {
            seed = nextSeed(seed);
        }
    }
    throw GeneratorError("could not build an Indiana Jones world from this game data");
}

} // namespace das::world::indy
