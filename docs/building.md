# Building from source

Most players don't need this: ready-made packages are on the
[releases page](https://github.com/NilioDev/DesktopAdventureStories/releases).
This page is for people who want to build the program themselves, run the
tests or work on the code. See also [CONTRIBUTING.md](../CONTRIBUTING.md) and
[releases.md](releases.md) for how the release packages are made.

## What you need

The desktop build needs CMake 3.22+, a C++20 compiler and Git. SDL3 is
downloaded and built automatically when it is not installed. SDL3_ttf (with
its own copy of FreeType) is downloaded and built too, unless an installed
SDL3 is used and SDL3_ttf is installed next to it. The interface font in
`assets/fonts` is built into the program (`cmake/DasEmbed.cmake`).

## Windows

Install [Git](https://git-scm.com/), [CMake](https://cmake.org/download/) and
Visual Studio 2022 (the free Community edition, with *Desktop development with
C++*). Then, in a *Developer PowerShell for VS 2022*:

```powershell
git clone https://github.com/NilioDev/DesktopAdventureStories.git
cd DesktopAdventureStories
cmake -S . -B build
cmake --build build --config Release --parallel
.\build\src\app\Release\DesktopAdventureStories.exe
```

## macOS

Install the Xcode command line tools (`xcode-select --install`) and CMake (for
example `brew install cmake`). Then in Terminal:

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

## Linux

On Debian or Ubuntu, install the tools and SDL's build dependencies (other
distributions have packages with similar names):

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

To make an AppImage of your own build, configure with
`-DDAS_USE_SYSTEM_SDL3=OFF`, build, and run
`packaging/linux/make-appimage.sh build dist` (needs `desktop-file-utils`).

## Android

Install JDK 17 and the Android SDK and NDK (easiest with
[Android Studio](https://developer.android.com/studio)). Then:

```sh
git clone https://github.com/NilioDev/DesktopAdventureStories.git
cd DesktopAdventureStories/android
./gradlew assembleDebug
adb install app/build/outputs/apk/debug/*.apk
```

Or open the `android` folder in Android Studio and press *Run*.

## Game files in the repository

Copy your game files into [`gamedata/yoda/`](../gamedata/README.md) and
`gamedata/indy/`; that folder is ignored by Git. The program finds it when
started from the repository root:

```sh
./build/src/app/DesktopAdventureStories
```

## Command line and environment

| Option | What it does |
| --- | --- |
| `DesktopAdventureStories PATH` | Use the game folder or `.zip` at `PATH`. |
| `DAS_GAMEDATA_DIR=PATH` | The same, as an environment variable. |
| `--reimport` | Import the original game files again. |
| `--library DIR` / `DAS_LIBRARY_DIR` | Keep the imported library somewhere else. |

The library is our own open format, described in
[formats/pack.md](formats/pack.md).

## Tests

```sh
ctest --test-dir build --output-on-failure
```

Tests that need the original files run only when `DAS_GAMEDATA_DIR` is set:

```sh
DAS_GAMEDATA_DIR=gamedata ctest --test-dir build --output-on-failure
```
