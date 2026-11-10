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

struct Zone {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t type = 0;
    std::uint16_t planet = 0;
    std::vector<ZoneCell> cells; // width * height, row by row

    // Yoda Stories only: fields of the per-zone entry around the grid, and
    // the zone's remaining records (decoded in a later phase).
    std::uint16_t index = 0;
    std::uint16_t izonField = 0;
    std::uint16_t izonPlanet = 0;
    Bytes extra;

    const ZoneCell& at(int x, int y) const { return cells.at(static_cast<std::size_t>(y * width + x)); }
};

struct GameData {
    Container container; // all sections, in file order
    std::uint32_t version = 0;
    std::vector<std::uint8_t> setupScreen; // 288 x 288 palette indices
    std::vector<std::string> sounds;
    std::vector<Tile> tiles;
    std::vector<TileName> tileNames;
    std::vector<Zone> zones;

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
