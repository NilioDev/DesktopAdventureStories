# Save games

The original games save a whole world in their own binary format. We use
our own format: a small JSON file that is easy to read, to check and to
extend. The originals' save files are not read.

## When the game is saved

Each game has an autosave and three slots of the player's own, in a `saves`
folder next to the library:

- `indy.save.json`, `yoda.save.json`: the autosave. It is written when the
  player leaves play for the title screen, when the app closes, every two
  minutes of play, and when the app goes to the background or its window is
  minimised. A phone may close an app in the background without warning, so
  the game is saved before that can happen, and it waits in the pause menu
  when the player comes back.
- `indy.slot1.save.json` to `indy.slot3.save.json` (and the same for `yoda`):
  written only when the player picks *Save game* in the pause menu and a
  slot. A slot that is in use asks *Overwrite?* first.

A game that has been won or lost removes its autosave, so it is not
continued; the player's own slots are kept.

A save is only written when the world is at rest (`World::canSave`): no
speech bubble or script is waiting for the player. The file is written to
`<name>.new` first and then renamed over the old save, so a crash during the
write never leaves a broken save behind.

*Continue* on the title screen loads the newest save of all four. *Load
game*, on the title screen and in the pause menu, lists them with the day
and time they were saved. Next to *New game* the title screen offers the
world size (small, medium, large) and the difficulty (easy, normal,
hard).

## What is in a save

A world can be built again from the game data and the generator options, so
a save holds those options and only what play changed:

```json
{
  "format": "das-save",
  "version": 1,
  "game": "indy",
  "mods": ["more-temples"],
  "world": {"seed": 77, "size": 1, "planet": -1, "goal": -1,
            "gamesWon": 0, "recentGoals": [], "recentZones": []},
  "state": { ... }
}
```

`world` holds the `GeneratorOptions`. Loading generates the same world from
them again and then applies `state`.

`mods` lists the ids of the mods that were switched on (see
[formats/mod.md](formats/mod.md)); saves from before mods existed have no
`mods` and count as made without mods. Mods change the world the generator
makes, so a save is only continued with the same mods switched on.

`state` holds:

| Key | Meaning |
| --- | --- |
| `zone`, `player`, `facing` | where the hero is |
| `returns` | the way back out of rooms: zone and position for each step |
| `inventory`, `weapon`, `health`, `heroVisible` | the hero |
| `outcome`, `gamesWon`, `ending`, `mission` | how far the game is |
| `difficulty` | 0 to 100, see combat.md |
| `goals` | for each puzzle site: zone, site index, the two required items, the item to find, and whether it is solved |
| `zones` | the zones the player has seen, by zone number |
| `rng` | the state of the world's random numbers, so play goes on as it would have |

Each zone in `zones` holds:

| Key | Meaning |
| --- | --- |
| `cells` | the cells that differ from the game data: cell index, then the three tile layers |
| `hidden` | items still hidden in the zone: x, y, item |
| `hotspots`, `actionsOff`, `monstersOn`, `monstersDead` | one character per hotspot, script or monster: `1` or `0` |
| `monsters` | each monster's x, y, health, facing and waypoint |
| `entered` | whether the zone's first-entry scripts have run |
| `counter`, `random`, `sharedCounter` | the zone's script counters |

Zones and goals are written in a fixed order, so the same world always gives
the same file.

## Damaged saves

Reading a save checks the format name, the version and the game. Any missing
key, wrong type or zone that does not exist makes the load fail with a
message; the app then leaves the save alone and stays on the title screen.

## Later versions

A new version may add keys. Readers ignore keys they do not know, and a
newer version number than the reader knows is refused rather than guessed
at.
