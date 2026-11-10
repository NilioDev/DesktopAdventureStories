// SPDX-License-Identifier: GPL-3.0-or-later
#include "zone_records.hpp"

#include <map>

namespace das::data::detail {

namespace {

constexpr std::uint16_t kListEnd = 0xffff;

void expectTag(ByteReader& reader, std::string_view tag) {
    const std::size_t at = reader.position();
    const std::string found = reader.tag();
    if (found != tag) {
        throw DataError("expected " + std::string(tag) + " at offset " + std::to_string(at) + ", found '" + found + "'");
    }
}

// Blocks whose u32 size counts from the tag (IZAX, IZX2, IZX3).
template <typename Body>
void readSizedBlock(ByteReader& reader, std::string_view tag, Body&& body) {
    const std::size_t start = reader.position();
    expectTag(reader, tag);
    const std::uint32_t size = reader.u32();
    body();
    if (reader.position() - start != size) {
        throw DataError(std::string(tag) + " at offset " + std::to_string(start) + " has size " +
                        std::to_string(size) + " but its content is " + std::to_string(reader.position() - start) +
                        " bytes");
    }
}

template <typename Body>
void writeSizedBlock(ByteWriter& writer, std::string_view tag, Body&& body) {
    const std::size_t start = writer.size();
    writer.tag(tag);
    writer.u32(0);
    body();
    writer.patchU32(start + 4, static_cast<std::uint32_t>(writer.size() - start));
}

std::vector<std::uint16_t> readU16List(ByteReader& reader) {
    std::vector<std::uint16_t> values(reader.u16());
    for (auto& value : values) {
        value = reader.u16();
    }
    return values;
}

void writeU16List(ByteWriter& writer, const std::vector<std::uint16_t>& values) {
    writer.u16(static_cast<std::uint16_t>(values.size()));
    for (const auto value : values) {
        writer.u16(value);
    }
}

std::vector<ScriptLine> readScriptLines(ByteReader& reader) {
    std::vector<ScriptLine> lines(reader.u16());
    for (auto& line : lines) {
        line.opcode = reader.u16();
        for (auto& arg : line.args) {
            arg = reader.i16();
        }
        const auto text = reader.bytes(reader.u16());
        line.text.assign(text.begin(), text.end());
    }
    return lines;
}

void writeScriptLines(ByteWriter& writer, const std::vector<ScriptLine>& lines) {
    writer.u16(static_cast<std::uint16_t>(lines.size()));
    for (const auto& line : lines) {
        writer.u16(line.opcode);
        for (const auto arg : line.args) {
            writer.i16(arg);
        }
        writer.u16(static_cast<std::uint16_t>(line.text.size()));
        writer.bytes(ByteView(reinterpret_cast<const std::uint8_t*>(line.text.data()), line.text.size()));
    }
}

void requireEnd(const ByteReader& reader, std::string_view what) {
    if (!reader.atEnd()) {
        throw DataError(std::string(what) + " has " + std::to_string(reader.remaining()) + " unexpected bytes at its end");
    }
}

// HTSP and ACTN: (u16 zone, u16 count, items)* then 0xFFFF.
template <typename ReadItems>
void readZoneIndexedList(ByteView payload, std::size_t zoneCount, std::string_view tag, ReadItems&& readItems) {
    ByteReader reader(payload);
    for (;;) {
        const std::uint16_t zone = reader.u16();
        if (zone == kListEnd) {
            break;
        }
        if (zone >= zoneCount) {
            throw DataError(std::string(tag) + " refers to zone " + std::to_string(zone) + " which does not exist");
        }
        readItems(zone, reader, reader.u16());
    }
    requireEnd(reader, tag);
}

} // namespace

std::vector<Hotspot> readHotspots(ByteReader& reader, std::size_t count) {
    std::vector<Hotspot> hotspots(count);
    for (auto& h : hotspots) {
        h.type = reader.u32();
        h.x = reader.u16();
        h.y = reader.u16();
        h.enabled = reader.u16();
        h.argument = reader.u16();
    }
    return hotspots;
}

void writeHotspots(ByteWriter& writer, const std::vector<Hotspot>& hotspots) {
    for (const auto& h : hotspots) {
        writer.u32(h.type);
        writer.u16(h.x);
        writer.u16(h.y);
        writer.u16(h.enabled);
        writer.u16(h.argument);
    }
}

void readIzax(ByteReader& reader, Zone& zone, Layout layout) {
    readSizedBlock(reader, "IZAX", [&] {
        zone.auxField = reader.u16();
        zone.monsters.resize(reader.u16());
        for (auto& m : zone.monsters) {
            m.character = reader.u16();
            m.x = reader.u16();
            m.y = reader.u16();
            if (layout == Layout::Yoda) {
                m.item = reader.u16();
                m.unknown = reader.u32();
                for (auto& w : m.waypoints) {
                    w = reader.u16();
                }
            }
        }
        zone.requiredItems = readU16List(reader);
        if (layout == Layout::Yoda) {
            zone.goalItems = readU16List(reader);
        }
    });
}

void writeIzax(ByteWriter& writer, const Zone& zone, Layout layout) {
    writeSizedBlock(writer, "IZAX", [&] {
        writer.u16(zone.auxField);
        writer.u16(static_cast<std::uint16_t>(zone.monsters.size()));
        for (const auto& m : zone.monsters) {
            writer.u16(m.character);
            writer.u16(m.x);
            writer.u16(m.y);
            if (layout == Layout::Yoda) {
                writer.u16(m.item);
                writer.u32(m.unknown);
                for (const auto w : m.waypoints) {
                    writer.u16(w);
                }
            }
        }
        writeU16List(writer, zone.requiredItems);
        if (layout == Layout::Yoda) {
            writeU16List(writer, zone.goalItems);
        }
    });
}

std::vector<std::uint16_t> readTileList(ByteReader& reader, std::string_view tag) {
    std::vector<std::uint16_t> tiles;
    readSizedBlock(reader, tag, [&] { tiles = readU16List(reader); });
    return tiles;
}

void writeTileList(ByteWriter& writer, std::string_view tag, const std::vector<std::uint16_t>& tiles) {
    writeSizedBlock(writer, tag, [&] { writeU16List(writer, tiles); });
}

// IZX4 is the odd one out: its u32 size counts only the content (2 bytes).
std::uint16_t readIzx4(ByteReader& reader) {
    expectTag(reader, "IZX4");
    if (reader.u32() != 2) {
        throw DataError("IZX4 with a size other than 2");
    }
    return reader.u16();
}

void writeIzx4(ByteWriter& writer, std::uint16_t value) {
    writer.tag("IZX4");
    writer.u32(2);
    writer.u16(value);
}

// IACT: tag, u32 size, conditions, instructions. Yoda Stories stores the
// size of the content; Indy always stores 0.
Action parseActionBody(ByteReader& reader) {
    Action action;
    action.conditions = readScriptLines(reader);
    action.instructions = readScriptLines(reader);
    return action;
}

std::vector<Action> readActions(ByteReader& reader, std::size_t count, Layout layout) {
    std::vector<Action> actions;
    actions.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        expectTag(reader, "IACT");
        const std::uint32_t size = reader.u32();
        const std::size_t start = reader.position();
        actions.push_back(parseActionBody(reader));
        const std::size_t actual = reader.position() - start;
        const std::size_t expected = layout == Layout::Yoda ? actual : 0;
        if (size != expected) {
            throw DataError("IACT at offset " + std::to_string(start - 8) + " has size " + std::to_string(size) +
                            ", expected " + std::to_string(expected));
        }
    }
    return actions;
}

void writeActions(ByteWriter& writer, const std::vector<Action>& actions, Layout layout) {
    for (const auto& action : actions) {
        writer.tag("IACT");
        const std::size_t sizeAt = writer.size();
        writer.u32(0);
        writeScriptLines(writer, action.conditions);
        writeScriptLines(writer, action.instructions);
        if (layout == Layout::Yoda) {
            writer.patchU32(sizeAt, static_cast<std::uint32_t>(writer.size() - sizeAt - 4));
        }
    }
}

void readYodaZoneRecords(ByteReader& reader, Zone& zone) {
    zone.hotspots = readHotspots(reader, reader.u16());
    readIzax(reader, zone, Layout::Yoda);
    zone.providedItems = readTileList(reader, "IZX2");
    zone.npcs = readTileList(reader, "IZX3");
    zone.izx4 = readIzx4(reader);
    zone.actions = readActions(reader, reader.u16(), Layout::Yoda);
}

void writeYodaZoneRecords(ByteWriter& writer, const Zone& zone) {
    const auto& hotspots = zone.hotspots ? *zone.hotspots : std::vector<Hotspot>{};
    writer.u16(static_cast<std::uint16_t>(hotspots.size()));
    writeHotspots(writer, hotspots);
    writeIzax(writer, zone, Layout::Yoda);
    writeTileList(writer, "IZX2", zone.providedItems);
    writeTileList(writer, "IZX3", zone.npcs);
    writeIzx4(writer, zone.izx4);
    const auto& actions = zone.actions ? *zone.actions : std::vector<Action>{};
    writer.u16(static_cast<std::uint16_t>(actions.size()));
    writeActions(writer, actions, Layout::Yoda);
}

void readIndyZoneSections(const Container& container, std::vector<Zone>& zones) {
    // One block per zone, in zone order.
    // A section that is absent leaves the zones' records empty.
    const auto perZone = [&](std::string_view tag, auto&& readOne) {
        const Section* section = container.find(tag);
        if (!section) {
            return;
        }
        ByteReader reader(section->payload);
        for (auto& zone : zones) {
            readOne(reader, zone);
        }
        requireEnd(reader, tag);
    };
    perZone("ZAUX", [](ByteReader& r, Zone& z) { readIzax(r, z, Layout::Indy); });
    perZone("ZAX2", [](ByteReader& r, Zone& z) { z.providedItems = readTileList(r, "IZX2"); });
    perZone("ZAX3", [](ByteReader& r, Zone& z) { z.npcs = readTileList(r, "IZX3"); });
    perZone("ZAX4", [](ByteReader& r, Zone& z) { z.izx4 = readIzx4(r); });

    if (const Section* htsp = container.find("HTSP")) {
        readZoneIndexedList(htsp->payload, zones.size(), "HTSP",
                            [&](std::uint16_t zone, ByteReader& r, std::size_t count) {
                                zones[zone].hotspots = readHotspots(r, count);
                            });
    }
    if (const Section* actn = container.find("ACTN")) {
        readZoneIndexedList(actn->payload, zones.size(), "ACTN",
                            [&](std::uint16_t zone, ByteReader& r, std::size_t count) {
                                zones[zone].actions = readActions(r, count, Layout::Indy);
                            });
    }
}

void writeIndyZoneSections(Container& container, const std::vector<Zone>& zones) {
    const auto replace = [&](std::string_view tag, Bytes payload) {
        if (auto* section = container.find(tag)) {
            section->payload = std::move(payload);
        }
    };
    const auto perZone = [&](std::string_view tag, auto&& writeOne) {
        ByteWriter writer;
        for (const auto& zone : zones) {
            writeOne(writer, zone);
        }
        replace(tag, std::move(writer).take());
    };
    perZone("ZAUX", [](ByteWriter& w, const Zone& z) { writeIzax(w, z, Layout::Indy); });
    perZone("ZAX2", [](ByteWriter& w, const Zone& z) { writeTileList(w, "IZX2", z.providedItems); });
    perZone("ZAX3", [](ByteWriter& w, const Zone& z) { writeTileList(w, "IZX3", z.npcs); });
    perZone("ZAX4", [](ByteWriter& w, const Zone& z) { writeIzx4(w, z.izx4); });

    ByteWriter htsp;
    ByteWriter actn;
    for (std::size_t i = 0; i < zones.size(); ++i) {
        if (zones[i].hotspots) {
            htsp.u16(static_cast<std::uint16_t>(i));
            htsp.u16(static_cast<std::uint16_t>(zones[i].hotspots->size()));
            writeHotspots(htsp, *zones[i].hotspots);
        }
        if (zones[i].actions) {
            actn.u16(static_cast<std::uint16_t>(i));
            actn.u16(static_cast<std::uint16_t>(zones[i].actions->size()));
            writeActions(actn, *zones[i].actions, Layout::Indy);
        }
    }
    htsp.u16(kListEnd);
    actn.u16(kListEnd);
    replace("HTSP", std::move(htsp).take());
    replace("ACTN", std::move(actn).take());
}

} // namespace das::data::detail
