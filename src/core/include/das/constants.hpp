// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace das {

// Tiles in the original data files are 32x32 pixels, 8-bit indexed colour.
inline constexpr int kTileSize = 32;

// The original games show a 9x9 tile window onto the current zone.
inline constexpr int kViewTiles = 9;
inline constexpr int kViewPixels = kViewTiles * kTileSize; // 288

} // namespace das
