# Contributing

## Clean-room rules

DesktopAdventureStories is an independent, from-scratch implementation.

1. **Write your own code.** Do not copy or adapt code from other Desktop Adventures
   reimplementations or tools, whatever their license. They may be read only as
   documentation of the file format; describe what you learned in your own words in
   `docs/formats/` and implement from that description.
2. **No decompiled code.** A disassembler or debugger may be used to observe how the
   original engine behaves. Record the behaviour as a specification, never paste
   decompiler output into the source tree.
3. **No game data.** Never commit original data files, executables, or anything
   extracted from them (tiles, sprites, sounds, text, palettes). Tests that need game
   data must skip when the files are not present.

## Layout

| Path | What |
| --- | --- |
| `src/core` | Engine core. Platform independent, no SDL, fully unit-testable. |
| `src/app` | Platform layer and game executable (SDL3). |
| `tests` | Unit tests for the core. |
| `android` | Gradle project that packages `src/app` as an Android app. |
| `docs` | Our own documentation, including the file formats. |

## Code style

C++20. Warnings are errors in CI (`-DDAS_WARNINGS_AS_ERRORS=ON`). Every source file
starts with `// SPDX-License-Identifier: GPL-3.0-or-later`.
