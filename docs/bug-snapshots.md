# Reporting a bug from the game

When something goes wrong while playing, report it right there, at the
moment it happens. The game keeps a *bug snapshot*: everything needed to see
the problem again on another computer.

## Opening the bug page

- **F8** on a keyboard,
- **both sticks pressed in** (L3 + R3) on a gamepad,
- **Report a bug** in the pause menu, or **Options → Report a problem**.

The frame on screen at that moment becomes the snapshot's screenshot (taken
without the menu over it), and the game waits under the *Report a bug* page:

| Row | What it does |
| --- | --- |
| Kind | What sort of problem: Stuck, Wrong picture, Puzzle or talk, Fight, Sound, Controls, Menu or screen, Other. |
| Note | Your own words. Press it, type, and press Enter (or the note again) when done. On a phone the on-screen keyboard opens. |
| Save snapshot | Saves the snapshot only, to report later or to collect several. |
| Report on GitHub | Saves the snapshot, opens a bug report in the browser with your note and the game's state filled in, and opens the snapshots folder so the zip can be dragged onto the report. |
| Show position | Shows the zone number and the hero's cell over the play field (in a game). Handy for saying where something is. |
| Back | Back to the game or the menu. |

Nothing leaves your device by itself. A snapshot is only shared when you add
it to a report. It has a screenshot of your game in it, so it shows your own
copy's graphics; don't attach it anywhere public if you don't want that.

## Where snapshots are kept

In the `snapshots` folder next to the `library` folder (see the README's
table), named after the date and time, such as
`bug-2026-11-11-014355.zip`. The newest 30 are kept.

On Android they are in `Android/data/org.desktopadventurestories/files/snapshots`
on the device's storage, which a file manager or a computer connected over
USB can open.

## What a snapshot holds

A zip file with:

| File | What |
| --- | --- |
| `report.txt` | The kind and note, version, system, time, game and mods, and a description of the moment: zone, hero position and facing, health, weapon, items, whether a script was running, the world's seed and size, and the map site with its puzzle. |
| `save.json` | A save game of that moment, in the normal save format ([savegames.md](savegames.md)). |
| `state.json` | Instead of `save.json` when a single zone was played from the data viewer or the editor: the zone, position and what play changed. |
| `screenshot.png` | The screen at that moment, at most 1600 pixels wide. |
| `inputs.txt` | The last 150 controls pressed, with the time, zone and position. |
| `log.txt` | The end of the log, and of the run before if that one stopped unexpectedly. |

On the title screen there is no game: the snapshot has the report,
screenshot, controls and log.

## Seeing the problem again

Drop a snapshot zip on the game's window, or start the program with

```sh
DesktopAdventureStories --snapshot bug-2026-11-11-014355.zip
```

and the saved game starts at the same place, with the same world, items and
state. The game the snapshot was made with must be installed, with the same
mods switched on.

A save is made when no script is running, so a snapshot taken in the middle
of a conversation starts just before or after it; the report says whether a
script was running.

## For developers

The snapshot is made in the core (`das/world/snapshot.hpp`); zip and PNG
writing are our own (`das/util/zip.hpp`, `das/util/png.hpp`). The report link
puts the note in *What went wrong?* and the description in the form's
*Game state* field; when the link gets too long the log is cut first, then
the end of the description.
