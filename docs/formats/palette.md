# Palette

The data files contain only palette indices. The 256 colours are stored in the
game's executable (`DESKADV.EXE`, `YODESK.EXE`) as a Windows-style table:
256 entries of `blue, green, red, 0`.

We do not hard-code an offset. `findPalette` scans the executable for a
1024-byte block where:

- every fourth byte is 0,
- entry 0 is black and entry 255 is white (as in the Windows system palette),
- at least 200 of the entries are different colours.

Each original executable has exactly one such block. The palette is read from
the player's own executable when the game loads; it is never stored in this
repository.

Some palette entries are animated in the original games (water, lights). That
is not handled yet.
