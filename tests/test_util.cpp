// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.hpp"

#include "das/util/json.hpp"
#include "das/util/png.hpp"
#include "das/util/text.hpp"
#include "das/util/zip.hpp"
#include "das/util/zlib.hpp"

#include <string>

using namespace das;

namespace {

template <typename F>
bool throws(F&& f) {
    try {
        f();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

data::Bytes bytes(std::string_view s) { return data::Bytes(s.begin(), s.end()); }

} // namespace

DAS_TEST("json writes and parses values in key order") {
    const json::Value v = json::Object{{"b", 1},
                                       {"a", json::Array{1, 2, 3}},
                                       {"s", "line\n\"quoted\" \xc3\xa9"},
                                       {"n", nullptr},
                                       {"t", true},
                                       {"neg", -42},
                                       {"d", 0.5}};
    const std::string text = json::write(v);
    DAS_CHECK(text.find("\"b\"") < text.find("\"a\""));
    DAS_CHECK(text.find("[1, 2, 3]") != std::string::npos);
    const json::Value back = json::parse(text);
    DAS_CHECK(back["b"].asInt() == 1);
    DAS_CHECK(back["a"].asArray().size() == 3);
    DAS_CHECK(back["s"].asString() == "line\n\"quoted\" \xc3\xa9");
    DAS_CHECK(back["n"].isNull());
    DAS_CHECK(back["t"].asBool());
    DAS_CHECK(back["neg"].as<std::int16_t>() == -42);
    DAS_CHECK(back["d"].asDouble() == 0.5);
    DAS_CHECK(back.find("missing") == nullptr);
    DAS_CHECK(json::write(back) == text);
    DAS_CHECK(json::write(json::parse(json::write(v, false)), false) == json::write(v, false));
}

DAS_TEST("json parses escapes and rejects bad input") {
    DAS_CHECK(json::parse(R"("\u00e9\u20ac\ud83d\ude00\t")").asString() == "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\t");
    DAS_CHECK(json::parse(" [ ] ").asArray().empty());
    DAS_CHECK(json::parse("{}").asObject().empty());
    DAS_CHECK(json::parse("1e3").asDouble() == 1000.0);
    DAS_CHECK(json::write(json::Value(std::string("\x01", 1)), false) == "\"\\u0001\"");
    DAS_CHECK(throws([] { json::parse("{"); }));
    DAS_CHECK(throws([] { json::parse("[1,]"); }));
    DAS_CHECK(throws([] { json::parse("01x"); }));
    DAS_CHECK(throws([] { json::parse("\"abc"); }));
    DAS_CHECK(throws([] { json::parse("1 2"); }));
    DAS_CHECK(throws([] { (void)json::parse("300").as<std::uint8_t>(); }));
    DAS_CHECK(throws([] { (void)json::parse("{}")["x"]; }));
}

DAS_TEST("crc32 and adler32 match known values") {
    DAS_CHECK(zlib::crc32(bytes("123456789")) == 0xcbf43926u);
    DAS_CHECK(zlib::adler32(bytes("Wikipedia")) == 0x11e60398u);
}

DAS_TEST("deflate round-trips data") {
    std::string text;
    for (int i = 0; i < 2000; ++i) {
        text += "tile " + std::to_string(i % 37) + ", ";
    }
    data::Bytes noise(70000);
    std::uint32_t seed = 1;
    for (auto& b : noise) {
        seed = seed * 1103515245u + 12345u;
        b = static_cast<std::uint8_t>(seed >> 24);
    }
    for (const data::Bytes& input : {data::Bytes{}, bytes("a"), bytes(text), noise, data::Bytes(100000, 7)}) {
        const data::Bytes packed = zlib::compress(input);
        DAS_CHECK(zlib::decompress(packed) == input);
    }
    DAS_CHECK(zlib::compress(bytes(text)).size() < text.size() / 4);
}

DAS_TEST("inflate reads stored and dynamic Huffman blocks") {
    // "hello" as a stored block.
    const data::Bytes stored{0x01, 0x05, 0x00, 0xfa, 0xff, 'h', 'e', 'l', 'l', 'o'};
    DAS_CHECK(zlib::inflate(stored) == bytes("hello"));
    // A zlib stream with a dynamic Huffman block, as written by a common
    // compressor at its highest level.
    const data::Bytes dynamic{
        0x78, 0xda, 0xb5, 0xca, 0xcb, 0x11, 0x83, 0x30, 0x10, 0x04, 0xd1, 0x54, 0x26, 0x02, 0xe5, 0x40, 0x15,
        0x19, 0xc0, 0x85, 0xa3, 0x40, 0x03, 0xc8, 0xd8, 0xbb, 0x62, 0xf5, 0xb1, 0x4d, 0xf4, 0x26, 0x09, 0x5f,
        0xfb, 0xf5, 0xb8, 0x13, 0x67, 0x8d, 0xcb, 0x81, 0xd9, 0xf4, 0x2d, 0x58, 0xf5, 0x83, 0x47, 0x7d, 0xa5,
        0x0c, 0x6d, 0x34, 0x94, 0x9b, 0x9f, 0xfe, 0xfa, 0x22, 0xe8, 0xe6, 0x30, 0xfe, 0x6d, 0x9e, 0x34, 0x78,
        0x0c, 0x45, 0x2d, 0x32, 0xc3, 0x4b, 0x40, 0xcf, 0x7c, 0x14, 0x4d, 0xe8, 0x42, 0xa3, 0x94, 0x6a, 0x77,
        0xce, 0xbb, 0x37, 0x42, 0x85, 0xa0, 0x6c, 0x51, 0xe8, 0x7e, 0x10, 0x9f, 0x43, 0xf3};
    std::string expected;
    for (int i = 0; i < 3; ++i) {
        expected += "The quick brown fox jumps over the lazy dog. ";
    }
    expected += "Yoda Stories and Desktop Adventures share one engine.";
    DAS_CHECK(zlib::decompress(dynamic) == bytes(expected));
    DAS_CHECK(throws([] { zlib::inflate(data::Bytes{0x07}); }));
    DAS_CHECK(throws([] { zlib::decompress(data::Bytes{0x78, 0x01, 0x03, 0x00, 0, 0, 0, 2}); }));
}

DAS_TEST("png round-trips an indexed image") {
    png::IndexedImage image;
    image.width = 37;
    image.height = 11;
    for (int i = 0; i < 256; ++i) {
        image.palette.colors[static_cast<std::size_t>(i)] = {static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(255 - i),
                                                             static_cast<std::uint8_t>(i * 7)};
    }
    image.transparentIndex = 0;
    for (int i = 0; i < image.width * image.height; ++i) {
        image.pixels.push_back(static_cast<std::uint8_t>((i * 13) ^ (i >> 3)));
    }
    const data::Bytes file = png::write(image);
    const png::IndexedImage back = png::read(file);
    DAS_CHECK(back.width == image.width);
    DAS_CHECK(back.height == image.height);
    DAS_CHECK(back.pixels == image.pixels);
    DAS_CHECK(back.palette.colors == image.palette.colors);
    DAS_CHECK(back.transparentIndex == std::optional<std::uint8_t>(0));

    data::Bytes broken = file;
    broken[40] ^= 0xff;
    DAS_CHECK(throws([&] { (void)png::read(broken); }));
}

DAS_TEST("windows-1252 text maps to utf-8 and back for every byte") {
    std::string all;
    for (int b = 0; b < 256; ++b) {
        all.push_back(static_cast<char>(b));
    }
    const std::string utf8 = text::windows1252ToUtf8(all);
    DAS_CHECK(text::utf8ToWindows1252(utf8) == all);
    DAS_CHECK(text::windows1252ToUtf8("\x80") == "\xe2\x82\xac");
    DAS_CHECK(throws([] { (void)text::utf8ToWindows1252("\xe4\xb8\xad"); }));
    DAS_CHECK(throws([] { (void)text::utf8ToWindows1252("\xc3"); }));
}

namespace {

// Builds a zip archive with one stored and one deflated file.
data::Bytes makeZip(const std::vector<std::pair<std::string, data::Bytes>>& files) {
    data::ByteWriter out;
    data::ByteWriter dir;
    std::uint16_t count = 0;
    for (const auto& [name, content] : files) {
        const bool deflate = count % 2 == 1;
        const data::Bytes packed = deflate ? zlib::deflate(content) : content;
        const auto offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t crc = zlib::crc32(content);
        out.u32(0x04034b50);
        out.u16(20);
        out.u16(0);
        out.u16(deflate ? 8 : 0);
        out.u32(0);
        out.u32(crc);
        out.u32(static_cast<std::uint32_t>(packed.size()));
        out.u32(static_cast<std::uint32_t>(content.size()));
        out.u16(static_cast<std::uint16_t>(name.size()));
        out.u16(0);
        out.bytes(bytes(name));
        out.bytes(packed);

        dir.u32(0x02014b50);
        dir.u16(20);
        dir.u16(20);
        dir.u16(0);
        dir.u16(deflate ? 8 : 0);
        dir.u32(0);
        dir.u32(crc);
        dir.u32(static_cast<std::uint32_t>(packed.size()));
        dir.u32(static_cast<std::uint32_t>(content.size()));
        dir.u16(static_cast<std::uint16_t>(name.size()));
        dir.u16(0);
        dir.u16(0);
        dir.u16(0);
        dir.u16(0);
        dir.u32(0);
        dir.u32(offset);
        dir.bytes(bytes(name));
        ++count;
    }
    const auto dirOffset = static_cast<std::uint32_t>(out.size());
    out.bytes(dir.data());
    out.u32(0x06054b50);
    out.u16(0);
    out.u16(0);
    out.u16(count);
    out.u16(count);
    out.u32(static_cast<std::uint32_t>(dir.size()));
    out.u32(dirOffset);
    out.u16(0);
    return std::move(out).take();
}

} // namespace

DAS_TEST("zip archives with stored and deflated entries are read") {
    const data::Bytes big(5000, 'x');
    const data::Bytes archive = makeZip({{"Game/readme.txt", bytes("hello")}, {"Game/sfx/big.wav", big}});
    const auto entries = zip::read(archive);
    DAS_CHECK(entries.size() == 2);
    DAS_CHECK(entries[0].name == "Game/readme.txt");
    DAS_CHECK(entries[0].content == bytes("hello"));
    DAS_CHECK(entries[1].name == "Game/sfx/big.wav");
    DAS_CHECK(entries[1].content == big);

    data::Bytes damaged = archive;
    damaged[30 + 15 + 1] ^= 1; // a byte of the stored file
    DAS_CHECK(throws([&] { (void)zip::read(damaged); }));
    DAS_CHECK(throws([] { (void)zip::read(bytes("not a zip at all, just some text here")); }));
}
