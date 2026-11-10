// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading and writing 8-bit palette PNG images, the format our own data
// packs use for tiles and screens. See docs/formats/pack.md.
#pragma once

#include "das/data/binary.hpp"
#include "das/data/palette.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace das::png {

struct IndexedImage {
    int width = 0;
    int height = 0;
    data::Palette palette;
    // When set, this palette index is stored as fully transparent.
    std::optional<std::uint8_t> transparentIndex;
    std::vector<std::uint8_t> pixels; // width * height palette indices
};

data::Bytes write(const IndexedImage& image);
// Reads a PNG with colour type 3 (palette) and bit depth 8. Throws
// data::DataError for anything else.
IndexedImage read(data::ByteView file);

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // width * height * 4: red, green, blue, alpha
};

// Reads any 8-bit PNG without interlacing (grey, RGB, palette, with or
// without alpha), as drawing programs save them.
RgbaImage readRgba(data::ByteView file);

// Turns colours into palette indices: mostly transparent pixels become
// index 0, the others the palette colour that is equal or closest.
std::vector<std::uint8_t> toPaletteIndices(const RgbaImage& image, const data::Palette& palette);

} // namespace das::png
