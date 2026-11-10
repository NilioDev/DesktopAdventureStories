// SPDX-License-Identifier: GPL-3.0-or-later
//
// Little-endian byte reading and writing for the original data files.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace das::data {

using Bytes = std::vector<std::uint8_t>;
using ByteView = std::span<const std::uint8_t>;

// Thrown when a data file does not have the structure we expect.
class DataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class ByteReader {
public:
    explicit ByteReader(ByteView bytes) : bytes_(bytes) {}

    std::size_t position() const { return pos_; }
    std::size_t size() const { return bytes_.size(); }
    std::size_t remaining() const { return bytes_.size() - pos_; }
    bool atEnd() const { return pos_ == bytes_.size(); }

    std::uint8_t u8();
    std::uint16_t u16();
    std::int16_t i16();
    std::uint32_t u32();
    // Four ASCII characters, such as a section tag.
    std::string tag();
    ByteView bytes(std::size_t count);

    // Looks ahead without moving.
    ByteView peek(std::size_t count) const;

    void skip(std::size_t count) { (void)bytes(count); }

private:
    void need(std::size_t count) const;

    ByteView bytes_;
    std::size_t pos_ = 0;
};

class ByteWriter {
public:
    void u8(std::uint8_t value) { out_.push_back(value); }
    void u16(std::uint16_t value);
    void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }
    void u32(std::uint32_t value);
    void tag(std::string_view tag);
    void bytes(ByteView bytes) { out_.insert(out_.end(), bytes.begin(), bytes.end()); }

    std::size_t size() const { return out_.size(); }
    // Overwrites a u32 written earlier, for sizes known only afterwards.
    void patchU32(std::size_t offset, std::uint32_t value);

    const Bytes& data() const& { return out_; }
    Bytes take() && { return std::move(out_); }

private:
    Bytes out_;
};

} // namespace das::data
