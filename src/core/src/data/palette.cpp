// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/data/palette.hpp"

#include <algorithm>
#include <unordered_set>

namespace das::data {

namespace {

constexpr std::size_t kEntries = 256;
constexpr std::size_t kTableSize = kEntries * 4;
constexpr std::size_t kMinDistinctColors = 200;

bool looksLikePalette(ByteView table) {
    // Windows palette entries are blue, green, red, then a zero byte.
    // Entry 0 is black and entry 255 is white, like the Windows system palette.
    const auto entry = [&](std::size_t i) { return table.subspan(i * 4, 4); };
    const auto first = entry(0);
    const auto last = entry(kEntries - 1);
    if (!std::all_of(first.begin(), first.end(), [](std::uint8_t b) { return b == 0; })) {
        return false;
    }
    if (last[0] != 0xff || last[1] != 0xff || last[2] != 0xff || last[3] != 0) {
        return false;
    }
    std::unordered_set<std::uint32_t> distinct;
    for (std::size_t i = 0; i < kEntries; ++i) {
        const auto e = entry(i);
        if (e[3] != 0) {
            return false;
        }
        distinct.insert(static_cast<std::uint32_t>(e[0] | (e[1] << 8) | (e[2] << 16)));
    }
    return distinct.size() >= kMinDistinctColors;
}

} // namespace

std::optional<Palette> findPalette(ByteView executable) {
    if (executable.size() < kTableSize) {
        return std::nullopt;
    }
    for (std::size_t offset = 0; offset + kTableSize <= executable.size(); ++offset) {
        // Cheap pre-checks before the full test.
        if (executable[offset + 3] != 0 || executable[offset + kTableSize - 4] != 0xff ||
            executable[offset + kTableSize - 1] != 0) {
            continue;
        }
        const auto table = executable.subspan(offset, kTableSize);
        if (!looksLikePalette(table)) {
            continue;
        }
        Palette palette;
        for (std::size_t i = 0; i < kEntries; ++i) {
            palette.colors[i] = Color{table[i * 4 + 2], table[i * 4 + 1], table[i * 4]};
        }
        return palette;
    }
    return std::nullopt;
}

Palette greyscalePalette() {
    Palette palette;
    for (std::size_t i = 0; i < kEntries; ++i) {
        const auto v = static_cast<std::uint8_t>(i);
        palette.colors[i] = Color{v, v, v};
    }
    return palette;
}

} // namespace das::data
