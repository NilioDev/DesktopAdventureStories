// SPDX-License-Identifier: GPL-3.0-or-later
//
// The world generator of Yoda Stories, as specified in docs/world-generator.md:
// after the map layout (yoda_layout.cpp) it picks the goal, chains puzzles
// back from it, and gives every map cell a zone. It draws from the game's
// random number sequence in the same order as the original, so a seed, a
// planet and a size give the same world.
#include "yoda_generator.hpp"

#include "yoda_layout.hpp"

#include "das/data/puzzle.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>

namespace das::world::yoda {

namespace {

constexpr std::int16_t kNone = -1;
constexpr int kAnyGoal = 9999; // choosePuzzle: a goal
constexpr int kAttempts = 64;  // seeds tried before we give up

// Hotspot types the generator looks at.
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

// Two items every world hides; the player needs them early.
constexpr std::uint16_t kFixedItemNear = 0x1a5;
constexpr std::uint16_t kFixedItemFar = 0x1ff;

struct Entry {
    std::int16_t item = kNone;
    std::int16_t ring = 0; // how far out the item may be found
};

struct CellInfo {
    std::int16_t zone = kNone;
    std::int32_t type = -1;
    std::int16_t indexA = kNone; // chain index of the cell's puzzle
    std::int16_t indexB = kNone; // goal only: index in the second chain
    std::int16_t required = kNone;
    std::int16_t required2 = kNone;
    std::int16_t find = kNone;
    std::int16_t find2 = kNone;
    std::int16_t npc = kNone;
    bool chainA = false;
};

// What the zone setups report back (the original keeps these in the game
// object and copies them into a map cell).
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

class Generator {
public:
    Generator(const data::GameData& data, const GeneratorOptions& options, int planet)
        : data_(data), options_(options), planet_(planet), puzzles_(data::parsePuzzles(data)) {}

    WorldMap run(std::uint32_t seed);

private:
    // --- the zone data -----------------------------------------------------

    const data::Zone* zone(int z) const {
        return z >= 0 && static_cast<std::size_t>(z) < data_.zones.size() ? &data_.zones[static_cast<std::size_t>(z)]
                                                                           : nullptr;
    }
    static const std::vector<data::Hotspot>& spots(const data::Zone& z) {
        static const std::vector<data::Hotspot> none;
        return z.hotspots ? *z.hotspots : none;
    }
    static std::int16_t arg(const data::Hotspot& h) { return static_cast<std::int16_t>(h.argument); }
    static bool has(const std::vector<std::uint16_t>& list, std::int16_t item) {
        return std::find(list.begin(), list.end(), static_cast<std::uint16_t>(item)) != list.end();
    }
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
    void setSpot(int z, int spot, std::int16_t value) {
        changes_[{z, spot}] = static_cast<std::uint16_t>(value);
    }

    // --- bookkeeping lists ---------------------------------------------------

    bool zoneUsed(int z) const {
        return zone(z) == nullptr || std::find(usedZones_.begin(), usedZones_.end(), z) != usedZones_.end();
    }
    void markZoneUsed(int z) { usedZones_.push_back(static_cast<std::int16_t>(z)); }
    bool puzzleUsed(int p) const {
        return std::find(usedPuzzles_.begin(), usedPuzzles_.end(), static_cast<std::int16_t>(p)) != usedPuzzles_.end();
    }
    bool itemUsed(std::int16_t item) const {
        return std::any_of(usedItems_.begin(), usedItems_.end(), [&](const Entry& e) { return e.item == item; });
    }
    void markItemUsed(std::int16_t item, int ring) {
        if (!itemUsed(item)) {
            usedItems_.push_back({item, static_cast<std::int16_t>(ring)});
        }
    }
    void unmarkItem(std::int16_t item) {
        auto it = std::find_if(usedItems_.begin(), usedItems_.end(), [&](const Entry& e) { return e.item == item; });
        if (it != usedItems_.end()) {
            usedItems_.erase(it);
        }
    }
    void addRequired(std::int16_t item, int ring) {
        required_.insert(required_.begin(), Entry{item, static_cast<std::int16_t>(ring)});
    }
    void removeRequired(std::int16_t item) {
        auto it = std::find_if(required_.begin(), required_.end(), [&](const Entry& e) { return e.item == item; });
        if (it != required_.end()) {
            required_.erase(it);
        }
    }
    std::vector<std::int16_t>& chain(bool a) { return a ? chainA_ : chainB_; }

    // --- zone checks (each also looks into the rooms behind the doors) -----

    // Whether the items in the zone's crates are all still free.
    bool zoneItemsFree(int z) const {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return false;
        }
        for (const auto& h : spots(*zz)) {
            if (h.type >= CrateItem && h.type <= CrateWeapon) {
                if (arg(h) >= 0 && itemUsed(arg(h))) {
                    return false;
                }
            } else if (h.type == DoorIn && arg(h) >= 0) {
                if (!zoneItemsFree(arg(h))) {
                    return false;
                }
            }
        }
        return true;
    }
    void markZoneItems(int z) {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return;
        }
        for (const auto& h : spots(*zz)) {
            if (h.type >= CrateItem && h.type <= CrateWeapon) {
                if (arg(h) >= 0) {
                    markItemUsed(arg(h), -1);
                }
            } else if (h.type == DoorIn && arg(h) >= 0) {
                markZoneItems(arg(h));
            }
        }
    }
    bool provides(int z, std::int16_t item) const {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return false;
        }
        if (has(zz->providedItems, item)) {
            return true;
        }
        for (const auto& h : spots(*zz)) {
            if (h.type == DoorIn && arg(h) >= 0 && provides(arg(h), item)) {
                return true;
            }
        }
        return false;
    }
    bool asks(int z, std::int16_t item, bool goalList) const {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return false;
        }
        if (has(required(*zz, goalList), item)) {
            return true;
        }
        for (const auto& h : spots(*zz)) {
            if (h.type == DoorIn && arg(h) >= 0 && asks(arg(h), item, goalList)) {
                return true;
            }
        }
        return false;
    }
    bool hasNpc(int z, std::int16_t npc) const {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return false;
        }
        if (npc == kNone) {
            return !zz->npcs.empty();
        }
        if (has(zz->npcs, npc)) {
            return true;
        }
        for (const auto& h : spots(*zz)) {
            if (h.type == DoorIn && arg(h) >= 0 && hasNpc(arg(h), npc)) {
                return true;
            }
        }
        return false;
    }

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
    // A free character of the zone, picked at random.
    std::int16_t pickNpc(int z) {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return kNone;
        }
        std::int16_t out = kNone;
        if (!zz->npcs.empty()) {
            std::vector<std::int16_t> free;
            for (const auto npc : zz->npcs) {
                if (!itemUsed(static_cast<std::int16_t>(npc))) {
                    free.push_back(static_cast<std::int16_t>(npc));
                }
            }
            if (!free.empty()) {
                out = free[static_cast<std::size_t>(rng_.next() % static_cast<int>(free.size()))];
            }
        }
        if (out < 0) {
            for (const auto& h : spots(*zz)) {
                if (h.type == DoorIn && arg(h) >= 0) {
                    out = pickNpc(arg(h));
                }
                if (out >= 0) {
                    break;
                }
            }
        }
        return out;
    }
    // Puts a value on a random hotspot of one type, in the zone that lists
    // the value (npcs or provided items), else in a room behind its doors.
    std::int16_t placeOn(int z, std::int16_t value, bool npc) {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return kNone;
        }
        const auto& list = npc ? zz->npcs : zz->providedItems;
        const std::uint32_t type = npc ? NpcSpot : ItemSpot;
        const auto& hs = spots(*zz);
        std::int16_t out = kNone;
        for (const auto entry : list) {
            if (static_cast<std::int16_t>(entry) != value) {
                continue;
            }
            std::vector<int> fits;
            for (std::size_t i = 0; i < hs.size(); ++i) {
                if (hs[i].type == type) {
                    fits.push_back(static_cast<int>(i));
                }
            }
            if (!fits.empty()) {
                const int spot = fits[static_cast<std::size_t>(rng_.next() % static_cast<int>(fits.size()))];
                setSpot(z, spot, value);
                out = value;
                if (npc) {
                    report_.npc = value;
                    npcAt_ = {z, spot};
                } else {
                    itemAt_ = {z, spot};
                }
            }
            if (out >= 0) {
                break;
            }
        }
        if (out == kNone) {
            for (const auto& h : hs) {
                if (h.type == DoorIn && arg(h) >= 0) {
                    out = placeOn(arg(h), value, npc);
                }
                if (out >= 0) {
                    break;
                }
            }
        }
        return out;
    }
    std::int16_t findRequired(int z, std::int16_t item, bool goalList) const {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return kNone;
        }
        if (has(required(*zz, goalList), item)) {
            return item;
        }
        for (const auto& h : spots(*zz)) {
            if (h.type == DoorIn && arg(h) >= 0) {
                if (const auto r = findRequired(arg(h), item, goalList); r >= 0) {
                    return r;
                }
            }
        }
        return kNone;
    }
    // The lock hotspot asks for the item.
    bool placeLock(int z, int ring, std::int16_t item, bool goalList) {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return false;
        }
        const auto& hs = spots(*zz);
        if (has(required(*zz, goalList), item)) {
            for (std::size_t i = 0; i < hs.size(); ++i) {
                if (hs[i].type == Lock) {
                    markItemUsed(item, ring);
                    (goalList ? report_.required2 : report_.required) = item;
                    setSpot(z, static_cast<int>(i), item);
                    return true;
                }
            }
        }
        for (const auto& h : hs) {
            if (h.type == DoorIn && placeLock(arg(h), ring, item, goalList)) {
                return true;
            }
        }
        return false;
    }
    // The item lies on a random item spot.
    bool placeReward(int z, int ring, std::int16_t item) {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return false;
        }
        const auto& hs = spots(*zz);
        if (has(zz->providedItems, item)) {
            std::vector<int> fits;
            for (std::size_t i = 0; i < hs.size(); ++i) {
                if (hs[i].type == ItemSpot) {
                    fits.push_back(static_cast<int>(i));
                }
            }
            if (!fits.empty()) {
                const int spot = fits[static_cast<std::size_t>(rng_.next() % static_cast<int>(fits.size()))];
                markItemUsed(item, ring);
                report_.find = item;
                setSpot(z, spot, item);
                itemAt_ = {z, spot};
                return true;
            }
        }
        for (const auto& h : hs) {
            if (h.type == DoorIn && placeReward(arg(h), ring, item)) {
                return true;
            }
        }
        return false;
    }

    // --- zone setups -------------------------------------------------------

    // A blockade or a travel start: asks for one of its items.
    bool setupBlockade(int z, int ring, bool goalList) {
        const auto* zz = zone(z);
        if (zz == nullptr) {
            return false;
        }
        const auto& list = required(*zz, goalList);
        if (list.empty() || !zoneItemsFree(z)) {
            return false;
        }
        std::vector<std::int16_t> free;
        for (const auto item : list) {
            if (!itemUsed(static_cast<std::int16_t>(item))) {
                free.push_back(static_cast<std::int16_t>(item));
            }
        }
        if (free.empty()) {
            return false;
        }
        const auto item = free[static_cast<std::size_t>(rng_.next() % static_cast<int>(free.size()))];
        if (zz->providedItems.size() == 1) {
            const auto only = static_cast<std::int16_t>(zz->providedItems.front());
            if (std::find(singleProvided_.begin(), singleProvided_.end(), only) != singleProvided_.end()) {
                return false;
            }
            singleProvided_.push_back(only);
        }
        addRequired(item, zz->type == static_cast<std::uint32_t>(ZoneType::TravelStart) ? 5 : ring);
        markItemUsed(item, ring);
        report_.required = item;
        markZoneItems(z);
        return true;
    }
    // An item to find, on a spot that fits its kind.
    bool setupFind(int z, std::int16_t item, int ring) {
        if (z < 0 || !zoneItemsFree(z)) {
            return false;
        }
        const auto* zz = zone(z);
        if (zz == nullptr || !zz->requiredItems.empty() || !zz->npcs.empty()) {
            return false;
        }
        const auto& hs = spots(*zz);
        bool done = false;
        if (has(zz->providedItems, item)) {
            const auto f = tileFlags(item);
            std::uint32_t type = ItemSpot;
            if ((f & 0x40) != 0) {
                type = WeaponSpot;
            } else if ((f & 0x100000) != 0) {
                type = LocatorSpot;
            }
            std::vector<int> fits;
            for (std::size_t i = 0; i < hs.size(); ++i) {
                if (hs[i].type == type) {
                    fits.push_back(static_cast<int>(i));
                }
            }
            if (!fits.empty()) {
                const int spot = fits[static_cast<std::size_t>(rng_.next() % static_cast<int>(fits.size()))];
                setSpot(z, spot, item);
                itemAt_ = {z, spot};
                markItemUsed(item, ring);
                report_.find = item;
                done = true;
            }
        } else {
            for (const auto& h : hs) {
                if (h.type == DoorIn && arg(h) >= 0) {
                    done = setupFind(arg(h), item, ring);
                }
                if (done) {
                    break;
                }
            }
        }
        if (done) {
            markZoneItems(z);
        }
        return done;
    }
    // Use an item on something (a lock), get the next item.
    bool setupUse(int z, int index, int ring, bool inA) {
        const auto* zz = zone(z);
        if (zz == nullptr || zz->type != static_cast<std::uint32_t>(ZoneType::Use) || !zz->npcs.empty()) {
            return false;
        }
        auto& c = chain(inA);
        const auto p = static_cast<std::uint16_t>(c[static_cast<std::size_t>(index)]);
        const std::int16_t item = item1(p);
        std::int16_t next = kNone;
        if (static_cast<int>(c.size()) - 1 >= index) {
            next = item1(static_cast<std::uint16_t>(c[static_cast<std::size_t>(index) + 1]));
        }
        markItemUsed(item, ring);
        markItemUsed(next, ring);
        if (!zoneItemsFree(z)) {
            unmarkItem(item);
            unmarkItem(item);
            return false;
        }
        bool ok = placeLock(z, ring, item, false);
        if (ok) {
            ok = placeReward(z, ring, next);
        }
        if (ok) {
            report_.indexA = static_cast<std::int16_t>(index);
            markZoneItems(z);
        }
        return ok;
    }
    // Give an item to someone, get the next item.
    bool setupTrade(int z, int index, int ring, bool inA) {
        const auto* zz = zone(z);
        if (zz == nullptr || zz->type != static_cast<std::uint32_t>(ZoneType::Trade) || !zoneItemsFree(z)) {
            return false;
        }
        auto& c = chain(inA);
        const std::int16_t p = c[static_cast<std::size_t>(index)];
        const std::int16_t q =
            static_cast<int>(c.size()) - 1 > index ? c[static_cast<std::size_t>(index) + 1] : kNone;
        if (puzzle(p) == nullptr || (q >= 0 && puzzle(q) == nullptr)) {
            return false;
        }
        const std::int16_t item = item1(p);
        const std::int16_t next = q >= 0 ? item1(q) : kNone;
        const std::int16_t npc = pickNpc(z);
        if (npc < 0) {
            return false;
        }
        const bool wants = asks(z, item, false);
        const bool gives = next < 0 || provides(z, next);
        if (!wants || !gives || placeOn(z, npc, true) < 0) {
            return false;
        }
        report_.npc = npc;
        report_.find = next;
        report_.required = item;
        report_.indexA = static_cast<std::int16_t>(index);
        markItemUsed(npc, ring);
        markZoneItems(z);
        return true;
    }
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
                const bool ok = type == 15 ? setupUse(z, a, ring, inA) : setupTrade(z, a, ring, inA);
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

    // --- the map ---------------------------------------------------------

    void resetReport() {
        report_ = Report{};
        npcAt_ = {-1, -1};
        itemAt_ = {-1, -1};
    }
    void store(int cell, int z, bool full) {
        CellInfo& c = cells_[static_cast<std::size_t>(cell)];
        c.zone = static_cast<std::int16_t>(z);
        c.type = report_.type;
        c.find = report_.find;
        c.required = report_.required;
        c.npc = report_.npc;
        c.indexA = report_.indexA;
        if (full) {
            c.find2 = report_.find2;
            c.required2 = report_.required2;
            c.indexB = report_.indexB;
        }
        grid_[static_cast<std::size_t>(cell)] = true;
        cellNpcAt_[static_cast<std::size_t>(cell)] = npcAt_;
        cellItemAt_[static_cast<std::size_t>(cell)] = itemAt_;
    }

    bool findCell(int ring, const Layout& layout, int& outX, int& outY);
    void placeTravel(Layout& layout);
    void placeChains(Layout& layout, int count, int sizeA, int sizeB);
    void placeOthers(const Layout& layout);
    void fillRest(Layout& layout);
    void placeDagobah(WorldMap& out);
    WorldMap result(std::uint32_t seed, int count) const;

    const data::GameData& data_;
    const GeneratorOptions& options_;
    int planet_ = 1;
    std::vector<data::Puzzle> puzzles_;
    Rng rng_;

    std::vector<std::int16_t> usedZones_;
    std::vector<std::int16_t> usedPuzzles_;
    std::vector<Entry> usedItems_;
    std::vector<Entry> required_;
    std::vector<std::int16_t> singleProvided_;
    std::vector<std::int16_t> chainA_;
    std::vector<std::int16_t> chainB_;
    std::vector<int> recent_;
    int goal_ = -1;
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


// Where an item the player must find goes: a free cell no further out than
// the ring, preferably with no puzzle next to it.
bool Generator::findCell(int ringLimit, const Layout& layout, int& outX, int& outY) {
    std::vector<std::pair<int, int>> alone;
    std::vector<std::pair<int, int>> near;
    std::vector<std::pair<int, int>> beyond;
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            const Cell v = layout.at(x, y);
            if (ring(x, y) <= ringLimit) {
                if (v == Cell::Plain || v == Cell::Behind) {
                    const bool free = (x < 1 || layout.at(x - 1, y) != Cell::PuzzleSlot) &&
                                      (x > 8 || layout.at(x + 1, y) != Cell::PuzzleSlot) &&
                                      (y < 1 || layout.at(x, y - 1) != Cell::PuzzleSlot) &&
                                      (y > 8 || layout.at(x, y + 1) != Cell::PuzzleSlot);
                    (free ? alone : near).emplace_back(x, y);
                }
            } else if (v == Cell::Plain) {
                beyond.emplace_back(x, y);
            }
        }
    }
    for (const auto* list : {&alone, &near, &beyond}) {
        if (!list->empty()) {
            const auto& at = (*list)[static_cast<std::size_t>(rng_.next() % static_cast<int>(list->size()))];
            outX = at.first;
            outY = at.second;
            return true;
        }
    }
    return false;
}

// Travel starts and, on an island's landing, where the vehicle goes.
void Generator::placeTravel(Layout& layout) {
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            resetReport();
            const int cell = y * kSide + x;
            if (layout.at(x, y) != Cell::TravelStart || grid_[static_cast<std::size_t>(cell)]) {
                continue;
            }
            const int z = findZone(6, -1, -1, kNone, kNone, ring(x, y), true);
            if (z < 0) {
                continue;
            }
            CellInfo& c = cells_[static_cast<std::size_t>(cell)];
            c.zone = static_cast<std::int16_t>(z);
            c.type = 6;
            c.required = report_.required;
            int dest = -1;
            for (const auto& h : spots(data_.zones[static_cast<std::size_t>(z)])) {
                if (h.type == VehicleTo) {
                    dest = arg(h);
                    break;
                }
            }
            if (dest < 0) {
                continue;
            }
            // The first free landing: left column, top row, right column,
            // bottom row.
            int dockX = -1;
            int dockY = -1;
            const auto look = [&](int cx, int cy) {
                if (dockX < 0 && layout.at(cx, cy) == Cell::Dock && !grid_[static_cast<std::size_t>(cy * kSide + cx)]) {
                    dockX = cx;
                    dockY = cy;
                }
            };
            for (int i = 0; i < kSide; ++i) look(0, i);
            for (int i = 0; i < kSide; ++i) look(i, 0);
            for (int i = 0; i < kSide; ++i) look(9, i);
            for (int i = 0; i < kSide; ++i) look(i, 9);
            if (dockX >= 0) {
                if (!zoneUsed(dest)) {
                    const int dock = dockY * kSide + dockX;
                    grid_[static_cast<std::size_t>(dock)] = true;
                    CellInfo& d = cells_[static_cast<std::size_t>(dock)];
                    d.zone = static_cast<std::int16_t>(dest);
                    d.type = 7;
                    d.required = report_.required;
                    grid_[static_cast<std::size_t>(cell)] = true;
                    markZoneUsed(z);
                    markZoneUsed(dest);
                }
            } else {
                removeRequired(report_.required);
                layout.at(x, y) = Cell::Plain;
                grid_[static_cast<std::size_t>(cell)] = false;
                c.zone = kNone;
                c.type = -1;
                c.required = kNone;
            }
        }
    }
}

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

// The town and the blockades.
void Generator::placeOthers(const Layout& layout) {
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            resetReport();
            const int cell = y * kSide + x;
            const Cell v = layout.at(x, y);
            if (v == Cell::None || v == Cell::KeptClear || grid_[static_cast<std::size_t>(cell)]) {
                continue;
            }
            int type = 0;
            switch (v) {
            case Cell::Town: type = 11; break;
            case Cell::BlockadeToWest: type = 5; break;
            case Cell::BlockadeToEast: type = 4; break;
            case Cell::BlockadeToNorth: type = 2; break;
            case Cell::BlockadeToSouth: type = 3; break;
            default: break;
            }
            if (type == 0) {
                continue;
            }
            CellInfo& c = cells_[static_cast<std::size_t>(cell)];
            int z = findZone(type, -1, -1, kNone, kNone, ring(x, y), false);
            if (z >= 0) {
                c.type = type;
                if (type != 11) {
                    c.required = report_.required;
                }
                grid_[static_cast<std::size_t>(cell)] = true;
                c.zone = static_cast<std::int16_t>(z);
                c.find = kNone;
                if (type != 11) {
                    markZoneUsed(z);
                }
                continue;
            }
            z = findZone(1, -1, -1, kNone, kNone, ring(x, y), false);
            if (z >= 0) {
                grid_[static_cast<std::size_t>(cell)] = true;
                c.zone = static_cast<std::int16_t>(z);
                c.type = 1;
                c.find = kNone;
                c.required = kNone;
                c.indexA = kNone;
                c.npc = kNone;
                markZoneUsed(z);
            }
        }
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

WorldMap Generator::result(std::uint32_t seed, int count) const {
    WorldMap out;
    out.seed = seed;
    out.planet = planet_;
    out.goalPuzzle = goal_;
    out.puzzleCount = count - 1;
    out.recentGoals = recent_;
    Map& map = out.world;
    map.width = kSide;
    map.height = kSide;
    map.sites.assign(kCells, Site{});
    const auto tile = [](std::int16_t v) { return static_cast<std::uint16_t>(v); };
    std::vector<int> placed;
    for (int i = 0; i < kCells; ++i) {
        const CellInfo& c = cells_[static_cast<std::size_t>(i)];
        if (c.zone < 0) {
            continue;
        }
        Site& s = map.sites[static_cast<std::size_t>(i)];
        switch (c.type) {
        case 1: s.kind = SiteKind::Empty; break;
        case 10: s.kind = SiteKind::Goal; break;
        case 11: s.kind = SiteKind::Town; break;
        default: s.kind = SiteKind::Puzzle; break;
        }
        s.role = static_cast<std::uint32_t>(c.type);
        s.zone = c.zone;
        s.zones = zoneCluster(data_, c.zone);
        placed.insert(placed.end(), s.zones.begin(), s.zones.end());
        s.requiredItem = tile(c.required);
        s.requiredItem2 = tile(c.required2);
        s.findItem = tile(c.find);
        s.findItem2 = tile(c.find2);
        s.npc = tile(c.npc);
        s.npcZone = cellNpcAt_[static_cast<std::size_t>(i)].first;
        s.npcSpot = cellNpcAt_[static_cast<std::size_t>(i)].second;
        s.itemZone = cellItemAt_[static_cast<std::size_t>(i)].first;
        s.itemSpot = cellItemAt_[static_cast<std::size_t>(i)].second;
        if (c.indexA >= 0 && (c.type == 10 || c.type == 15 || c.type == 16)) {
            const auto& list = c.chainA ? chainA_ : chainB_;
            const auto at = static_cast<std::size_t>(c.indexA);
            if (at < list.size()) {
                s.requestPuzzle = static_cast<std::uint16_t>(list[at]);
            }
            if (at + 1 < list.size()) {
                s.offerPuzzle = static_cast<std::uint16_t>(list[at + 1]);
            }
        }
    }
    for (const auto& [where, value] : changes_) {
        if (std::find(placed.begin(), placed.end(), where.first) != placed.end()) {
            out.hotspots.push_back({where.first, where.second, value});
        }
    }
    return out;
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
    usedZones_.clear();
    usedPuzzles_.clear();
    usedItems_.clear();
    required_.clear();
    singleProvided_.clear();
    changes_.clear();
    cells_.fill(CellInfo{});
    grid_.fill(false);
    cellNpcAt_.fill({-1, -1});
    cellItemAt_.fill({-1, -1});
    goal_ = -1;
    emptyWithoutHotspots_ = false;

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
    WorldMap out = result(seed, count);
    placeDagobah(out);
    return out;
}

// The next seed to try when a seed gives no world. The original takes a new
// seed from the clock; we mix one from the old seed the same way, so the
// result stays repeatable.
std::uint32_t nextSeed(std::uint32_t seed) {
    Rng r;
    r.seed(seed);
    const auto a = static_cast<std::uint32_t>(r.next());
    auto b = static_cast<std::uint32_t>(r.next());
    auto c = static_cast<std::uint32_t>(r.next());
    auto d = static_cast<std::uint32_t>(r.next());
    switch (r.next() % 3) {
    case 0: b = (b << 8) & 0xff00u, c = (c << 16) & 0xff0000u, d = (d << 24) & 0xff000000u; break;
    case 1: c = (c << 8) & 0xff00u, d = (d << 16) & 0xff0000u, b = (b << 24) & 0xff000000u; break;
    default: d = (d << 8) & 0xff00u, b = (b << 16) & 0xff0000u, c = (c << 24) & 0xff000000u; break;
    }
    return a | b | c | d;
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
