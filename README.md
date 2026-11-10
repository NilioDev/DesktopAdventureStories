# DesktopAdventureStories

An open-source, clean-room reimplementation of the 1996–1997 "Desktop Adventures"
game engine, so that *Indiana Jones and His Desktop Adventures* and
*Star Wars: Yoda Stories* stay playable on modern Windows, Linux, macOS and Android,
with a modern interface, gamepad support and an editor for new content.

> **Status:** early development. Nothing is playable yet.

## You need your own copy of the games

This project contains **no game data**. To play, point it at the files from your own
copy of the original games:

| Game | Data file | Executable (only used to read the colour palette) |
| --- | --- | --- |
| Indiana Jones and His Desktop Adventures | `DESKTOP.DAW` | `DESKADV.EXE` |
| Star Wars: Yoda Stories | `YODESK.DTA` | `YODESK.EXE` |

Never commit these files, or anything extracted from them, to this repository.

## Ground rules

- **Clean room.** All code here is written from scratch for this project. Code from
  other engine reimplementations is not copied or adapted; other projects may only be
  used as documentation of the file format.
- **Own look.** The original game world (tiles, sprites, sounds) is loaded from the
  player's files; the interface around it is our own and modern, not a copy of the
  Windows 3.x interface.
- **Trademarks.** This project is not affiliated with or endorsed by Lucasfilm, Disney
  or LucasArts. Game names are used only to describe compatibility.

## Building

Requirements: CMake 3.22+, a C++20 compiler and Git. SDL3 is downloaded and built
automatically when it is not installed.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/src/app/DesktopAdventureStories
```

On Linux, install SDL's build dependencies first (see `.github/workflows/ci.yml`).

Android (needs the Android SDK and NDK):

```sh
cd android
./gradlew assembleDebug
```

### Pointing it at your games

Copy your game files into [`gamedata/yoda/`](gamedata/README.md) and
`gamedata/indy/`; that folder is ignored by Git. The program finds it when
started from the repository root:

```sh
./build/src/app/DesktopAdventureStories
```

You can also pass another folder or the game's `.zip` on the command line, set
`DAS_GAMEDATA_DIR`, or drop a folder or zip on the window.

The first time a game is found it is imported into our own open format (see
[docs/formats/pack.md](docs/formats/pack.md)) in the app's data folder. After
that the original files are no longer needed. Use `--reimport` to import again,
or `--library DIR` (or `DAS_LIBRARY_DIR`) to keep the library somewhere else.

| System | Library folder |
| --- | --- |
| Windows | `%APPDATA%\DesktopAdventureStories\library` |
| Linux | `~/.local/share/DesktopAdventureStories/library` |
| macOS | `~/Library/Application Support/DesktopAdventureStories/library` |

The title screen starts a new game in a generated world (pick the world
size and difficulty), continues a saved one, or opens the options. The game
saves itself when you go back to the title screen or close the app. The data
viewer (title menu, desktop only) shows every zone and tile of the data.

The screen is laid out for any window size: the play field is scaled in
whole pixels, with a panel beside it (or below it on a tall screen) for
health, the weapon and the inventory. See [docs/interface.md](docs/interface.md).

Tests that need the original files run only when `DAS_GAMEDATA_DIR` is set:

```sh
DAS_GAMEDATA_DIR=gamedata ctest --test-dir build --output-on-failure
```

### Controls

| Action | Keyboard | Gamepad | Mouse / touch |
| --- | --- | --- | --- |
| Move | Arrow keys / WASD | D-pad / left stick | Click or tap where to go |
| Use the selected item, talk | Space / Enter | A (south) | Click the selected item again |
| Attack | Ctrl / X | B (east) | |
| Previous / next item | Q / E | LB / RB | Click an item |
| Map | M / Tab | Back / Select | Map button |
| Menu, back | Esc | Start (B goes back in menus) | Menu button |

Controls will be rebindable.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), especially the clean-room rules.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
