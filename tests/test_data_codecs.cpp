// SPDX-License-Identifier: GPL-3.0-or-later
//
// Codec tests on small hand-made inputs. They need no game files.
#include "test.hpp"

#include "das/data/binary.hpp"
#include "das/data/container.hpp"
#include "das/data/game_data.hpp"
#include "das/data/palette.hpp"

#include <initializer_list>

using namespace das::data;

namespace {

Bytes bytes(std::initializer_list<int> values) {
    Bytes out;
    for (int v : values) {
        out.push_back(static_cast<std::uint8_t>(v));
    }
    return out;
}

void append(Bytes& out, const Bytes& more) { out.insert(out.end(), more.begin(), more.end()); }

void appendText(Bytes& out, std::string_view text) { out.insert(out.end(), text.begin(), text.end()); }

// A 1x1 zone: IZON block with a single cell.
Bytes izon(Layout layout, std::uint16_t tile) {
    ByteWriter w;
    w.tag("IZON");
    w.u32(layout == Layout::Yoda ? 26 : 22);
    w.u16(1);
    w.u16(1);
    w.u32(3);
    if (layout == Layout::Yoda) {
        w.u16(0xffff);
        w.u16(2);
    }
    w.u16(tile);
    w.u16(kNoTile);
    w.u16(kNoTile);
    return std::move(w).take();
}

} // namespace

DAS_TEST("byte reader reads little-endian values and reports overruns") {
    const Bytes data = bytes({0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 0xfe, 0xff});
    ByteReader r(data);
    DAS_CHECK(r.u16() == 0x1234);
    DAS_CHECK(r.u32() == 0x12345678);
    DAS_CHECK(r.i16() == -2);
    DAS_CHECK(r.atEnd());
    bool threw = false;
    try {
        r.u8();
    } catch (const DataError&) {
        threw = true;
    }
    DAS_CHECK(threw);
}

DAS_TEST("byte writer patches sizes in place") {
    ByteWriter w;
    w.u32(0);
    w.u16(0xbeef);
    w.patchU32(0, 0x01020304);
    DAS_CHECK(w.data() == bytes({4, 3, 2, 1, 0xef, 0xbe}));
}

DAS_TEST("sound list round-trips") {
    const std::vector<std::string> sounds{"door.wav", "whip.wav"};
    const Bytes encoded = writeSounds(sounds);
    DAS_CHECK(encoded.size() == 2 + (2 + 9) + (2 + 9));
    DAS_CHECK(encoded[0] == 0xfe && encoded[1] == 0xff); // -2
    DAS_CHECK(parseSounds(encoded) == sounds);
}

DAS_TEST("tile names use a wider field in Yoda Stories") {
    const std::vector<TileName> names{{7, "Whip"}, {300, "Golden Idol"}};
    const Bytes indy = writeTileNames(names, Layout::Indy);
    const Bytes yoda = writeTileNames(names, Layout::Yoda);
    DAS_CHECK(indy.size() == 2 * (2 + 16) + 2);
    DAS_CHECK(yoda.size() == 2 * (2 + 24) + 2);
    const auto back = parseTileNames(yoda, Layout::Yoda);
    DAS_CHECK(back.size() == 2 && back[1].tile == 300 && back[1].name == "Golden Idol");
}

DAS_TEST("container reads both layouts and writes them back unchanged") {
    // Indy layout: ZONE has a length field.
    Bytes indy;
    appendText(indy, "VERS");
    append(indy, bytes({0, 2, 0, 0}));
    appendText(indy, "ZONE");
    const Bytes zoneIndy = izon(Layout::Indy, 5);
    append(indy, bytes({static_cast<int>(zoneIndy.size() + 2), 0, 0, 0, 1, 0}));
    append(indy, zoneIndy);
    appendText(indy, "ENDF");
    append(indy, bytes({0, 0, 0, 0}));

    const Container ci = readContainer(indy);
    DAS_CHECK(ci.layout == Layout::Indy);
    DAS_CHECK(ci.sections.size() == 3);
    DAS_CHECK(writeContainer(ci) == indy);

    const GameData gi = parseGameData(indy);
    DAS_CHECK(gi.zones.size() == 1 && gi.zones[0].at(0, 0).layers[0] == 5);
    DAS_CHECK(writeGameData(gi) == indy);

    // Yoda layout: ZONE has no length field; each entry has its own size.
    Bytes yoda;
    appendText(yoda, "VERS");
    append(yoda, bytes({0, 2, 0, 0}));
    appendText(yoda, "ZONE");
    const Bytes zoneYoda = izon(Layout::Yoda, 9);
    // Records after the grid: one hotspot, empty IZAX, IZX2 with one tile,
    // empty IZX3, IZX4, and one action with one condition.
    ByteWriter records;
    records.u16(1);
    records.u32(5); // hotspot type
    records.u16(3);
    records.u16(4);
    records.u16(1);
    records.u16(0xffff);
    records.tag("IZAX");
    records.u32(16);
    for (int i = 0; i < 4; ++i) {
        records.u16(0);
    }
    records.tag("IZX2");
    records.u32(12);
    records.u16(1);
    records.u16(77);
    records.tag("IZX3");
    records.u32(10);
    records.u16(0);
    records.tag("IZX4");
    records.u32(2);
    records.u16(1);
    records.u16(1); // one action
    records.tag("IACT");
    records.u32(2 + 14 + 2 + 2);
    records.u16(1); // one condition
    records.u16(6);
    for (int i = 0; i < 5; ++i) {
        records.i16(static_cast<std::int16_t>(-i));
    }
    records.u16(2);
    records.u8('h');
    records.u8('i');
    records.u16(0); // no instructions
    const Bytes recordBytes = std::move(records).take();

    const auto entrySize = static_cast<int>(2 + zoneYoda.size() + recordBytes.size());
    append(yoda, bytes({1, 0, 2, 0, entrySize & 0xff, entrySize >> 8, 0, 0, 4, 0}));
    append(yoda, zoneYoda);
    append(yoda, recordBytes);
    appendText(yoda, "ENDF");
    append(yoda, bytes({0, 0, 0, 0}));

    const GameData gy = parseGameData(yoda);
    DAS_CHECK(gy.layout() == Layout::Yoda);
    DAS_CHECK(gy.zones.size() == 1);
    DAS_CHECK(gy.zones[0].planet == 2 && gy.zones[0].index == 4 && gy.zones[0].izonPlanet == 2);
    const Zone& z = gy.zones[0];
    DAS_CHECK(z.hotspots && z.hotspots->size() == 1 && (*z.hotspots)[0].type == 5 && (*z.hotspots)[0].y == 4);
    DAS_CHECK(z.providedItems == std::vector<std::uint16_t>{77});
    DAS_CHECK(z.npcs.empty() && z.izx4 == 1);
    DAS_CHECK(z.actions && z.actions->size() == 1);
    const auto& line = (*z.actions)[0].conditions.at(0);
    DAS_CHECK(line.opcode == 6 && line.args[4] == -4 && line.text == "hi");
    DAS_CHECK(writeGameData(gy) == yoda);
}

DAS_TEST("files that are not data files are rejected") {
    bool threw = false;
    try {
        (void)readContainer(bytes({'M', 'Z', 0, 0, 0, 0, 0, 0}));
    } catch (const DataError&) {
        threw = true;
    }
    DAS_CHECK(threw);
}

DAS_TEST("palette is found by its shape") {
    Bytes exe(300, 0x90);
    Bytes table(1024, 0);
    for (std::size_t i = 1; i < 256; ++i) {
        table[i * 4 + 0] = static_cast<std::uint8_t>(i);       // blue
        table[i * 4 + 1] = static_cast<std::uint8_t>(255 - i); // green
        table[i * 4 + 2] = static_cast<std::uint8_t>(i / 2);   // red
    }
    table[1020] = table[1021] = table[1022] = 0xff;
    append(exe, table);
    append(exe, Bytes(50, 0x90));

    const auto palette = findPalette(exe);
    DAS_CHECK(palette.has_value());
    DAS_CHECK(palette->colors[0] == (Color{0, 0, 0}));
    DAS_CHECK(palette->colors[10] == (Color{5, 245, 10}));
    DAS_CHECK(palette->colors[255] == (Color{255, 255, 255}));
    DAS_CHECK(!findPalette(Bytes(2000, 0)).has_value());
}
