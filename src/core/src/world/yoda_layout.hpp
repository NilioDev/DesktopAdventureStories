// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal: the Yoda Stories world map layout (see docs/world-generator.md).
#pragma once

#include <array>
#include <cstdint>

namespace das::world::yoda {

inline constexpr int kSide = 10;
inline constexpr int kCells = kSide * kSide;

// The random number sequence of the original (the C library of its compiler).
class Rng {
public:
    void seed(std::uint32_t value) { state_ = value; }
    int next(); // 0..32767
private:
    std::uint32_t state_ = 1;
};

// What a map cell is while the map is laid out. The numbers are the ones the
// original uses; their order matters in a few comparisons.
enum class Cell : std::int16_t {
    None = 0,
    Plain = 1,
    TravelStart = 0x65,
    Dock = 0x66, // an island's landing
    Island = 0x68,
    Town = 0xc9,
    Behind = 0x12c,         // past a blockade
    BlockadeToWest = 0x12d, // what it guards lies west of it
    BlockadeToEast = 0x12e,
    BlockadeToNorth = 0x12f,
    BlockadeToSouth = 0x130,
    KeptClear = 0x131, // must stay empty
    PuzzleSlot = 0x132,
};

// Distance from the middle: 1 for the four middle cells, 5 on the edge.
int ring(int x, int y);

class Layout {
public:
    explicit Layout(Rng& rng) : rng_(rng) { puzzle.fill(-1); }

    std::array<Cell, kCells> cells{};
    std::array<std::int16_t, kCells> puzzle{}; // puzzle number per cell, -1: none

    // Cells may be addressed one past a row's end: like the original, that
    // reaches into the next or previous row.
    Cell& at(int x, int y) { return cells[static_cast<std::size_t>(y * kSide + x)]; }
    Cell at(int x, int y) const { return cells[static_cast<std::size_t>(y * kSide + x)]; }

    void placeRing(int ringType, int count, int maxTravel, int maxBlockades);
    void placeEdge(int count);
    void placeIslands(int count);
    int assignPuzzles();

    int travels() const { return travels_; }
    int blockades() const { return blockades_; }
    int cellCount() const { return cells_; }

private:
    int islandDirection(int x, int y) const;

    Rng& rng_;
    int travels_ = 0;
    int blockades_ = 0;
    int cells_ = 4;
    Cell neighbour_ = Cell::None;
    std::array<int, 4> islandLength_{};
};

} // namespace das::world::yoda
