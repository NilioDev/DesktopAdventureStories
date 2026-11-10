// SPDX-License-Identifier: GPL-3.0-or-later
//
// Our own small DEFLATE (RFC 1951) and zlib (RFC 1950) implementation, used
// for PNG files and for reading zip archives. The compressor uses LZ77 with
// fixed Huffman codes: simple, and good enough for pixel art.
#pragma once

#include "das/data/binary.hpp"

#include <cstdint>

namespace das::zlib {

std::uint32_t crc32(data::ByteView bytes, std::uint32_t crc = 0);
std::uint32_t adler32(data::ByteView bytes, std::uint32_t adler = 1);

// Raw DEFLATE streams.
data::Bytes deflate(data::ByteView bytes);
// Decodes a raw DEFLATE stream; `consumed` (if given) receives the number of
// input bytes used. Throws data::DataError on corrupt input.
data::Bytes inflate(data::ByteView bytes, std::size_t* consumed = nullptr);

// zlib-wrapped streams (header, DEFLATE data, Adler-32).
data::Bytes compress(data::ByteView bytes);
data::Bytes decompress(data::ByteView bytes);

} // namespace das::zlib
