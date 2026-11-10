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
#include "das/data/puzzle.hpp"
#include "das/input.hpp"
#include "das/script/opcodes.hpp"
#include "das/util/json.hpp"
#include "das/world/generator.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace das::world {

// Hotspot types we act on so far (see docs/formats/zone-records.md).
enum class HotspotType : std::uint32_t {
    ItemSpot = 0,  // where the generator puts a site's find item
    Spawn = 1,     // where a trading character stands
    CrateItem = 6, // argument: an item lying here or hidden in the object
    DoorIn = 9,    // argument: the zone behind the door
    DoorOut = 10,  // leads back to where the player came from
    ShipFromDagobah = 14, // argument: the town zone it flies to
    ShipToDagobah = 15,   // argument: the Dagobah zone it flies to
};

struct Position {
    int x = 0;
    int y = 0;
    friend bool operator==(const Position&, const Position&) = default;
};

inline constexpr int kMaxHealth = 300;

// A character from the data as the combat rules see it (see docs/combat.md).
struct Fighter {
    data::Character character;
    int health = 0;            // CHWP value; 0: cannot be hurt
    int damage = 0;            // CAUX value; 0: does no harm
    int weapon = -1;           // enemies: the weapon character they use (CHWP reference)
    int sound = -1;            // weapons: the sound they make (CHWP reference)
    std::uint16_t item = 0xffff; // weapons: the inventory tile
};

// A monster as the app draws it.
struct MonsterView {
    std::size_t index = 0; // into Zone::monsters
    Position at;
    std::uint16_t tile = 0xffff;
};

// Something flying: a shot from the hero or a monster.
struct Shot {
    Position at;
    int dx = 0;
    int dy = 0;
    std::uint16_t tile = 0xffff;
    int damage = 0;
    int range = 0;    // cells left
    bool fromHero = false;
};

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
    enum class Kind { Speech, Wait, Sound, StopSound, Won, Lost, ItemFound };
    Kind kind = Kind::Speech;
    std::string text;          // Speech: UTF-8, lines separated by '\n'
    std::optional<Position> at; // Speech: the speaker's cell; none: the hero
    int value = 0;             // Wait: milliseconds; Sound: sound index; ItemFound: the tile
};

enum class Outcome { Playing, Won, Lost };

enum class MoveResult {
    Moved,
    Pushed,  // moved and pushed a block ahead
    Blocked,
    ChangedZone,
};

namespace detail {

struct MonsterState {
    Position at;
    int health = 0;
    data::Facing facing = data::Facing::Down;
    int waypoint = 0;
    int moveMs = 0;   // until the next step
    int attackMs = 0; // until the next attack
    int frame = 0;
};

struct ZoneState {
    std::vector<data::ZoneCell> cells;
    std::vector<MonsterState> monsters;
    // Items hidden in an object (a crate, a bush); bumping it finds them.
    std::map<std::pair<int, int>, std::uint16_t> hidden;
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
    // Takes a generated world: the zones' goals, the map for walking from
    // zone to zone, the trading characters and items. Then start() the
    // game in map.startZone, or call begin().
    void setMap(WorldMap map);
    // Indy also starts with his whip (the first weapon in the data).
    void begin();
    const WorldMap& map() const { return map_; }
    void enter(int zone, Position at);

    MoveResult move(Step step);
    // Uses an inventory item on a cell (next to the hero). Returns whether a
    // script reacted.
    bool useItem(std::uint16_t item, Position at);

    // Combat (docs/combat.md). update() lets time pass: monsters move and
    // attack, shots fly.
    void update(int ms);
    // The hero attacks in the direction he faces with the weapon in hand.
    bool attack();
    // Takes a weapon from the inventory in hand; returns false for other items.
    bool equip(std::uint16_t item);
    std::uint16_t weapon() const { return weapon_; }
    bool isWeapon(std::uint16_t item) const;
    std::vector<MonsterView> monsters() const;
    const std::vector<Shot>& shots() const { return shots_; }
    // Frames of an attack still showing, for the hero's attack pose.
    bool attacking() const { return attackShowMs_ > 0; }
    const std::vector<Fighter>& fighters() const { return fighters_; }
    // How much harder monsters hit: 0 (easy) to 100 (hard); 50 is normal.
    void setDifficulty(int difficulty) { difficulty_ = std::clamp(difficulty, 0, 100); }
    int difficulty() const { return difficulty_; }

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
    void setHealth(int health) { health_ = std::clamp(health, 0, kMaxHealth); }
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

    // Zones on one map site share their goal.
    void setGoal(int zone, ZoneGoal goal);
    ZoneGoal goal(int zone) const;
    void setGamesWon(int count) { gamesWon_ = count; }
    void setEnding(int ending) { ending_ = ending; }
    void setMissionDone(bool done) { mission_ = done ? 1 : -1; }
    void setRandomSeed(std::uint32_t seed) { rng_.seed(seed); }

    // Everything play changed, for a save game (docs/savegames.md). The map
    // itself is not in it: it is made again from the generator options.
    // Saving is only possible while no script is paused.
    bool canSave() const { return !paused_ && passes_.empty(); }
    json::Value saveState() const;
    // Restores a saved state on a world that got the same map with setMap().
    // Throws json::Error on a damaged save.
    void loadState(const json::Value& state);

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
    using MonsterState = detail::MonsterState;

    ZoneState& state();
    const ZoneState* findState(int zone) const;
    ZoneState& stateOf(int zone);
    void trigger(script::Event event, EventArgs args = {});
    void runScripts();
    bool conditionHolds(const data::ScriptLine& line, const Pass& pass);
    // Returns false when the scripts have to pause.
    bool execute(const data::ScriptLine& line, Pass& pass);
    void changeZone(int zone, Position at);
    bool walkableIn(int zone, Position at) const;
    std::uint16_t tileIn(int zone, Position at, int layer) const;
    const Site* site(int zone) const;
    void setSolved(int zone);
    void prepareZone(int zone, ZoneState& state);
    bool leaveEdge(Step step);
    bool fly(Position target);
    Position nearWalkable(int zone, Position at) const;
    // The hero's own reactions when no script reacted.
    bool bumpBuiltIn(Position target);
    bool useBuiltIn(std::uint16_t item, Position at);
    void found(std::uint16_t item);
    std::string puzzleText(int puzzle, data::PuzzleText which) const;
    void setTile(Position at, int layer, std::uint16_t tile);
    std::string speechText(const std::string& raw) const;
    std::string tileName(std::uint16_t tile) const;
    // Combat helpers (combat.cpp).
    void loadFighters();
    void initMonsters(int zone, ZoneState& state);
    const Fighter* fighter(int character) const;
    const Fighter* weaponFighter(std::uint16_t item) const;
    bool monsterAt(Position at, std::size_t* index = nullptr) const;
    bool freeForMonster(Position at) const;
    void stepMonster(std::size_t index, MonsterState& m, const Fighter& f);
    void hurtHero(int damage);
    void hurtMonster(std::size_t index, int damage);
    bool lineClear(Position from, Position to) const;
    void fire(Position from, int dx, int dy, const Fighter& weapon, int damage, bool fromHero);
    void moveShots();

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
    std::vector<data::Puzzle> puzzles_;
    WorldMap map_;
    std::unordered_map<int, int> zoneSite_; // zone -> index into goals_
    std::vector<ZoneGoal> goals_;
    std::vector<Site> sites_;              // parallel to goals_

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

    std::vector<Fighter> fighters_; // by character index
    std::uint16_t weapon_ = data::kNoTile;
    std::vector<Shot> shots_;
    int shotMs_ = 0;
    int attackShowMs_ = 0;
    int attackCooldownMs_ = 0;
    int difficulty_ = 50;
};

} // namespace das::world
