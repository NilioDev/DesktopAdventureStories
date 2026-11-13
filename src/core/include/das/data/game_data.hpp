// SPDX-License-Identifier: GPL-3.0-or-later
//
// A game data file with its sections decoded. Sections we do not decode yet
// are kept as raw bytes, so writing the data back gives the identical file.
// See docs/formats/ for the layouts.
#pragma once

#include "das/data/binary.hpp"
#include "das/data/container.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace das::data {

inline constexpr int kTilePixels = 32 * 32;
inline constexpr int kSetupScreenSize = 288;
inline constexpr std::uint16_t kNoTile = 0xffff;

struct Tile {
    std::uint32_t flags = 0;
    // 8-bit palette indices, row by row. Index 0 is transparent.
    std::array<std::uint8_t, kTilePixels> pixels{};
};

struct TileName {
    std::uint16_t tile = 0;
    std::string name;
};

// One grid position of a zone: up to three tiles drawn on top of each other.
struct ZoneCell {
    std::array<std::uint16_t, 3> layers{kNoTile, kNoTile, kNoTile};
};

// A trigger area in a zone (doors, item spots, spawn points...).
struct Hotspot {
    std::uint32_t type = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t enabled = 0;
    std::uint16_t argument = 0;
    friend bool operator==(const Hotspot&, const Hotspot&) = default;
};

// A character placed in a zone (IZAX).
struct ZoneMonster {
    std::uint16_t character = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    // Yoda Stories only.
    std::uint16_t item = kNoTile;       // one more than the tile it drops; 0: the zone's item
    std::uint32_t unknown = 0;          // 1: it drops its item when it dies
    std::array<std::uint16_t, 16> waypoints{}; // four 32-bit x, y pairs, as 16-bit halves
};

// One script line: a condition or an instruction (IACT).
struct ScriptLine {
    std::uint16_t opcode = 0;
    std::array<std::int16_t, 5> args{};
    std::string text; // raw bytes, not zero-terminated
};

// A script: when all conditions hold, the instructions run.
struct Action {
    std::vector<ScriptLine> conditions;
    std::vector<ScriptLine> instructions;
};

struct Zone {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t type = 0;
    std::uint16_t planet = 0;
    std::vector<ZoneCell> cells; // width * height, row by row

    // Indy keeps hotspots and actions in separate sections that list only
    // some zones; "not listed" is kept apart from "listed, but empty".
    std::optional<std::vector<Hotspot>> hotspots;
    std::optional<std::vector<Action>> actions;

    std::uint16_t auxField = 0;             // IZAX: first field, meaning unknown
    std::vector<ZoneMonster> monsters;      // IZAX
    std::vector<std::uint16_t> requiredItems; // IZAX
    std::vector<std::uint16_t> goalItems;     // IZAX, Yoda Stories only
    std::vector<std::uint16_t> providedItems; // IZX2
    std::vector<std::uint16_t> npcs;          // IZX3
    std::uint16_t izx4 = 0;                   // IZX4, meaning unknown

    // Yoda Stories only: fields of the per-zone entry around the grid.
    std::uint16_t index = 0;
    std::uint16_t izonField = 0;
    std::uint16_t izonPlanet = 0;

    const ZoneCell& at(int x, int y) const { return cells.at(static_cast<std::size_t>(y * width + x)); }
};

// An entry of a list section such as PUZ2 (IPUZ) or CHAR (ICHA). The entry
// body is not decoded yet.
struct TaggedEntry {
    std::uint16_t id = 0;
    Bytes body;
};

// CHWP and CAUX: per-character values. Meaning still to be confirmed.
struct CharacterValues {
    std::uint16_t character = 0;
    std::uint16_t reference = 0; // CHWP only
    std::uint16_t value = 0;
};

struct GameData {
    Container container; // all sections, in file order
    std::uint32_t version = 0;
    std::vector<std::uint8_t> setupScreen; // 288 x 288 palette indices
    std::vector<std::string> sounds;
    std::vector<Tile> tiles;
    std::vector<TileName> tileNames;
    std::vector<Zone> zones;
    std::vector<TaggedEntry> puzzles;    // PUZ2
    std::vector<TaggedEntry> characters; // CHAR
    std::vector<CharacterValues> characterWeapons; // CHWP
    std::vector<CharacterValues> characterAux;     // CAUX

    Layout layout() const { return container.layout; }
};

GameData parseGameData(ByteView file);

// Encodes the decoded sections and writes the whole file.
Bytes writeGameData(const GameData& data);

// Single-section codecs, exposed for tests and tools.
std::vector<std::string> parseSounds(ByteView payload);
Bytes writeSounds(const std::vector<std::string>& sounds);
std::vector<Tile> parseTiles(ByteView payload);
Bytes writeTiles(const std::vector<Tile>& tiles);
std::vector<TileName> parseTileNames(ByteView payload, Layout layout);
Bytes writeTileNames(const std::vector<TileName>& names, Layout layout);
std::vector<Zone> parseZones(ByteView payload, Layout layout);
Bytes writeZones(const std::vector<Zone>& zones, Layout layout);

} // namespace das::data
