// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mods (docs/formats/mod.md) and the colour PNG reading they rely on. Most
// tests build a tiny pack by hand; the last one uses the real games when
// DAS_GAMEDATA_DIR is set.
#include "test.hpp"

#include "das/data/game_files.hpp"
#include "das/pack/import.hpp"
#include "das/pack/mod.hpp"
#include "das/util/json.hpp"
#include "das/util/png.hpp"
#include "das/util/zlib.hpp"
#include "das/world/generator.hpp"
#include "das/world/world.hpp"

#include <cstdlib>
#include <filesystem>
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

void put32(data::Bytes& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(v >> shift));
    }
}

void chunk(data::Bytes& out, const char* type, const data::Bytes& body) {
    put32(out, static_cast<std::uint32_t>(body.size()));
    const std::size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    put32(out, zlib::crc32(data::ByteView(out).subspan(start)));
}

// An RGBA PNG as a drawing program would save it, with the Sub filter on
// every row.
data::Bytes rgbaPng(int width, int height, const std::vector<std::uint8_t>& rgba) {
    data::Bytes out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    data::Bytes ihdr;
    put32(ihdr, static_cast<std::uint32_t>(width));
    put32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    data::Bytes raw;
    const auto stride = static_cast<std::size_t>(width) * 4;
    for (std::size_t y = 0; y < static_cast<std::size_t>(height); ++y) {
        raw.push_back(1);
        for (std::size_t x = 0; x < stride; ++x) {
            const std::uint8_t left = x >= 4 ? rgba[y * stride + x - 4] : 0;
            raw.push_back(static_cast<std::uint8_t>(rgba[y * stride + x] - left));
        }
    }
    chunk(out, "IDAT", zlib::compress(raw));
    chunk(out, "IEND", {});
    return out;
}

data::Palette testPalette() {
    data::Palette p;
    for (std::size_t i = 0; i < 256; ++i) {
        p.colors[i] = {static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(255 - i),
                       static_cast<std::uint8_t>((i * 7) & 255)};
    }
    return p;
}

pack::Pack tinyPack() {
    pack::Pack p;
    p.game = "yoda";
    p.palette = testPalette();
    data::GameData& d = p.data;
    d.container.layout = data::Layout::Yoda;
    d.tiles.resize(3);
    for (std::size_t t = 0; t < d.tiles.size(); ++t) {
        d.tiles[t].flags = static_cast<std::uint32_t>(t + 1);
        d.tiles[t].pixels.fill(static_cast<std::uint8_t>(10 + t));
    }
    d.tileNames = {{1, "Rock"}};
    data::Zone zone;
    zone.width = 9;
    zone.height = 9;
    zone.cells.resize(81);
    for (auto& c : zone.cells) {
        c.layers[0] = 0;
    }
    zone.hotspots = std::vector<data::Hotspot>{};
    zone.actions = std::vector<data::Action>{};
    d.zones = {zone};
    d.sounds = {"C:\\GAME\\PUSH.WAV"};
    p.sounds["sounds/push.wav"] = bytes("RIFF push");
    d.puzzles = {{0, bytes("puzzle0")}};
    d.characters = {{0, bytes("hero")}};
    return p;
}

pack::ModInfo info(const char* id) {
    pack::ModInfo i;
    i.id = id;
    i.game = "yoda";
    i.name = "Test mod";
    i.version = "1.0";
    return i;
}

} // namespace

DAS_TEST("colour PNG files are read and matched to the palette") {
    const data::Palette palette = testPalette();
    std::vector<std::uint8_t> rgba;
    const auto add = [&](data::Color c, std::uint8_t alpha) { rgba.insert(rgba.end(), {c.r, c.g, c.b, alpha}); };
    add(palette.colors[5], 255);   // exact
    add(palette.colors[200], 255); // exact
    add({0, 0, 0}, 0);             // transparent
    add({101, 155, 191}, 255);     // close to colour 100 (100, 155, 188)
    const png::RgbaImage image = png::readRgba(rgbaPng(4, 1, rgba));
    DAS_CHECK(image.width == 4 && image.height == 1);
    DAS_CHECK(image.pixels == rgba);
    const auto indices = png::toPaletteIndices(image, palette);
    DAS_CHECK((indices == std::vector<std::uint8_t>{5, 200, 0, 100}));

    // Our own palette PNG files read the same way.
    png::IndexedImage indexed;
    indexed.width = 2;
    indexed.height = 1;
    indexed.palette = palette;
    indexed.transparentIndex = std::uint8_t{0};
    indexed.pixels = {0, 7};
    const png::RgbaImage back = png::readRgba(png::write(indexed));
    DAS_CHECK(back.pixels[3] == 0);
    DAS_CHECK(back.pixels[4] == 7 && back.pixels[7] == 255);
    DAS_CHECK(png::toPaletteIndices(back, palette) == indexed.pixels);
}

DAS_TEST("a mod made from an edited pack turns the original into the edit") {
    const pack::Pack original = tinyPack();
    pack::Pack changed = original;
    data::GameData& d = changed.data;
    d.tiles[2].pixels[5] = 99;                // a changed tile
    d.tiles.push_back(d.tiles[0]);            // a new tile
    d.tiles.back().pixels[0] = 0;             // with a transparent pixel
    d.tiles.back().flags = 0x40;
    d.tileNames.push_back({3, "Crystal"});    // a new name
    d.tileNames[0].name = "Big rock";         // a changed name
    d.zones[0].cells[10].layers[1] = 3;       // a changed zone, using the new tile
    d.zones.push_back(d.zones[0]);            // a new zone
    d.zones.back().type = 5;
    changed.sounds["sounds/push.wav"] = bytes("RIFF louder push");
    changed.sounds["sounds/bell.wav"] = bytes("RIFF bell");
    d.sounds.push_back("bell.wav");
    d.puzzles[0].body = bytes("puzzle0b");
    d.puzzles.push_back({1, bytes("puzzle1")});

    const pack::Mod mod = pack::makeMod(original, changed, info("test-mod"));
    const auto& files = mod.files;
    DAS_CHECK(files.count("mod.json") == 1);
    DAS_CHECK(files.count("tiles/0002.png") == 1 && files.count("tiles/0003.png") == 1);
    DAS_CHECK(files.count("tiles/0000.png") == 0);
    DAS_CHECK(files.count("zones/0000.json") == 1 && files.count("zones/0001.json") == 1);
    DAS_CHECK(files.count("sounds/push.wav") == 1 && files.count("sounds/bell.wav") == 1);
    DAS_CHECK(files.count("puzzles.json") == 1);
    DAS_CHECK(files.count("characters.json") == 0 && files.count("setup.png") == 0);
    DAS_CHECK(pack::readModInfo(files).id == "test-mod");

    pack::Pack applied = original;
    pack::applyMod(applied, mod);
    DAS_CHECK(pack::encode(applied) == pack::encode(changed));

    // Nothing changed: only the manifest.
    DAS_CHECK(pack::makeMod(original, original, info("empty")).files.size() == 1);
}

DAS_TEST("mods that do not fit the pack are refused") {
    const pack::Pack original = tinyPack();
    const auto refused = [&](const std::string& name, const data::Bytes& content) {
        pack::Mod mod;
        mod.info = info("bad");
        mod.files["mod.json"] = pack::writeModInfo(mod.info);
        mod.files[name] = content;
        pack::Pack p = original;
        return throws([&] { pack::applyMod(p, mod); });
    };
    pack::Pack edited = original;
    const auto zoneFile = pack::makeMod(original, [&] {
                              edited.data.zones[0].cells[0].layers[2] = 2;
                              return edited;
                          }(),
                                        info("z"))
                              .files.at("zones/0000.json");
    DAS_CHECK(!refused("zones/0000.json", zoneFile));
    DAS_CHECK(refused("zones/0005.json", zoneFile)); // leaves a gap
    std::string text(zoneFile.begin(), zoneFile.end());
    text.replace(text.find("[2,"), 3, "[9,"); // no tile 9
    DAS_CHECK(text.find("[9,") != std::string::npos);
    DAS_CHECK(refused("zones/0000.json", bytes(text)));
    DAS_CHECK(refused("tiles/0000.png", rgbaPng(2, 2, std::vector<std::uint8_t>(16, 255)))); // not 32 x 32
    DAS_CHECK(refused("tiles.json", bytes(R"({"tiles": [{"tile": 40, "flags": 1}]})")));

    pack::Mod indy;
    indy.info = info("indy-only");
    indy.info.game = "indy";
    pack::Pack p = original;
    DAS_CHECK(throws([&] { pack::applyMod(p, indy); }));

    DAS_CHECK(pack::validModId("my-mod_2"));
    DAS_CHECK(!pack::validModId("My Mod"));
    DAS_CHECK(!pack::validModId("../up"));
    DAS_CHECK(!pack::validModId(""));
    DAS_CHECK(throws([] { pack::readModInfo({{"mod.json", bytes(R"({"format": "something-else"})")}}); }));
}

DAS_TEST("mods are written to and found in a folder") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "das-test-mods";
    fs::remove_all(root);
    const pack::Pack original = tinyPack();
    pack::Pack changed = original;
    changed.data.zones[0].cells[3].layers[1] = 1;

    pack::Mod a = pack::makeMod(original, changed, info("b-second"));
    pack::writeMod(a, root / "b-second");
    pack::Mod b = pack::makeMod(original, original, info("a-first"));
    pack::writeMod(b, root / "a-first");
    pack::writeMod(a, root / "b-second"); // replacing works
    fs::create_directories(root / "not-a-mod");
    pack::writeFolder({{"readme.txt", bytes("hello")}}, root / "not-a-mod");

    std::vector<std::string> problems;
    const auto found = pack::findMods(root, &problems);
    DAS_CHECK(found.size() == 2);
    DAS_CHECK(found.size() == 2 && found[0].info.id == "a-first" && found[1].info.id == "b-second");
    DAS_CHECK(problems.size() == 1);
    DAS_CHECK(found.size() == 2 && found[1].files == a.files);
    // Never overwrite a folder that is no mod.
    DAS_CHECK(throws([&] { pack::writeMod(a, root / "not-a-mod"); }));
    fs::remove_all(root);
}

DAS_TEST("a mod with a new zone and tile works on the real games") {
    const char* dir = std::getenv("DAS_GAMEDATA_DIR");
    if (!dir || !*dir) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    const auto source = pack::openSource(dir);
    DAS_CHECK(source != nullptr);
    if (!source) {
        return;
    }
    for (const pack::Pack& original : pack::importGames(*source)) {
        pack::Pack changed = original;
        auto& d = changed.data;
        d.tiles.push_back(d.tiles[0]);
        d.zones[1].cells[0].layers[1] = static_cast<std::uint16_t>(d.tiles.size() - 1);
        d.zones.push_back(d.zones[1]);
        pack::ModInfo i = info("real");
        i.game = original.game;
        const pack::Mod mod = pack::makeMod(original, changed, i);
        DAS_CHECK(mod.files.size() == 5); // mod.json, a tile with its flags, two zones
        pack::Pack applied = original;
        pack::applyMod(applied, mod);
        DAS_CHECK(pack::encode(applied) == pack::encode(changed));

        // The game still runs: a world is made and its first zone entered.
        world::GeneratorOptions options;
        options.seed = 7;
        world::World w(applied.data);
        world::WorldMap map = world::generateWorld(applied.data, options);
        const int start = map.startZone;
        w.setMap(std::move(map));
        w.start(start);
        w.begin();
        DAS_CHECK(w.zoneIndex() == start);
    }
}
