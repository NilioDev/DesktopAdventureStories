// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/util/zlib.hpp"

#include <array>
#include <vector>

namespace das::zlib {

using data::ByteView;
using data::Bytes;
using data::DataError;

namespace {

const std::array<std::uint32_t, 256>& crcTable() {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            t[n] = c;
        }
        return t;
    }();
    return table;
}

// Length codes 257..285 and distance codes 0..29 (RFC 1951, 3.2.5).
constexpr std::array<std::uint16_t, 29> kLengthBase{3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                                    15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                                    67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> kLengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> kDistBase{
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> kDistExtra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                  6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// ---- Writing -------------------------------------------------------------

class BitWriter {
public:
    void bits(std::uint32_t value, int count) {
        acc_ |= value << used_;
        used_ += count;
        while (used_ >= 8) {
            out_.push_back(static_cast<std::uint8_t>(acc_));
            acc_ >>= 8;
            used_ -= 8;
        }
    }
    // Huffman codes are sent most significant bit first.
    void code(std::uint32_t code, int length) {
        std::uint32_t reversed = 0;
        for (int i = 0; i < length; ++i) {
            reversed = (reversed << 1) | ((code >> i) & 1);
        }
        bits(reversed, length);
    }
    Bytes finish() {
        if (used_ > 0) {
            out_.push_back(static_cast<std::uint8_t>(acc_));
        }
        acc_ = 0;
        used_ = 0;
        return std::move(out_);
    }

private:
    Bytes out_;
    std::uint32_t acc_ = 0;
    int used_ = 0;
};

void fixedLiteral(BitWriter& w, int symbol) {
    if (symbol < 144) {
        w.code(static_cast<std::uint32_t>(0x30 + symbol), 8);
    } else if (symbol < 256) {
        w.code(static_cast<std::uint32_t>(0x190 + symbol - 144), 9);
    } else if (symbol < 280) {
        w.code(static_cast<std::uint32_t>(symbol - 256), 7);
    } else {
        w.code(static_cast<std::uint32_t>(0xc0 + symbol - 280), 8);
    }
}

void writeMatch(BitWriter& w, int length, int distance) {
    int lc = 28;
    while (kLengthBase[static_cast<std::size_t>(lc)] > length) {
        --lc;
    }
    fixedLiteral(w, 257 + lc);
    w.bits(static_cast<std::uint32_t>(length - kLengthBase[static_cast<std::size_t>(lc)]),
           kLengthExtra[static_cast<std::size_t>(lc)]);
    int dc = 29;
    while (kDistBase[static_cast<std::size_t>(dc)] > distance) {
        --dc;
    }
    w.code(static_cast<std::uint32_t>(dc), 5);
    w.bits(static_cast<std::uint32_t>(distance - kDistBase[static_cast<std::size_t>(dc)]),
           kDistExtra[static_cast<std::size_t>(dc)]);
}

// ---- Reading -------------------------------------------------------------

class BitReader {
public:
    explicit BitReader(ByteView in) : in_(in) {}
    std::uint32_t bits(int count) {
        while (used_ < count) {
            if (pos_ >= in_.size()) {
                throw DataError("deflate stream ends early");
            }
            acc_ |= static_cast<std::uint32_t>(in_[pos_++]) << used_;
            used_ += 8;
        }
        const std::uint32_t value = acc_ & ((1u << count) - 1u);
        acc_ = count == 32 ? 0 : acc_ >> count;
        used_ -= count;
        return value;
    }
    void alignToByte() {
        acc_ = 0;
        used_ = 0;
    }
    std::uint8_t byte() {
        if (pos_ >= in_.size()) {
            throw DataError("deflate stream ends early");
        }
        return in_[pos_++];
    }
    std::size_t consumed() const { return pos_; }

private:
    ByteView in_;
    std::size_t pos_ = 0;
    std::uint32_t acc_ = 0;
    int used_ = 0;
};

// Canonical Huffman decoder: counts per length and symbols sorted by code.
struct Huffman {
    std::array<std::uint16_t, 16> counts{};
    std::vector<std::uint16_t> symbols;

    explicit Huffman(const std::uint8_t* lengths, int n) {
        for (int i = 0; i < n; ++i) {
            ++counts[lengths[i]];
        }
        counts[0] = 0;
        std::array<std::uint16_t, 16> offsets{};
        for (int len = 1; len < 16; ++len) {
            offsets[static_cast<std::size_t>(len)] =
                static_cast<std::uint16_t>(offsets[static_cast<std::size_t>(len - 1)] +
                                           counts[static_cast<std::size_t>(len - 1)]);
        }
        symbols.resize(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            if (lengths[i] != 0) {
                symbols[offsets[lengths[i]]++] = static_cast<std::uint16_t>(i);
            }
        }
    }

    int decode(BitReader& r) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= static_cast<int>(r.bits(1));
            const int count = counts[static_cast<std::size_t>(len)];
            if (code - first < count) {
                return symbols[static_cast<std::size_t>(index + code - first)];
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        throw DataError("bad Huffman code in deflate stream");
    }
};

void inflateBlock(BitReader& r, Bytes& out, const Huffman& lit, const Huffman& dist) {
    for (;;) {
        const int symbol = lit.decode(r);
        if (symbol < 256) {
            out.push_back(static_cast<std::uint8_t>(symbol));
            continue;
        }
        if (symbol == 256) {
            return;
        }
        const int lc = symbol - 257;
        if (lc >= 29) {
            throw DataError("bad length code in deflate stream");
        }
        const int length = kLengthBase[static_cast<std::size_t>(lc)] +
                           static_cast<int>(r.bits(kLengthExtra[static_cast<std::size_t>(lc)]));
        const int dc = dist.decode(r);
        if (dc >= 30) {
            throw DataError("bad distance code in deflate stream");
        }
        const std::size_t distance =
            kDistBase[static_cast<std::size_t>(dc)] + r.bits(kDistExtra[static_cast<std::size_t>(dc)]);
        if (distance > out.size()) {
            throw DataError("deflate distance points before the start");
        }
        const std::size_t from = out.size() - distance;
        for (int i = 0; i < length; ++i) {
            out.push_back(out[from + static_cast<std::size_t>(i)]);
        }
    }
}

} // namespace

std::uint32_t crc32(ByteView bytes, std::uint32_t crc) {
    const auto& table = crcTable();
    crc = ~crc;
    for (const std::uint8_t b : bytes) {
        crc = table[(crc ^ b) & 0xffu] ^ (crc >> 8);
    }
    return ~crc;
}

std::uint32_t adler32(ByteView bytes, std::uint32_t adler) {
    std::uint32_t a = adler & 0xffffu;
    std::uint32_t b = adler >> 16;
    for (const std::uint8_t byte : bytes) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

Bytes deflate(ByteView in) {
    constexpr int kWindow = 32768;
    constexpr int kHashBits = 15;
    constexpr int kMaxChain = 64;
    std::vector<int> head(1u << kHashBits, -1);
    std::vector<int> prev(in.size(), -1);
    const auto hash = [&](std::size_t i) {
        const std::uint32_t v = (static_cast<std::uint32_t>(in[i]) << 16) |
                                (static_cast<std::uint32_t>(in[i + 1]) << 8) | in[i + 2];
        return (v * 2654435761u) >> (32 - kHashBits);
    };
    const auto insert = [&](std::size_t i) {
        if (i + 2 < in.size()) {
            const auto h = hash(i);
            prev[i] = head[h];
            head[h] = static_cast<int>(i);
        }
    };

    BitWriter w;
    w.bits(1, 1); // final block
    w.bits(1, 2); // fixed Huffman codes
    std::size_t i = 0;
    while (i < in.size()) {
        int bestLength = 0;
        int bestDistance = 0;
        if (i + 2 < in.size()) {
            int candidate = head[hash(i)];
            const std::size_t maxLength = std::min<std::size_t>(258, in.size() - i);
            for (int chain = 0; candidate >= 0 && chain < kMaxChain; ++chain) {
                const int distance = static_cast<int>(i) - candidate;
                if (distance > kWindow) {
                    break;
                }
                std::size_t length = 0;
                while (length < maxLength && in[static_cast<std::size_t>(candidate) + length] == in[i + length]) {
                    ++length;
                }
                if (static_cast<int>(length) > bestLength) {
                    bestLength = static_cast<int>(length);
                    bestDistance = distance;
                    if (length == maxLength) {
                        break;
                    }
                }
                candidate = prev[static_cast<std::size_t>(candidate)];
            }
        }
        if (bestLength >= 3) {
            writeMatch(w, bestLength, bestDistance);
            for (int k = 0; k < bestLength; ++k) {
                insert(i + static_cast<std::size_t>(k));
            }
            i += static_cast<std::size_t>(bestLength);
        } else {
            fixedLiteral(w, in[i]);
            insert(i);
            ++i;
        }
    }
    fixedLiteral(w, 256);
    return w.finish();
}

Bytes inflate(ByteView in, std::size_t* consumed) {
    BitReader r(in);
    Bytes out;
    bool final = false;
    while (!final) {
        final = r.bits(1) != 0;
        const std::uint32_t type = r.bits(2);
        if (type == 0) {
            r.alignToByte();
            const std::uint16_t len = static_cast<std::uint16_t>(r.byte() | (r.byte() << 8));
            const std::uint16_t nlen = static_cast<std::uint16_t>(r.byte() | (r.byte() << 8));
            if (static_cast<std::uint16_t>(~nlen) != len) {
                throw DataError("bad stored block in deflate stream");
            }
            for (int k = 0; k < len; ++k) {
                out.push_back(r.byte());
            }
        } else if (type == 1) {
            static const auto tables = [] {
                std::array<std::uint8_t, 288> lit{};
                for (int k = 0; k < 288; ++k) {
                    lit[static_cast<std::size_t>(k)] = k < 144 ? 8 : k < 256 ? 9 : k < 280 ? 7 : 8;
                }
                std::array<std::uint8_t, 30> dist{};
                dist.fill(5);
                return std::pair{Huffman(lit.data(), 288), Huffman(dist.data(), 30)};
            }();
            inflateBlock(r, out, tables.first, tables.second);
        } else if (type == 2) {
            const int hlit = static_cast<int>(r.bits(5)) + 257;
            const int hdist = static_cast<int>(r.bits(5)) + 1;
            const int hclen = static_cast<int>(r.bits(4)) + 4;
            static constexpr std::array<int, 19> kOrder{16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                        11, 4,  12, 3, 13, 2, 14, 1, 15};
            std::array<std::uint8_t, 19> codeLengths{};
            for (int k = 0; k < hclen; ++k) {
                codeLengths[static_cast<std::size_t>(kOrder[static_cast<std::size_t>(k)])] =
                    static_cast<std::uint8_t>(r.bits(3));
            }
            const Huffman lengthCodes(codeLengths.data(), 19);
            std::vector<std::uint8_t> lengths;
            while (static_cast<int>(lengths.size()) < hlit + hdist) {
                const int symbol = lengthCodes.decode(r);
                if (symbol < 16) {
                    lengths.push_back(static_cast<std::uint8_t>(symbol));
                } else if (symbol == 16) {
                    if (lengths.empty()) {
                        throw DataError("bad code lengths in deflate stream");
                    }
                    const std::uint8_t last = lengths.back();
                    for (std::uint32_t k = 3 + r.bits(2); k > 0; --k) {
                        lengths.push_back(last);
                    }
                } else {
                    const std::uint32_t repeat = symbol == 17 ? 3 + r.bits(3) : 11 + r.bits(7);
                    lengths.insert(lengths.end(), repeat, 0);
                }
            }
            if (static_cast<int>(lengths.size()) != hlit + hdist) {
                throw DataError("bad code lengths in deflate stream");
            }
            const Huffman lit(lengths.data(), hlit);
            const Huffman dist(lengths.data() + hlit, hdist);
            inflateBlock(r, out, lit, dist);
        } else {
            throw DataError("bad block type in deflate stream");
        }
    }
    if (consumed != nullptr) {
        *consumed = r.consumed();
    }
    return out;
}

Bytes compress(ByteView in) {
    Bytes out{0x78, 0x01}; // deflate, 32K window, "fastest" level hint
    const Bytes body = deflate(in);
    out.insert(out.end(), body.begin(), body.end());
    const std::uint32_t adler = adler32(in);
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(adler >> shift));
    }
    return out;
}

Bytes decompress(ByteView in) {
    if (in.size() < 6 || (in[0] & 0x0f) != 8 || ((in[0] << 8) | in[1]) % 31 != 0 || (in[1] & 0x20) != 0) {
        throw DataError("not a zlib stream");
    }
    std::size_t used = 0;
    Bytes out = inflate(in.subspan(2), &used);
    if (2 + used + 4 > in.size()) {
        throw DataError("zlib stream ends early");
    }
    const ByteView tail = in.subspan(2 + used, 4);
    const std::uint32_t expected = (static_cast<std::uint32_t>(tail[0]) << 24) |
                                   (static_cast<std::uint32_t>(tail[1]) << 16) |
                                   (static_cast<std::uint32_t>(tail[2]) << 8) | tail[3];
    if (adler32(out) != expected) {
        throw DataError("zlib checksum mismatch");
    }
    return out;
}

} // namespace das::zlib
