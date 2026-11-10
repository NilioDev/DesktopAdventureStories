// SPDX-License-Identifier: GPL-3.0-or-later
//
// Script opcodes. The two games number their opcodes differently; we use
// one set of names (numbered as in Yoda Stories) and translate Indy's
// numbers when reading. See docs/formats/scripts.md.
#pragma once

#include "das/data/container.hpp"

#include <cstdint>
#include <string_view>

namespace das::script {

// What made the scripts run. Conditions such as Bump only hold for their
// own event.
enum class Event {
    Walk = 1,      // the hero stepped onto a cell: x, y
    Bump = 2,      // the hero walked against a cell: x, y, dx, dy
    PlaceItem = 3, // an item was used on a cell: x, y, item
    Enter = 4,     // the hero entered the zone
    Update = 5,    // after entering, and after a speech bubble was closed
};

enum class Condition : std::uint16_t {
    FirstEnter = 0,             // Enter, the first time this zone is entered
    Enter = 1,                  // Enter
    Bump = 2,                   // x, y, tile: bumped into tile at x, y (object layer)
    PlaceItem = 3,              // x, y, layer, tile, item: item used on tile
    StandingOn = 4,             // x, y, tile: walked onto x, y with this floor tile
    CounterIs = 5,              // value
    RandomIs = 6,               // value
    RandomIsGreaterThan = 7,    // value
    RandomIsLessThan = 8,       // value
    Update = 9,                 // the Update event
    TileIs = 10,                // tile, x, y, layer
    MonsterIsDead = 11,         // monster index
    NoMonstersActive = 12,      //
    HasItem = 13,               // item (-1: the zone's find item)
    RequiredItemIs = 14,        // item: one of the zone's required items
    EndingIs = 15,              // value: which ending this game uses
    ZoneIsSolved = 16,          //
    MissionNotDone = 17,        // the mission state is "not done"
    MissionDone = 18,           // the mission state is "done"
    HealthIsLessThan = 19,      // value
    HealthIsGreaterThan = 20,   // value
    Unused21 = 21,              // never used, always fails
    FindItemIs = 22,            // item: the zone's find item
    PlaceItemIsNot = 23,        // x, y, layer, tile, item: a different item used on tile
    HeroIsAt = 24,              // x, y
    SharedCounterIs = 25,       // value
    SharedCounterIsLessThan = 26,
    SharedCounterIsGreaterThan = 27,
    GamesWonIs = 28,            // value
    HasQuestHotspotAt = 29,     // x, y: an enabled hotspot of type 0 is there
    HasRequiredItem = 30,       // the hero carries one of the zone's required items
    CounterIsNot = 31,
    RandomIsNot = 32,
    SharedCounterIsNot = 33,
    VariableIs = 34,            // value, x, y, layer (same as TileIs)
    GamesWonIsGreaterThan = 35,
    // Indiana Jones only.
    RandomIsSet = 100,          // the random value is not negative
    Always = 101,               // always holds
    Unknown = 0xffff,
};

enum class Instruction : std::uint16_t {
    PlaceTile = 0,         // x, y, layer, tile
    RemoveTile = 1,        // x, y, layer
    MoveTile = 2,          // x, y, layer, to x, to y
    DrawTile = 3,          // x, y, tile: draw only, the zone is unchanged
    SpeakHero = 4,         // text
    SpeakNpc = 5,          // x, y, text
    RedrawTile = 6,        // x, y
    RedrawTiles = 7,       // x1, y1, x2, y2
    Render = 8,
    Wait = 9,              // tenths of a second
    PlaySound = 10,        // sound index
    StopSound = 11,
    RollDice = 12,         // n: random = 1..n
    SetCounter = 13,       // value
    AddToCounter = 14,     // value
    SetVariable = 15,      // x, y, layer, value (same as PlaceTile)
    HideHero = 16,
    ShowHero = 17,
    SetHeroPosition = 18,  // x, y
    MoveHeroTo = 19,       // from x, from y (-1: hero), to x, to y, tenths of a second per step
    DisableAction = 20,
    EnableHotspot = 21,    // hotspot index
    DisableHotspot = 22,   // hotspot index
    EnableMonster = 23,    // monster index
    DisableMonster = 24,   // monster index
    EnableAllMonsters = 25,
    DisableAllMonsters = 26,
    DropItem = 27,         // item (-1: the zone's find item), x, y
    AddItem = 28,          // item
    RemoveItem = 29,       // item (-1: the zone's required item)
    MarkAsSolved = 30,
    WinGame = 31,
    LoseGame = 32,
    ChangeZone = 33,       // zone, x, y
    SetSharedCounter = 34, // value
    AddToSharedCounter = 35,
    SetRandom = 36,        // value
    AddHealth = 37,        // value (may be negative)
    // Indiana Jones only.
    RedrawZone = 100,
    Nothing = 101,
    Unknown = 0xffff,
};

// Translate a game's own opcode number.
Condition condition(data::Layout game, std::uint16_t opcode);
Instruction instruction(data::Layout game, std::uint16_t opcode);
// The game's own number for an opcode, or -1 when that game has no such
// opcode.
int opcode(data::Layout game, Condition c);
int opcode(data::Layout game, Instruction i);

std::string_view name(Condition c);
std::string_view name(Instruction i);

} // namespace das::script
