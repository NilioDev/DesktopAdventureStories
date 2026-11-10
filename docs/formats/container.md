# Container

A data file is a sequence of sections. Each starts with a four-character tag.

| Tag | Framing |
| --- | --- |
| `VERS` | followed by a `u32` version, no length field (both games: `0x00000200`) |
| `ZONE` in Yoda Stories | no length field; see below |
| everything else | `u32` length, then that many bytes |

The file ends with `ENDF` (length 0). Nothing follows it.

## Section order

Indiana Jones and His Desktop Adventures (`DESKTOP.DAW`):

```
VERS STUP SNDS TILE ZONE ZAUX ZAX2 ZAX3 ZAX4 HTSP ACTN PUZ2 CHAR CHWP CAUX TNAM ZNAM PNAM ANAM ENDF
```

Star Wars: Yoda Stories (`YODESK.DTA`):

```
VERS STUP SNDS TILE ZONE PUZ2 CHAR CHWP CAUX TNAM ENDF
```

Yoda Stories moved the per-zone records that Indy keeps in `ZAUX`..`ACTN`
into each zone entry, and dropped the name sections `ZNAM`, `PNAM`, `ANAM`.

## The Yoda Stories ZONE section

It has no length field. Its content is a `u16` zone count followed by that
many entries, each of which begins with `u16 planet, u32 size` and then `size`
bytes. Walking the entries gives the end of the section.

To tell the two layouts apart before parsing, look at the bytes after the
`ZONE` tag: in Indy, `IZON` appears 6 bytes in (after `u32 length, u16 count`);
in Yoda Stories, 10 bytes in (after `u16 count, u16 planet, u32 size, u16 index`).
