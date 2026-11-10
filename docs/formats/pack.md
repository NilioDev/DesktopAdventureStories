# Our own format: the data pack

When the program first finds an original game it imports it into a *pack*: a
folder of plain JSON and PNG files that anyone can read, diff and edit. After
that the program uses only the pack. The import is lossless: turning a pack
back into the original format gives the original file byte for byte
(`tests/test_game_files.cpp` checks this for both games).

The pack is also the starting point for the editor and for mods.

```
library/
└── yoda/                   one folder per game: "yoda" or "indy"
    ├── pack.json           manifest
    ├── tiles.json          tile flags and names
    ├── tiles.png           every tile, 32 per row
    ├── setup.png           288 x 288 title screen
    ├── zones/0000.json     one file per zone
    ├── puzzles.json
    ├── characters.json
    ├── sounds/*.wav, *.mid copied unchanged from the game
    └── raw/*.bin           sections we do not decode yet
```

Text is UTF-8. The original games use Windows-1252; the five bytes that
Windows-1252 leaves undefined are stored as the matching C1 control characters,
so every byte survives. Tile number `-1` means "no tile".

## pack.json

| Key | Meaning |
| --- | --- |
| `format` | Always `"desktop-adventure-stories-pack"` |
| `formatVersion` | `1`. A reader refuses packs with a higher number |
| `game`, `layout` | `"yoda"` or `"indy"`; `layout` decides how the original file is written |
| `fileVersion` | The VERS value of the original file |
| `sections` | The section tags of the original file in order. A section with a `file` is stored as raw bytes in that file; the others are rebuilt from the decoded data |
| `tiles`, `puzzles`, `characters` | File names of those parts |
| `setupScreen` | `"setup.png"`, or `null` |
| `sounds` | The sound list, in the order scripts refer to it: `{"name": original path, "file": "sounds/x.wav"}`. `file` is missing when the sound was not found during import |
| `zones` | Zone files, in zone number order |

## tiles.json and tiles.png

`tiles.png` is an 8-bit palette PNG whose palette is the game palette, with
index 0 transparent. Tile *n* is at column `n % columns`, row `n / columns`,
each `tileSize` (32) pixels square. `tiles.json` holds `count`, `columns`,
`tileSize`, `flags` (one number per tile, see
[sections.md](sections.md#tile-tiles)) and `names` (`{"tile", "name"}`, in file
order).

## zones/NNNN.json

| Key | Meaning |
| --- | --- |
| `id`, `width`, `height`, `type`, `planet` | As in the original IZON entry |
| `index`, `izonField`, `izonPlanet` | Yoda Stories entry fields (see [sections.md](sections.md)) |
| `layers` | Three arrays of `width * height` tile numbers, row by row: floor, objects, roof |
| `hotspots` | `{"type", "x", "y", "enabled", "argument"}`, or `null` when an Indy zone is not listed in HTSP |
| `monsters` | Placed characters (IZAX) |
| `requiredItems`, `goalItems`, `providedItems`, `npcs` | Item and character lists (IZAX, IZX2, IZX3) |
| `auxField`, `izx4` | Values whose meaning is not known yet |
| `actions` | Scripts: `{"conditions": [...], "instructions": [...]}` of `{"opcode", "args": [5 numbers], "text"}`; `null` when an Indy zone is not listed in ACTN |

See [zone-records.md](zone-records.md) for what the fields mean.

## puzzles.json and characters.json

Puzzles and characters are still stored as hex strings of their original
entry bodies (`{"id", "body"}`); they get proper fields once their layouts are
fully worked out. `characters.json` also holds `weapons` (CHWP) and `aux`
(CAUX).

## Implementation

Everything is our own code in `src/core`: JSON (`util/json`), DEFLATE and zlib
(`util/zlib`), PNG (`util/png`), zip reading (`util/zip`), and the pack itself
(`pack/pack`, `pack/import`).
