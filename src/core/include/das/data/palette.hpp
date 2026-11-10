// SPDX-License-Identifier: GPL-3.0-or-later
//
// The 256-colour palette. The data files store only palette indices; the
// colours live in the game's executable. See docs/formats/palette.md.
#pragma once

#include "das/data/binary.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace das::data {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    friend bool operator==(const Color&, const Color&) = default;
};

struct Palette {
    std::array<Color, 256> colors{};
};

// Finds the palette inside an original executable (DESKADV.EXE, YODESK.EXE).
std::optional<Palette> findPalette(ByteView executable);

// A neutral grey ramp, used when no executable is available.
Palette greyscalePalette();

} // namespace das::data
