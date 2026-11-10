# Sections

## STUP: title screen

288 x 288 bytes, one palette index per pixel, row by row. Shown while the game
starts.

## SNDS: sound effects

```
i16  -count            the count is stored negated
count times:
  u16  length          including the terminating zero
  char name[length]    e.g. "schwing.wav"
```

The list index is the sound number that scripts refer to. Names are file names
relative to the game folder (Indy) or its `sfx` folder (Yoda Stories).

## TILE: tiles

A plain array; the position is the tile number.

```
u32  flags
u8   pixels[32 * 32]   palette indices, row by row; index 0 is transparent
```

Indy has 1144 tiles, Yoda Stories 2123. Flag meanings are documented once they
are decoded.

## TNAM: tile names

Names for tiles that are items or characters.

```
repeat:
  u16  tile            0xFFFF ends the list
  char name[N]         zero-padded; N = 16 in Indy, 24 in Yoda Stories
```

## ZONE: zones

```
u16  count
count times: a zone entry
```

Indy: the entry is just the `IZON` block. Yoda Stories:

```
u16  planet
u32  size              bytes that follow, up to the next entry
u16  index             the zone's own number
IZON block
records                hotspots, IZAX, IZX2, IZX3, IZX4, IACT; see zone-records.md
```

### IZON block

```
char tag[4]            "IZON"
u32  size              counting from the tag up to the end of the grid
u16  width             9 or 18 tiles
u16  height
u32  type
(Yoda Stories only)
u16  unknown           0xFFFF in the zones seen so far
u16  planet            the same as the entry's planet
grid                   width * height cells, row by row
```

Each cell is three `u16` tile numbers, drawn in order: floor, object, overlay.
`0xFFFF` means empty.

## PUZ2 and CHAR: puzzles and characters

Both are lists:

```
repeat:
  u16  id              0xFFFF ends the list
  char tag[4]          "IPUZ" (PUZ2) or "ICHA" (CHAR)
  u32  size
  u8   body[size]
```

Yoda Stories has 205 puzzles and 77 characters, Indy 157 and 27. The bodies
are decoded in a later phase. Character names are zero-terminated but the
fixed field after the zero can hold leftover bytes, which must be kept.

## CHWP and CAUX: character values

```
CHWP repeat:  u16 character (0xFFFF ends), u16 reference, u16 value
CAUX repeat:  u16 character (0xFFFF ends), u16 value
```

What the values mean (weapon, health, damage) is still to be confirmed.
