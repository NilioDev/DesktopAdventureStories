// SPDX-License-Identifier: GPL-3.0-or-later
//
// The single files of a pack, for mods and the editor, which read and write
// parts of a pack on their own. See docs/formats/pack.md.
#pragma once

#include "das/data/game_data.hpp"
#include "das/util/json.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace das::pack::detail {

std::string zoneFileName(std::size_t index); // "zones/0042.json"
json::Value encodeZone(const data::Zone& zone, std::size_t index);
data::Zone decodeZone(const json::Value& v);

json::Value encodeTaggedEntries(const std::vector<data::TaggedEntry>& entries);
std::vector<data::TaggedEntry> decodeTaggedEntries(const json::Value& v);
json::Value encodeCharacterValues(const std::vector<data::CharacterValues>& values, bool withReference);
std::vector<data::CharacterValues> decodeCharacterValues(const json::Value& v, bool withReference);

} // namespace das::pack::detail
