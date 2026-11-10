# The zone editor

*Editor* in the title menu opens the selected game's zones for editing.
Changes are saved as a mod (see [formats/mod.md](formats/mod.md)) with the
id `my-yoda` or `my-indy`, in the mods folder, and that mod is switched on.
The original game and its pack are never changed.

The editor shows the game with only its own mod on top. Other mods stay
switched on for play; they are left out while editing so that the saved mod
holds only what was changed in the editor. To make a mod of your own, rename
the saved folder and change `id`, `name` and `description` in its
`mod.json`.

## Drawing

The play field shows the whole zone. The panel shows the tile in hand and
the editor's menu.

| | Mouse / touch | Keyboard | Gamepad |
| --- | --- | --- | --- |
| Draw with the tool | Click or drag on the zone | Arrows, then Space | D-pad, then A |
| Take the tile under the cursor | Tool *Pick* | Ctrl | B |
| Choose a tile | *Tile* in the menu | M | Back |
| Change layer | *Layer* in the menu | Q / E | LB / RB |
| Menu | Click it | Esc | Start |

A drag is one step for *Undo*. Layers above the one being edited are drawn
see-through.

## The menu

| Row | What it does |
| --- | --- |
| Zone | The zone being edited; left and right step through the zones |
| Layer | Floor, Objects, Roof, or Hotspots |
| Tool | Paint, Erase, or Pick (takes the tile or hotspot type under the cursor) |
| Tile | The tile to paint; pressing it opens the tile picker (9 x 9 tiles a page; Q / E or LB / RB turn pages) |
| Type, Argument | On the Hotspots layer: the type that Paint places, and the argument of the hotspot under the cursor |
| Zone type | The zone's type, which the world generator chooses zones by |
| Scripts | The number of scripts in the zone; pressing it opens them as text in the system's text editor (desktop) |
| Undo | Takes back the last change (up to 100) |
| New zone | Adds a zone of the same size, type and planet, with its floor in the tile in hand |
| Try it out | Plays the zone as it is now; Esc or the Back button returns to the editor |
| Save mod | Saves all changes as the mod; `*` means there are unsaved changes |
| Leave editor | Back to the title menu; with unsaved changes it asks to be chosen again |

## Hotspots

Hotspots are the places in a zone where something can happen: a door, a
spot for an item, a place for a person. Paint places the chosen type (or
changes the type of the hotspot already there), Erase removes it. The
argument depends on the type, for example the zone behind a door. These
are the names the editor uses:

| Type | Name | Meaning |
| --- | --- | --- |
| 0 | Item spot | where the generator puts the item a puzzle gives |
| 1 | Person spot | where a trading person stands |
| 2 | Weapon spot | |
| 3, 4 | Vehicle to, Vehicle back | |
| 5 | Locator | |
| 6 | Item | argument: the item lying or hidden here |
| 7 | Person | |
| 8 | Weapon | argument: the weapon lying here |
| 9 | Door in | argument: the zone behind the door |
| 10 | Door out | leads back to where the hero came from |
| 12 | Lock | |
| 13 | Teleporter | |
| 14, 15 | Ship out, Ship in | argument: the zone the ship flies to |

## Scripts

*Scripts* saves the mod (when needed), writes the zone's scripts to
`scripts/NNNN.txt` in the mod's folder and opens that file in the system's
text editor. The format is in [formats/script-text.md](formats/script-text.md)
and what each line does in [formats/scripts.md](formats/scripts.md). Save the
file, then choose *Try it out* or *Save mod*: both read the scripts files
again first. A mistake stops them and names the file and line.

## Not in the editor yet

Placed characters and the zone's item lists are kept as they are. They can
be changed in the zone's JSON file in the mod (see
[formats/pack.md](formats/pack.md)). New tiles and sounds are added as files
to the mod.
