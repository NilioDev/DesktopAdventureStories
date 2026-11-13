// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/world/world.hpp"

#include "das/data/tile_flags.hpp"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <limits>

namespace das::world {

namespace flags_ = data::tile_flags;

namespace {

constexpr int kFloorLayer = 0;
constexpr int kObjectLayer = 1;
// Indy: Marcus at his desk in the town, who gives the mission.
constexpr std::uint16_t kIndyMissionGiver = 794;
// Indy: the ladder floor tiles drawn down a cliff face.
constexpr std::uint16_t kIndyLadders[] = {44, 45};

data::Facing facingFor(Step step) {
    if (step.dy < 0) {
        return data::Facing::Up;
    }
    if (step.dy > 0) {
        return data::Facing::Down;
    }
    return step.dx < 0 ? data::Facing::Left : data::Facing::Right;
}

std::size_t cellIndex(const data::Zone& zone, Position at) {
    return static_cast<std::size_t>(at.y * zone.width + at.x);
}

} // namespace

World::World(const data::GameData& data) : data_(data), puzzles_(data::parsePuzzles(data)) {
    if (data_.zones.empty()) {
        throw data::DataError("the game data has no zones");
    }
    loadFighters();
}

World::ZoneState& World::stateOf(int zoneIndex) {
    auto it = states_.find(zoneIndex);
    if (it == states_.end()) {
        const auto& z = data_.zones.at(static_cast<std::size_t>(zoneIndex));
        ZoneState fresh;
        fresh.cells = z.cells;
        if (z.hotspots) {
            for (const auto& h : *z.hotspots) {
                fresh.hotspotEnabled.push_back(h.enabled != 0);
            }
        }
        fresh.actionDisabled.assign(z.actions ? z.actions->size() : 0, false);
        fresh.monsterEnabled.assign(z.monsters.size(), true);
        fresh.monsterDead.assign(z.monsters.size(), false);
        // Both games start a zone's random value (and Yoda Stories its
        // shared counter) at -1, so "is 0" does not hold until a script sets it.
        fresh.random = -1;
        fresh.sharedCounter = -1;
        initMonsters(zoneIndex, fresh);
        prepareZone(zoneIndex, fresh);
        it = states_.emplace(zoneIndex, std::move(fresh)).first;
    }
    return it->second;
}

World::ZoneState& World::state() { return stateOf(zone_); }

const World::ZoneState* World::findState(int zoneIndex) const {
    const auto it = states_.find(zoneIndex);
    return it == states_.end() ? nullptr : &it->second;
}

const std::vector<data::ZoneCell>& World::cells() const {
    const auto* s = findState(zone_);
    return s != nullptr ? s->cells : zone().cells;
}

std::vector<data::ZoneCell>& World::cells() { return state().cells; }

bool World::hotspotEnabled(std::size_t index) const {
    if (const auto* s = findState(zone_); s != nullptr && index < s->hotspotEnabled.size()) {
        return s->hotspotEnabled[index];
    }
    return zone().hotspots && index < zone().hotspots->size() && (*zone().hotspots)[index].enabled != 0;
}

bool World::monsterEnabled(std::size_t index) const {
    if (const auto* s = findState(zone_); s != nullptr && index < s->monsterEnabled.size()) {
        return s->monsterEnabled[index];
    }
    return index < zone().monsters.size();
}

bool World::actionDisabled(std::size_t index) const {
    const auto* s = findState(zone_);
    return s != nullptr && index < s->actionDisabled.size() && s->actionDisabled[index];
}

int World::counter() const {
    const auto* s = findState(zone_);
    return s != nullptr ? s->counter : 0;
}

int World::random() const {
    const auto* s = findState(zone_);
    return s != nullptr ? s->random : 0;
}

int World::sharedCounter() const {
    const auto* s = findState(zone_);
    return s != nullptr ? s->sharedCounter : 0;
}

ZoneGoal World::goal(int zoneIndex) const {
    const auto it = zoneSite_.find(zoneIndex);
    return it == zoneSite_.end() ? ZoneGoal{} : goals_[static_cast<std::size_t>(it->second)];
}

const Site* World::site(int zoneIndex) const {
    const auto it = zoneSite_.find(zoneIndex);
    return it == zoneSite_.end() ? nullptr : &sites_[static_cast<std::size_t>(it->second)];
}

void World::setGoal(int zoneIndex, ZoneGoal g) {
    if (const auto it = zoneSite_.find(zoneIndex); it != zoneSite_.end()) {
        goals_[static_cast<std::size_t>(it->second)] = g;
        return;
    }
    Site s;
    s.zone = zoneIndex;
    s.zones = {zoneIndex};
    zoneSite_[zoneIndex] = static_cast<int>(goals_.size());
    goals_.push_back(g);
    sites_.push_back(std::move(s));
}

void World::carrySharedCounter(int toZone) {
    if (toZone == zone_ || toZone < 0 ||
        static_cast<std::size_t>(toZone) >= data_.zones.size()) {
        return;
    }
    const int value = state().sharedCounter;
    if (value >= 0) {
        stateOf(toZone).sharedCounter = value;
    }
}

void World::setSolved(int zoneIndex) {
    ZoneGoal g = goal(zoneIndex);
    g.solved = true;
    setGoal(zoneIndex, g);
}

void World::setMap(WorldMap map) {
    map_ = std::move(map);
    states_.clear();
    goals_.clear();
    sites_.clear();
    zoneSite_.clear();
    for (const Map* m : {&map_.world, &map_.dagobah}) {
        for (const Site& s : m->sites) {
            if (s.zone < 0) {
                continue;
            }
            const int index = static_cast<int>(goals_.size());
            goals_.push_back({s.requiredItem, s.requiredItem2, s.findItem, false});
            sites_.push_back(s);
            for (const int z : s.zones) {
                zoneSite_[z] = index;
            }
        }
    }
    if (map_.goalPuzzle >= 0 && static_cast<std::size_t>(map_.goalPuzzle) < puzzles_.size()) {
        setEnding(puzzles_[static_cast<std::size_t>(map_.goalPuzzle)].item1);
    }
}

void World::prepareZone(int zoneIndex, ZoneState& fresh) {
    const auto& z = data_.zones.at(static_cast<std::size_t>(zoneIndex));
    const auto put = [&](const data::Hotspot& h, std::uint16_t item) {
        if (h.x >= z.width || h.y >= z.height || item >= data_.tiles.size()) {
            return;
        }
        auto& layer = fresh.cells[static_cast<std::size_t>(h.y * z.width + h.x)].layers[kObjectLayer];
        if (layer == data::kNoTile) {
            layer = item;
        } else {
            fresh.hidden[{h.x, h.y}] = item;
        }
    };
    if (z.hotspots) {
        for (const auto& h : *z.hotspots) {
            if (h.type == static_cast<std::uint32_t>(HotspotType::CrateItem) && h.argument != data::kNoTile) {
                put(h, h.argument);
            }
        }
    }
    const Site* s = site(zoneIndex);
    if (s == nullptr || !z.hotspots) {
        return;
    }
    const auto spot = [&](int index) -> const data::Hotspot* {
        return index >= 0 && static_cast<std::size_t>(index) < z.hotspots->size()
                   ? &(*z.hotspots)[static_cast<std::size_t>(index)]
                   : nullptr;
    };
    if (s->npcZone == zoneIndex && s->npc != data::kNoTile) {
        if (const auto* h = spot(s->npcSpot); h && h->x < z.width && h->y < z.height) {
            fresh.cells[static_cast<std::size_t>(h->y * z.width + h->x)].layers[kObjectLayer] = s->npc;
        }
    }
    if (s->itemZone == zoneIndex && s->findItem != data::kNoTile) {
        if (const auto* h = spot(s->itemSpot)) {
            put(*h, s->findItem);
        }
    }
}

bool World::hasItem(std::uint16_t item) const {
    return std::find(inventory_.begin(), inventory_.end(), item) != inventory_.end();
}

void World::addItem(std::uint16_t item) {
    if (!hasItem(item)) {
        inventory_.push_back(item);
    }
    if (weapon_ == data::kNoTile && isWeapon(item)) {
        weapon_ = item;
    }
}

std::uint32_t World::flags(std::uint16_t tile) const {
    return tile < data_.tiles.size() ? data_.tiles[tile].flags : 0;
}

bool World::inside(Position at) const {
    return at.x >= 0 && at.y >= 0 && at.x < zone().width && at.y < zone().height;
}

std::uint16_t World::tileIn(int zoneIndex, Position at, int layer) const {
    const auto& z = data_.zones.at(static_cast<std::size_t>(zoneIndex));
    if (at.x < 0 || at.y < 0 || at.x >= z.width || at.y >= z.height) {
        return data::kNoTile;
    }
    const auto* s = findState(zoneIndex);
    const auto& grid = s != nullptr ? s->cells : z.cells;
    return grid[cellIndex(z, at)].layers[static_cast<std::size_t>(layer)];
}

std::uint16_t World::tile(Position at, int layer) const { return tileIn(zone_, at, layer); }

bool World::walkableIn(int zoneIndex, Position at) const {
    const auto& z = data_.zones.at(static_cast<std::size_t>(zoneIndex));
    if (at.x < 0 || at.y < 0 || at.x >= z.width || at.y >= z.height) {
        return false;
    }
    const std::uint16_t floor = tileIn(zoneIndex, at, kFloorLayer);
    if (floor != data::kNoTile && (flags(floor) & flags_::kBlocking) != 0) {
        return false;
    }
    const std::uint16_t object = tileIn(zoneIndex, at, kObjectLayer);
    if (object == data::kNoTile || (flags(object) & flags_::kBlocking) == 0) {
        return true;
    }
    return ladderEnd(zoneIndex, at);
}

bool World::ladderEnd(int zoneIndex, Position at) const {
    // Indy: the cell at the top or bottom of a ladder often has the lip of
    // the cliff on it. That lip is scenery: the ladder leads over it, so it
    // does not stop the hero (or the only way into a cliff maze is cut off).
    if (data_.layout() != data::Layout::Indy) {
        return false;
    }
    const std::uint32_t lip = flags(tileIn(zoneIndex, at, kObjectLayer));
    constexpr std::uint32_t kNotScenery = flags_::kCharacter | flags_::kItem | flags_::kWeapon | flags_::kDraggable;
    if ((lip & kNotScenery) != 0) {
        return false;
    }
    for (int dy : {-1, 1}) {
        const Position next{at.x, at.y + dy};
        const std::uint16_t floor = tileIn(zoneIndex, next, kFloorLayer);
        if (tileIn(zoneIndex, next, kObjectLayer) == data::kNoTile &&
            std::find(std::begin(kIndyLadders), std::end(kIndyLadders), floor) != std::end(kIndyLadders)) {
            return true;
        }
    }
    return false;
}

bool World::walkable(Position at) const { return walkableIn(zone_, at); }

const data::Hotspot* World::hotspotAt(Position at, HotspotType type) const {
    if (!zone().hotspots) {
        return nullptr;
    }
    const auto& list = *zone().hotspots;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& h = list[i];
        if (hotspotEnabled(i) && h.type == static_cast<std::uint32_t>(type) && h.x == at.x && h.y == at.y) {
            return &h;
        }
    }
    return nullptr;
}

Position World::startPosition(int zoneIndex) const {
    const auto& z = data_.zones.at(static_cast<std::size_t>(zoneIndex));
    if (z.hotspots) {
        for (const auto& h : *z.hotspots) {
            if (h.type == static_cast<std::uint32_t>(HotspotType::DoorOut)) {
                return {h.x, h.y};
            }
        }
    }
    return {z.width / 2, z.height / 2};
}

void World::enter(int zoneIndex, Position at) {
    if (zoneIndex < 0 || static_cast<std::size_t>(zoneIndex) >= data_.zones.size()) {
        throw data::DataError("there is no zone " + std::to_string(zoneIndex));
    }
    zone_ = zoneIndex;
    (void)state(); // places the zone's characters and items
    player_ = at;
    if (!inside(player_)) {
        player_ = {zone().width / 2, zone().height / 2};
    }
    overlays_.clear();
    shots_.clear();
    // Scripts of the zone we left do not continue here.
    passes_.clear();
    pendingZone_.reset();
    paused_ = false;
    zoneChanged_ = true;
    trigger(script::Event::Enter);
    trigger(script::Event::Update);
    // The originals run the Update scripts once more when the change of
    // zone has finished.
    trigger(script::Event::Update);
    runScripts();
}

void World::start(int zoneIndex) {
    returns_.clear();
    enter(zoneIndex, startPosition(zoneIndex));
    if (walkable(player_) || hotspotAt(player_, HotspotType::DoorOut)) {
        return;
    }
    // Closest walkable cell to the middle.
    Position best = player_;
    int bestDistance = std::numeric_limits<int>::max();
    for (int y = 0; y < zone().height; ++y) {
        for (int x = 0; x < zone().width; ++x) {
            const int distance = std::abs(x - player_.x) + std::abs(y - player_.y);
            if (distance < bestDistance && walkable({x, y})) {
                best = {x, y};
                bestDistance = distance;
            }
        }
    }
    player_ = best;
}

Position World::nearWalkable(int zoneIndex, Position at) const {
    const auto& z = data_.zones.at(static_cast<std::size_t>(zoneIndex));
    const int most = z.width + z.height;
    for (int d = 1; d <= most; ++d) {
        // Below first: ships and doors face the player that way.
        for (int dy = d; dy >= -d; --dy) {
            const int rest = d - std::abs(dy);
            for (const int dx : {rest, -rest}) {
                const Position p{at.x + dx, at.y + dy};
                if (walkableIn(zoneIndex, p)) {
                    return p;
                }
                if (rest == 0) {
                    break;
                }
            }
        }
    }
    return {z.width / 2, z.height / 2};
}

bool World::leaveEdge(Step step) {
    const Map* m = nullptr;
    int x = 0;
    int y = 0;
    if (!map_.locate(zone_, m, x, y) || !m->inside(x + step.dx, y + step.dy)) {
        return false;
    }
    const int to = m->at(x + step.dx, y + step.dy).zone;
    if (to < 0) {
        return false;
    }
    const auto& z = data_.zones.at(static_cast<std::size_t>(to));
    Position at = player_;
    if (step.dx != 0) {
        at.x = step.dx > 0 ? 0 : z.width - 1;
    }
    if (step.dy != 0) {
        at.y = step.dy > 0 ? 0 : z.height - 1;
    }
    if (!walkableIn(to, at)) {
        return false;
    }
    returns_.clear();
    enter(to, at);
    ++steps_;
    return true;
}

bool World::fly(Position target) {
    if (!zone().hotspots) {
        return false;
    }
    // The town the ship flies to: the one on the map, if there is a map.
    int town = -1;
    for (const auto& s : map_.world.sites) {
        if (s.kind == SiteKind::Town) {
            town = s.zone;
        }
    }
    const auto& list = *zone().hotspots;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& h = list[i];
        if (!hotspotEnabled(i) || h.x != target.x || h.y != target.y || h.argument >= data_.zones.size()) {
            continue;
        }
        HotspotType arrival;
        if (h.type == static_cast<std::uint32_t>(HotspotType::ShipFromDagobah) && (town < 0 || h.argument == town)) {
            arrival = HotspotType::ShipToDagobah;
        } else if (h.type == static_cast<std::uint32_t>(HotspotType::ShipToDagobah)) {
            arrival = HotspotType::ShipFromDagobah;
        } else {
            continue;
        }
        const int to = h.argument;
        const auto& z = data_.zones[static_cast<std::size_t>(to)];
        Position at{z.width / 2, z.height / 2};
        if (z.hotspots) {
            for (const auto& other : *z.hotspots) {
                if (other.type == static_cast<std::uint32_t>(arrival)) {
                    at = {other.x, other.y};
                    break;
                }
            }
        }
        returns_.clear();
        enter(to, nearWalkable(to, at));
        ++steps_;
        return true;
    }
    return false;
}

std::string World::puzzleText(int puzzle, data::PuzzleText which) const {
    if (puzzle < 0 || static_cast<std::size_t>(puzzle) >= puzzles_.size()) {
        return {};
    }
    return puzzles_[static_cast<std::size_t>(puzzle)].text(which);
}

void World::found(std::uint16_t item) {
    addItem(item);
    effects_.push_back({Effect::Kind::ItemFound, {}, {}, item});
    // Taking the item a site hands out solves the site.
    if (goal(zone_).findItem == item) {
        setSolved(zone_);
    }
}

bool World::bumpBuiltIn(Position target) {
    auto& s = state();
    // Something hidden in the object.
    if (const auto it = s.hidden.find({target.x, target.y}); it != s.hidden.end()) {
        const std::uint16_t item = it->second;
        s.hidden.erase(it);
        found(item);
        return true;
    }
    const std::uint16_t object = tile(target, kObjectLayer);
    if (object == data::kNoTile) {
        return false;
    }
    const Site* here = site(zone_);
    // Indy: the mission giver tells the mission and, the first time, hands
    // over the item the first puzzle asks for; later he repeats the hint.
    if (data_.layout() == data::Layout::Indy && object == kIndyMissionGiver && here != nullptr &&
        here->kind == SiteKind::Town && map_.goalPuzzle >= 0) {
        const bool briefed = goal(zone_).solved;
        const std::string raw =
            puzzleText(map_.goalPuzzle, briefed ? data::PuzzleText::Offer : data::PuzzleText::Mission);
        if (!raw.empty()) {
            effects_.push_back({Effect::Kind::Speech, speechText(raw), target, 0});
            paused_ = true;
        }
        if (!briefed) {
            setSolved(zone_);
            if (map_.startItem != data::kNoTile) {
                found(map_.startItem);
            }
        }
        return true;
    }
    // A trading character.
    if (here != nullptr && here->npcZone == zone_ && object == here->npc) {
        const ZoneGoal g = goal(zone_);
        // A helper who asks for nothing (Yoda on Dagobah) hands the item over.
        if (!g.solved && g.requiredItem == data::kNoTile && g.findItem != data::kNoTile) {
            const std::string offer = puzzleText(here->offerPuzzle, data::PuzzleText::Offer);
            if (!offer.empty()) {
                effects_.push_back({Effect::Kind::Speech, speechText(offer), target, 0});
                paused_ = true;
            }
            setSolved(zone_);
            found(g.findItem);
            return true;
        }
        std::string raw;
        if (g.solved) {
            raw = puzzleText(here->requestPuzzle, data::PuzzleText::Thanks);
        } else {
            raw = puzzleText(here->requestPuzzle, data::PuzzleText::Request);
            const std::string offer = puzzleText(here->offerPuzzle, data::PuzzleText::Offer);
            if (!offer.empty()) {
                raw += raw.empty() ? offer : " " + offer;
            }
        }
        if (raw.empty()) {
            return false;
        }
        effects_.push_back({Effect::Kind::Speech, speechText(raw), target, 0});
        paused_ = true;
        return true;
    }
    // An item to pick up.
    if ((flags(object) & (flags_::kItem | flags_::kWeapon)) != 0) {
        setTile(target, kObjectLayer, data::kNoTile);
        found(object);
        return true;
    }
    return false;
}

bool World::useBuiltIn(std::uint16_t item, Position at) {
    const Site* here = site(zone_);
    if (here == nullptr || here->npcZone != zone_ || tile(at, kObjectLayer) != here->npc) {
        return false;
    }
    const ZoneGoal g = goal(zone_);
    if (g.solved || (item != g.requiredItem && item != g.requiredItem2)) {
        return false;
    }
    inventory_.erase(std::find(inventory_.begin(), inventory_.end(), item));
    setSolved(zone_);
    const std::string thanks = puzzleText(here->requestPuzzle, data::PuzzleText::Thanks);
    if (g.findItem != data::kNoTile) {
        found(g.findItem);
    }
    if (!thanks.empty()) {
        effects_.push_back({Effect::Kind::Speech, speechText(thanks), at, 0});
        paused_ = true;
    }
    return true;
}

MoveResult World::move(Step step) {
    if (paused_ || drop_ || outcome_ != Outcome::Playing) {
        return MoveResult::Blocked;
    }
    facing_ = facingFor(step);
    const Position target{player_.x + step.dx, player_.y + step.dy};
    if (!inside(target)) {
        return leaveEdge(step) ? MoveResult::ChangedZone : MoveResult::Blocked;
    }
    if (fly(target)) {
        return MoveResult::ChangedZone;
    }

    if (const auto* door = hotspotAt(target, HotspotType::DoorIn)) {
        if (door->argument < data_.zones.size()) {
            returns_.push_back({zone_, target});
            const int to = door->argument;
            carrySharedCounter(to);
            enter(to, startPosition(to));
            ++steps_;
            return MoveResult::ChangedZone;
        }
    }
    if (hotspotAt(target, HotspotType::DoorOut) && !returns_.empty()) {
        const Return back = returns_.back();
        returns_.pop_back();
        carrySharedCounter(back.zone);
        enter(back.zone, back.at);
        ++steps_;
        return MoveResult::ChangedZone;
    }

    if (monsterAt(target)) {
        return MoveResult::Blocked;
    }
    if (walkable(target)) {
        player_ = target;
        ++steps_;
        zoneChanged_ = false;
        trigger(script::Event::Walk, {target.x, target.y, 0, 0, -1});
        runScripts();
        return zoneChanged_ ? MoveResult::ChangedZone : MoveResult::Moved;
    }

    // Searching an object with something hidden in it comes before pushing it.
    const bool hides = state().hidden.contains({target.x, target.y});

    // A draggable block moves one cell ahead when that cell is free.
    const std::uint16_t block = tile(target, kObjectLayer);
    if (!hides && block != data::kNoTile && (flags(block) & flags_::kDraggable) != 0) {
        const Position beyond{target.x + step.dx, target.y + step.dy};
        if (inside(beyond) && walkable(beyond) && tile(beyond, kObjectLayer) == data::kNoTile) {
            auto& grid = cells();
            grid[cellIndex(zone(), beyond)].layers[kObjectLayer] = block;
            grid[cellIndex(zone(), target)].layers[kObjectLayer] = data::kNoTile;
            player_ = target;
            ++steps_;
            return MoveResult::Pushed;
        }
    }

    zoneChanged_ = false;
    const std::size_t effectsBefore = effects_.size();
    trigger(script::Event::Bump, {player_.x, player_.y, step.dx, step.dy, -1});
    runScripts();
    if (zoneChanged_) {
        return MoveResult::ChangedZone;
    }
    const bool scriptSpoke = std::any_of(effects_.begin() + static_cast<std::ptrdiff_t>(effectsBefore), effects_.end(),
                                         [](const Effect& e) { return e.kind == Effect::Kind::Speech; });
    if (!paused_ && !scriptSpoke && outcome_ == Outcome::Playing) {
        bumpBuiltIn(target);
    }
    return MoveResult::Blocked;
}

bool World::useItem(std::uint16_t item, Position at) {
    if (paused_ || drop_ || outcome_ != Outcome::Playing || !inside(at) || !hasItem(item)) {
        return false;
    }
    reacted_ = false;
    trigger(script::Event::PlaceItem, {at.x, at.y, 0, 0, item});
    runScripts();
    if (!reacted_ && !paused_ && outcome_ == Outcome::Playing) {
        return useBuiltIn(item, at);
    }
    return reacted_;
}

} // namespace das::world
