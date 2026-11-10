# File formats

Our own description of the Desktop Adventures data files, written from our own
analysis of the files. Every decoded section has a reader and a writer in
`src/core/src/data`, and `tests/test_game_files.cpp` checks that reading and
writing an original file gives back the exact same bytes.

| Document | Covers |
| --- | --- |
| [container.md](container.md) | The section list, and how the two games differ |
| [sections.md](sections.md) | VERS, STUP, SNDS, TILE, TNAM, ZONE (grid) |
| [zone-records.md](zone-records.md) | Hotspots, placed characters, item lists, scripts |
| [scripts.md](scripts.md) | What the zone scripts' conditions and instructions do |
| [palette.md](palette.md) | Where the colours come from |
| [pack.md](pack.md) | Our own open format that the originals are imported into |
| [mod.md](mod.md) | Mods: small overlays on a pack that change or add zones, tiles and sounds |

All numbers are little-endian. `u16`/`u32` are unsigned, `i16` is signed.

Status: the bodies of puzzles (IPUZ) and characters (ICHA) are kept as raw
bytes for now, and so are Indy's name sections ZNAM, PNAM and ANAM. Everything
else is decoded.
