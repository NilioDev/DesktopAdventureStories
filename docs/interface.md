# The interface

The play field shows the original pixel art. Everything around it is our
own, drawn with SDL's built-in 8x8 font and plain shapes, in one colour
scheme (`src/app/ui.hpp`).

## Layout

Everything is drawn in interface units. One unit is a whole number of screen
pixels, the interface scale, so text and lines stay sharp. The play field is
288 x 288 game pixels (9 x 9 cells of 32 pixels). With *Scaling: Pixel* it
is shown at a whole multiple of that; with *Fill* it takes all the space it
has.

- On a wide screen the panel (200 units) sits right of the play field, as
  high as the field.
- On a tall screen (a phone held upright) the panel sits below the field and
  takes the rest of the screen.

The interface scale is the largest one that does not make the play field
smaller, for screens at least 320 units on their short side.

## Screens

- **Title:** the game's own title picture in the field; the menu in the
  panel: Continue (when there is a save), New game, Game (when both games
  are installed), World size, Difficulty, Options, Data viewer and Quit
  (these two not on phones).
- **Game:** the panel shows the puzzles solved, health, the weapon in hand,
  the inventory as a grid (scrolling to the selected item), the selected
  item's name, hints for the input device used last, and Map and Menu
  buttons.
- **Pause menu:** Resume, Map, Options, Save and quit. The game waits
  while it is open.
- **Options:** Fullscreen (desktop), Scaling (Pixel / Fill), Filter (Sharp,
  Smooth, or Scanlines over the play field) and Sound volume.
- **Map:** the world's sites coloured by kind (town, open puzzle, solved
  puzzle, goal), with the hero's site blinking.

## Pointer and touch

A click or tap on a menu row selects it; on a value's left arrow it steps
back. In the game:

- tapping a cell walks there along the shortest free path, one step every
  150 ms, and bumps into the cell when it cannot be walked on (to talk or
  push);
- tapping an item selects it, and tapping the selected item uses it;
- a tap anywhere moves a speech bubble on, or closes the map.

## Settings

Kept in `settings.json` next to the library folder:

```json
{"fullscreen": false, "pixelPerfect": true, "filter": "sharp", "volume": 80}
```

A missing or damaged file gives these defaults.

## Testing options

`--window WxH` sets the window size, `--menu pause|options|map` opens a
menu, `--device keyboard|gamepad|touch` picks the hints and `--tap X,Y`
presses at a point (in units) after the first frame. With `--screenshot` they
make pictures of every screen for checking a change.
