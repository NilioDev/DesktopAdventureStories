# Zone scripts

Every zone has a list of *actions* (IACT records, see
[zone-records.md](zone-records.md)). An action is a list of conditions and a
list of instructions. Each line has an opcode, five signed 16-bit arguments
and an optional text. Arguments an opcode does not use often hold leftover
bytes and must be ignored.

What follows is our own specification, written from observing how the
original engines treat each opcode (clean-room rule 2 in
[CONTRIBUTING.md](../../CONTRIBUTING.md)). The interpreter is
`src/core/src/world/scripts.cpp`; opcode names and the translation between the
two games are in `src/core/src/script/opcodes.cpp`.

## When scripts run

Scripts run in *passes*. A pass is started by an event and goes over the
zone's actions in order. For each action that is not disabled, all its
conditions are checked against the state at that moment; when they all hold,
its instructions run in order. Then the pass goes on with the next action, so
a later action sees what an earlier one changed.

| Event | Started when | Event values |
| --- | --- | --- |
| Walk | on every game tick, for the cell the hero stands on (we also run it right after a step) | x, y |
| Bump | the hero walked against something that blocks | hero x, y and the step dx, dy |
| PlaceItem | the player used an inventory item on a cell | x, y, item |
| Enter | the hero entered the zone | |
| Update | twice after Enter: once when the new zone is swapped in and once when the change of zone has finished | |

This was checked in both original programs (Yodesk.exe and the 16-bit
DESKADV.EXE). A speech bubble or a pause stops the pass in the middle of an
action; it continues once the bubble is closed or the time has passed. The
originals show their bubbles modally, inside the instruction, and do not
start another pass after one.

Because the hero's cell is checked on every tick, StandingOn and HeroIsAt
also notice a hero who stands still or whom a script put somewhere (the
landing on Dagobah needs this).

ChangeZone only asks for the change: the rest of the action and the rest of
the pass still run in the old zone (the data has DisableAction and tile
changes after a ChangeZone), and the hero goes to the new zone when the pass
is over.

## Zone state

Each zone keeps, for the rest of the game: its tiles, which hotspots and
monsters are enabled, which actions are disabled, whether it has been
entered before, and three numbers that scripts use: the *counter*, the
*random* value and the *shared counter*.

The counter starts at 0 and the random value and (Yoda Stories) the shared
counter at -1, so "RandomIs 0" or "SharedCounterIs 0" do not hold until a
script sets them (many zones do so on their first Enter). The shared counter
goes along when the hero changes zones through a door (in and out) or a
ChangeZone: when it is 0 or more, the zone he goes to gets the same value.
Walking off the edge to the next zone does not take it along.

"Variables" are tiles: SetVariable and VariableIs read and write the zone grid
like PlaceTile and TileIs do, usually on a layer or cell that is not shown.

The world generator gives each zone a *goal*: up to two *required items* (the
puzzle wants one of them), a *find item* (what solving it hands out) and a
*solved* flag. Several opcodes read these.

## Text

Texts use Windows-1252 and CR LF line breaks. Byte `0xA2` stands for the name
of the zone's required item, and `0xA5` for the name of its find item; names
come from the tile names (TNAM). When the name starts with a vowel and the text
just before it reads "a ", that becomes "an ".

## Opcodes

Both games have the same opcodes but number them differently. We number them
as Yoda Stories does; the Indy column gives Indiana Jones' own number.

### Conditions

| Yoda | Indy | Name | Arguments | Holds when |
| --- | --- | --- | --- | --- |
| 0 | 4 | FirstEnter | | Enter event, and the zone has not been entered before |
| 1 | 5 | Enter | | Enter event |
| 2 | 2 | Bump | x, y, tile | Bump event against x, y, and the object layer there holds tile |
| 3 | 3 | PlaceItem | x, y, layer, tile, item | PlaceItem event on x, y; the cell on layer holds tile (-1: any tile); the item used is item (-1: the zone's required item) |
| 4 | 1 | StandingOn | x, y, tile | Walk event onto x, y, whose floor tile is tile |
| 5 | 6 | CounterIs | value | counter = value |
| 6 | 13 | RandomIs | value | random = value |
| 7 | 12 | RandomIsGreaterThan | value | random > value |
| 8 | 14 | RandomIsLessThan | value | random < value |
| 9 | 16 | Update | | Update event |
| 10 | 19 | TileIs | tile, x, y, layer | the cell holds tile |
| 11 | 17 | MonsterIsDead | index | that placed character is dead |
| 12 | 18 | NoMonstersActive | | every placed character is dead |
| 13 | 10 | HasItem | item | the hero carries item (-1: the zone's find item) |
| 14 | 20 | RequiredItemIs | item | item is one of the zone's required items (Indy: the first one) |
| 15 | 21 | EndingIs | value | the game's chosen ending is value |
| 16 | 15 | ZoneIsSolved | | the zone's goal is solved |
| 17 | 8 | MissionNotDone | | the mission state is "not done" |
| 18 | 9 | MissionDone | | the mission state is "done" |
| 19 | 22 | HealthIsLessThan | value | health < value |
| 20 | | HealthIsGreaterThan | value | health > value |
| 21 | | | | never used; the original treats it as always true |
| 22 | | FindItemIs | item | item is the zone's find item |
| 23 | | PlaceItemIsNot | x, y, layer, tile, item | PlaceItem event on x, y; the cell holds tile or is empty; a different item than item was used, and not one of the special items 18, 510-514, 516, 517 and 794 (the lightsabers, the Force, the blasters, the thermal detonator, a key card and R2-D2) |
| 24 | | HeroIsAt | x, y | the hero stands on x, y |
| 25 | | SharedCounterIs | value | |
| 26 | | SharedCounterIsLessThan | value | |
| 27 | | SharedCounterIsGreaterThan | value | |
| 28 | | GamesWonIs | value | number of games won = value |
| 29 | | HasQuestHotspotAt | x, y | an enabled hotspot of type 0 is on x, y |
| 30 | | HasRequiredItem | | the hero carries one of the zone's required items |
| 31 | | CounterIsNot | value | |
| 32 | | RandomIsNot | value | |
| 33 | | SharedCounterIsNot | value | |
| 34 | | VariableIs | value, x, y, layer | the cell holds value (same as TileIs) |
| 35 | | GamesWonIsGreaterThan | value | |
| | 7 | Always | | always (never used) |
| | 11 | RandomIsSet | | random >= 0 (never used) |
| | 0 | | | never used |

### Instructions

| Yoda | Indy | Name | Arguments | Does |
| --- | --- | --- | --- | --- |
| 0 | 0 | PlaceTile | x, y, layer, tile | puts tile on the cell |
| 1 | 2 | RemoveTile | x, y, layer | clears the cell |
| 2 | 1 | MoveTile | x, y, layer, to x, to y | moves a tile on the same layer |
| 3 | 16 | DrawTile | x, y, tile | draws tile over the cell without changing the zone |
| 4 | 5 | SpeakHero | text | a speech bubble from the hero |
| 5 | 28 | SpeakNpc | x, y, text | a speech bubble from the cell x, y |
| 6 | 17 | RedrawTile | x, y | screen update only |
| 7 | 19 | RedrawTiles | x1, y1, x2, y2 | screen update only |
| 8 | 12 | Render | | screen update only |
| 9 | 6 | Wait | tenths | pauses |
| 10 | 11 | PlaySound | sound | plays a sound from the SNDS list |
| 11 | 35 | StopSound | | stops sounds |
| 12 | 13 | RollDice | n | random = 1..n |
| 13 | 9 | SetCounter | value | |
| 14 | 10 | AddToCounter | value | |
| 15 | | SetVariable | x, y, layer, value | same as PlaceTile |
| 16 | 14 | HideHero | | |
| 17 | 15 | ShowHero | | |
| 18 | 18 | SetHeroPosition | x, y | |
| 19 | 26 | MoveHeroTo | from x, from y, to x, to y, tenths | walks the hero there, one cell at a time with a pause in between; from -1 means where the hero stands |
| 20 | 21 | DisableAction | | this action will not run again |
| 21 | 3 | EnableHotspot | index | |
| 22 | 4 | DisableHotspot | index | |
| 23 | 22 | EnableMonster | index | |
| 24 | 23 | DisableMonster | index | |
| 25 | 24 | EnableAllMonsters | | |
| 26 | 25 | DisableAllMonsters | | |
| 27 | 30 | DropItem | item, x, y | the item (-1: the zone's find item) blinks on the cell and the game stands still until the player takes it (Use, Attack or a tap); it then goes straight into the inventory. The rest of the pass runs on first |
| 28 | 29 | AddItem | item | into the inventory |
| 29 | 32 | RemoveItem | item | out of the inventory (-1: the zone's required item) |
| 30 | 33 | MarkAsSolved | | the zone's goal is solved |
| 31 | 27 | WinGame | | |
| 32 | 31 | LoseGame | | |
| 33 | 34 | ChangeZone | zone, x, y | the hero goes to zone, on x, y, once the pass is over (see above) |
| 34 | | SetSharedCounter | value | |
| 35 | | AddToSharedCounter | value | |
| 36 | | SetRandom | value | |
| 37 | | AddHealth | value | may be negative; a loss is scaled by the difficulty like a hit (docs/combat.md), and a loss down to 0 loses the game |
| | 20 | RedrawZone | | screen update only |
| | 7, 8 | | | do nothing |

## Open questions

- Health: 0..300 (three bars of 100). The originals store it as a bar and
  points within the bar; the comparisons read 300 at full health, so our
  scale fits.
- In the Indy program we checked the flow of passes, PlaceItem, ChangeZone
  and the counters; the other Indy opcodes still follow the Yoda Stories
  behaviour.
- MarkAsSolved also sets three more flags on the map cell; the originals only
  use them for their locator map. Our map shows solved puzzles its own way.
