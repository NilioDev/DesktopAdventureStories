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
