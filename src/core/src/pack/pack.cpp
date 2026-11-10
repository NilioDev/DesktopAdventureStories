// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/pack/pack.hpp"

#include "das/data/game_files.hpp"
#include "das/pack/names.hpp"
#include "das/util/json.hpp"
#include "das/util/png.hpp"
#include "das/util/text.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>

namespace das::pack {

using data::ByteView;
using data::Bytes;
using data::DataError;
using json::Array;
using json::Object;
using json::Value;

namespace {

constexpr const char* kFormatName = "desktop-adventure-stories-pack";
constexpr int kAtlasColumns = 32;
constexpr int kTileSize = 32;

// Sections whose content comes from the decoded data.
const std::set<std::string>& decodedTags() {
    static const std::set<std::string> tags{"VERS", "STUP", "SNDS", "TILE", "TNAM", "ZONE", "PUZ2", "CHAR",
                                            "CHWP", "CAUX", "ZAUX", "ZAX2", "ZAX3", "ZAX4", "HTSP", "ACTN"};
    return tags;
}

Bytes toBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

std::string toString(const Bytes& b) { return std::string(b.begin(), b.end()); }

std::string hex(ByteView bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 15]);
    }
    return out;
}

Bytes unhex(const std::string& s) {
    if (s.size() % 2 != 0) {
        throw DataError("hex string has an odd length");
    }
    const auto nibble = [](char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        throw DataError("bad hex digit");
    };
    Bytes out(s.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<std::uint8_t>((nibble(s[i * 2]) << 4) | nibble(s[i * 2 + 1]));
    }
    return out;
}

std::string zoneFile(std::size_t id) {
    char name[32];
    std::snprintf(name, sizeof name, "zones/%04zu.json", id);
    return name;
}

const Bytes& need(const Files& files, const std::string& name) {
    const auto it = files.find(name);
    if (it == files.end()) {
        throw DataError("pack has no file " + name);
    }
    return it->second;
}

template <typename T, std::size_t N>
Array numbers(const std::array<T, N>& values) {
    Array out;
    for (const auto v : values) {
        out.emplace_back(static_cast<std::int64_t>(v));
    }
    return out;
}

template <typename T>
Array numbers(const std::vector<T>& values) {
    Array out;
    for (const auto v : values) {
        out.emplace_back(static_cast<std::int64_t>(v));
    }
    return out;
}

template <typename T>
std::vector<T> readNumbers(const Value& v) {
    std::vector<T> out;
    for (const auto& item : v.asArray()) {
        out.push_back(item.as<T>());
    }
    return out;
}

template <typename T, std::size_t N>
std::array<T, N> readFixed(const Value& v, const char* what) {
    const auto& a = v.asArray();
    if (a.size() != N) {
        throw DataError(std::string(what) + " has the wrong number of values");
    }
    std::array<T, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = a[i].as<T>();
    }
    return out;
}

// ---- Zones -----------------------------------------------------------------

Value scriptLines(const std::vector<data::ScriptLine>& lines) {
    Array out;
    for (const auto& line : lines) {
        Object o;
        o.reserve(3);
        o.emplace_back("opcode", line.opcode);
        o.emplace_back("args", numbers(line.args));
        if (!line.text.empty()) {
            o.emplace_back("text", text::windows1252ToUtf8(line.text));
        }
        out.emplace_back(std::move(o));
    }
    return out;
}

std::vector<data::ScriptLine> readScriptLines(const Value& v) {
    std::vector<data::ScriptLine> out;
    for (const auto& item : v.asArray()) {
        data::ScriptLine line;
        line.opcode = item["opcode"].as<std::uint16_t>();
        line.args = readFixed<std::int16_t, 5>(item["args"], "script line args");
        if (const auto* t = item.find("text")) {
            line.text = text::utf8ToWindows1252(t->asString());
        }
        out.push_back(std::move(line));
    }
    return out;
}

Value encodeZone(const data::Zone& zone, std::size_t id) {
    Array layers;
    for (int layer = 0; layer < 3; ++layer) {
        Array cells;
        for (const auto& cell : zone.cells) {
            const auto t = cell.layers[static_cast<std::size_t>(layer)];
            cells.emplace_back(t == data::kNoTile ? std::int64_t{-1} : std::int64_t{t});
        }
        layers.emplace_back(std::move(cells));
    }

    Value hotspots;
    if (zone.hotspots) {
        Array list;
        for (const auto& h : *zone.hotspots) {
            list.emplace_back(Object{{"type", h.type}, {"x", h.x}, {"y", h.y}, {"enabled", h.enabled},
                                     {"argument", h.argument}});
        }
        hotspots = std::move(list);
    }

    Value actions;
    if (zone.actions) {
        Array list;
        for (const auto& a : *zone.actions) {
            list.emplace_back(
                Object{{"conditions", scriptLines(a.conditions)}, {"instructions", scriptLines(a.instructions)}});
        }
        actions = std::move(list);
    }

    Array monsters;
    for (const auto& m : zone.monsters) {
        monsters.emplace_back(Object{{"character", m.character},
                                     {"x", m.x},
                                     {"y", m.y},
                                     {"item", m.item},
                                     {"unknown", m.unknown},
                                     {"waypoints", numbers(m.waypoints)}});
    }

    return Object{
        {"id", static_cast<std::int64_t>(id)},
        {"width", zone.width},
        {"height", zone.height},
        {"type", zone.type},
        {"planet", zone.planet},
        {"index", zone.index},
        {"izonField", zone.izonField},
        {"izonPlanet", zone.izonPlanet},
        {"auxField", zone.auxField},
        {"izx4", zone.izx4},
        {"layers", std::move(layers)},
        {"hotspots", std::move(hotspots)},
        {"monsters", std::move(monsters)},
        {"requiredItems", numbers(zone.requiredItems)},
        {"goalItems", numbers(zone.goalItems)},
        {"providedItems", numbers(zone.providedItems)},
        {"npcs", numbers(zone.npcs)},
        {"actions", std::move(actions)},
    };
}

data::Zone decodeZone(const Value& v) {
    data::Zone zone;
    zone.width = v["width"].as<std::uint16_t>();
    zone.height = v["height"].as<std::uint16_t>();
    zone.type = v["type"].as<std::uint32_t>();
    zone.planet = v["planet"].as<std::uint16_t>();
    zone.index = v["index"].as<std::uint16_t>();
    zone.izonField = v["izonField"].as<std::uint16_t>();
    zone.izonPlanet = v["izonPlanet"].as<std::uint16_t>();
    zone.auxField = v["auxField"].as<std::uint16_t>();
    zone.izx4 = v["izx4"].as<std::uint16_t>();

    const std::size_t count = static_cast<std::size_t>(zone.width) * zone.height;
    zone.cells.resize(count);
    const auto& layers = v["layers"].asArray();
    if (layers.size() != 3) {
        throw DataError("zone does not have three layers");
    }
    for (std::size_t layer = 0; layer < 3; ++layer) {
        const auto& cells = layers[layer].asArray();
        if (cells.size() != count) {
            throw DataError("zone layer has the wrong number of cells");
        }
        for (std::size_t i = 0; i < count; ++i) {
            const std::int64_t t = cells[i].asInt();
            zone.cells[i].layers[layer] = t == -1 ? data::kNoTile : cells[i].as<std::uint16_t>();
        }
    }

    if (const auto& h = v["hotspots"]; !h.isNull()) {
        zone.hotspots.emplace();
        for (const auto& item : h.asArray()) {
            zone.hotspots->push_back({item["type"].as<std::uint32_t>(), item["x"].as<std::uint16_t>(),
                                      item["y"].as<std::uint16_t>(), item["enabled"].as<std::uint16_t>(),
                                      item["argument"].as<std::uint16_t>()});
        }
    }
    if (const auto& a = v["actions"]; !a.isNull()) {
        zone.actions.emplace();
        for (const auto& item : a.asArray()) {
            zone.actions->push_back({readScriptLines(item["conditions"]), readScriptLines(item["instructions"])});
        }
    }
    for (const auto& item : v["monsters"].asArray()) {
        data::ZoneMonster m;
        m.character = item["character"].as<std::uint16_t>();
        m.x = item["x"].as<std::uint16_t>();
        m.y = item["y"].as<std::uint16_t>();
        m.item = item["item"].as<std::uint16_t>();
        m.unknown = item["unknown"].as<std::uint32_t>();
        m.waypoints = readFixed<std::uint16_t, 16>(item["waypoints"], "waypoints");
        zone.monsters.push_back(m);
    }
    zone.requiredItems = readNumbers<std::uint16_t>(v["requiredItems"]);
    zone.goalItems = readNumbers<std::uint16_t>(v["goalItems"]);
    zone.providedItems = readNumbers<std::uint16_t>(v["providedItems"]);
    zone.npcs = readNumbers<std::uint16_t>(v["npcs"]);
    return zone;
}

// ---- Lists -----------------------------------------------------------------

Value taggedEntries(const std::vector<data::TaggedEntry>& entries) {
    Array out;
    for (const auto& e : entries) {
        out.emplace_back(Object{{"id", e.id}, {"body", hex(e.body)}});
    }
    return out;
}

std::vector<data::TaggedEntry> readTaggedEntries(const Value& v) {
    std::vector<data::TaggedEntry> out;
    for (const auto& item : v.asArray()) {
        out.push_back({item["id"].as<std::uint16_t>(), unhex(item["body"].asString())});
    }
    return out;
}

Value characterValues(const std::vector<data::CharacterValues>& values, bool withReference) {
    Array out;
    for (const auto& c : values) {
        Object o;
        o.reserve(3);
        o.emplace_back("character", c.character);
        if (withReference) {
            o.emplace_back("reference", c.reference);
        }
        o.emplace_back("value", c.value);
        out.emplace_back(std::move(o));
    }
    return out;
}

std::vector<data::CharacterValues> readCharacterValues(const Value& v, bool withReference) {
    std::vector<data::CharacterValues> out;
    for (const auto& item : v.asArray()) {
        data::CharacterValues c;
        c.character = item["character"].as<std::uint16_t>();
        if (withReference) {
            c.reference = item["reference"].as<std::uint16_t>();
        }
        c.value = item["value"].as<std::uint16_t>();
        out.push_back(c);
    }
    return out;
}

png::IndexedImage tileAtlas(const std::vector<data::Tile>& tiles, const data::Palette& palette) {
    png::IndexedImage image;
    const int rows = std::max(1, static_cast<int>((tiles.size() + kAtlasColumns - 1) / kAtlasColumns));
    image.width = kAtlasColumns * kTileSize;
    image.height = rows * kTileSize;
    image.palette = palette;
    image.transparentIndex = std::uint8_t{0};
    image.pixels.assign(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height), 0);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const int ox = static_cast<int>(i % kAtlasColumns) * kTileSize;
        const int oy = static_cast<int>(i / kAtlasColumns) * kTileSize;
        for (int y = 0; y < kTileSize; ++y) {
            std::copy_n(tiles[i].pixels.begin() + y * kTileSize, kTileSize,
                        image.pixels.begin() + static_cast<std::ptrdiff_t>((oy + y) * image.width + ox));
        }
    }
    return image;
}

} // namespace

std::string gameId(data::Layout layout) { return layout == data::Layout::Indy ? "indy" : "yoda"; }

Files encode(const Pack& pack) {
    const data::GameData& d = pack.data;
    Files files;

    Array sections;
    for (const auto& section : d.container.sections) {
        Object o;
        o.reserve(2);
        o.emplace_back("tag", section.tag);
        if (decodedTags().count(section.tag) == 0) {
            const std::string name = "raw/" + section.tag + ".bin";
            files[name] = section.payload;
            o.emplace_back("file", name);
        }
        sections.emplace_back(std::move(o));
    }

    Array sounds;
    for (const auto& name : d.sounds) {
        const std::string file = "sounds/" + detail::soundFileName(name);
        Object o;
        o.reserve(2);
        o.emplace_back("name", text::windows1252ToUtf8(name));
        if (pack.sounds.count(file) != 0) {
            o.emplace_back("file", file);
        }
        sounds.emplace_back(std::move(o));
    }
    for (const auto& [name, bytes] : pack.sounds) {
        files[name] = bytes;
    }

    Array zones;
    for (std::size_t i = 0; i < d.zones.size(); ++i) {
        const std::string name = zoneFile(i);
        files[name] = toBytes(json::write(encodeZone(d.zones[i], i)));
        zones.emplace_back(name);
    }

    Array tileFlags;
    for (const auto& t : d.tiles) {
        tileFlags.emplace_back(static_cast<std::int64_t>(t.flags));
    }
    Array tileNames;
    for (const auto& n : d.tileNames) {
        tileNames.emplace_back(Object{{"tile", n.tile}, {"name", text::windows1252ToUtf8(n.name)}});
    }
    files["tiles.png"] = png::write(tileAtlas(d.tiles, pack.palette));
    files["tiles.json"] = toBytes(json::write(Object{{"image", "tiles.png"},
                                                     {"tileSize", kTileSize},
                                                     {"columns", kAtlasColumns},
                                                     {"count", static_cast<std::int64_t>(d.tiles.size())},
                                                     {"flags", std::move(tileFlags)},
                                                     {"names", std::move(tileNames)}}));

    Value setup;
    if (!d.setupScreen.empty()) {
        png::IndexedImage image;
        image.width = data::kSetupScreenSize;
        image.height = data::kSetupScreenSize;
        image.palette = pack.palette;
        image.pixels = d.setupScreen;
        files["setup.png"] = png::write(image);
        setup = "setup.png";
    }

    files["puzzles.json"] = toBytes(json::write(taggedEntries(d.puzzles)));
    files["characters.json"] = toBytes(json::write(Object{
        {"characters", taggedEntries(d.characters)},
        {"weapons", characterValues(d.characterWeapons, true)},
        {"aux", characterValues(d.characterAux, false)},
    }));

    files[kManifestName] = toBytes(json::write(Object{
        {"format", kFormatName},
        {"formatVersion", kFormatVersion},
        {"game", pack.game},
        {"layout", gameId(d.layout())},
        {"fileVersion", static_cast<std::int64_t>(d.version)},
        {"sections", std::move(sections)},
        {"tiles", "tiles.json"},
        {"setupScreen", std::move(setup)},
        {"sounds", std::move(sounds)},
        {"zones", std::move(zones)},
        {"puzzles", "puzzles.json"},
        {"characters", "characters.json"},
    }));
    return files;
}

Pack decode(const Files& files) {
    const Value manifest = json::parse(toString(need(files, kManifestName)));
    if (manifest["format"].asString() != kFormatName) {
        throw DataError("not a DesktopAdventureStories pack");
    }
    if (manifest["formatVersion"].asInt() > kFormatVersion) {
        throw DataError("pack was made by a newer version");
    }

    Pack pack;
    pack.game = manifest["game"].asString();
    data::GameData& d = pack.data;
    const std::string layout = manifest["layout"].asString();
    if (layout == "indy") {
        d.container.layout = data::Layout::Indy;
    } else if (layout == "yoda") {
        d.container.layout = data::Layout::Yoda;
    } else {
        throw DataError("unknown layout " + layout);
    }
    d.version = manifest["fileVersion"].as<std::uint32_t>();
    for (const auto& s : manifest["sections"].asArray()) {
        data::Section section;
        section.tag = s["tag"].asString();
        if (const auto* file = s.find("file")) {
            section.payload = need(files, file->asString());
        }
        d.container.sections.push_back(std::move(section));
    }

    for (const auto& s : manifest["sounds"].asArray()) {
        d.sounds.push_back(text::utf8ToWindows1252(s["name"].asString()));
        if (const auto* file = s.find("file")) {
            pack.sounds[file->asString()] = need(files, file->asString());
        }
    }

    const Value tiles = json::parse(toString(need(files, manifest["tiles"].asString())));
    const png::IndexedImage atlas = png::read(need(files, tiles["image"].asString()));
    pack.palette = atlas.palette;
    const int size = tiles["tileSize"].as<int>();
    const int columns = tiles["columns"].as<int>();
    if (size != kTileSize || columns <= 0 || atlas.width < columns * size) {
        throw DataError("tile atlas has an unexpected layout");
    }
    const auto& flags = tiles["flags"].asArray();
    const std::size_t count = tiles["count"].as<std::uint32_t>();
    if (flags.size() != count ||
        static_cast<std::size_t>(atlas.height / size) * static_cast<std::size_t>(columns) < count) {
        throw DataError("tile atlas does not hold every tile");
    }
    d.tiles.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        d.tiles[i].flags = flags[i].as<std::uint32_t>();
        const int ox = static_cast<int>(i % static_cast<std::size_t>(columns)) * size;
        const int oy = static_cast<int>(i / static_cast<std::size_t>(columns)) * size;
        for (int y = 0; y < size; ++y) {
            std::copy_n(atlas.pixels.begin() + static_cast<std::ptrdiff_t>((oy + y) * atlas.width + ox), size,
                        d.tiles[i].pixels.begin() + y * size);
        }
    }
    for (const auto& n : tiles["names"].asArray()) {
        d.tileNames.push_back({n["tile"].as<std::uint16_t>(), text::utf8ToWindows1252(n["name"].asString())});
    }

    if (const auto& setup = manifest["setupScreen"]; !setup.isNull()) {
        const png::IndexedImage image = png::read(need(files, setup.asString()));
        if (image.width != data::kSetupScreenSize || image.height != data::kSetupScreenSize) {
            throw DataError("setup screen is not 288 x 288");
        }
        d.setupScreen = image.pixels;
    }

    for (const auto& z : manifest["zones"].asArray()) {
        d.zones.push_back(decodeZone(json::parse(toString(need(files, z.asString())))));
    }

    d.puzzles = readTaggedEntries(json::parse(toString(need(files, manifest["puzzles"].asString()))));
    const Value chars = json::parse(toString(need(files, manifest["characters"].asString())));
    d.characters = readTaggedEntries(chars["characters"]);
    d.characterWeapons = readCharacterValues(chars["weapons"], true);
    d.characterAux = readCharacterValues(chars["aux"], false);
    return pack;
}

void writeFolder(const Files& files, const std::filesystem::path& dir) {
    for (const auto& [name, bytes] : files) {
        const std::filesystem::path path = dir / std::filesystem::path(name);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            throw DataError("could not write " + path.string());
        }
    }
}

Files readFolder(const std::filesystem::path& dir) {
    Files files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            files[std::filesystem::relative(entry.path(), dir).generic_string()] = data::readFile(entry.path());
        }
    }
    return files;
}

} // namespace das::pack
