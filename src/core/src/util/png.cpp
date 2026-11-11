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

Bytes writeRgba(const RgbaImage& image) {
    const std::size_t stride = static_cast<std::size_t>(image.width) * 4;
    if (image.width <= 0 || image.height <= 0 || image.pixels.size() != stride * static_cast<std::size_t>(image.height)) {
        throw DataError("image size does not match its pixels");
    }
    Bytes out(kSignature.begin(), kSignature.end());
    Bytes ihdr;
    put32(ihdr, static_cast<std::uint32_t>(image.width));
    put32(ihdr, static_cast<std::uint32_t>(image.height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // depth 8, RGBA, deflate, filter 0, no interlace
    chunk(out, "IHDR", ihdr);
    // Filter "sub" on every row: flat areas of colour become runs of zeros.
    Bytes raw;
    raw.reserve(image.pixels.size() + static_cast<std::size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        raw.push_back(1);
        const std::uint8_t* row = image.pixels.data() + static_cast<std::size_t>(y) * stride;
        for (std::size_t i = 0; i < stride; ++i) {
            raw.push_back(static_cast<std::uint8_t>(row[i] - (i >= 4 ? row[i - 4] : 0)));
        }
    }
    chunk(out, "IDAT", zlib::compress(raw));
    chunk(out, "IEND", {});
    return out;
}

namespace {

// A PNG file after inflating and unfiltering: one byte per channel.
struct Decoded {
    int width = 0;
    int height = 0;
    int colorType = 0;
    int channels = 1;
    data::Palette palette;
    bool havePalette = false;
    Bytes transparency; // tRNS body
    Bytes pixels;       // width * height * channels
};

int channelsOf(int colorType) {
    switch (colorType) {
    case 0: return 1; // grey
    case 2: return 3; // RGB
    case 3: return 1; // palette
    case 4: return 2; // grey + alpha
    case 6: return 4; // RGBA
    default: throw DataError("unknown PNG colour type");
    }
}

Decoded decode(ByteView file) {
    if (file.size() < kSignature.size() || !std::equal(kSignature.begin(), kSignature.end(), file.begin())) {
        throw DataError("not a PNG file");
    }
    Decoded image;
    Bytes idat;
    bool haveHeader = false;
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
            image.colorType = body[9];
            image.channels = channelsOf(image.colorType);
            if (body[8] != 8 || body[10] != 0 || body[11] != 0 || body[12] != 0) {
                throw DataError("only 8-bit PNG files without interlacing are supported");
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
            image.havePalette = true;
        } else if (type == "tRNS") {
            image.transparency.assign(body.begin(), body.end());
        } else if (type == "IDAT") {
            idat.insert(idat.end(), body.begin(), body.end());
        } else if (type == "IEND") {
            break;
        } else if ((type[0] & 0x20) == 0) {
            throw DataError("unknown critical PNG chunk " + type);
        }
    }
    if (!haveHeader) {
        throw DataError("PNG file lacks a header");
    }
    if (image.colorType == 3 && !image.havePalette) {
        throw DataError("PNG file lacks a palette");
    }

    const Bytes raw = zlib::decompress(idat);
    const std::size_t bpp = static_cast<std::size_t>(image.channels);
    const std::size_t stride = static_cast<std::size_t>(image.width) * bpp;
    const std::size_t h = static_cast<std::size_t>(image.height);
    if (raw.size() != (stride + 1) * h) {
        throw DataError("PNG image data has the wrong size");
    }
    image.pixels.resize(stride * h);
    for (std::size_t y = 0; y < h; ++y) {
        const std::uint8_t filter = raw[y * (stride + 1)];
        const std::uint8_t* src = raw.data() + y * (stride + 1) + 1;
        std::uint8_t* row = image.pixels.data() + y * stride;
        const std::uint8_t* up = y > 0 ? row - stride : nullptr;
        for (std::size_t x = 0; x < stride; ++x) {
            const int a = x >= bpp ? row[x - bpp] : 0;
            const int b = up != nullptr ? up[x] : 0;
            const int c = (x >= bpp && up != nullptr) ? up[x - bpp] : 0;
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

} // namespace

IndexedImage read(ByteView file) {
    Decoded d = decode(file);
    if (d.colorType != 3) {
        throw DataError("only 8-bit palette PNG files are supported here");
    }
    IndexedImage image;
    image.width = d.width;
    image.height = d.height;
    image.palette = d.palette;
    for (std::size_t i = 0; i < d.transparency.size(); ++i) {
        if (d.transparency[i] == 0) {
            image.transparentIndex = static_cast<std::uint8_t>(i);
            break;
        }
    }
    image.pixels = std::move(d.pixels);
    return image;
}

RgbaImage readRgba(ByteView file) {
    const Decoded d = decode(file);
    RgbaImage image;
    image.width = d.width;
    image.height = d.height;
    const std::size_t count = static_cast<std::size_t>(d.width) * static_cast<std::size_t>(d.height);
    image.pixels.resize(count * 4);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* s = d.pixels.data() + i * static_cast<std::size_t>(d.channels);
        std::uint8_t* o = image.pixels.data() + i * 4;
        switch (d.colorType) {
        case 0:
            o[0] = o[1] = o[2] = s[0];
            o[3] = (d.transparency.size() >= 2 && d.transparency[1] == s[0] && d.transparency[0] == 0) ? 0 : 255;
            break;
        case 2:
            o[0] = s[0];
            o[1] = s[1];
            o[2] = s[2];
            o[3] = (d.transparency.size() >= 6 && d.transparency[1] == s[0] && d.transparency[3] == s[1] &&
                    d.transparency[5] == s[2])
                       ? 0
                       : 255;
            break;
        case 3: {
            const auto& c = d.palette.colors[s[0]];
            o[0] = c.r;
            o[1] = c.g;
            o[2] = c.b;
            o[3] = s[0] < d.transparency.size() ? d.transparency[s[0]] : 255;
            break;
        }
        case 4:
            o[0] = o[1] = o[2] = s[0];
            o[3] = s[1];
            break;
        default:
            std::copy(s, s + 4, o);
            break;
        }
    }
    return image;
}

std::vector<std::uint8_t> toPaletteIndices(const RgbaImage& image, const data::Palette& palette) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::uint8_t* p = image.pixels.data() + i * 4;
        if (p[3] < 128) {
            out[i] = 0;
            continue;
        }
        // Exact matches first, so an image saved from the game palette comes back unchanged.
        int best = -1;
        for (int c = 1; c < 256; ++c) {
            const auto& col = palette.colors[static_cast<std::size_t>(c)];
            if (col.r == p[0] && col.g == p[1] && col.b == p[2]) {
                best = c;
                break;
            }
        }
        if (best < 0) {
            int bestDistance = 1 << 30;
            for (int c = 1; c < 256; ++c) {
                const auto& col = palette.colors[static_cast<std::size_t>(c)];
                const int dr = col.r - p[0];
                const int dg = col.g - p[1];
                const int db = col.b - p[2];
                const int distance = 2 * dr * dr + 4 * dg * dg + 3 * db * db;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = c;
                }
            }
        }
        out[i] = static_cast<std::uint8_t>(best);
    }
    return out;
}

} // namespace das::png
