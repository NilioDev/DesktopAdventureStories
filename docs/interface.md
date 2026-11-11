# The interface

The play field shows the original pixel art. Everything around it is our
own, drawn with plain shapes and the Fira Sans font (SIL Open Font License,
`assets/fonts`), in one colour scheme (`src/app/ui.hpp`). Speech bubbles
during play use the 8x8 pixel font PxPlus IBM CGA (CC BY-SA 4.0), always at
a whole multiple of its 8 pixels and without smoothing (`ui::Font::Story`). Text is drawn with
SDL_ttf at the screen's own resolution, so it stays sharp at every interface
scale. Widths are measured, so wrapping and cutting long text (`ui::wrap`,
`ui::fit`) work for any letter width. If the font cannot be loaded, the
interface falls back to SDL's built-in 8x8 font.

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

- **Welcome:** before any game is installed. It says what is needed (the
  player's own copy of one or both games), where to find it in three steps,
  which file shows the right folder (`DESKTOP.DAW` or `YODESK.DTA`), and has
  one button to choose it (Enter, Space or a gamepad's A press it too). A
  folder without game files says so in words, not as a path. After a game is
  added, the title screen says it is ready.
- **Title:** the game's own title picture in the field; the menu in the
  panel: Continue (the newest save), New game, Load game (when there is a
  save), Game (when both games
  are installed), World size, Difficulty, Options, Mods, Editor, Add games, Data
  viewer and Quit (these two not on phones).
- **Game:** the panel shows the puzzles solved, health, the weapon in hand,
  the inventory as a grid (scrolling to the selected item), the selected
  item's name, hints for the input device used last, and Map and Menu
  buttons.
- **How to play:** a card with the aim of the game and the controls of
  the device used last (keyboard, gamepad or touch). It opens by itself at
  the very first game (`howToPlaySeen` in the settings) and from the pause
  menu.
- **Pause menu:** Resume, Map, Save game, Load game, How to play, Report a
  bug, Options, Save and quit. The game waits while it is open. *Save game* lists the three slots;
  one in use asks *Overwrite?* first. It is also where the game waits after
  the app was in the background.
- **Options:** Fullscreen (desktop), Scaling (Pixel / Fill), Filter (Sharp,
  Smooth, or Scanlines over the play field), Sound and Music volume, Touch
  pad, Controls and Report a problem. *Report a problem* opens the bug page.
- **Report a bug:** F8, both gamepad sticks pressed in, or the pause menu.
  Kind, Note, Save snapshot, Report on GitHub, Show position and Back; it
  saves a bug snapshot and can open a new GitHub issue with the note, the
  game's state, the version, the system and the end of the log filled in.
  See [bug-snapshots.md](bug-snapshots.md); the log is kept by
  `src/app/problem_log.hpp`.
- **Controls:** every action with its keys, or its gamepad buttons when a
  gamepad was used last. Choosing one waits six seconds for the new key or
  button (Esc cancels); it becomes the only one for that action and is taken
  away from any other action. *Reset controls* brings back the defaults. The
  Android back button always opens the menu.
- **Mods:** the mods of the selected game, each On or Off, with its version,
  author and description below the menu. A mod that cannot be applied shows
  Error and the reason. *Open mods folder* (desktop) opens the folder in the
  file manager. See [formats/mod.md](formats/mod.md).
- **Editor:** its own screen for changing zones, hotspots and tiles, trying
  a zone out and saving the changes as a mod. See [editor.md](editor.md).
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

## The touch pad

On a touch screen a direction pad and *Use* and *Hit* (attack) buttons show
while the game is on screen: in the empty strips beside the field when the
screen is held sideways, in a strip under the panel when it is held upright,
and over the field's bottom corners otherwise. Several fingers work at once.
A finger that starts on the pad keeps steering wherever it moves; in the
pad's middle it holds nothing. Options, *Touch pad*: Auto (shown once the
screen was touched), On or Off. Touches are read as fingers, not as a mouse.

## Settings

Kept in `settings.json` next to the library folder:

```json
{"fullscreen": false, "pixelPerfect": true, "filter": "sharp", "volume": 80, "music": 70, "touchPad": "auto",
 "keys": {"use": ["Space", "Return"], ...}, "buttons": {"use": ["a"], ...}}
```

Keys use SDL's scancode names and buttons SDL's gamepad button names. An
action missing from `keys` or `buttons` keeps its default controls. A
missing or damaged file gives the defaults.

## Testing options

`--window WxH` sets the window size, `--menu pause|options|controls|map|bug` opens a
menu, `--device keyboard|gamepad|touch|touch-pad` picks the hints and `--tap X,Y`
presses at a point (in units) after the first frame. With `--screenshot` they
make pictures of every screen for checking a change.
