// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/data/puzzle.hpp"

namespace das::data {

Puzzle parsePuzzle(ByteView body, Layout layout) {
    ByteReader reader(body);
    Puzzle p;
    p.type = reader.u32();
    p.itemClass = reader.u32();
    if (layout == Layout::Yoda) {
        p.unknown = reader.u32();
    }
    p.flags = reader.u16();
    for (auto& text : p.texts) {
        const auto length = reader.u16();
        const auto bytes = reader.bytes(length);
        text.assign(bytes.begin(), bytes.end());
    }
    p.item1 = reader.u16();
    if (layout == Layout::Yoda) {
        p.item2 = reader.u16();
    }
    if (!reader.atEnd()) {
        throw DataError("puzzle entry has " + std::to_string(reader.remaining()) + " bytes left over");
    }
    return p;
}

Bytes writePuzzle(const Puzzle& p, Layout layout) {
    ByteWriter w;
    w.u32(p.type);
    w.u32(p.itemClass);
    if (layout == Layout::Yoda) {
        w.u32(p.unknown);
    }
    w.u16(p.flags);
    for (const auto& text : p.texts) {
        if (text.size() > 0xffff) {
            throw DataError("puzzle text is too long");
        }
        w.u16(static_cast<std::uint16_t>(text.size()));
        w.bytes(ByteView(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }
    w.u16(p.item1);
    if (layout == Layout::Yoda) {
        w.u16(p.item2);
    }
    return std::move(w).take();
}

std::vector<Puzzle> parsePuzzles(const GameData& data) {
    std::vector<Puzzle> out;
    out.reserve(data.puzzles.size());
    for (const auto& entry : data.puzzles) {
        out.push_back(parsePuzzle(entry.body, data.layout()));
    }
    return out;
}

} // namespace das::data
