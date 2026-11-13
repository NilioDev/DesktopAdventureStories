# Zone records

Besides its tile grid, every zone has hotspots, placed characters, item lists
and scripts. The two games store the same records in different places:

| Record | Yoda Stories | Indy |
| --- | --- | --- |
| Hotspots | after the IZON block | `HTSP` section |
| IZAX (characters, items) | after the hotspots | `ZAUX` section, one block per zone |
| IZX2 (provided items) | after IZAX | `ZAX2` section, one block per zone |
| IZX3 (NPCs) | after IZX2 | `ZAX3` section, one block per zone |
| IZX4 | after IZX3 | `ZAX4` section, one block per zone |
| Scripts (IACT) | after IZX4 | `ACTN` section |

## Yoda Stories zone entry, after the IZON block

```
u16  hotspot count
     hotspots
IZAX block
IZX2 block
IZX3 block
IZX4 block
u16  action count
     IACT blocks
```

## Indy sections

`ZAUX`, `ZAX2`, `ZAX3`, `ZAX4`: the blocks for zone 0, 1, 2, ... back to back.

`HTSP` and `ACTN` list only zones that have entries, in ascending zone order:

```
repeat:
  u16  zone            0xFFFF ends the list
  u16  count
  count hotspots (HTSP) or IACT blocks (ACTN)
```

Not every zone is listed; a zone that is not listed is different from a zone
listed with count 0, and we keep that difference so files round-trip.

## Hotspot (12 bytes)

```
u32  type
u16  x, y              tile position in the zone
u16  enabled
u16  argument          depends on the type, 0xFFFF for none
```

## IZAX block

`u32 size` counts from the tag.

```
char tag[4]            "IZAX"
u32  size
u16  unknown
u16  character count
     characters
u16  required item count, then that many u16 tile numbers
(Yoda Stories only)
u16  goal item count, then that many u16 tile numbers
```

A placed character is 6 bytes in Indy and 44 bytes in Yoda Stories:

```
u16  character         index into CHAR
u16  x, y
(Yoda Stories only)
u16  item              one more than the tile it drops; 0: the zone's own item
u32  drops             1: drops its item when it dies, 0: drops nothing
u32  waypoints[8]      four x, y pairs for patrols (movement 10)
```

The originals read these fields this way (see [combat.md](../combat.md)). Our
data model keeps the waypoints as sixteen 16-bit halves and the drop flag as
`unknown`, so files round-trip byte for byte.

## IZX2 and IZX3 blocks

`u32 size` counts from the tag. The content is a `u16` count followed by that
many `u16` tile numbers: items the zone can provide (IZX2), and NPCs (IZX3).

## IZX4 block

Unlike the others, its `u32 size` counts only the content, which is always 2:
a single `u16` whose meaning is still open.

## IACT block (script)

```
char tag[4]            "IACT"
u32  size              Yoda Stories: size of what follows. Indy: always 0
u16  condition count
     script lines
u16  instruction count
     script lines
```

A script line:

```
u16  opcode
i16  args[5]
u16  text length
char text[length]      not zero-terminated; empty for most opcodes
```

The opcodes are documented when we implement them in the engine.
