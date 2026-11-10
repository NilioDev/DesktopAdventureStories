# Mods

A mod changes or extends one game. It holds only what it changes, in the same
formats as the game's [pack](pack.md), so a mod can be made by hand with a
text editor and a drawing program, or with the editor in the app.

Mods live in the `mods` folder next to the library (on desktop, *Mods → Open
mods folder* in the title menu opens it). Each mod is a folder, or a `.zip`
of that folder, which makes a mod easy to share. In the title menu, *Mods*
lists the mods of the selected game and switches each on or off. Mods that
are switched on are applied in the order of their ids.

```
mods/
└── more-temples/          the mod's folder; its name does not matter
    ├── mod.json           required
    ├── zones/0012.json    changes zone 12
    ├── zones/0658.json    adds a zone (Yoda Stories has 658: 0 to 657)
    ├── tiles/2123.png     adds a tile
    ├── tiles.json         flags and names of tiles
    ├── sounds/door.wav    changes or adds a sound
    ├── setup.png          a new title screen
    ├── puzzles.json       changes or adds puzzles
    └── characters.json    changes or adds characters
```

Every file except `mod.json` is optional. Other files (a readme, for
example) are left alone.

## mod.json

```json
{
  "format": "desktop-adventure-stories-mod",
  "formatVersion": 1,
  "id": "more-temples",
  "game": "yoda",
  "name": "More temples",
  "version": "1.0",
  "author": "Your name",
  "description": "Adds two temples to Dagobah."
}
```

| Key | Meaning |
| --- | --- |
| `format` | Always `"desktop-adventure-stories-mod"` |
| `formatVersion` | `1`. A reader refuses mods with a higher number |
| `id` | Lower-case letters, digits, `-` and `_`; unique per game |
| `game` | `"yoda"` or `"indy"` |
| `name`, `version`, `author`, `description` | Shown in the Mods menu; all optional |

## Zones

`zones/NNNN.json` has the format of the pack's zone files. A number below
the game's zone count replaces that zone; the next free number adds a zone,
and further new zones must follow on without gaps. Every tile a zone uses
must exist in the game or be added by the mod.

New zones take part in world generation like the game's own zones, by their
`type` and `planet`. A mod that adds or changes zones therefore changes the
worlds the generator makes.

## Tiles

`tiles/NNNN.png` is one 32 x 32 tile. Like zones, a number below the tile
count replaces that tile and the next free numbers add tiles. The image may
be any 8-bit PNG (with or without palette or alpha). Its colours are
turned into the game's palette: pixels that are mostly transparent become
transparent, the others take the palette colour that is equal or closest.
Tiles exported from the pack's `tiles.png` keep their exact colours.

`tiles.json` sets flags and names:

```json
{"tiles": [{"tile": 2123, "flags": 4, "name": "Crystal"}]}
```

`flags` is the tile's type (see [sections.md](sections.md#tile-tiles)); a new
tile without flags gets 0. `name` is the name shown for items; an empty name
removes it.

## Sounds

A file in `sounds/` replaces the game's sound with the same file name (the
pack's `sounds` folder shows those names). A file with any other name is
added to the end of the game's sound list, in the order of the file names,
so scripts can play it by its number. `.wav` files play as sound effects and
`.mid` files as music.

## Title screen

`setup.png` is a 288 x 288 image, read like a tile.

## Puzzles and characters

`puzzles.json` is a list like the pack's: `[{"id": 3, "body": "..."}]`. An
entry replaces the puzzle with that id, or adds one when the id is the next
free one. `characters.json` holds the same three lists as the pack's
(`characters`, `weapons`, `aux`), each optional; `weapons` and `aux` entries
replace the entry of the same character or are added.

## Save games

A save game records which mods were on (see [savegames.md](../savegames.md)),
and is only continued with the same mods switched on.

## Implementation

`src/core/include/das/pack/mod.hpp`: reading (`readMod`, `findMods`),
applying (`applyMod`), and making a mod from an edited pack (`makeMod`,
`writeMod`). `tests/test_mods.cpp` checks that a mod made from an edit turns
the original pack into the edit.
