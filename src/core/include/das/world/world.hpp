// SPDX-License-Identifier: GPL-3.0-or-later
//
// The running game world: which zone the player is in, where they stand,
// the zones as changed by play, and the zone scripts. Platform independent.
//
// Scripts run when something happens (entering a zone, walking, bumping into
// something, using an item). Some instructions need the app: a speech bubble
// or a pause. The world then hands out an Effect and waits until the app
// calls resume(); see effects().
#pragma once

#include "das/data/character.hpp"
#include "das/data/game_data.hpp"
#include "das/input.hpp"
#include "das/script/opcodes.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <random>
#include <string>
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

inline constexpr int kMaxHealth = 300;

// What the world generator decides for a zone: which items its puzzle asks
// for and hands out. Scripts refer to these.
struct ZoneGoal {
    std::uint16_t requiredItem = data::kNoTile;
    std::uint16_t requiredItem2 = data::kNoTile;
    std::uint16_t findItem = data::kNoTile;
    bool solved = false;
};

// Something the app has to show or play. Speech and Wait pause the scripts
// until resume().
struct Effect {
    enum class Kind { Speech, Wait, Sound, StopSound, Won, Lost };
    Kind kind = Kind::Speech;
    std::string text;          // Speech: UTF-8, lines separated by '\n'
    std::optional<Position> at; // Speech: the speaker's cell; none: the hero
    int value = 0;             // Wait: milliseconds; Sound: sound index
};

enum class Outcome { Playing, Won, Lost };

enum class MoveResult {
    Moved,
    Pushed,  // moved and pushed a block ahead
    Blocked,
    ChangedZone,
};

namespace detail {

struct ZoneState {
    std::vector<data::ZoneCell> cells;
    std::vector<bool> hotspotEnabled;
    std::vector<bool> actionDisabled;
    std::vector<bool> monsterEnabled;
    std::vector<bool> monsterDead;
    bool initialized = false;
    int counter = 0;
    int random = 0;
    int sharedCounter = 0;
};

struct EventArgs {
    int x = 0;
    int y = 0;
    int dx = 0;
    int dy = 0;
    int item = -1;
};

// A run over a zone's scripts for one event; it can pause in the middle
// of an action and continue later.
struct Pass {
    script::Event event = script::Event::Update;
    EventArgs args;
    std::size_t action = 0;
    std::size_t instruction = 0;
    bool inAction = false;
    bool disableAction = false;
    bool spoke = false;
    bool moving = false; // in the middle of MoveHeroTo
};

} // namespace detail

class World {
public:
    explicit World(const data::GameData& data);

    // Puts the player in a zone: on its exit door if it has one, otherwise
    // on the walkable cell closest to the middle.
    void start(int zone);
    void enter(int zone, Position at);

    MoveResult move(Step step);
    // Uses an inventory item on a cell (next to the hero). Returns whether a
    // script reacted.
    bool useItem(std::uint16_t item, Position at);

    // Effects produced since the last call. When the last one pauses the
    // scripts (Speech, Wait), call resume() once it is done.
    std::vector<Effect> takeEffects();
    bool paused() const { return paused_; }
    void resume();

    int zoneIndex() const { return zone_; }
    const data::Zone& zone() const { return data_.zones.at(static_cast<std::size_t>(zone_)); }
    Position player() const { return player_; }
    data::Facing facing() const { return facing_; }
    // Alternates on every step, for the walk animation.
    int stepCount() const { return steps_; }
    std::size_t doorDepth() const { return returns_.size(); }

    // Game state that scripts read and change.
    const std::vector<std::uint16_t>& inventory() const { return inventory_; }
    bool hasItem(std::uint16_t item) const;
    void addItem(std::uint16_t item);
    int health() const { return health_; }
    bool heroVisible() const { return heroVisible_; }
    Outcome outcome() const { return outcome_; }
    int counter() const;
    int random() const;
    int sharedCounter() const;
    bool hotspotEnabled(std::size_t index) const;
    bool monsterEnabled(std::size_t index) const;
    bool actionDisabled(std::size_t index) const;
    // A tile drawn over the zone by a script (DrawTile) until the next zone change.
    const std::vector<std::pair<Position, std::uint16_t>>& overlays() const { return overlays_; }

    void setGoal(int zone, ZoneGoal goal) { goals_[zone] = goal; }
    ZoneGoal goal(int zone) const;
    void setGamesWon(int count) { gamesWon_ = count; }
    void setEnding(int ending) { ending_ = ending; }
    void setMissionDone(bool done) { mission_ = done ? 1 : -1; }
    void setRandomSeed(std::uint32_t seed) { rng_.seed(seed); }

    // The tile at a position and layer, including changes made by play.
    std::uint16_t tile(Position at, int layer) const;
    bool inside(Position at) const;
    bool walkable(Position at) const;

private:
    struct Return {
        int zone;
        Position at;
    };

    using ZoneState = detail::ZoneState;
    using EventArgs = detail::EventArgs;
    using Pass = detail::Pass;

    ZoneState& state();
    const ZoneState* findState(int zone) const;
    ZoneState& stateOf(int zone);
    void trigger(script::Event event, EventArgs args = {});
    void runScripts();
    bool conditionHolds(const data::ScriptLine& line, const Pass& pass);
    // Returns false when the scripts have to pause.
    bool execute(const data::ScriptLine& line, Pass& pass);
    void changeZone(int zone, Position at);
    void setTile(Position at, int layer, std::uint16_t tile);
    std::string speechText(const std::string& raw) const;
    std::string tileName(std::uint16_t tile) const;

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
    std::unordered_map<int, ZoneState> states_;
    std::unordered_map<int, ZoneGoal> goals_;

    std::vector<std::uint16_t> inventory_;
    int health_ = kMaxHealth;
    bool heroVisible_ = true;
    Outcome outcome_ = Outcome::Playing;
    int gamesWon_ = 0;
    int ending_ = 0;
    int mission_ = 0;
    std::vector<std::pair<Position, std::uint16_t>> overlays_;

    std::deque<Pass> passes_;
    std::vector<Effect> effects_;
    bool paused_ = false;
    bool zoneChanged_ = false;
    bool reacted_ = false;
    int chainedUpdates_ = 0;
    std::mt19937 rng_{0x5eed};
};

} // namespace das::world
