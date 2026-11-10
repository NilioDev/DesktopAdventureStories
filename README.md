# DesktopAdventureStories

An open-source, clean-room reimplementation of the 1996–1997 "Desktop Adventures"
game engine, so that *Indiana Jones and His Desktop Adventures* and
*Star Wars: Yoda Stories* stay playable on modern Windows, Linux, macOS and Android,
with a modern interface, gamepad support and an editor for new content.

> **Status:** playable. Both games can be played from start to finish in
> generated worlds, and the editor makes mods. Not yet tested much on real
> devices, so please report what goes wrong.

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

## Installing and starting

Ready-made packages are on the
[releases page](https://github.com/NilioDev/DesktopAdventureStories/releases)
once a version is out (see [docs/releases.md](docs/releases.md) for how they
are made). Below, for each system, how to start a package and how to build
it yourself. The first start always needs your game files; see
[Pointing it at your games](#pointing-it-at-your-games).

### Windows

**Package:** download `DesktopAdventureStories-<version>-windows.zip`, unpack
it and double-click `DesktopAdventureStories.exe`. Windows SmartScreen may
warn the first time because the program is not signed: choose *More info*,
*Run anyway*. Put your games in a `gamedata` folder next to the `.exe`, or
start the program and press *Choose game files*.

**Build it yourself:** install [Git](https://git-scm.com/),
[CMake](https://cmake.org/download/) and Visual Studio 2022 (the free
Community edition, with *Desktop development with C++*). Then, in a
*Developer PowerShell for VS 2022*:

```powershell
git clone https://github.com/NilioDev/DesktopAdventureStories.git
cd DesktopAdventureStories
cmake -S . -B build
cmake --build build --config Release --parallel
.\build\src\app\Release\DesktopAdventureStories.exe
```

### macOS

**Package:** download `DesktopAdventureStories-<version>-macos.dmg`, open it
and drag the app to *Applications*. The app is not signed, so the first time
right-click it and choose *Open*, then *Open* again. Start it and press
*Choose game files*, or drop your game folder on the window.

**Build it yourself:** install the Xcode command line tools
(`xcode-select --install`) and CMake (for example `brew install cmake`). Then
in Terminal:

```sh
git clone https://github.com/NilioDev/DesktopAdventureStories.git
cd DesktopAdventureStories
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
open build/src/app/DesktopAdventureStories.app
```

Started this way the app does not look in the repository's `gamedata`
folder; choose the game files in the app, or start the program inside the
app from the repository root:
`./build/src/app/DesktopAdventureStories.app/Contents/MacOS/DesktopAdventureStories`.

### Linux

**Package:** download `DesktopAdventureStories-<version>-linux.tar.gz`, then:

```sh
tar xzf DesktopAdventureStories-*-linux.tar.gz
cd DesktopAdventureStories-*/
./DesktopAdventureStories
```

Put your games in a `gamedata` folder next to the program, or press
*Choose game files*.

**Build it yourself:** on Debian or Ubuntu, install the tools and SDL's build
dependencies (other distributions have packages with similar names):

```sh
sudo apt install git cmake g++ ninja-build pkg-config \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
  libwayland-dev libdecor-0-dev wayland-protocols \
  libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
  libdbus-1-dev libibus-1.0-dev libudev-dev
git clone https://github.com/NilioDev/DesktopAdventureStories.git
cd DesktopAdventureStories
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/src/app/DesktopAdventureStories
```

### Android

**Package:** download `DesktopAdventureStories-<version>-android.apk` on your
phone or tablet and open it. Android asks to allow installing apps from this
source (your browser or file manager) the first time. Start the app and
choose a `.zip` of your game folder, or its files. Android 5 or newer.

**Build it yourself:** install JDK 17 and the Android SDK and NDK (easiest
with [Android Studio](https://developer.android.com/studio)). Then:

```sh
git clone https://github.com/NilioDev/DesktopAdventureStories.git
cd DesktopAdventureStories/android
./gradlew assembleDebug
adb install app/build/outputs/apk/debug/*.apk
```

Or open the `android` folder in Android Studio and press *Run*.

### All desktop systems

The build needs CMake 3.22+, a C++20 compiler and Git. SDL3 is downloaded and
built automatically when it is not installed. Run the tests with:

```sh
ctest --test-dir build --output-on-failure
```

### Pointing it at your games

Copy your game files into [`gamedata/yoda/`](gamedata/README.md) and
`gamedata/indy/`; that folder is ignored by Git. The program finds it when
started from the repository root:

```sh
./build/src/app/DesktopAdventureStories
```

You can also pass another folder or the game's `.zip` on the command line, set
`DAS_GAMEDATA_DIR`, drop a folder or zip on the window, or press *Choose game files* (title menu: *Add games*). On Android the picker takes a zip of the game folder, or its files.

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

Every key and button can be changed under Options, Controls.

## Mods

Mods change or extend a game: new or changed zones, tiles, sounds and title
screens. A mod is a folder (or a .zip of it) in the `mods` folder next to the
library; switch it on under *Mods* in the title menu. See
[docs/formats/mod.md](docs/formats/mod.md) for how to make one. The
*Editor* in the title menu draws zones and saves them as a mod; see
[docs/editor.md](docs/editor.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md), especially the clean-room rules.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
