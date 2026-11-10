// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decoding of puzzle entries (IPUZ bodies in the PUZ2 section). A puzzle is
// one item with the lines people say about it: asking for it, thanking for
// it, offering it. The world generator chains puzzles into a quest; see
// docs/formats/puzzles.md.
#pragma once

#include "das/data/container.hpp"
#include "das/data/game_data.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace das::data {

enum class PuzzleType : std::uint32_t {
    Trade = 0, // can be asked for and handed out
    Offer = 1, // other words for handing an item out
    Final = 2, // hands out an item the goal zone asks for
    Goal = 3,  // the mission of a game
    Disabled = 0xffffffff,
};

enum class PuzzleText : std::size_t {
    Request = 0, // someone asks for the item
    Thanks = 1,  // after they got it
    Offer = 2,   // someone offers the item; for a goal: the mission hint
    Mission = 3, // goal only: the briefing at the start of a game
    Unused = 4,
};

inline constexpr std::size_t kPuzzleTexts = 5;

struct Puzzle {
    std::uint32_t type = 0;
    std::uint32_t itemClass = 0; // a category of the item; meaning not settled
    std::uint32_t unknown = 0;   // Yoda Stories only
    std::uint16_t flags = 0;
    std::array<std::string, kPuzzleTexts> texts; // raw Windows-1252 bytes
    std::uint16_t item1 = kNoTile;
    std::uint16_t item2 = kNoTile; // Yoda Stories only; used by goals

    PuzzleType kind() const { return static_cast<PuzzleType>(type); }
    const std::string& text(PuzzleText which) const { return texts[static_cast<std::size_t>(which)]; }
    friend bool operator==(const Puzzle&, const Puzzle&) = default;
};

Puzzle parsePuzzle(ByteView body, Layout layout);
Bytes writePuzzle(const Puzzle& puzzle, Layout layout);

// All puzzles of the data, in PUZ2 order (index = puzzle id).
std::vector<Puzzle> parsePuzzles(const GameData& data);

} // namespace das::data
