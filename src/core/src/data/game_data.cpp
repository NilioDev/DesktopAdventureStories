// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/data/game_data.hpp"

#include "zone_records.hpp"

#include <algorithm>

namespace das::data {

namespace {

constexpr std::uint16_t kListEnd = 0xffff;

// Fixed width of the name field in a TNAM entry.
std::size_t tileNameWidth(Layout layout) { return layout == Layout::Yoda ? 24 : 16; }

std::string fromZeroPadded(ByteView field) {
    const auto end = std::find(field.begin(), field.end(), std::uint8_t{0});
    return std::string(field.begin(), end);
}

std::size_t cellBytes(const Zone& zone) { return static_cast<std::size_t>(zone.width) * zone.height * 3 * 2; }

void readCells(ByteReader& reader, Zone& zone) {
    zone.cells.resize(static_cast<std::size_t>(zone.width) * zone.height);
    for (auto& cell : zone.cells) {
        for (auto& tile : cell.layers) {
            tile = reader.u16();
        }
    }
}

void writeCells(ByteWriter& writer, const Zone& zone) {
    if (zone.cells.size() != static_cast<std::size_t>(zone.width) * zone.height) {
        throw DataError("zone has " + std::to_string(zone.cells.size()) + " cells, expected width * height");
    }
    for (const auto& cell : zone.cells) {
        for (const auto tile : cell.layers) {
            writer.u16(tile);
        }
    }
}

// IZON block: tag, u32 size (counting from the tag), width, height, type,
// then on Yoda Stories two more u16 fields, then the cell grid.
constexpr std::size_t kIzonHeaderIndy = 4 + 4 + 2 + 2 + 4;
constexpr std::size_t kIzonHeaderYoda = kIzonHeaderIndy + 2 + 2;

Zone readIzon(ByteReader& reader, Layout layout) {
    const std::size_t start = reader.position();
    if (reader.tag() != "IZON") {
        throw DataError("expected IZON at offset " + std::to_string(start));
    }
    const std::uint32_t size = reader.u32();
    Zone zone;
    zone.width = reader.u16();
    zone.height = reader.u16();
    zone.type = reader.u32();
    if (layout == Layout::Yoda) {
        zone.izonField = reader.u16();
        zone.izonPlanet = reader.u16();
    }
    readCells(reader, zone);
    if (reader.position() - start != size) {
        throw DataError("IZON at offset " + std::to_string(start) + " has size " + std::to_string(size) +
                        " but its grid ends after " + std::to_string(reader.position() - start) + " bytes");
    }
    return zone;
}

void writeIzon(ByteWriter& writer, const Zone& zone, Layout layout) {
    const std::size_t header = layout == Layout::Yoda ? kIzonHeaderYoda : kIzonHeaderIndy;
    writer.tag("IZON");
    writer.u32(static_cast<std::uint32_t>(header + cellBytes(zone)));
    writer.u16(zone.width);
    writer.u16(zone.height);
    writer.u32(zone.type);
    if (layout == Layout::Yoda) {
        writer.u16(zone.izonField);
        writer.u16(zone.izonPlanet);
    }
    writeCells(writer, zone);
}

void requireEnd(const ByteReader& reader, const char* section) {
    if (!reader.atEnd()) {
        throw DataError(std::string(section) + " has " + std::to_string(reader.remaining()) +
                        " unexpected bytes at its end");
    }
}

void replacePayload(Container& container, std::string_view tag, Bytes payload) {
    if (auto* section = container.find(tag)) {
        section->payload = std::move(payload);
    }
}

// PUZ2 and CHAR: (u16 id, tag, u32 size, body)* then 0xFFFF.
std::vector<TaggedEntry> parseTaggedList(ByteView payload, std::string_view tag) {
    ByteReader reader(payload);
    std::vector<TaggedEntry> entries;
    for (;;) {
        const std::uint16_t id = reader.u16();
        if (id == kListEnd) {
            break;
        }
        if (reader.tag() != tag) {
            throw DataError("expected " + std::string(tag) + " in list at offset " + std::to_string(reader.position() - 4));
        }
        const auto body = reader.bytes(reader.u32());
        entries.push_back({id, Bytes(body.begin(), body.end())});
    }
    requireEnd(reader, std::string(tag).c_str());
    return entries;
}

Bytes writeTaggedList(const std::vector<TaggedEntry>& entries, std::string_view tag) {
    ByteWriter writer;
    for (const auto& entry : entries) {
        writer.u16(entry.id);
        writer.tag(tag);
        writer.u32(static_cast<std::uint32_t>(entry.body.size()));
        writer.bytes(entry.body);
    }
    writer.u16(kListEnd);
    return std::move(writer).take();
}

// CHWP: (u16 character, u16 reference, u16 value)*; CAUX: (u16 character,
// u16 value)*; both end with 0xFFFF.
std::vector<CharacterValues> parseCharacterValues(ByteView payload, bool withReference, const char* tag) {
    ByteReader reader(payload);
    std::vector<CharacterValues> values;
    for (;;) {
        const std::uint16_t character = reader.u16();
        if (character == kListEnd) {
            break;
        }
        CharacterValues v;
        v.character = character;
        if (withReference) {
            v.reference = reader.u16();
        }
        v.value = reader.u16();
        values.push_back(v);
    }
    requireEnd(reader, tag);
    return values;
}

Bytes writeCharacterValues(const std::vector<CharacterValues>& values, bool withReference) {
    ByteWriter writer;
    for (const auto& v : values) {
        writer.u16(v.character);
        if (withReference) {
            writer.u16(v.reference);
        }
        writer.u16(v.value);
    }
    writer.u16(kListEnd);
    return std::move(writer).take();
}

} // namespace

std::vector<std::string> parseSounds(ByteView payload) {
    ByteReader reader(payload);
    const std::int16_t negativeCount = reader.i16();
    if (negativeCount > 0) {
        throw DataError("SNDS count should be stored negated");
    }
    std::vector<std::string> sounds;
    for (int i = 0; i < -negativeCount; ++i) {
        const std::uint16_t length = reader.u16();
        const auto raw = reader.bytes(length);
        std::string name = fromZeroPadded(raw);
        if (name.size() + 1 != length) {
            throw DataError("SNDS entry " + std::to_string(i) + " is not a single zero-terminated name");
        }
        sounds.push_back(std::move(name));
    }
    requireEnd(reader, "SNDS");
    return sounds;
}

Bytes writeSounds(const std::vector<std::string>& sounds) {
    ByteWriter writer;
    writer.i16(static_cast<std::int16_t>(-static_cast<int>(sounds.size())));
    for (const auto& name : sounds) {
        writer.u16(static_cast<std::uint16_t>(name.size() + 1));
        writer.bytes(ByteView(reinterpret_cast<const std::uint8_t*>(name.data()), name.size()));
        writer.u8(0);
    }
    return std::move(writer).take();
}

std::vector<Tile> parseTiles(ByteView payload) {
    constexpr std::size_t kEntry = 4 + kTilePixels;
    if (payload.size() % kEntry != 0) {
        throw DataError("TILE size is not a multiple of one tile entry");
    }
    ByteReader reader(payload);
    std::vector<Tile> tiles(payload.size() / kEntry);
    for (auto& tile : tiles) {
        tile.flags = reader.u32();
        const auto pixels = reader.bytes(kTilePixels);
        std::copy(pixels.begin(), pixels.end(), tile.pixels.begin());
    }
    return tiles;
}

Bytes writeTiles(const std::vector<Tile>& tiles) {
    ByteWriter writer;
    for (const auto& tile : tiles) {
        writer.u32(tile.flags);
        writer.bytes(tile.pixels);
    }
    return std::move(writer).take();
}

std::vector<TileName> parseTileNames(ByteView payload, Layout layout) {
    ByteReader reader(payload);
    std::vector<TileName> names;
    for (;;) {
        const std::uint16_t tile = reader.u16();
        if (tile == kListEnd) {
            break;
        }
        const auto field = reader.bytes(tileNameWidth(layout));
        if (!std::all_of(field.begin() + static_cast<std::ptrdiff_t>(fromZeroPadded(field).size()), field.end(),
                         [](std::uint8_t b) { return b == 0; })) {
            throw DataError("TNAM entry for tile " + std::to_string(tile) + " has data after its name");
        }
        names.push_back({tile, fromZeroPadded(field)});
    }
    requireEnd(reader, "TNAM");
    return names;
}

Bytes writeTileNames(const std::vector<TileName>& names, Layout layout) {
    const std::size_t width = tileNameWidth(layout);
    ByteWriter writer;
    for (const auto& entry : names) {
        if (entry.name.size() >= width) {
            throw DataError("tile name '" + entry.name + "' is too long");
        }
        writer.u16(entry.tile);
        writer.bytes(ByteView(reinterpret_cast<const std::uint8_t*>(entry.name.data()), entry.name.size()));
        for (std::size_t i = entry.name.size(); i < width; ++i) {
            writer.u8(0);
        }
    }
    writer.u16(kListEnd);
    return std::move(writer).take();
}

std::vector<Zone> parseZones(ByteView payload, Layout layout) {
    ByteReader reader(payload);
    const std::uint16_t count = reader.u16();
    std::vector<Zone> zones;
    zones.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        if (layout == Layout::Indy) {
            zones.push_back(readIzon(reader, layout));
            continue;
        }
        const std::uint16_t planet = reader.u16();
        const std::uint32_t size = reader.u32();
        ByteReader entry(reader.bytes(size));
        const std::uint16_t index = entry.u16();
        Zone zone = readIzon(entry, layout);
        zone.planet = planet;
        zone.index = index;
        detail::readYodaZoneRecords(entry, zone);
        requireEnd(entry, "zone entry");
        zones.push_back(std::move(zone));
    }
    requireEnd(reader, "ZONE");
    return zones;
}

Bytes writeZones(const std::vector<Zone>& zones, Layout layout) {
    ByteWriter writer;
    writer.u16(static_cast<std::uint16_t>(zones.size()));
    for (const auto& zone : zones) {
        if (layout == Layout::Indy) {
            writeIzon(writer, zone, layout);
            continue;
        }
        writer.u16(zone.planet);
        const std::size_t sizeAt = writer.size();
        writer.u32(0);
        writer.u16(zone.index);
        writeIzon(writer, zone, layout);
        detail::writeYodaZoneRecords(writer, zone);
        writer.patchU32(sizeAt, static_cast<std::uint32_t>(writer.size() - sizeAt - 4));
    }
    return std::move(writer).take();
}

GameData parseGameData(ByteView file) {
    GameData data;
    data.container = readContainer(file);
    const Layout layout = data.container.layout;

    if (const auto* vers = data.container.find("VERS")) {
        ByteReader reader(vers->payload);
        data.version = reader.u32();
    }
    if (const auto* stup = data.container.find("STUP")) {
        if (stup->payload.size() != static_cast<std::size_t>(kSetupScreenSize * kSetupScreenSize)) {
            throw DataError("STUP is not a 288 x 288 image");
        }
        data.setupScreen = stup->payload;
    }
    if (const auto* snds = data.container.find("SNDS")) {
        data.sounds = parseSounds(snds->payload);
    }
    if (const auto* tile = data.container.find("TILE")) {
        data.tiles = parseTiles(tile->payload);
    }
    if (const auto* tnam = data.container.find("TNAM")) {
        data.tileNames = parseTileNames(tnam->payload, layout);
    }
    if (const auto* zone = data.container.find("ZONE")) {
        data.zones = parseZones(zone->payload, layout);
    }
    if (layout == Layout::Indy) {
        detail::readIndyZoneSections(data.container, data.zones);
    }
    if (const auto* puz2 = data.container.find("PUZ2")) {
        data.puzzles = parseTaggedList(puz2->payload, "IPUZ");
    }
    if (const auto* chars = data.container.find("CHAR")) {
        data.characters = parseTaggedList(chars->payload, "ICHA");
    }
    if (const auto* chwp = data.container.find("CHWP")) {
        data.characterWeapons = parseCharacterValues(chwp->payload, true, "CHWP");
    }
    if (const auto* caux = data.container.find("CAUX")) {
        data.characterAux = parseCharacterValues(caux->payload, false, "CAUX");
    }
    return data;
}

Bytes writeGameData(const GameData& data) {
    Container container = data.container;
    const Layout layout = container.layout;
    {
        ByteWriter vers;
        vers.u32(data.version);
        replacePayload(container, "VERS", std::move(vers).take());
    }
    replacePayload(container, "STUP", data.setupScreen);
    replacePayload(container, "SNDS", writeSounds(data.sounds));
    replacePayload(container, "TILE", writeTiles(data.tiles));
    replacePayload(container, "TNAM", writeTileNames(data.tileNames, layout));
    replacePayload(container, "ZONE", writeZones(data.zones, layout));
    if (layout == Layout::Indy) {
        detail::writeIndyZoneSections(container, data.zones);
    }
    replacePayload(container, "PUZ2", writeTaggedList(data.puzzles, "IPUZ"));
    replacePayload(container, "CHAR", writeTaggedList(data.characters, "ICHA"));
    replacePayload(container, "CHWP", writeCharacterValues(data.characterWeapons, true));
    replacePayload(container, "CAUX", writeCharacterValues(data.characterAux, false));
    return writeContainer(container);
}

} // namespace das::data
