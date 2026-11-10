// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bits of a tile's flags word, as found by our analysis of how tiles are used
// in the zones (see docs/formats/sections.md).
#pragma once

#include <cstdint>

namespace das::data::tile_flags {

inline constexpr std::uint32_t kTransparent = 1u << 0; // pixel index 0 is see-through
inline constexpr std::uint32_t kFloor = 1u << 1;
inline constexpr std::uint32_t kObject = 1u << 2;      // blocks movement
inline constexpr std::uint32_t kDraggable = 1u << 3;   // can be pushed
inline constexpr std::uint32_t kRoof = 1u << 4;        // drawn above the characters
inline constexpr std::uint32_t kMap = 1u << 5;         // world map (locator) tile
inline constexpr std::uint32_t kWeapon = 1u << 6;
inline constexpr std::uint32_t kItem = 1u << 7;      // can be picked up and carried
inline constexpr std::uint32_t kCharacter = 1u << 8; // a person or creature

// Anything on these layers with one of these bits stops the player.
inline constexpr std::uint32_t kBlocking = kObject | kCharacter | kItem | kWeapon;

} // namespace das::data::tile_flags
