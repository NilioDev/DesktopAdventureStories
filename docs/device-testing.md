# Testing on real devices

The automatic checks build and test the code on Windows, macOS, Linux and
Android, but they cannot hold a phone, plug in a gamepad or read small
text. This list is for people who try a release on a real computer, phone
or tablet. It takes about half an hour.

When you are done, open a
[device test](https://github.com/NilioDev/DesktopAdventureStories/issues/new?template=device_test.yml)
issue and tick what worked. For a single problem, use **Options → Report a
problem** in the game: it opens a bug report with the version, your system
and the end of the log already filled in.

Never attach the original game files to an issue.

## Install and first start

- Install the package for your system (see the README) and start it.
- Add your games, once from a folder and once from a zip if you can.
- Is everything on the title screen readable, and does it fit the screen?
  On a phone: try it upright and sideways.

## Play

- Start a new game in both games. Walk, talk to someone, push a block, use
  an item, attack.
- Open the map and the inventory; pick another item.
- Listen: do sounds and music play? Change the volume in Options.
- Desktop: switch fullscreen, scaling (Pixel / Fill) and the filter.

## Controls

- Keyboard and mouse: click a cell to walk there, click an item.
- Gamepad: plug it in while playing and again before starting. Do the
  hints in the panel show the gamepad's buttons? Note the gamepad's name.
- Touch: tap to walk, use the touch pad, use two fingers at once.
- Change a key or button in **Options → Controls**, then *Reset controls*.
- Android: the back button opens the menu.

## Saving

- **Save game** in the pause menu, in a slot. Play on, then **Load game**:
  are you back where you saved?
- Close the app while playing and start it again: does *Continue* bring
  you back?
- Phone: switch to another app for a minute, then come back. The game
  should wait in the pause menu. Then swipe the app away from the recent
  apps and start it again: *Continue* should still have your game.

## Mods and editor (desktop and tablet)

- Open the editor, change a tile in a zone and try the zone out.
- Switch a mod on and off in **Mods**.

## If something goes wrong

- If the game closes by itself, start it again: the title screen says it
  stopped unexpectedly. Use **Options → Report a problem** then, because
  the log of the run that went wrong is still kept (`log-previous.txt`).
- The log is `log.txt` in the app's folder, next to `settings.json`:
  - Windows: `%APPDATA%\DesktopAdventureStories`
  - macOS: `~/Library/Application Support/DesktopAdventureStories`
  - Linux: `~/.local/share/DesktopAdventureStories`
  - Android: inside the app's own storage (use *Report a problem* instead)
