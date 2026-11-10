// SPDX-License-Identifier: GPL-3.0-or-later
//
// The world generator of Yoda Stories, as specified in docs/world-generator.md:
// after the map layout (yoda_layout.cpp) it picks the goal, chains puzzles
// back from it, and gives every map cell a zone. It draws from the game's
// random number sequence in the same order as the original, so a seed, a
// planet and a size give the same world. The zone rules it shares with
// Indy's generator are in zone_rules.cpp.
#include "yoda_generator.hpp"

#include "zone_rules.hpp"

#include <algorithm>
#include <cstdlib>

namespace das::world::yoda {

namespace {

using namespace rules;

constexpr int kAttempts = 64; // seeds tried before we give up

// Two items every world hides; the player needs them early.
constexpr std::uint16_t kFixedItemNear = 0x1a5;
constexpr std::uint16_t kFixedItemFar = 0x1ff;

class Generator : public Builder {
public:
    Generator(const data::GameData& data, const GeneratorOptions& options, int planet)
        : Builder(data), options_(options), planet_(planet) {}

    WorldMap run(std::uint32_t seed);

private:
    std::vector<std::int16_t>& chain(bool a) { return a ? chainA_ : chainB_; }

    // A free item the zone asks for, picked at random.
    std::int16_t pickItem(int z, bool goalList) {
        const auto& zz = data_.zones.at(static_cast<std::size_t>(z));
        std::vector<std::int16_t> free;
        for (const auto item : required(zz, goalList)) {
            if (!itemUsed(static_cast<std::int16_t>(item))) {
                free.push_back(static_cast<std::int16_t>(item));
            }
        }
        std::int16_t out = kNone;
        if (!free.empty()) {
            out = free[static_cast<std::size_t>(rng_.next() % static_cast<int>(free.size()))];
        }
        if (out < 0) {
            for (const auto& h : spots(zz)) {
                if (h.type == DoorIn) {
                    out = pickItem(arg(h), goalList);
                    if (out >= 0) {
                        break;
                    }
                }
            }
        }
        return out;
    }
    // --- zone setups -------------------------------------------------------

    // The goal zone: asks for the last items of both chains, hands out the
    // goal's items.
    bool setupGoal(int z, int a, int b, int ring) {
        const auto* zz = zone(z);
        if (zz == nullptr || zz->type != static_cast<std::uint32_t>(ZoneType::Goal)) {
            return false;
        }
        const auto goal = static_cast<std::uint16_t>(chainA_[static_cast<std::size_t>(a) + 1]);
        const std::int16_t a1 = item1(static_cast<std::uint16_t>(chainA_[static_cast<std::size_t>(a)]));
        const std::int16_t b1 = item1(static_cast<std::uint16_t>(chainB_[static_cast<std::size_t>(b)]));
        const std::int16_t g1 = item1(goal);
        const auto* gp = puzzle(goal);
        const std::int16_t g2 = gp != nullptr ? static_cast<std::int16_t>(gp->item2) : kNone;
        if (!zoneItemsFree(z)) {
            return false;
        }
        bool withNpc = false;
        const std::int16_t npc = pickNpc(z);
        if (npc >= 0) {
            withNpc = hasNpc(z, npc);
        }
        const bool wants = asks(z, a1, false);
        const bool gives = provides(z, g1);
        if (!wants || !gives) {
            return false;
        }
        const auto fill = [&] {
            report_.find = g1;
            report_.find2 = g2;
            report_.required = a1;
            report_.required2 = b1;
            report_.indexA = static_cast<std::int16_t>(a);
            report_.indexB = static_cast<std::int16_t>(b);
        };
        if (withNpc) {
            if (placeOn(z, npc, true) < 0) {
                return false;
            }
            report_.npc = npc;
            fill();
            markZoneItems(z);
            return true;
        }
        const std::int16_t ra = findRequired(z, a1, false);
        const std::int16_t ga = placeOn(z, g1, false);
        if (ra >= 0 && ga >= 0) {
            markItemUsed(a1, ring);
            markItemUsed(g1, ring);
        }
        const std::int16_t rb = findRequired(z, b1, true);
        const std::int16_t gb = placeOn(z, g2, false);
        if (rb >= 0 && gb >= 0) {
            markItemUsed(b1, ring);
            markItemUsed(g2, ring);
        }
        if (ra >= 0 && ga >= 0 && rb >= 0 && gb >= 0) {
            report_.npc = kNone;
            fill();
            markZoneItems(z);
            return true;
        }
        unmarkItem(a1);
        unmarkItem(g1);
        unmarkItem(b1);
        unmarkItem(g2);
        return false;
    }
    // --- choosing --------------------------------------------------------

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
        for (int i = n - 1; i >= 0; --i) {
            for (;;) {
                if (std::find(out.begin(), out.end(), kNone) == out.end()) {
                    break;
                }
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

    bool goalOfPlanet(int p) const {
        switch (planet_) {
        case 1: return p == 85 || p == 115 || p == 185 || p == 199 || p == 201;
        case 2: return p == 103 || p == 108 || p == 135 || p == 189 || p == 197;
        case 3: return (p >= 131 && p <= 134) || p == 198;
        default: return false;
        }
    }

    // A free puzzle of a kind whose item1 is the item (any goal for goals).
    int choosePuzzle(std::int16_t item, int kind) {
        const auto wantedType = [&]() -> std::uint32_t {
            switch (kind) {
            case 10: return 2;
            case 15: return 1;
            case 16: return 0;
            default: return 3;
            }
        }();
        std::vector<std::int16_t> list;
        for (std::size_t i = 0; i < puzzles_.size() && i < 0x8000; ++i) {
            const int p = static_cast<int>(i);
            if (puzzles_[i].type != wantedType) {
                continue;
            }
            if (kind == kAnyGoal) {
                const bool recent = std::find(recent_.begin(), recent_.end(), p) != recent_.end();
                if ((recent && options_.goal < 0) || !goalOfPlanet(p)) {
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
            const auto* q = puzzle(static_cast<std::uint16_t>(p));
            if (puzzleUsed(static_cast<std::uint16_t>(p)) || q == nullptr || q->type != wantedType) {
                continue;
            }
            if (kind == kAnyGoal || q->item1 == static_cast<std::uint16_t>(item)) {
                return static_cast<std::uint16_t>(p);
            }
        }
        return -1;
    }

    // A free zone of a type for a map cell; sets it up for its role.
    // a, b: chain indices; c, d: items; ring: the cell's ring; inA: which chain.
    int findZone(int type, int a, int b, std::int16_t c, std::int16_t d, int ring, bool inA) {
        std::vector<std::int16_t> list;
        for (std::size_t i = 0; i < data_.zones.size() && i < 0x8000; ++i) {
            const auto& z = data_.zones[i];
            if (z.planet != planet_) {
                continue;
            }
            const bool fits = type == 17 ? (z.type == 17 || z.type == 18)
                                         : ((type >= 1 && type <= 7) || type == 10 || type == 11 || type == 15 ||
                                            type == 16) &&
                                               z.type == static_cast<std::uint32_t>(type);
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
                if (setupBlockade(z, ring, false)) {
                    return z;
                }
                break;
            case 10: {
                if (!provides(z, c) || !provides(z, d)) {
                    break;
                }
                const std::int16_t ra = pickItem(z, false);
                const std::int16_t rb = pickItem(z, true);
                if (ra < 0 || rb < 0) {
                    break;
                }
                const int pa = choosePuzzle(ra, 10);
                if (pa >= 0) {
                    usedPuzzles_.push_back(static_cast<std::int16_t>(pa));
                }
                const int pb = choosePuzzle(rb, 10);
                if (pb >= 0) {
                    usedPuzzles_.push_back(static_cast<std::int16_t>(pb));
                }
                if (pa < 0 || pb < 0) {
                    break;
                }
                chainA_[static_cast<std::size_t>(a)] = static_cast<std::int16_t>(pa);
                chainB_[static_cast<std::size_t>(b)] = static_cast<std::int16_t>(pb);
                if (setupGoal(z, a, b, ring)) {
                    markItemUsed(ra, ring);
                    markItemUsed(rb, ring);
                    return z;
                }
                chainA_[static_cast<std::size_t>(a)] = kNone;
                chainB_[static_cast<std::size_t>(b)] = kNone;
                break;
            }
            case 11: return z;
            case 15:
            case 16: {
                if (!provides(z, c)) {
                    break;
                }
                const std::int16_t r = pickItem(z, false);
                if (r < 0) {
                    break;
                }
                const int p = choosePuzzle(r, type);
                if (p < 0) {
                    break;
                }
                chain(inA)[static_cast<std::size_t>(a)] = static_cast<std::int16_t>(p);
                const bool ok = type == 15 ? setupUse(z, a, ring, chain(inA)) : setupTrade(z, a, ring, chain(inA));
                if (ok) {
                    usedPuzzles_.push_back(static_cast<std::int16_t>(p));
                    markItemUsed(r, ring);
                    return z;
                }
                break;
            }
            case 17:
                if (setupFind(z, c, ring)) {
                    return z;
                }
                break;
            default: break;
            }
        }
        return -1;
    }

    int findPlainZone(int type, int ring) override {
        return findZone(type, -1, -1, kNone, kNone, ring, type == 6);
    }
    const std::vector<std::int16_t>& chainOf(const CellInfo& c) const override {
        return c.chainA ? chainA_ : chainB_;
    }

    void placeChains(Layout& layout, int count, int sizeA, int sizeB);
    void fillRest(Layout& layout);
    void placeDagobah(WorldMap& out);

    const GeneratorOptions& options_;
    int planet_ = 1;
    std::vector<std::int16_t> chainA_;
    std::vector<std::int16_t> chainB_;
    std::vector<int> recent_;
    int goal_ = -1;
};

// Both puzzle chains, from the goal back to the first puzzle; the puzzle
// numbers of the layout say where each one goes.
void Generator::placeChains(Layout& layout, int count, int sizeA, int sizeB) {
    const auto cellOf = [&](int number) {
        for (int i = 0; i < kCells; ++i) {
            if (layout.puzzle[static_cast<std::size_t>(i)] == number) {
                return i;
            }
        }
        throw Failed{};
    };
    const auto pickType = [&](int firstOnEven, int ring, int index, std::int16_t item, bool inA) {
        const int t = rng_.next() % 2 == 0 ? firstOnEven : (firstOnEven == 16 ? 15 : 16);
        int z = findZone(t, index, -1, item, kNone, ring, inA);
        if (z >= 0) {
            report_.type = t;
            return z;
        }
        const int other = t == 16 ? 15 : 16;
        z = findZone(other, index, -1, item, kNone, ring, inA);
        if (z < 0) {
            throw Failed{};
        }
        report_.type = other;
        return z;
    };

    int number = count;
    int b = sizeB;
    for (int index = sizeA; index > 0; --index, --number, --b) {
        resetReport();
        const auto cur = static_cast<std::uint16_t>(chainA_[static_cast<std::size_t>(index)]);
        const int cell = cellOf(number - 1);
        const int r = ring(cell % kSide, cell / kSide);
        const auto* p = puzzle(cur);
        const std::int16_t i1 = p != nullptr ? static_cast<std::int16_t>(p->item1) : kNone;
        const std::int16_t i2 = p != nullptr ? static_cast<std::int16_t>(p->item2) : kNone;
        int z = -1;
        if (number == count) {
            z = findZone(10, index - 1, b - 1, i1, i2, r, true);
            if (z < 0) {
                throw Failed{};
            }
            report_.type = 10;
            report_.indexB = static_cast<std::int16_t>(b - 1);
        } else {
            z = pickType(16, r, index - 1, i1, true);
        }
        report_.indexA = static_cast<std::int16_t>(index - 1);
        markZoneUsed(z);
        store(cell, z, true);
        cells_[static_cast<std::size_t>(cell)].chainA = true;
        if (index == 1) {
            addRequired(report_.required, r);
        }
    }
    for (int index = sizeB - 1; index > 0; --index, --number) {
        resetReport();
        const auto cur = static_cast<std::uint16_t>(chainB_[static_cast<std::size_t>(index)]);
        const int cell = cellOf(number - 1);
        const int r = ring(cell % kSide, cell / kSide);
        const int z = pickType(15, r, index - 1, item1(cur), false);
        report_.indexA = static_cast<std::int16_t>(index - 1);
        markZoneUsed(z);
        store(cell, z, false);
        cells_[static_cast<std::size_t>(cell)].chainA = false;
    }
}
// The items to find, then empty zones on all cells still free.
void Generator::fillRest(Layout& layout) {
    addRequired(static_cast<std::int16_t>(kFixedItemFar), 2);
    addRequired(static_cast<std::int16_t>(kFixedItemNear), 1);
    const std::size_t count = required_.size();
    for (std::size_t i = 0; i < count; ++i) {
        const Entry e = required_[i];
        int x = 0;
        int y = 0;
        if (!findCell(e.ring, layout, x, y)) {
            throw Failed{};
        }
        itemAt_ = {-1, -1};
        const int z = findZone(17, -1, -1, e.item, kNone, e.ring, false);
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

    // Empty zones. Two zones with a teleporter must not be close together;
    // a zone that would be is kept for a later cell.
    const int limit = options_.size == WorldSize::Large ? 2 : 1;
    int teleporters = 0;
    bool deferred = false;
    int deferredZone = -1;
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
            int z = -1;
            if (emptyWithoutHotspots_ || !deferred) {
                z = findZone(1, -1, -1, kNone, kNone, r, false);
            } else {
                z = deferredZone;
            }
            if (z < 0) {
                throw Failed{};
            }
            while (!emptyWithoutHotspots_) {
                const auto& hs = spots(data_.zones[static_cast<std::size_t>(z)]);
                const bool teleporter =
                    std::any_of(hs.begin(), hs.end(), [](const data::Hotspot& h) { return h.type == Teleporter; });
                if (!teleporter) {
                    break;
                }
                if (teleporters > 0 && std::abs(lastX - x) <= limit && std::abs(lastY - y) <= limit) {
                    deferredZone = z;
                    deferred = true;
                    z = findZone(1, -1, -1, kNone, kNone, r, false);
                    if (z < 0) {
                        throw Failed{};
                    }
                    continue;
                }
                ++teleporters;
                lastX = x;
                lastY = y;
                if (deferred) {
                    deferred = false;
                    deferredZone = -1;
                }
                break;
            }
            setEmpty(y * kSide + x, z);
            if (z == deferredZone) {
                deferredZone = -1;
                deferred = false;
            }
        }
    }
    // A lone teleporter leads nowhere: swap its zone for a plain one.
    if (teleporters == 1) {
        emptyWithoutHotspots_ = true;
        const int z = findZone(1, -1, -1, kNone, kNone, ring(lastX, lastY), false);
        if (z >= 0) {
            setEmpty(lastY * kSide + lastX, z);
        }
    }
}
// Dagobah, where the game starts: four zones around Yoda, who hands out the
// item the second puzzle chain starts with.
void Generator::placeDagobah(WorldMap& out) {
    constexpr int kYoda = 780;
    constexpr int kOtherHelper = 2034;
    constexpr int kHut = 535; // the room inside zone 95
    int pick = rng_.next() % 4;
    if (goal_ == 132) {
        pick = 3;
    } else if (goal_ == 189) {
        pick = 4;
    }
    static constexpr int kZones[] = {94, kHut, 96, 93, kHut};
    const int z = kZones[pick];
    const int npc = pick == 4 ? kOtherHelper : kYoda;
    std::pair<int, int> npcAt{-1, -1};
    if (const auto* zz = zone(z)) {
        const auto& hs = spots(*zz);
        if (z == kHut) {
            const int wantY = npc == kYoda ? 3 : 2;
            for (std::size_t i = 0; i < hs.size(); ++i) {
                if (hs[i].x == 3 && hs[i].y == wantY) {
                    npcAt = {z, static_cast<int>(i)};
                    break;
                }
            }
        } else {
            for (const auto entry : zz->npcs) {
                if (entry != npc) {
                    continue;
                }
                std::vector<int> fits;
                for (std::size_t i = 0; i < hs.size(); ++i) {
                    if (hs[i].type == NpcSpot) {
                        fits.push_back(static_cast<int>(i));
                    }
                }
                if (!fits.empty()) {
                    npcAt = {z, fits[static_cast<std::size_t>(rng_.next() % static_cast<int>(fits.size()))]};
                    break;
                }
            }
        }
    }
    if (npcAt.first >= 0) {
        out.hotspots.push_back({npcAt.first, npcAt.second, static_cast<std::uint16_t>(npc)});
    }

    Map& map = out.dagobah;
    map.width = 2;
    map.height = 2;
    map.sites.assign(4, Site{});
    const int zones[] = {94, 95, 93, 96};
    for (int i = 0; i < 4; ++i) {
        Site& s = map.sites[static_cast<std::size_t>(i)];
        s.kind = SiteKind::Dagobah;
        s.zone = zones[i];
        s.zones = zoneCluster(data_, zones[i]);
        s.role = data_.zones.at(static_cast<std::size_t>(zones[i])).type;
        if (std::find(s.zones.begin(), s.zones.end(), z) == s.zones.end()) {
            continue;
        }
        s.role = static_cast<std::uint32_t>(ZoneType::Trade);
        if (npcAt.first >= 0) {
            s.npc = static_cast<std::uint16_t>(npc);
            s.npcZone = npcAt.first;
            s.npcSpot = npcAt.second;
        }
        s.findItem = static_cast<std::uint16_t>(item1(static_cast<std::uint16_t>(chainB_.front())));
        s.offerPuzzle = static_cast<std::uint16_t>(chainB_.front());
    }
    out.startZone = 93;
}

WorldMap Generator::run(std::uint32_t seed) {
    reset();
    goal_ = -1;

    const auto signedSeed = static_cast<std::int32_t>(seed);
    rng_.seed(signedSeed < 0 ? static_cast<std::uint32_t>(-static_cast<std::int64_t>(signedSeed))
                             : static_cast<std::uint32_t>(signedSeed));
    const int maxTravel = rng_.next() % 3;
    const int maxBlockades = rng_.next() % 4;
    rng_.next();
    const int townX = 4 + rng_.next() % 2;
    const int townY = 4 + rng_.next() % 2;

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
    Range inner{5, 9}, middle{5, 9}, outer{4, 8}, edge{3, 8};
    if (options_.size == WorldSize::Small) {
        inner = {5, 8}, middle = {4, 6}, outer = {1, 1}, edge = {1, 1};
    } else if (options_.size == WorldSize::Large) {
        inner = {6, 12}, middle = {6, 12}, outer = {6, 11}, edge = {4, 11};
    }
    const auto draw = [&](Range r) { return r.lo + rng_.next() % (r.hi - r.lo + 1); };
    const int n1 = std::min(12, draw(inner) + maxBlockades + maxTravel);
    layout.placeRing(2, n1, maxTravel, maxBlockades);
    layout.placeRing(3, draw(middle), maxTravel, maxBlockades);
    layout.placeRing(4, draw(outer), maxTravel, maxBlockades);
    layout.placeEdge(draw(edge));
    layout.placeIslands(layout.travels());
    const int count = layout.assignPuzzles();

    int sizeA = 0;
    int sizeB = 0;
    if (count % 2 == 0) {
        sizeB = count / 2;
        sizeA = sizeB + 1;
    } else {
        sizeA = sizeB = (count + 1) / 2;
    }
    chainA_.assign(static_cast<std::size_t>(sizeA) + 1, 0);
    chainB_.assign(static_cast<std::size_t>(sizeB) + 1, 0);

    // Goals of the last games on this planet are left out, and two goals
    // wait until the player has won games.
    recent_.clear();
    if (options_.goal < 0) {
        recent_ = options_.recentGoals;
    }
    if (recent_.size() > 3) {
        recent_.erase(recent_.begin());
    }
    if (planet_ == 2) {
        if (options_.gamesWon < 1) {
            recent_.push_back(189);
        }
        if (options_.gamesWon < 10) {
            recent_.push_back(197);
        }
    }
    goal_ = options_.goal >= 0 ? options_.goal : choosePuzzle(kNone, kAnyGoal);
    if (goal_ < 0) {
        throw Failed{};
    }
    chainA_[static_cast<std::size_t>(sizeA)] = static_cast<std::int16_t>(goal_);
    chainB_[static_cast<std::size_t>(sizeB)] = static_cast<std::int16_t>(goal_);
    usedPuzzles_.push_back(static_cast<std::int16_t>(goal_));
    recent_.push_back(goal_);

    placeTravel(layout);
    placeChains(layout, count, sizeA, sizeB);
    placeOthers(layout);
    fillRest(layout);
    rng_.next(); // the original draws once when it saves the goal history
    WorldMap out;
    out.seed = seed;
    out.planet = planet_;
    out.goalPuzzle = goal_;
    out.puzzleCount = count - 1;
    out.recentGoals = recent_;
    writeSites(out);
    placeDagobah(out);
    return out;
}
} // namespace

WorldMap generate(const data::GameData& data, const GeneratorOptions& options) {
    const int planet = options.planet >= 1 && options.planet <= 3 ? options.planet
                                                                  : 1 + static_cast<int>(options.seed % 3);
    Generator generator(data, options, planet);
    std::uint32_t seed = options.seed;
    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        try {
            return generator.run(seed);
        } catch (const Failed&) {
            seed = nextSeed(seed);
        }
    }
    throw GeneratorError("could not build a Yoda Stories world from this game data");
}

} // namespace das::world::yoda
