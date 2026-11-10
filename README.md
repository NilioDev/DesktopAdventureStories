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

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
