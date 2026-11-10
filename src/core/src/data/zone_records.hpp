// SPDX-License-Identifier: GPL-3.0-or-later
//
// Codecs for the records attached to a zone. Yoda Stories stores them after
// the zone's grid; Indy stores them in separate top-level sections.
// Internal to das_core; see docs/formats/zone-records.md.
#pragma once

#include "das/data/binary.hpp"
#include "das/data/game_data.hpp"

namespace das::data::detail {

std::vector<Hotspot> readHotspots(ByteReader& reader, std::size_t count);
void writeHotspots(ByteWriter& writer, const std::vector<Hotspot>& hotspots);

void readIzax(ByteReader& reader, Zone& zone, Layout layout);
void writeIzax(ByteWriter& writer, const Zone& zone, Layout layout);

std::vector<std::uint16_t> readTileList(ByteReader& reader, std::string_view tag);
void writeTileList(ByteWriter& writer, std::string_view tag, const std::vector<std::uint16_t>& tiles);

std::uint16_t readIzx4(ByteReader& reader);
void writeIzx4(ByteWriter& writer, std::uint16_t value);

std::vector<Action> readActions(ByteReader& reader, std::size_t count, Layout layout);
void writeActions(ByteWriter& writer, const std::vector<Action>& actions, Layout layout);

// Yoda Stories: everything after the IZON block of a zone entry.
void readYodaZoneRecords(ByteReader& reader, Zone& zone);
void writeYodaZoneRecords(ByteWriter& writer, const Zone& zone);

// Indy: the separate sections. `read*` fill the given zones.
void readIndyZoneSections(const Container& container, std::vector<Zone>& zones);
void writeIndyZoneSections(Container& container, const std::vector<Zone>& zones);

} // namespace das::data::detail
