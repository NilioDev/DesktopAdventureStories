// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/world/world.hpp"

#include "das/data/tile_flags.hpp"

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

const std::vector<data::ZoneCell>& World::cells() const {
    const auto it = changed_.find(zone_);
    return it != changed_.end() ? it->second : zone().cells;
}

std::vector<data::ZoneCell>& World::cells() {
    auto it = changed_.find(zone_);
    if (it == changed_.end()) {
        it = changed_.emplace(zone_, zone().cells).first;
    }
    return it->second;
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
    for (const auto& h : *zone().hotspots) {
        if (h.enabled != 0 && h.type == static_cast<std::uint32_t>(type) && h.x == at.x && h.y == at.y) {
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
        return MoveResult::Moved;
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
    return MoveResult::Blocked;
}

} // namespace das::world
