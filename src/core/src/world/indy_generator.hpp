// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal: the world generator of Indiana Jones and His Desktop Adventures
// (see docs/world-generator.md).
#pragma once

#include "das/world/generator.hpp"

namespace das::world::indy {

WorldMap generate(const data::GameData& data, const GeneratorOptions& options);

} // namespace das::world::indy
