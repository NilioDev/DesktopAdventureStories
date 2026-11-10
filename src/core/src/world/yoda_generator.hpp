// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal: the Yoda Stories world generator (see docs/world-generator.md).
#pragma once

#include "das/world/generator.hpp"

namespace das::world::yoda {

// The planet's world map; the caller adds Dagobah.
WorldMap generate(const data::GameData& data, const GeneratorOptions& options);

} // namespace das::world::yoda
