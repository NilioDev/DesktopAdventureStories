// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zone checks and setups both world generators share; see zone_rules.hpp and
// docs/world-generator.md.
#include "zone_rules.hpp"

#include <algorithm>

namespace das::world::rules {

const std::vector<data::Hotspot>& Builder::spots(const data::Zone& z) {
    static const std::vector<data::Hotspot> none;
    return z.hotspots ? *z.hotspots : none;
}

bool Builder::has(const std::vector<std::uint16_t>& list, std::int16_t item) {
    return std::find(list.begin(), list.end(), static_cast<std::uint16_t>(item)) != list.end();
}

// --- bookkeeping lists -----------------------------------------------------

bool Builder::zoneUsed(int z) const {
    return zone(z) == nullptr || std::find(usedZones_.begin(), usedZones_.end(), z) != usedZones_.end();
}

bool Builder::puzzleUsed(int p) const {
    return std::find(usedPuzzles_.begin(), usedPuzzles_.end(), static_cast<std::int16_t>(p)) != usedPuzzles_.end();
}

bool Builder::itemUsed(std::int16_t item) const {
    return std::any_of(usedItems_.begin(), usedItems_.end(), [&](const Entry& e) { return e.item == item; });
}

void Builder::markItemUsed(std::int16_t item, int ring) {
    if (!itemUsed(item)) {
        usedItems_.push_back({item, static_cast<std::int16_t>(ring)});
    }
}

void Builder::unmarkItem(std::int16_t item) {
    auto it = std::find_if(usedItems_.begin(), usedItems_.end(), [&](const Entry& e) { return e.item == item; });
    if (it != usedItems_.end()) {
        usedItems_.erase(it);
    }
}

void Builder::removeRequired(std::int16_t item) {
    auto it = std::find_if(required_.begin(), required_.end(), [&](const Entry& e) { return e.item == item; });
    if (it != required_.end()) {
        required_.erase(it);
    }
}

// --- zone checks -----------------------------------------------------------

// Whether the items in the zone's crates are all still free.
bool Builder::zoneItemsFree(int z) const {
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

void Builder::markZoneItems(int z) {
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

bool Builder::provides(int z, std::int16_t item) const {
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

bool Builder::asks(int z, std::int16_t item, bool goalList) const {
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

bool Builder::hasNpc(int z, std::int16_t npc) const {
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

// A free character of the zone, picked at random.
std::int16_t Builder::pickNpc(int z) {
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

// Puts a value on a random hotspot of one type, in the zone that lists the
// value (npcs or provided items), else in a room behind its doors.
std::int16_t Builder::placeOn(int z, std::int16_t value, bool npc) {
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

std::int16_t Builder::findRequired(int z, std::int16_t item, bool goalList) const {
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
bool Builder::placeLock(int z, int ring, std::int16_t item, bool goalList) {
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
bool Builder::placeReward(int z, int ring, std::int16_t item) {
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

// --- zone setups -----------------------------------------------------------

// A blockade or a travel start: asks for one of its items.
bool Builder::setupBlockade(int z, int ring, bool goalList) {
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
bool Builder::setupFind(int z, std::int16_t item, int ring) {
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
bool Builder::setupUse(int z, int index, int ring, const std::vector<std::int16_t>& chain) {
    const auto* zz = zone(z);
    if (zz == nullptr || zz->type != static_cast<std::uint32_t>(ZoneType::Use) || !zz->npcs.empty()) {
        return false;
    }
    const auto p = static_cast<std::uint16_t>(chain[static_cast<std::size_t>(index)]);
    const std::int16_t item = item1(p);
    std::int16_t next = kNone;
    if (static_cast<int>(chain.size()) - 1 >= index) {
        next = item1(static_cast<std::uint16_t>(chain[static_cast<std::size_t>(index) + 1]));
    }
    markItemUsed(item, ring);
    markItemUsed(next, ring);
    if (!zoneItemsFree(z)) {
        // The originals free the first item twice and keep the second.
        unmarkItem(item);
        unmarkItem(item);
        return false;
    }
    bool ok = placeLock(z, ring, item);
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
bool Builder::setupTrade(int z, int index, int ring, const std::vector<std::int16_t>& chain) {
    const auto* zz = zone(z);
    if (zz == nullptr || zz->type != static_cast<std::uint32_t>(ZoneType::Trade) || !zoneItemsFree(z)) {
        return false;
    }
    const std::int16_t p = chain[static_cast<std::size_t>(index)];
    const std::int16_t q =
        static_cast<int>(chain.size()) - 1 > index ? chain[static_cast<std::size_t>(index) + 1] : kNone;
    if (puzzle(p) == nullptr || (q >= 0 && puzzle(q) == nullptr)) {
        return false;
    }
    const std::int16_t item = item1(p);
    const std::int16_t next = q >= 0 ? item1(q) : kNone;
    const std::int16_t npc = pickNpc(z);
    if (npc < 0) {
        return false;
    }
    const bool wants = asks(z, item);
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

// --- the map ---------------------------------------------------------------

void Builder::reset() {
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
    emptyWithoutHotspots_ = false;
    resetReport();
}

void Builder::store(int cell, int z, bool full) {
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

// Where an item the player must find goes: a free cell no further out than
// the ring, preferably with no puzzle next to it.
bool Builder::findCell(int ringLimit, const Layout& layout, int& outX, int& outY) {
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
void Builder::placeTravel(Layout& layout) {
    for (int y = 0; y < kSide; ++y) {
        for (int x = 0; x < kSide; ++x) {
            resetReport();
            const int cell = y * kSide + x;
            if (layout.at(x, y) != Cell::TravelStart || grid_[static_cast<std::size_t>(cell)]) {
                continue;
            }
            const int z = findPlainZone(6, ring(x, y));
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

// The town and the blockades.
void Builder::placeOthers(const Layout& layout) {
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
            int z = findPlainZone(type, ring(x, y));
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
            z = findPlainZone(1, ring(x, y));
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

void Builder::writeSites(WorldMap& out) const {
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
            const auto& list = chainOf(c);
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
}

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

} // namespace das::world::rules
