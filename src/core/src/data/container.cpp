// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/data/container.hpp"

#include <algorithm>

namespace das::data {

namespace {

bool matches(ByteView view, std::size_t offset, std::string_view text) {
    return view.size() >= offset + text.size() &&
           std::equal(text.begin(), text.end(), view.begin() + static_cast<std::ptrdiff_t>(offset));
}

// Yoda Stories writes the ZONE section without a length field. Each zone
// entry carries its own size, so the section's end is found by walking them.
std::size_t unframedZoneLength(const ByteReader& reader) {
    ByteReader walker(reader.peek(reader.remaining()));
    const std::uint16_t count = walker.u16();
    for (std::uint16_t i = 0; i < count; ++i) {
        walker.u16(); // planet
        walker.skip(walker.u32());
    }
    return walker.position();
}

bool isUnframedZone(const ByteReader& reader) {
    // Framed:   u32 length, u16 count, "IZON"...
    // Unframed: u16 count, u16 planet, u32 size, u16 index, "IZON"...
    const auto ahead = reader.peek(std::min<std::size_t>(reader.remaining(), 14));
    if (matches(ahead, 10, "IZON")) {
        return true;
    }
    if (matches(ahead, 6, "IZON")) {
        return false;
    }
    throw DataError("ZONE section has an unknown layout");
}

} // namespace

std::string_view layoutName(Layout layout) {
    return layout == Layout::Indy ? "Indiana Jones and His Desktop Adventures" : "Yoda Stories";
}

const Section* Container::find(std::string_view tag) const {
    const auto it = std::find_if(sections.begin(), sections.end(), [&](const Section& s) { return s.tag == tag; });
    return it == sections.end() ? nullptr : &*it;
}

Section* Container::find(std::string_view tag) {
    const auto it = std::find_if(sections.begin(), sections.end(), [&](const Section& s) { return s.tag == tag; });
    return it == sections.end() ? nullptr : &*it;
}

Container readContainer(ByteView file) {
    Container container;
    ByteReader reader(file);
    bool sawZone = false;
    bool sawEnd = false;

    while (!reader.atEnd()) {
        if (sawEnd) {
            throw DataError("data after the ENDF section at offset " + std::to_string(reader.position()));
        }
        Section section;
        section.tag = reader.tag();
        std::size_t length = 0;
        if (section.tag == "VERS") {
            length = 4; // the version number, no length field
        } else if (section.tag == "ZONE" && isUnframedZone(reader)) {
            container.layout = Layout::Yoda;
            sawZone = true;
            length = unframedZoneLength(reader);
        } else {
            if (section.tag == "ZONE") {
                container.layout = Layout::Indy;
                sawZone = true;
            }
            length = reader.u32();
        }
        const auto payload = reader.bytes(length);
        section.payload.assign(payload.begin(), payload.end());
        sawEnd = section.tag == "ENDF";
        container.sections.push_back(std::move(section));
    }

    if (container.sections.empty() || container.sections.front().tag != "VERS") {
        throw DataError("not a Desktop Adventures data file (no VERS section at the start)");
    }
    if (!sawZone) {
        throw DataError("data file has no ZONE section");
    }
    return container;
}

Bytes writeContainer(const Container& container) {
    ByteWriter writer;
    for (const auto& section : container.sections) {
        writer.tag(section.tag);
        const bool unframed =
            section.tag == "VERS" || (section.tag == "ZONE" && container.layout == Layout::Yoda);
        if (!unframed) {
            writer.u32(static_cast<std::uint32_t>(section.payload.size()));
        }
        writer.bytes(section.payload);
    }
    return std::move(writer).take();
}

} // namespace das::data
