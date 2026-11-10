// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/data/binary.hpp"

namespace das::data {

void ByteReader::need(std::size_t count) const {
    if (count > remaining()) {
        throw DataError("unexpected end of data at offset " + std::to_string(pos_) + " (need " +
                        std::to_string(count) + " bytes, " + std::to_string(remaining()) + " left)");
    }
}

std::uint8_t ByteReader::u8() {
    need(1);
    return bytes_[pos_++];
}

std::uint16_t ByteReader::u16() {
    need(2);
    const auto value = static_cast<std::uint16_t>(bytes_[pos_] | (bytes_[pos_ + 1] << 8));
    pos_ += 2;
    return value;
}

std::int16_t ByteReader::i16() { return static_cast<std::int16_t>(u16()); }

std::uint32_t ByteReader::u32() {
    need(4);
    const std::uint32_t value = static_cast<std::uint32_t>(bytes_[pos_]) |
                                (static_cast<std::uint32_t>(bytes_[pos_ + 1]) << 8) |
                                (static_cast<std::uint32_t>(bytes_[pos_ + 2]) << 16) |
                                (static_cast<std::uint32_t>(bytes_[pos_ + 3]) << 24);
    pos_ += 4;
    return value;
}

std::string ByteReader::tag() {
    const auto raw = bytes(4);
    return std::string(raw.begin(), raw.end());
}

ByteView ByteReader::bytes(std::size_t count) {
    need(count);
    const auto view = bytes_.subspan(pos_, count);
    pos_ += count;
    return view;
}

ByteView ByteReader::peek(std::size_t count) const {
    need(count);
    return bytes_.subspan(pos_, count);
}

void ByteWriter::u16(std::uint16_t value) {
    out_.push_back(static_cast<std::uint8_t>(value & 0xff));
    out_.push_back(static_cast<std::uint8_t>(value >> 8));
}

void ByteWriter::u32(std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
}

void ByteWriter::tag(std::string_view tag) {
    if (tag.size() != 4) {
        throw DataError("section tags are four characters, got '" + std::string(tag) + "'");
    }
    out_.insert(out_.end(), tag.begin(), tag.end());
}

void ByteWriter::patchU32(std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out_.at(offset + static_cast<std::size_t>(i)) = static_cast<std::uint8_t>((value >> (8 * i)) & 0xff);
    }
}

} // namespace das::data
