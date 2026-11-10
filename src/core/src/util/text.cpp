// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/util/text.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace das::text {

namespace {

// Code points for bytes 0x80..0x9f; the rest of the bytes equal their code point.
constexpr std::array<char32_t, 32> kHigh{
    0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008d, 0x017d, 0x008f, 0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
    0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178};

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

} // namespace

std::string windows1252ToUtf8(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (const char c : bytes) {
        const auto b = static_cast<std::uint8_t>(c);
        appendUtf8(out, b >= 0x80 && b < 0xa0 ? kHigh[b - 0x80] : static_cast<char32_t>(b));
    }
    return out;
}

std::string utf8ToWindows1252(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size();) {
        const auto b = static_cast<std::uint8_t>(utf8[i]);
        char32_t cp = 0;
        int extra = 0;
        if (b < 0x80) {
            cp = b;
        } else if ((b & 0xe0) == 0xc0) {
            cp = b & 0x1fu;
            extra = 1;
        } else if ((b & 0xf0) == 0xe0) {
            cp = b & 0x0fu;
            extra = 2;
        } else {
            throw std::invalid_argument("text has a character Windows-1252 cannot hold");
        }
        if (i + static_cast<std::size_t>(extra) >= utf8.size()) {
            throw std::invalid_argument("text is not valid UTF-8");
        }
        for (int k = 1; k <= extra; ++k) {
            const auto cont = static_cast<std::uint8_t>(utf8[i + static_cast<std::size_t>(k)]);
            if ((cont & 0xc0) != 0x80) {
                throw std::invalid_argument("text is not valid UTF-8");
            }
            cp = (cp << 6) | (cont & 0x3fu);
        }
        i += static_cast<std::size_t>(extra) + 1;
        if (cp < 0x80 || (cp >= 0xa0 && cp < 0x100)) {
            out.push_back(static_cast<char>(cp));
            continue;
        }
        bool found = false;
        for (std::size_t k = 0; k < kHigh.size(); ++k) {
            if (kHigh[k] == cp) {
                out.push_back(static_cast<char>(0x80 + k));
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::invalid_argument("text has a character Windows-1252 cannot hold");
        }
    }
    return out;
}

} // namespace das::text
