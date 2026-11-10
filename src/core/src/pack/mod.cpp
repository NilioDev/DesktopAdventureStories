// SPDX-License-Identifier: GPL-3.0-or-later
//
// Mods: overlays on a pack (docs/formats/mod.md).
#include "das/pack/mod.hpp"

#include "das/data/game_files.hpp"
#include "das/pack/names.hpp"
#include "das/pack/parts.hpp"
#include "das/script/script_text.hpp"
#include "das/util/json.hpp"
#include "das/util/png.hpp"
#include "das/util/text.hpp"
#include "das/util/zip.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <system_error>

namespace das::pack {

using data::Bytes;
using data::DataError;
using json::Array;
using json::Object;
using json::Value;

namespace {

constexpr const char* kModFormatName = "desktop-adventure-stories-mod";
constexpr int kTileSize = 32;

Bytes toBytes(const std::string& s) { return Bytes(s.begin(), s.end()); }
std::string toString(const Bytes& b) { return std::string(b.begin(), b.end()); }

std::string scriptFileName(std::size_t index) {
    char name[48];
    std::snprintf(name, sizeof name, "scripts/%04zu.txt", index);
    return name;
}

bool sameScripts(const std::optional<std::vector<data::Action>>& a, const std::optional<std::vector<data::Action>>& b,
                 data::Layout game) {
    if (a.has_value() != b.has_value()) {
        return false;
    }
    return !a || script::writeScriptText(*a, game) == script::writeScriptText(*b, game);
}

std::string tileFileName(std::size_t index) {
    char name[48];
    std::snprintf(name, sizeof name, "tiles/%04zu.png", index);
    return name;
}

// The number in "zones/0042.json" or "tiles/0042.png", if the name has that shape.
std::optional<std::size_t> numberedFile(const std::string& name, const std::string& folder,
                                        const std::string& extension) {
    if (name.size() <= folder.size() + extension.size() || name.compare(0, folder.size(), folder) != 0 ||
        name.compare(name.size() - extension.size(), extension.size(), extension) != 0) {
        return std::nullopt;
    }
    const std::string digits = name.substr(folder.size(), name.size() - folder.size() - extension.size());
    if (digits.empty() || digits.size() > 5 ||
        !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::stoul(digits));
}

// Where a numbered entry goes: an existing one, or the next new one.
void checkSlot(std::size_t index, std::size_t count, const std::string& file) {
    if (index > count) {
        throw DataError(file + ": a new entry must come right after the last one (" + std::to_string(count) + ")");
    }
}

std::vector<std::uint8_t> imagePixels(const Bytes& file, const data::Palette& palette, int width, int height,
                                      const std::string& name) {
    png::RgbaImage image;
    try {
        image = png::readRgba(file);
    } catch (const std::exception& e) {
        throw DataError(name + ": " + e.what());
    }
    if (image.width != width || image.height != height) {
        throw DataError(name + ": the image must be " + std::to_string(width) + " x " + std::to_string(height) +
                        " pixels");
    }
    return png::toPaletteIndices(image, palette);
}

Bytes indexedPng(const std::uint8_t* pixels, int size, const data::Palette& palette) {
    png::IndexedImage image;
    image.width = size;
    image.height = size;
    image.palette = palette;
    image.transparentIndex = std::uint8_t{0};
    image.pixels.assign(pixels, pixels + static_cast<std::ptrdiff_t>(size) * size);
    return png::write(image);
}

Value parseFile(const Files& files, const std::string& name) {
    try {
        return json::parse(toString(files.at(name)));
    } catch (const std::exception& e) {
        throw DataError(name + ": " + e.what());
    }
}

// Replaces the entry with the same id, or adds it as the next one.
void mergeEntries(std::vector<data::TaggedEntry>& list, const std::vector<data::TaggedEntry>& changes,
                  const std::string& file) {
    for (const auto& e : changes) {
        const auto it = std::find_if(list.begin(), list.end(), [&](const auto& x) { return x.id == e.id; });
        if (it != list.end()) {
            it->body = e.body;
        } else {
            checkSlot(e.id, list.size(), file);
            list.push_back(e);
        }
    }
}

void mergeValues(std::vector<data::CharacterValues>& list, const std::vector<data::CharacterValues>& changes) {
    for (const auto& c : changes) {
        const auto it =
            std::find_if(list.begin(), list.end(), [&](const auto& x) { return x.character == c.character; });
        if (it != list.end()) {
            *it = c;
        } else {
            list.push_back(c);
        }
    }
}

bool sameValues(const data::CharacterValues& a, const data::CharacterValues& b) {
    return a.character == b.character && a.reference == b.reference && a.value == b.value;
}

std::map<std::uint16_t, std::string> namesByTile(const std::vector<data::TileName>& names) {
    std::map<std::uint16_t, std::string> out;
    for (const auto& n : names) {
        out[n.tile] = n.name;
    }
    return out;
}

void readMovedFiles(Files& files) {
    if (files.count(kModManifestName) != 0) {
        return;
    }
    // A zip made from the mod's folder: everything sits in one folder.
    std::optional<std::string> prefix;
    for (const auto& [name, bytes] : files) {
        const auto slash = name.find('/');
        if (slash != std::string::npos && name.compare(slash + 1, std::string::npos, kModManifestName) == 0) {
            if (prefix) {
                throw DataError("the archive holds more than one mod");
            }
            prefix = name.substr(0, slash + 1);
        }
    }
    if (!prefix) {
        throw DataError("no mod.json found");
    }
    Files moved;
    for (auto& [name, bytes] : files) {
        if (name.compare(0, prefix->size(), *prefix) == 0) {
            moved[name.substr(prefix->size())] = std::move(bytes);
        }
    }
    files = std::move(moved);
}

} // namespace

bool validModId(const std::string& id) {
    return !id.empty() && id.size() <= 64 && id.front() != '-' && id.front() != '_' &&
           std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
           });
}

ModInfo readModInfo(const Files& files) {
    if (files.count(kModManifestName) == 0) {
        throw DataError("no mod.json found");
    }
    const Value v = parseFile(files, kModManifestName);
    if (const auto* f = v.find("format"); f == nullptr || !f->isString() || f->asString() != kModFormatName) {
        throw DataError("mod.json: this is not a DesktopAdventureStories mod");
    }
    if (v["formatVersion"].asInt() > kModFormatVersion) {
        throw DataError("mod.json: the mod was made for a newer version");
    }
    const auto text = [&](const char* key) {
        const auto* value = v.find(key);
        return value != nullptr && value->isString() ? value->asString() : std::string();
    };
    ModInfo info;
    info.id = text("id");
    info.game = text("game");
    info.name = text("name");
    info.version = text("version");
    info.author = text("author");
    info.description = text("description");
    if (!validModId(info.id)) {
        throw DataError("mod.json: \"" + info.id + "\" cannot be a mod id (use a-z, 0-9, - and _)");
    }
    if (info.game != "yoda" && info.game != "indy") {
        throw DataError("mod.json: game must be \"yoda\" or \"indy\"");
    }
    if (info.name.empty()) {
        info.name = info.id;
    }
    return info;
}

Bytes writeModInfo(const ModInfo& info) {
    return toBytes(json::write(Object{
        {"format", kModFormatName},
        {"formatVersion", kModFormatVersion},
        {"id", info.id},
        {"game", info.game},
        {"name", info.name},
        {"version", info.version},
        {"author", info.author},
        {"description", info.description},
    }));
}

Mod readMod(const std::filesystem::path& where) {
    Mod mod;
    mod.location = where;
    if (std::filesystem::is_directory(where)) {
        mod.files = readFolder(where);
    } else {
        for (auto& entry : zip::read(data::readFile(where))) {
            mod.files[entry.name] = std::move(entry.content);
        }
        readMovedFiles(mod.files);
    }
    mod.info = readModInfo(mod.files);
    return mod;
}

std::vector<Mod> findMods(const std::filesystem::path& folder, std::vector<std::string>* problems) {
    std::vector<Mod> mods;
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) {
        return mods;
    }
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        const auto& path = entry.path();
        const bool isFolder = entry.is_directory();
        const bool isZip = entry.is_regular_file() && detail::lower(path.extension().string()) == ".zip";
        const std::string fileName = path.filename().string();
        const bool leftOver = fileName.ends_with(".old") || fileName.ends_with(".new"); // of an interrupted save
        if ((!isFolder && !isZip) || leftOver || fileName.starts_with(".")) {
            continue;
        }
        try {
            Mod mod = readMod(path);
            const auto same = std::find_if(mods.begin(), mods.end(), [&](const Mod& m) {
                return m.info.id == mod.info.id && m.info.game == mod.info.game;
            });
            if (same != mods.end()) {
                // A folder wins over a zip of the same mod: it is the one being edited.
                if (problems) {
                    problems->push_back(path.filename().string() + ": mod \"" + mod.info.id + "\" is there twice");
                }
                if (isFolder) {
                    *same = std::move(mod);
                }
                continue;
            }
            mods.push_back(std::move(mod));
        } catch (const std::exception& e) {
            if (problems) {
                problems->push_back(path.filename().string() + ": " + e.what());
            }
        }
    }
    std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) {
        return a.info.game != b.info.game ? a.info.game < b.info.game : a.info.id < b.info.id;
    });
    return mods;
}

void applyMod(Pack& pack, const Mod& mod) {
    if (mod.info.game != pack.game) {
        throw DataError("mod \"" + mod.info.id + "\" is for another game");
    }
    data::GameData& d = pack.data;
    const Files& files = mod.files;

    // Tiles first, so that zones may use the new ones.
    for (const auto& [name, bytes] : files) {
        const auto index = numberedFile(name, "tiles/", ".png");
        if (!index) {
            continue;
        }
        checkSlot(*index, d.tiles.size(), name);
        const auto pixels = imagePixels(bytes, pack.palette, kTileSize, kTileSize, name);
        if (*index == d.tiles.size()) {
            d.tiles.emplace_back();
        }
        std::copy(pixels.begin(), pixels.end(), d.tiles[*index].pixels.begin());
    }
    if (files.count("tiles.json") != 0) {
        const Value v = parseFile(files, "tiles.json");
        for (const auto& t : v["tiles"].asArray()) {
            const auto tile = t["tile"].as<std::uint16_t>();
            if (tile >= d.tiles.size()) {
                throw DataError("tiles.json: there is no tile " + std::to_string(tile));
            }
            if (const auto* flags = t.find("flags")) {
                d.tiles[tile].flags = flags->as<std::uint32_t>();
            }
            if (const auto* name = t.find("name")) {
                const std::string text = text::utf8ToWindows1252(name->asString());
                const auto it = std::find_if(d.tileNames.begin(), d.tileNames.end(),
                                             [&](const data::TileName& n) { return n.tile == tile; });
                if (text.empty()) {
                    if (it != d.tileNames.end()) {
                        d.tileNames.erase(it);
                    }
                } else if (it != d.tileNames.end()) {
                    it->name = text;
                } else {
                    const auto at = std::find_if(d.tileNames.begin(), d.tileNames.end(),
                                                 [&](const data::TileName& n) { return n.tile > tile; });
                    d.tileNames.insert(at, {tile, text});
                }
            }
        }
    }

    for (const auto& [name, bytes] : files) {
        const auto index = numberedFile(name, "zones/", ".json");
        if (!index) {
            continue;
        }
        checkSlot(*index, d.zones.size(), name);
        data::Zone zone;
        try {
            zone = detail::decodeZone(json::parse(toString(bytes)));
        } catch (const std::exception& e) {
            throw DataError(name + ": " + e.what());
        }
        if (zone.cells.size() != static_cast<std::size_t>(zone.width) * zone.height || zone.width == 0) {
            throw DataError(name + ": the layers do not match the zone size");
        }
        for (const auto& cell : zone.cells) {
            for (const auto t : cell.layers) {
                if (t != data::kNoTile && t >= d.tiles.size()) {
                    throw DataError(name + ": there is no tile " + std::to_string(t));
                }
            }
        }
        if (*index == d.zones.size()) {
            d.zones.push_back(std::move(zone));
        } else {
            d.zones[*index] = std::move(zone);
        }
    }

    // Scripts as text replace a zone's scripts, also those of its zone file.
    for (const auto& [name, bytes] : files) {
        const auto index = numberedFile(name, "scripts/", ".txt");
        if (!index) {
            continue;
        }
        if (*index >= d.zones.size()) {
            throw DataError(name + ": there is no zone " + std::to_string(*index));
        }
        try {
            d.zones[*index].actions = script::readScriptText(toString(bytes), d.layout());
        } catch (const std::exception& e) {
            throw DataError(name + ": " + e.what());
        }
    }

    // Sounds replace the game's sound of the same file name, or are added
    // at the end of the sound list.
    for (const auto& [name, bytes] : files) {
        if (name.compare(0, 7, "sounds/") != 0 || name.size() <= 7 || name.find('/', 7) != std::string::npos) {
            continue;
        }
        const std::string file = detail::lower(name.substr(7));
        const bool known = std::any_of(d.sounds.begin(), d.sounds.end(),
                                       [&](const std::string& s) { return detail::soundFileName(s) == file; });
        if (!known) {
            d.sounds.push_back(text::utf8ToWindows1252(file));
        }
        pack.sounds["sounds/" + file] = bytes;
    }

    if (const auto it = files.find("setup.png"); it != files.end()) {
        d.setupScreen = imagePixels(it->second, pack.palette, data::kSetupScreenSize, data::kSetupScreenSize,
                                    "setup.png");
    }

    if (files.count("puzzles.json") != 0) {
        try {
            mergeEntries(d.puzzles, detail::decodeTaggedEntries(parseFile(files, "puzzles.json")), "puzzles.json");
        } catch (const json::Error& e) {
            throw DataError(std::string("puzzles.json: ") + e.what());
        }
    }
    if (files.count("characters.json") != 0) {
        try {
            const Value v = parseFile(files, "characters.json");
            if (const auto* c = v.find("characters")) {
                mergeEntries(d.characters, detail::decodeTaggedEntries(*c), "characters.json");
            }
            if (const auto* w = v.find("weapons")) {
                mergeValues(d.characterWeapons, detail::decodeCharacterValues(*w, true));
            }
            if (const auto* a = v.find("aux")) {
                mergeValues(d.characterAux, detail::decodeCharacterValues(*a, false));
            }
        } catch (const json::Error& e) {
            throw DataError(std::string("characters.json: ") + e.what());
        }
    }
}

Mod makeMod(const Pack& original, const Pack& changed, const ModInfo& info) {
    const data::GameData& a = original.data;
    const data::GameData& b = changed.data;
    if (original.game != changed.game) {
        throw DataError("the packs are of different games");
    }
    if (b.tiles.size() < a.tiles.size() || b.zones.size() < a.zones.size()) {
        throw DataError("a mod cannot remove zones or tiles");
    }
    Mod mod;
    mod.info = info;
    mod.info.game = changed.game;
    Files& files = mod.files;

    Array tileChanges;
    const auto namesA = namesByTile(a.tileNames);
    const auto namesB = namesByTile(b.tileNames);
    for (std::size_t i = 0; i < b.tiles.size(); ++i) {
        const bool added = i >= a.tiles.size();
        if (added || a.tiles[i].pixels != b.tiles[i].pixels) {
            files[tileFileName(i)] = indexedPng(b.tiles[i].pixels.data(), kTileSize, changed.palette);
        }
        const auto tile = static_cast<std::uint16_t>(i);
        const auto nameA = namesA.find(tile);
        const auto nameB = namesB.find(tile);
        const std::string before = nameA != namesA.end() ? nameA->second : std::string();
        const std::string after = nameB != namesB.end() ? nameB->second : std::string();
        const bool flagsChanged = added || a.tiles[i].flags != b.tiles[i].flags;
        if (flagsChanged || before != after) {
            Object o{{"tile", static_cast<int>(i)}};
            if (flagsChanged) {
                o.emplace_back("flags", static_cast<std::int64_t>(b.tiles[i].flags));
            }
            if (before != after) {
                o.emplace_back("name", text::windows1252ToUtf8(after));
            }
            tileChanges.emplace_back(std::move(o));
        }
    }
    if (!tileChanges.empty()) {
        files["tiles.json"] = toBytes(json::write(Object{{"tiles", std::move(tileChanges)}}));
    }

    // A zone file for zones whose grid, hotspots or lists changed, and the
    // scripts as text for zones whose scripts changed.
    for (std::size_t i = 0; i < b.zones.size(); ++i) {
        if (i >= a.zones.size()) {
            files[detail::zoneFileName(i)] = toBytes(json::write(detail::encodeZone(b.zones[i], i)));
            continue;
        }
        data::Zone rest = b.zones[i];
        rest.actions = a.zones[i].actions;
        if (json::write(detail::encodeZone(rest, i)) != json::write(detail::encodeZone(a.zones[i], i))) {
            files[detail::zoneFileName(i)] = toBytes(json::write(detail::encodeZone(b.zones[i], i)));
        }
        if (!sameScripts(a.zones[i].actions, b.zones[i].actions, b.layout())) {
            files[scriptFileName(i)] =
                toBytes(script::writeScriptText(b.zones[i].actions.value_or(std::vector<data::Action>{}), b.layout()));
        }
    }

    for (const auto& [name, bytes] : changed.sounds) {
        const auto it = original.sounds.find(name);
        if (it == original.sounds.end() || it->second != bytes) {
            files[name] = bytes;
        }
    }

    if (b.setupScreen != a.setupScreen && b.setupScreen.size() == data::kSetupScreenSize * data::kSetupScreenSize) {
        png::IndexedImage image;
        image.width = data::kSetupScreenSize;
        image.height = data::kSetupScreenSize;
        image.palette = changed.palette;
        image.pixels = b.setupScreen;
        files["setup.png"] = png::write(image);
    }

    const auto changedEntries = [](const std::vector<data::TaggedEntry>& before,
                                   const std::vector<data::TaggedEntry>& after) {
        std::vector<data::TaggedEntry> out;
        for (const auto& e : after) {
            const auto it = std::find_if(before.begin(), before.end(), [&](const auto& x) { return x.id == e.id; });
            if (it == before.end() || it->body != e.body) {
                out.push_back(e);
            }
        }
        return out;
    };
    const auto changedValues = [](const std::vector<data::CharacterValues>& before,
                                  const std::vector<data::CharacterValues>& after) {
        std::vector<data::CharacterValues> out;
        for (const auto& c : after) {
            if (std::none_of(before.begin(), before.end(), [&](const auto& x) { return sameValues(x, c); })) {
                out.push_back(c);
            }
        }
        return out;
    };
    if (const auto puzzles = changedEntries(a.puzzles, b.puzzles); !puzzles.empty()) {
        files["puzzles.json"] = toBytes(json::write(detail::encodeTaggedEntries(puzzles)));
    }
    const auto characters = changedEntries(a.characters, b.characters);
    const auto weapons = changedValues(a.characterWeapons, b.characterWeapons);
    const auto aux = changedValues(a.characterAux, b.characterAux);
    if (!characters.empty() || !weapons.empty() || !aux.empty()) {
        files["characters.json"] = toBytes(json::write(Object{
            {"characters", detail::encodeTaggedEntries(characters)},
            {"weapons", detail::encodeCharacterValues(weapons, true)},
            {"aux", detail::encodeCharacterValues(aux, false)},
        }));
    }

    files[kModManifestName] = writeModInfo(mod.info);
    return mod;
}

void writeMod(Mod& mod, const std::filesystem::path& folder) {
    // Only ever replace a mod: never a folder that holds something else.
    if (std::filesystem::exists(folder)) {
        if (!std::filesystem::exists(folder / kModManifestName)) {
            throw DataError(folder.string() + " is not a mod folder");
        }
        const std::filesystem::path old = folder.string() + ".old";
        std::filesystem::remove_all(old);
        std::filesystem::path fresh = folder.string() + ".new";
        std::filesystem::remove_all(fresh);
        writeFolder(mod.files, fresh);
        std::filesystem::rename(folder, old);
        std::filesystem::rename(fresh, folder);
        std::filesystem::remove_all(old);
    } else {
        writeFolder(mod.files, folder);
    }
    mod.location = folder;
}

} // namespace das::pack
