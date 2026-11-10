// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's Tiles tab and the tile browser that the Zones tab uses too.
#include "editor.hpp"

#include "das/data/tile_flags.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace das::app {

namespace {

constexpr float kPad = 6;

float F(int v) { return static_cast<float>(v); }

struct Kind {
    const char* label;
    std::uint32_t flags; // 0: every tile
    const char* help;
};

constexpr Kind kKinds[] = {
    {"All", 0, "Show every tile."},
    {"Floor", data::tile_flags::kFloor, "Show floor tiles: ground the hero walks on."},
    {"Objects", data::tile_flags::kObject, "Show objects: walls, rocks, furniture. They block the way."},
    {"Roof", data::tile_flags::kRoof, "Show roof tiles, which are drawn over the hero."},
    {"Items", data::tile_flags::kItem, "Show items the hero can carry."},
    {"Weapons", data::tile_flags::kWeapon, "Show weapons."},
    {"People", data::tile_flags::kCharacter, "Show the pictures of people and creatures."},
    {"Map", data::tile_flags::kMap, "Show the tiles of the world map."},
};

struct Flag {
    std::uint32_t bit;
    const char* label;
    const char* help;
};

constexpr Flag kFlags[] = {
    {data::tile_flags::kTransparent, "See-through", "Colour 0 of the tile is see-through, so the layer below shows."},
    {data::tile_flags::kFloor, "Floor", "A floor tile: ground the hero can walk on."},
    {data::tile_flags::kObject, "Blocks the way", "An object the hero cannot walk through, such as a wall."},
    {data::tile_flags::kDraggable, "Can be pushed", "The hero can push this object to the next cell."},
    {data::tile_flags::kRoof, "Roof", "Drawn over the hero, such as tree tops."},
    {data::tile_flags::kMap, "World map", "A tile of the world map (shown by the locator)."},
    {data::tile_flags::kWeapon, "Weapon", "A weapon the hero can carry and use."},
    {data::tile_flags::kCharacter, "Person", "A picture of a person or creature."},
    {data::tile_flags::kItem, "Item", "An item the hero can pick up and carry."},
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string kindOf(std::uint32_t flags) {
    std::string out;
    for (const auto& f : kFlags) {
        if (f.bit != data::tile_flags::kTransparent && (flags & f.bit) != 0) {
            out += (out.empty() ? "" : ", ") + lower(f.label);
        }
    }
    return out.empty() ? "no kind" : out;
}

} // namespace

std::optional<std::uint16_t> Editor::tileBrowser(const SDL_FRect& r, std::string_view id, float cell) {
    const auto& tiles = host_.data->tiles;
    const float x = r.x + kPad;
    const float w = r.w - 2 * kPad;
    float y = r.y;
    // Which tiles: a kind and a search.
    int kind = tileFilter_;
    if (gui_.stepper(std::string(id) + "-kind", {x, y, w, gui::kRow}, "Show", kind, 0,
                     static_cast<int>(std::size(kKinds)) - 1, kKinds[static_cast<std::size_t>(tileFilter_)].label,
                     kKinds[static_cast<std::size_t>(tileFilter_)].help)) {
        tileFilter_ = kind;
    }
    y += gui::kRow + 3;
    gui_.textField(std::string(id) + "-search", {x, y, w, gui::kRow}, tileSearch_,
                   "Find tiles by name (such as \"key\") or number.", 40, "Search tiles...");
    y += gui::kRow + 4;

    const std::uint32_t wanted = kKinds[static_cast<std::size_t>(tileFilter_)].flags;
    const std::string query = lower(tileSearch_);
    std::vector<std::uint16_t> shown;
    for (std::size_t t = 0; t < tiles.size(); ++t) {
        if (wanted != 0 && (tiles[t].flags & wanted) == 0) {
            continue;
        }
        if (!query.empty()) {
            const std::string text = std::to_string(t) + " " + lower(tileName(static_cast<std::uint16_t>(t)));
            if (text.find(query) == std::string::npos) {
                continue;
            }
        }
        shown.push_back(static_cast<std::uint16_t>(t));
    }

    const SDL_FRect list{r.x, y, r.w, r.y + r.h - y - kPad};
    const int columns = std::max(1, static_cast<int>((w - 4) / (cell + 2)));
    const float step = cell + 2;
    const int rows = (static_cast<int>(shown.size()) + columns - 1) / columns;
    float& scroll = id == "zone-tiles" ? tileScroll_ : tileScroll2_;
    gui_.beginScroll(list, scroll, static_cast<float>(rows) * step + 4);
    std::optional<std::uint16_t> picked;
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const std::uint16_t t = shown[i];
        const SDL_FRect c{x + static_cast<float>(static_cast<int>(i) % columns) * step,
                          list.y + 2 + static_cast<float>(static_cast<int>(i) / columns) * step - scroll, cell, cell};
        if (!gui_.visible(c)) {
            continue;
        }
        ui::fill(renderer_, c, ui::Color{40, 44, 56});
        drawTile(t, c);
        if (t == tile_) {
            ui::outline(renderer_, {c.x - 1, c.y - 1, c.w + 2, c.h + 2}, ui::color::accent);
        }
        if (gui_.hovered(c)) {
            ui::outline(renderer_, c, ui::color::text);
            const std::string name = tileName(t);
            gui_.setHelp("Tile " + std::to_string(t) + (name.empty() ? "" : ": " + name) + " (" +
                         kindOf(tiles[t].flags) + "). Click to take it in hand.");
            if (gui_.input().mousePressed) {
                picked = t;
            }
        }
    }
    gui_.endScroll();
    if (shown.empty()) {
        ui::text(renderer_, x, list.y + 4, "No tile fits", ui::color::faint);
    }
    return picked;
}

void Editor::tilesTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right) {
    ui::fill(renderer_, left, ui::color::panel);
    ui::fill(renderer_, {left.x + left.w - 1, left.y, 1, left.h}, ui::color::border);
    ui::text(renderer_, left.x + kPad, left.y + kPad, "Tiles", ui::color::accent);
    if (const auto picked = tileBrowser({left.x, left.y + kPad + 12, left.w - 1, left.h - kPad - 12}, "tiles", 40)) {
        tile_ = *picked;
    }

    // The tile, large, and where it is used.
    const auto& tiles = host_.data->tiles;
    const float size = std::floor(std::min(center.w - 2 * kPad, center.h * 0.6f) / 32) * 32;
    const SDL_FRect big{std::floor(center.x + (center.w - size) / 2), center.y + kPad + 8, size, size};
    // A checkerboard shows what is see-through.
    const float check = size / 8;
    for (int cy = 0; cy < 8; ++cy) {
        for (int cx = 0; cx < 8; ++cx) {
            ui::fill(renderer_, {big.x + F(cx) * check, big.y + F(cy) * check, check, check},
                     (cx + cy) % 2 ? ui::Color{60, 64, 76} : ui::Color{44, 48, 58});
        }
    }
    drawTile(tile_, big);
    ui::outline(renderer_, {big.x - 1, big.y - 1, big.w + 2, big.h + 2}, ui::color::border);
    float y = big.y + big.h + kPad;
    const float x = center.x + kPad * 2;
    const float w = center.w - 4 * kPad;

    // Zones that use the tile, recounted when the tile or the data changes.
    if (usersTile_ != tile_ || usersStamp_ != changes_) {
        usersTile_ = tile_;
        usersStamp_ = changes_;
        users_.clear();
        const auto& zones = host_.data->zones;
        for (std::size_t z = 0; z < zones.size(); ++z) {
            for (const auto& c : zones[z].cells) {
                if (std::find(c.layers.begin(), c.layers.end(), tile_) != c.layers.end()) {
                    users_.push_back(static_cast<int>(z));
                    break;
                }
            }
        }
    }
    const auto& users = users_;
    ui::text(renderer_, x, y,
             users.empty() ? "Not used in any zone"
                           : "Used in " + std::to_string(users.size()) + (users.size() == 1 ? " zone:" : " zones:"),
             ui::color::dim);
    y += 12;
    float bx = x;
    for (const int z : users) {
        const std::string label = std::to_string(z);
        const float bw = ui::textWidth(label) + 8;
        if (bx + bw > x + w) {
            bx = x;
            y += gui::kRow + 2;
        }
        if (y + gui::kRow > center.y + center.h - kPad) {
            break;
        }
        if (gui_.button("used-" + label, {bx, y, bw, gui::kRow}, label,
                        "Open zone " + label + " (" + names::zoneType(host_.data->zones[static_cast<std::size_t>(z)].type) +
                            ") on the Zones tab.")) {
            zone_ = z;
            tab_ = Tab::Zones;
        }
        bx += bw + 2;
    }
    (void)tiles;
    tileInspector(right);
}

void Editor::tileInspector(const SDL_FRect& r) {
    ui::fill(renderer_, r, ui::color::panel);
    ui::fill(renderer_, {r.x, r.y, 1, r.h}, ui::color::border);
    auto& tile = host_.data->tiles[tile_];
    const float x = r.x + kPad;
    const float w = r.w - 2 * kPad;
    float y = r.y + kPad;
    ui::text(renderer_, x, y, "Tile " + std::to_string(tile_), ui::color::accent);
    y += 14;

    ui::text(renderer_, x, y, "Name", ui::color::dim);
    y += 10;
    std::string name = tileName(tile_);
    const std::string before = name;
    if (gui_.textField("tile-name", {x, y, w, gui::kRow}, name,
                       "The name shown for items and weapons, such as in the inventory. Leave it empty for no name.",
                       60, "No name")) {
        remember(-1, tile_);
        setTileName(tile_, name);
        // Typing changes the name letter by letter: one Undo step for it.
        if (undo_.size() >= 2) {
            const auto& last = undo_[undo_.size() - 1];
            const auto& prev = undo_[undo_.size() - 2];
            if (last.tile == prev.tile && last.tileData && prev.tileData && !last.zoneData && !prev.zoneData &&
                last.tileData->flags == prev.tileData->flags && last.tileData->pixels == prev.tileData->pixels) {
                undo_.pop_back();
            }
        }
    }
    (void)before;
    y += gui::kRow + kPad + 2;

    ui::text(renderer_, x, y, "What the tile is", ui::color::accent);
    y += 12;
    for (const auto& f : kFlags) {
        bool on = (tile.flags & f.bit) != 0;
        if (gui_.checkbox(std::string("flag-") + f.label, {x, y, w, gui::kRow}, f.label, on, f.help)) {
            remember(-1, tile_);
            host_.data->tiles[tile_].flags ^= f.bit; // `tile` may have moved: Undo copies
        }
        y += gui::kRow + 1;
    }
    const std::uint32_t known = [] {
        std::uint32_t all = 0;
        for (const auto& f : kFlags) {
            all |= f.bit;
        }
        return all;
    }();
    const std::uint32_t other = host_.data->tiles[tile_].flags & ~known;
    if (other != 0) {
        char line[64];
        std::snprintf(line, sizeof line, "Other bits: 0x%x", static_cast<unsigned>(other));
        ui::text(renderer_, x, y + 2, line, ui::color::faint);
        if (gui_.hovered({x, y, w, gui::kRow})) {
            gui_.setHelp("Bits whose meaning we do not know yet. They are kept as they are.");
        }
        y += gui::kRow;
    }
    y += kPad;
    if (gui_.button("add-tile", {x, y, w, gui::kRow}, "Add a copy as a new tile",
                    "Add a new tile at the end, a copy of this one. Draw it in any paint program as a 32 x 32 PNG "
                    "and put it in the mod's tiles folder (see docs/formats/mod.md).")) {
        addTile();
    }
}

void Editor::addTile() {
    remember(-1, -1, true);
    auto& tiles = host_.data->tiles;
    tiles.push_back(tiles[tile_]);
    tile_ = static_cast<std::uint16_t>(tiles.size() - 1);
    tileScroll2_ = 1e9f;
    if (host_.tilesChanged) {
        host_.tilesChanged();
    }
    notice_ = "Tile " + std::to_string(tile_) + " added.";
    noticeAt_ = SDL_GetTicks();
}

} // namespace das::app
