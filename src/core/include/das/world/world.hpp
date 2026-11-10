// SPDX-License-Identifier: GPL-3.0-or-later
//
// The running game world: which zone the player is in, where they stand,
// and the zone tiles as changed by play (pushed blocks). Platform independent.
#pragma once

#include "das/data/character.hpp"
#include "das/data/game_data.hpp"
#include "das/input.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace das::world {

// Hotspot types we act on so far (see docs/formats/zone-records.md).
enum class HotspotType : std::uint32_t {
    DoorIn = 9,   // argument: the zone behind the door
    DoorOut = 10, // leads back to where the player came from
};

struct Position {
    int x = 0;
    int y = 0;
    friend bool operator==(const Position&, const Position&) = default;
};

enum class MoveResult {
    Moved,
    Pushed,  // moved and pushed a block ahead
    Blocked,
    ChangedZone,
};

class World {
public:
    explicit World(const data::GameData& data);

    // Puts the player in a zone: on its exit door if it has one, otherwise
    // on the walkable cell closest to the middle.
    void start(int zone);
    void enter(int zone, Position at);

    MoveResult move(Step step);

    int zoneIndex() const { return zone_; }
    const data::Zone& zone() const { return data_.zones.at(static_cast<std::size_t>(zone_)); }
    Position player() const { return player_; }
    data::Facing facing() const { return facing_; }
    // Alternates on every step, for the walk animation.
    int stepCount() const { return steps_; }
    std::size_t doorDepth() const { return returns_.size(); }

    // The tile at a position and layer, including changes made by play.
    std::uint16_t tile(Position at, int layer) const;
    bool inside(Position at) const;
    bool walkable(Position at) const;

private:
    struct Return {
        int zone;
        Position at;
    };

    std::vector<data::ZoneCell>& cells();
    const std::vector<data::ZoneCell>& cells() const;
    std::uint32_t flags(std::uint16_t tile) const;
    const data::Hotspot* hotspotAt(Position at, HotspotType type) const;
    Position startPosition(int zone) const;

    const data::GameData& data_;
    int zone_ = 0;
    Position player_{};
    data::Facing facing_ = data::Facing::Down;
    int steps_ = 0;
    std::vector<Return> returns_;
    // Zones changed by play, keyed by zone index.
    std::unordered_map<int, std::vector<data::ZoneCell>> changed_;
};

} // namespace das::world
