// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decoding of character entries (ICHA bodies in the CHAR section).
#pragma once

#include "das/data/container.hpp"
#include "das/data/game_data.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace das::data {

// Facing directions, in the order the animation frames are stored.
enum class Facing : std::uint8_t {
    Up,
    Down,
    UpLeft,
    Left,
    DownLeft,
    UpRight,
    Right,
    DownRight,
};

inline constexpr int kFacingCount = 8;
inline constexpr int kFrameSets = 3; // standing, walk A, walk B

struct Character {
    std::string name;
    std::uint16_t type = 0;     // 1 = the hero
    std::uint16_t movement = 0;
    // frames[set][facing]: tile numbers.
    std::array<std::array<std::uint16_t, kFacingCount>, kFrameSets> frames{};

    std::uint16_t frame(int set, Facing facing) const {
        return frames[static_cast<std::size_t>(set)][static_cast<std::size_t>(facing)];
    }
};

// ICHA body: a 16-byte name field, u16 type, u16 movement, then on Yoda
// Stories 6 more bytes, then 24 u16 frame tiles.
Character parseCharacter(ByteView body, Layout layout);

// Writes a character back into an ICHA body. Bytes we do not decode (after
// the name's end, and Yoda Stories' 6 extra bytes) are taken from
// `original` when it has the right size, else they are zero.
Bytes writeCharacter(const Character& character, Layout layout, ByteView original = {});

// The player character (type 1), if the data has one.
std::optional<Character> findHero(const GameData& data);

} // namespace das::data
