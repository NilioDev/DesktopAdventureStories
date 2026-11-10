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

## For beginners

The first time a game is edited, a short tour shows the editor around; the
*Tour* button starts it again. Every control explains itself in the bar at
the bottom. *New from an example* (Zones tab) starts a zone from one of the
game's own, and *New script...* (Scripts tab) offers ready-made scripts: a
person who talks, a first visit, using an item to get another, a door that
opens with an item, something that happens only once. *Check* finds what
would go wrong.

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
  and on the right either *Draw* (its properties, tools, layers and tiles)
  or *Story* (what it asks for and gives).
- **Scripts tab:** the zones on the left, the zone's scripts as cards of
  plain sentences in the middle, and a map of the zone on the right.
- **Tiles tab:** all tiles on the left (filter by kind, search by number or
  name), the chosen tile large in the middle to draw on, with the zones
  that use it (press one to open it), and its name, what it is and the
  colours on the right.
- **Characters tab:** the hero, monsters, people and weapons: their names,
  how they move and fight, and their pictures.
- **Puzzles tab:** the items a game is built from and what people say
  about them.
- **Sounds tab:** listen to the sounds, replace them and add new ones.

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
| Layer | Floor, Objects, Roof, Hotspots, or People |
| New from an example | Adds a copy of one of the game's own zones of a chosen kind (room, trade, item to find...), on the same planet when there is one |
| New zone | Adds a zone of the same size, kind and planet, its floor all in the tile in hand |
| Copy this zone | Adds a copy of the zone |

## Tiles

Tiles can be given a name and their flags changed: See-through, Floor,
Blocks the way, Can be pushed, Roof, World map, Weapon, Person and Item.
Bits the editor has no name for are shown and kept. *Add a copy as a new
tile* adds a tile to change.

To draw, choose a brush under *Draw* (*Pencil*, *Fill* for every touching
pixel of the same colour, or *Pick* to take a colour) and a colour from the
game's palette; colour 0 is see-through. Click or drag on the large tile;
right-click takes the colour under the mouse. A stroke is one step for
*Undo*. Changed tiles are saved in the mod as PNG files.

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

## People

On the People layer, Paint places the character chosen under *Person to
place* on an empty cell; clicking someone selects them, and the panel then
changes who it is or removes them. Erase removes, Pick takes the character
under the cursor. The panel says what each character is and how it moves.
The hero and weapons are not offered.

## Story

*Story* shows the zone's lists that the world generator builds a game
from (see [world-generator.md](world-generator.md)):

| List | What it holds |
| --- | --- |
| Asks for | Items the zone can ask the hero to bring; one is picked for each game |
| Asks for second | Yoda Stories: the second item when the zone holds two puzzles |
| Gives | Items the hero can find or get in the zone |
| People | People who can stand on the zone's person spots |

The lists of the rooms behind the zone's doors count too. Clicking a tile
takes it off a list; *Add the tile in hand* adds the tile chosen below (it
warns when that tile is not marked as an item or a person).

## Scripts

Scripts make things happen in a zone: people talk, doors open, items
appear. The Scripts tab shows each script of the zone as a card: its
*When* lines (all must hold) and its *Then* lines (done in order), each
written as a sentence, such as "the hero walks against tile 12 at 3,4".

- Click a line to open it. *When* or *Then* chooses what the line is,
  from everything a script can check or do; the help bar explains each.
  Every number has a name (x, y, layer, tile, item, zone, person, hotspot,
  sound, time) and is changed with `<` and `>`.
- Places are pointed out on the map on the right: the places of the open
  script are outlined, the open line's are marked 1 and 2, and clicking the
  map sets the place (*On the map* chooses which one when a line has two).
  For a line about a hotspot or a person, clicking it on the map chooses
  it.
- *In hand* sets a tile or item to the tile in hand, chosen under the map.
- *+ When* and *+ Then* add a line; *Line up*, *Line down* and *Delete
  line* move and remove it. *New script*, *Up*, *Down*, *Copy* and
  *Delete* do the same for whole scripts. Scripts are checked from the top.

### As text

*Edit scripts* on the Zones tab saves the mod (when needed), writes the
zone's scripts to `scripts/NNNN.txt` in the mod's folder and opens that
file in the system's text editor. The format is in
[formats/script-text.md](formats/script-text.md) and what each line does in
[formats/scripts.md](formats/scripts.md). Save the file, then choose *Try it
out* or *Save*: both read the scripts files again first, when they were
changed. A mistake stops them and names the file and line.

## Characters

| Control | What it does |
| --- | --- |
| Name | The name the editor shows (at most 15 letters) |
| Kind | Hero, Monster or person, or Weapon |
| Moves | How it moves, from standing still to going for the hero; the help bar says what each does |
| Health, Weapon | Monsters and people: the hits they take, and the weapon they fight with |
| Damage, Sound | Weapons: how hard they hit, and the sound they make |
| Pictures | A tile for each of 8 directions, standing and in two steps of walking. Click one to give it the tile in hand; right-click takes its tile |

*Add a copy* adds a new character to change.

## Puzzles

A puzzle is an item with what people say about it. Its *Kind* says how the
world generator uses it: *Trade* (asked for and handed out on the way),
*Offer*, *Final* (hands out an item the goal zone asks for), *Goal* (the
mission of a game, with a hint and a briefing) or *Switched off*. *Tile in
hand* and *None* set the item; the texts are typed in place. *Add a copy*
adds a new puzzle.

## Sounds

*Listen* plays a sound. *Replace with a file...* takes a `.wav` sound or a
`.mid` song from the computer; the sound keeps its number and name, so
the scripts and weapons that played it play the new one. *Add a sound...*
adds one at the end of the list; its file name starts with its number. The
tab also says which scripts and weapons play a sound.

## Check

The *Check* tab lists what it finds, each with *Show*, which goes there:

- **Mistakes** go wrong in the game: a door that leads to no zone, a
  hotspot or a place outside the zone, a script line about a tile or zone
  that does not exist, a line the game does not know, no hero.
- **Tips** may be what you want, but are worth a look: zones the world
  generator cannot use (for example a trade zone without people, or an item
  to find without an item spot; the rooms behind a zone's doors count too),
  tiles on item lists that are not marked as items, script lines about a
  person, hotspot or sound that is not there, scripts that do nothing.

The original games give a few tips of their own; they play as they always
did.

## Not in the editor yet

The title screen (`setup.png`) is changed by putting a picture in the mod
(see [formats/mod.md](formats/mod.md)).
