# The editor

*Editor* in the title menu opens the selected game for editing.
Changes are saved as a mod (see [formats/mod.md](formats/mod.md)) with the
id `my-yoda` or `my-indy`, in the mods folder, and that mod is switched on.
The original game and its pack are never changed.

The editor shows the game with only its own mod on top. Other mods stay
switched on for play; they are left out while editing so that the saved mod
holds only what was changed in the editor. To make a mod of your own, rename
the saved folder and change `id`, `name` and `description` in its
`mod.json`.

## The screen

The editor has its own screen. Every button, list and field explains
itself: point at it (or move the focus to it) and the bar at the bottom of
the screen says what it does. Pointing at a cell of the zone names what is
in it.

- **Top bar:** the *Zones* and *Tiles* tabs, *Undo*, *Redo*, *Save* (`*`
  means there are unsaved changes), *Try it out*, and on the right the
  mod's name, *A-* / *A+* (smaller or larger interface) and *Leave* (with
  unsaved changes it asks to be pressed again).
- **Zones tab:** the zones on the left (search by number, kind, planet or
  size; *New zone* and *Copy this zone* below), the zone in the middle,
  and its properties, tools, layers and tiles on the right.
- **Tiles tab:** all tiles on the left (filter by kind, search by number or
  name), the chosen tile large in the middle with the zones that use it
  (press one to open it), and its name and what it is on the right.

## Controls

| | Mouse / touch | Keyboard | Gamepad |
| --- | --- | --- | --- |
| Use a button, field or cell | Click | Enter or Space | A |
| Move the focus | | Tab, Shift+Tab, arrows | D-pad, stick |
| Draw on the zone | Click or drag | Arrows move the cursor, Enter draws | D-pad, A |
| Leave the zone (focus) | | Esc or Tab | B |
| Take the tile under the mouse | Right-click, or tool *Pick* | | |
| Change a value | `<` and `>`, or the wheel | Left and right | D-pad left and right |
| Undo, redo, save | Top bar | Ctrl+Z, Ctrl+Y (or Ctrl+Shift+Z), Ctrl+S | Top bar |

A drag is one step for *Undo* (up to 100 steps). Layers above the one being
edited are drawn see-through; the boxes in front of the layers hide or show
them.

## Zones

| Control | What it does |
| --- | --- |
| Kind | The kind of zone, which the world generator chooses zones by; the help bar explains each |
| Planet | Desert, Snow, Forest or Swamp |
| 9 x 9, 18 x 18 | The zone's size; growing keeps what is there and fills the new cells with the tile in hand, shrinking cuts off the right and bottom |
| Edit scripts | Opens the zone's scripts as text (see below) |
| Tool | Paint, Erase, Pick (takes the tile or hotspot kind under the cursor), Fill (a rectangle: press, drag, let go) |
| Layer | Floor, Objects, Roof, or Hotspots |
| New zone | Adds a zone of the same size, kind and planet, its floor all in the tile in hand |
| Copy this zone | Adds a copy of the zone |

## Tiles

Tiles can be given a name and their flags changed: See-through, Floor,
Blocks the way, Can be pushed, Roof, World map, Weapon, Person and Item.
Bits the editor has no name for are shown and kept. *Add a copy as a new
tile* adds a tile to change; its picture is a PNG in the mod's `tiles`
folder (see [formats/mod.md](formats/mod.md)).

## Hotspots

Hotspots are the places in a zone where something can happen: a door, a
spot for an item, a place for a person. On the Hotspots layer, Paint places
the kind chosen under *Hotspot to place* on an empty cell; clicking a
hotspot selects it, and the panel then changes its kind, its argument and
whether it is active at the start. Erase removes it. The argument depends
on the kind, for example the zone behind a door (*Go to zone* opens it).
These are the names the editor uses:

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
file, then choose *Try it out* or *Save*: both read the scripts files
again first. A mistake stops them and names the file and line.

## Not in the editor yet

Placed characters and the zone's item lists are kept as they are. They can
be changed in the zone's JSON file in the mod (see
[formats/pack.md](formats/pack.md)). New tiles and sounds are added as files
to the mod.
