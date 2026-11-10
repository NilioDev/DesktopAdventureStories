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
records                hotspots, IZAX, IZX2, IZX3, IZX4, IACT (not decoded yet)
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
