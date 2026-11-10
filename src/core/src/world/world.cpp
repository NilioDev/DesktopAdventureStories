// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/world/world.hpp"

#include "das/data/tile_flags.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace das::world {

namespace flags_ = data::tile_flags;

namespace {

constexpr int kFloorLayer = 0;
constexpr int kObjectLayer = 1;

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

World::World(const data::GameData& data) : data_(data) {
    if (data_.zones.empty()) {
        throw data::DataError("the game data has no zones");
    }
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
    const auto it = goals_.find(zoneIndex);
    return it == goals_.end() ? ZoneGoal{} : it->second;
}

bool World::hasItem(std::uint16_t item) const {
    return std::find(inventory_.begin(), inventory_.end(), item) != inventory_.end();
}

void World::addItem(std::uint16_t item) {
    if (!hasItem(item)) {
        inventory_.push_back(item);
    }
}

std::uint32_t World::flags(std::uint16_t tile) const {
    return tile < data_.tiles.size() ? data_.tiles[tile].flags : 0;
}

bool World::inside(Position at) const {
    return at.x >= 0 && at.y >= 0 && at.x < zone().width && at.y < zone().height;
}

std::uint16_t World::tile(Position at, int layer) const {
    if (!inside(at)) {
        return data::kNoTile;
    }
    return cells()[cellIndex(zone(), at)].layers[static_cast<std::size_t>(layer)];
}

bool World::walkable(Position at) const {
    if (!inside(at)) {
        return false;
    }
    for (int layer : {kFloorLayer, kObjectLayer}) {
        const std::uint16_t t = tile(at, layer);
        if (t != data::kNoTile && (flags(t) & flags_::kBlocking) != 0) {
            return false;
        }
    }
    return true;
}

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
    player_ = at;
    if (!inside(player_)) {
        player_ = {zone().width / 2, zone().height / 2};
    }
    overlays_.clear();
    // Scripts of the zone we left do not continue here.
    passes_.clear();
    paused_ = false;
    zoneChanged_ = true;
    chainedUpdates_ = 0;
    trigger(script::Event::Enter);
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

MoveResult World::move(Step step) {
    if (paused_ || outcome_ != Outcome::Playing) {
        return MoveResult::Blocked;
    }
    facing_ = facingFor(step);
    const Position target{player_.x + step.dx, player_.y + step.dy};
    if (!inside(target)) {
        // Walking off the edge of an outdoor zone needs the world map, which
        // comes with the world generator.
        return MoveResult::Blocked;
    }

    if (const auto* door = hotspotAt(target, HotspotType::DoorIn)) {
        if (door->argument < data_.zones.size()) {
            returns_.push_back({zone_, target});
            const int to = door->argument;
            enter(to, startPosition(to));
            ++steps_;
            return MoveResult::ChangedZone;
        }
    }
    if (hotspotAt(target, HotspotType::DoorOut) && !returns_.empty()) {
        const Return back = returns_.back();
        returns_.pop_back();
        enter(back.zone, back.at);
        ++steps_;
        return MoveResult::ChangedZone;
    }

    if (walkable(target)) {
        player_ = target;
        ++steps_;
        zoneChanged_ = false;
        trigger(script::Event::Walk, {target.x, target.y, 0, 0, -1});
        runScripts();
        return zoneChanged_ ? MoveResult::ChangedZone : MoveResult::Moved;
    }

    // A draggable block moves one cell ahead when that cell is free.
    const std::uint16_t block = tile(target, kObjectLayer);
    if (block != data::kNoTile && (flags(block) & flags_::kDraggable) != 0) {
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
    trigger(script::Event::Bump, {player_.x, player_.y, step.dx, step.dy, -1});
    runScripts();
    return zoneChanged_ ? MoveResult::ChangedZone : MoveResult::Blocked;
}

bool World::useItem(std::uint16_t item, Position at) {
    if (paused_ || outcome_ != Outcome::Playing || !inside(at)) {
        return false;
    }
    reacted_ = false;
    trigger(script::Event::PlaceItem, {at.x, at.y, 0, 0, item});
    runScripts();
    return reacted_;
}

} // namespace das::world
