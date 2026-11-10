// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/util/png.hpp"

#include "das/util/zlib.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

namespace das::png {

using data::ByteView;
using data::Bytes;
using data::DataError;

namespace {

constexpr std::array<std::uint8_t, 8> kSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

void put32(Bytes& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(v >> shift));
    }
}

std::uint32_t get32(ByteView in, std::size_t at) {
    return (static_cast<std::uint32_t>(in[at]) << 24) | (static_cast<std::uint32_t>(in[at + 1]) << 16) |
           (static_cast<std::uint32_t>(in[at + 2]) << 8) | in[at + 3];
}

void chunk(Bytes& out, const char* type, ByteView body) {
    put32(out, static_cast<std::uint32_t>(body.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    put32(out, zlib::crc32(ByteView(out).subspan(start)));
}

int paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

} // namespace

Bytes write(const IndexedImage& image) {
    if (image.width <= 0 || image.height <= 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height)) {
        throw DataError("image size does not match its pixels");
    }
    Bytes out(kSignature.begin(), kSignature.end());

    Bytes ihdr;
    put32(ihdr, static_cast<std::uint32_t>(image.width));
    put32(ihdr, static_cast<std::uint32_t>(image.height));
    ihdr.insert(ihdr.end(), {8, 3, 0, 0, 0}); // depth 8, palette, deflate, filter 0, no interlace
    chunk(out, "IHDR", ihdr);

    Bytes plte;
    for (const auto& c : image.palette.colors) {
        plte.insert(plte.end(), {c.r, c.g, c.b});
    }
    chunk(out, "PLTE", plte);

    if (image.transparentIndex) {
        Bytes trns(static_cast<std::size_t>(*image.transparentIndex) + 1, 0xff);
        trns.back() = 0;
        chunk(out, "tRNS", trns);
    }

    // Filter type 0 (none) on every row: palette indices do not gain from
    // the arithmetic filters.
    Bytes raw;
    raw.reserve(image.pixels.size() + static_cast<std::size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(0);
        const auto row = image.pixels.begin() + static_cast<std::ptrdiff_t>(y) * image.width;
        raw.insert(raw.end(), row, row + image.width);
    }
    chunk(out, "IDAT", zlib::compress(raw));
    chunk(out, "IEND", {});
    return out;
}

IndexedImage read(ByteView file) {
    if (file.size() < kSignature.size() || !std::equal(kSignature.begin(), kSignature.end(), file.begin())) {
        throw DataError("not a PNG file");
    }
    IndexedImage image;
    Bytes idat;
    bool haveHeader = false;
    bool havePalette = false;
    std::size_t at = kSignature.size();
    for (;;) {
        if (at + 12 > file.size()) {
            throw DataError("PNG file ends early");
        }
        const std::uint32_t length = get32(file, at);
        if (length > file.size() - at - 12) {
            throw DataError("PNG chunk is too long");
        }
        const std::string type(reinterpret_cast<const char*>(file.data() + at + 4), 4);
        const ByteView body = file.subspan(at + 8, length);
        if (zlib::crc32(file.subspan(at + 4, length + 4)) != get32(file, at + 8 + length)) {
            throw DataError("PNG chunk " + type + " has a bad checksum");
        }
        at += 12 + length;

        if (type == "IHDR") {
            if (length != 13) {
                throw DataError("bad PNG header");
            }
            image.width = static_cast<int>(get32(body, 0));
            image.height = static_cast<int>(get32(body, 4));
            if (body[8] != 8 || body[9] != 3 || body[10] != 0 || body[11] != 0 || body[12] != 0) {
                throw DataError("only 8-bit palette PNG files without interlacing are supported");
            }
            if (image.width <= 0 || image.height <= 0 || image.width > 16384 || image.height > 16384) {
                throw DataError("PNG image size out of range");
            }
            haveHeader = true;
        } else if (type == "PLTE") {
            if (length % 3 != 0 || length > 768) {
                throw DataError("bad PNG palette");
            }
            for (std::size_t i = 0; i < length / 3; ++i) {
                image.palette.colors[i] = {body[i * 3], body[i * 3 + 1], body[i * 3 + 2]};
            }
            havePalette = true;
        } else if (type == "tRNS") {
            for (std::size_t i = 0; i < length; ++i) {
                if (body[i] == 0) {
                    image.transparentIndex = static_cast<std::uint8_t>(i);
                    break;
                }
            }
        } else if (type == "IDAT") {
            idat.insert(idat.end(), body.begin(), body.end());
        } else if (type == "IEND") {
            break;
        } else if ((type[0] & 0x20) == 0) {
            throw DataError("unknown critical PNG chunk " + type);
        }
    }
    if (!haveHeader || !havePalette) {
        throw DataError("PNG file lacks a header or palette");
    }

    const Bytes raw = zlib::decompress(idat);
    const std::size_t w = static_cast<std::size_t>(image.width);
    const std::size_t h = static_cast<std::size_t>(image.height);
    if (raw.size() != (w + 1) * h) {
        throw DataError("PNG image data has the wrong size");
    }
    image.pixels.resize(w * h);
    for (std::size_t y = 0; y < h; ++y) {
        const std::uint8_t filter = raw[y * (w + 1)];
        const std::uint8_t* src = raw.data() + y * (w + 1) + 1;
        std::uint8_t* row = image.pixels.data() + y * w;
        const std::uint8_t* up = y > 0 ? row - w : nullptr;
        for (std::size_t x = 0; x < w; ++x) {
            const int a = x > 0 ? row[x - 1] : 0;
            const int b = up != nullptr ? up[x] : 0;
            const int c = (x > 0 && up != nullptr) ? up[x - 1] : 0;
            int v = src[x];
            switch (filter) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: throw DataError("bad PNG filter type");
            }
            row[x] = static_cast<std::uint8_t>(v);
        }
    }
    return image;
}

} // namespace das::png
