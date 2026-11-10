// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/data/character.hpp"

#include <algorithm>

namespace das::data {

namespace {
constexpr std::size_t kNameField = 16;
constexpr std::uint16_t kHeroType = 1;
} // namespace

Character parseCharacter(ByteView body, Layout layout) {
    const std::size_t extra = layout == Layout::Yoda ? 6 : 0;
    const std::size_t expected = kNameField + 4 + extra + kFrameSets * kFacingCount * 2;
    if (body.size() != expected) {
        throw DataError("character entry has " + std::to_string(body.size()) + " bytes, expected " +
                        std::to_string(expected));
    }
    ByteReader reader(body);
    Character character;
    const auto name = reader.bytes(kNameField);
    character.name.assign(name.begin(), std::find(name.begin(), name.end(), std::uint8_t{0}));
    character.type = reader.u16();
    character.movement = reader.u16();
    reader.skip(extra);
    for (auto& set : character.frames) {
        for (auto& tile : set) {
            tile = reader.u16();
        }
    }
    return character;
}

Bytes writeCharacter(const Character& character, Layout layout, ByteView original) {
    const std::size_t extra = layout == Layout::Yoda ? 6 : 0;
    const std::size_t size = kNameField + 4 + extra + kFrameSets * kFacingCount * 2;
    Bytes body(size, 0);
    if (original.size() == size) {
        std::copy(original.begin(), original.end(), body.begin());
    }
    // The name: kept byte for byte when unchanged (the field may hold more
    // after the zero), else written with zeros after it.
    const auto oldEnd = std::find(body.begin(), body.begin() + kNameField, std::uint8_t{0});
    if (std::string(body.begin(), oldEnd) != character.name) {
        std::fill(body.begin(), body.begin() + kNameField, std::uint8_t{0});
        const std::size_t length = std::min(character.name.size(), kNameField - 1);
        std::copy(character.name.begin(), character.name.begin() + static_cast<std::ptrdiff_t>(length), body.begin());
    }
    ByteWriter writer;
    writer.u16(character.type);
    writer.u16(character.movement);
    const Bytes head = std::move(writer).take();
    std::copy(head.begin(), head.end(), body.begin() + kNameField);
    ByteWriter frames;
    for (const auto& set : character.frames) {
        for (const auto tile : set) {
            frames.u16(tile);
        }
    }
    const Bytes tail = std::move(frames).take();
    std::copy(tail.begin(), tail.end(), body.begin() + static_cast<std::ptrdiff_t>(kNameField + 4 + extra));
    return body;
}

std::optional<Character> findHero(const GameData& data) {
    for (const auto& entry : data.characters) {
        Character character = parseCharacter(entry.body, data.layout());
        if (character.type == kHeroType) {
            return character;
        }
    }
    return std::nullopt;
}

} // namespace das::data
