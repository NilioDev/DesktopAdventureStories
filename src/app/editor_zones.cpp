// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's Zones tab: the list of zones, the zone to draw on, and the
// tools, layers, zone settings and tiles beside it.
#include "editor.hpp"

#include "das/data/character.hpp"
#include "das/data/tile_flags.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace das::app {

namespace {

constexpr float kPad = 6;

float F(int v) { return static_cast<float>(v); }
constexpr std::uint16_t kNoArgument = 0xffff;
constexpr std::uint32_t kZoneTypes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18};
constexpr int kPlanets[] = {0, 1, 2, 3, 5};

const char* layerName(int layer) {
    switch (layer) {
    case 0: return "Floor";
    case 1: return "Objects";
    case 2: return "Roof";
    case 3: return "Hotspots";
    default: return "People";
    }
}

const char* layerHelp(int layer) {
    switch (layer) {
    case 0: return "Floor: the ground, drawn first. The hero walks on it.";
    case 1: return "Objects: walls, furniture, items and people. Most of them block the way.";
    case 2: return "Roof: drawn over the hero, such as tree tops and the tops of walls.";
    case 3: return "Hotspots: places where something happens, such as doors and spots for items.";
    default: return "People: monsters and people who are in the zone from the start. Most of them fight.";
    }
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

struct Grid {
    float cell;
    float x;
    float y;
};

Grid gridOf(const data::Zone& zone, const SDL_FRect& r) {
    const float cell = std::floor(std::min(r.w / zone.width, r.h / zone.height));
    return {cell, std::floor(r.x + (r.w - cell * zone.width) / 2), std::floor(r.y + (r.h - cell * zone.height) / 2)};
}

void section(SDL_Renderer* renderer, float x, float y, std::string_view title) {
    ui::text(renderer, x, y, title, ui::color::accent);
}

} // namespace

void Editor::zonesTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right) {
    zoneList(left);
    zoneCanvas(center);
    zoneInspector(right);
}

// --- the list of zones -----------------------------------------------------------

void Editor::zoneList(const SDL_FRect& r) {
    ui::fill(renderer_, r, ui::color::panel);
    ui::fill(renderer_, {r.x + r.w - 1, r.y, 1, r.h}, ui::color::border);
    const float x = r.x + kPad;
    const float w = r.w - 2 * kPad;
    float y = r.y + kPad;
    section(renderer_, x, y, "Zones");
    y += 12;
    gui_.textField("zone-search", {x, y, w, gui::kRow}, zoneSearch_,
                   "Find zones by number, kind (such as town or trade) or planet.", 40, "Search...");
    y += gui::kRow + kPad;

    const auto& zones = host_.data->zones;
    if (examplesOpen_) {
        zoneExamples({r.x, y, r.w, r.y + r.h - y});
        return;
    }
    const std::string query = lower(zoneSearch_);
    std::vector<int> shown;
    for (int i = 0; i < static_cast<int>(zones.size()); ++i) {
        const auto& z = zones[static_cast<std::size_t>(i)];
        if (!query.empty()) {
            const std::string text = std::to_string(i) + " " + lower(names::zoneType(z.type)) + " " +
                                     lower(names::planet(z.planet)) + " " + std::to_string(z.width) + "x" +
                                     std::to_string(z.height);
            if (text.find(query) == std::string::npos) {
                continue;
            }
        }
        shown.push_back(i);
    }

    const float buttonsH = 3 * gui::kRow + kPad + 4;
    const SDL_FRect list{r.x, y, r.w - 1, r.y + r.h - y - buttonsH - kPad};
    const float rowH = gui::kRow + 2;
    gui_.beginScroll(list, zoneScroll_, static_cast<float>(shown.size()) * rowH);
    float ry = list.y - zoneScroll_;
    for (const int i : shown) {
        const SDL_FRect row{x, ry, w - 4, gui::kRow};
        ry += rowH;
        const auto& z = zones[static_cast<std::size_t>(i)];
        char label[80];
        std::snprintf(label, sizeof label, "%-3d %s", i, names::zoneType(z.type).c_str());
        const std::string id = "zone-" + std::to_string(i);
        if (!gui_.visible(row)) {
            continue;
        }
        const std::string help = "Zone " + std::to_string(i) + ": " + names::zoneType(z.type) + ", " +
                                 names::planet(z.planet) + ", " + std::to_string(z.width) + " x " +
                                 std::to_string(z.height) + " tiles. " + names::zoneTypeHelp(z.type);
        if (gui_.button(id, row, "", help, i == zone_)) {
            zone_ = i;
            fillFrom_.reset();
            cursorX_ = std::min(cursorX_, z.width - 1);
            cursorY_ = std::min(cursorY_, z.height - 1);
        }
        const std::string size = std::to_string(z.width);
        std::string rowText = label;
        const float room = row.w - 12 - ui::textWidth(size);
        while (rowText.size() > 1 && ui::textWidth(rowText) > room) {
            rowText.pop_back();
        }
        ui::text(renderer_, row.x + 4, row.y + 3, rowText, i == zone_ ? ui::color::accent : ui::color::text);
        ui::textRight(renderer_, row.x + row.w - 4, row.y + 3, size, ui::color::faint);
    }
    gui_.endScroll();
    if (shown.empty()) {
        ui::text(renderer_, x, list.y + 4, "No zone fits", ui::color::faint);
    }

    float by = r.y + r.h - buttonsH;
    if (gui_.button("new-zone", {x, by, w, gui::kRow}, "New zone",
                    "Add an empty zone of the same size, kind and planet, with a floor of the tile in hand.")) {
        newZone(false);
    }
    by += gui::kRow + 2;
    if (gui_.button("copy-zone", {x, by, w, gui::kRow}, "Copy this zone",
                    "Add a copy of this zone, to make a variant of it.")) {
        newZone(true);
    }
    by += gui::kRow + 2;
    if (gui_.button("example-zone", {x, by, w, gui::kRow}, "New from an example",
                    "Start a new zone from a working example of the game: a room, a trade, an item to find...")) {
        examplesOpen_ = true;
    }
}

void Editor::zoneExamples(const SDL_FRect& r) {
    const float x = r.x + kPad;
    const float w = r.w - 2 * kPad;
    float y = r.y;
    y += gui_.paragraph(x, y, w,
                        "A new zone that starts as a copy of one of the game's own, with its hotspots, people and "
                        "scripts. Change it from there.",
                        ui::color::dim, 6) +
         kPad;
    const auto& zones = host_.data->zones;
    const int planet = zones[static_cast<std::size_t>(zone_)].planet;
    static constexpr std::uint32_t kinds[] = {1, 8, 2, 15, 16, 17, 18, 10};
    for (const auto kind : kinds) {
        // The first zone of that kind, on this planet when there is one.
        int example = -1;
        for (int i = 0; i < static_cast<int>(zones.size()); ++i) {
            const auto& z = zones[static_cast<std::size_t>(i)];
            if (z.type == kind && (example < 0 || (z.planet == planet &&
                                                   zones[static_cast<std::size_t>(example)].planet != planet))) {
                example = i;
            }
        }
        if (example < 0) {
            continue;
        }
        if (gui_.button("example-" + std::to_string(kind), {x, y, w, gui::kRow}, names::zoneType(kind),
                        names::zoneTypeHelp(kind) + " The example is zone " + std::to_string(example) + ".")) {
            zone_ = example;
            newZone(true);
            examplesOpen_ = false;
        }
        y += gui::kRow + 2;
    }
    y += kPad;
    if (gui_.button("example-cancel", {x, y, w, gui::kRow}, "Cancel", "Back to the list of zones.")) {
        examplesOpen_ = false;
    }
}

void Editor::newZone(bool copy) {
    remember(-1, -1, true);
    auto& zones = host_.data->zones;
    const data::Zone like = zones[static_cast<std::size_t>(zone_)];
    data::Zone zone;
    if (copy) {
        zone = like;
    } else {
        zone.width = like.width;
        zone.height = like.height;
        zone.type = like.type;
        zone.planet = like.planet;
        zone.izonField = like.izonField;
        zone.izonPlanet = like.izonPlanet;
        zone.cells.resize(static_cast<std::size_t>(zone.width) * zone.height);
        for (auto& c : zone.cells) {
            c.layers[0] = tile_;
        }
        zone.hotspots.emplace();
        zone.actions.emplace();
    }
    zone.index = static_cast<std::uint16_t>(zones.size());
    zones.push_back(std::move(zone));
    zone_ = static_cast<int>(zones.size()) - 1;
    zoneSearch_.clear();
    zoneScroll_ = 1e9f; // the end of the list
    notice_ = "Zone " + std::to_string(zone_) + " added.";
    noticeAt_ = SDL_GetTicks();
}

void Editor::resizeZone(int width, int height) {
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    if (z.width == width && z.height == height) {
        return;
    }
    remember(zone_, -1);
    std::vector<data::ZoneCell> cells(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto& c = cells[static_cast<std::size_t>(y * width + x)];
            if (x < z.width && y < z.height) {
                c = z.at(x, y);
            } else {
                c.layers[0] = tile_;
            }
        }
    }
    z.cells = std::move(cells);
    z.width = static_cast<std::uint16_t>(width);
    z.height = static_cast<std::uint16_t>(height);
    if (z.hotspots) {
        std::erase_if(*z.hotspots, [&](const data::Hotspot& h) { return h.x >= width || h.y >= height; });
    }
    std::erase_if(z.monsters, [&](const data::ZoneMonster& m) { return m.x >= width || m.y >= height; });
    cursorX_ = std::min(cursorX_, width - 1);
    cursorY_ = std::min(cursorY_, height - 1);
}

// --- drawing on the zone -----------------------------------------------------------

void Editor::applyTool(int x, int y, bool startOfStroke) {
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    if (x < 0 || y < 0 || x >= z.width || y >= z.height) {
        return;
    }
    cursorX_ = x;
    cursorY_ = y;
    if (tool_ == Tool::Fill) {
        if (startOfStroke) {
            fillFrom_ = std::make_pair(x, y);
        }
        return;
    }
    if (layer_ < 3) {
        auto& slot = z.cells[static_cast<std::size_t>(y * z.width + x)].layers[static_cast<std::size_t>(layer_)];
        if (tool_ == Tool::Pick) {
            if (slot != data::kNoTile) {
                tile_ = slot;
            }
            return;
        }
        const std::uint16_t value = tool_ == Tool::Erase ? data::kNoTile : tile_;
        if (slot == value) {
            return;
        }
        if (startOfStroke || undo_.empty()) {
            remember(zone_, -1);
        }
        z.cells[static_cast<std::size_t>(y * z.width + x)].layers[static_cast<std::size_t>(layer_)] = value;
        dirty_ = true;
        ++changes_;
        return;
    }
    if (!startOfStroke) {
        return;
    }
    if (layer_ == 4) {
        // People: one on a cell.
        auto& people = z.monsters;
        const auto who = std::find_if(people.begin(), people.end(),
                                      [&](const data::ZoneMonster& m) { return m.x == x && m.y == y; });
        if (tool_ == Tool::Pick) {
            if (who != people.end()) {
                character_ = who->character;
            }
            return;
        }
        if (tool_ == Tool::Erase) {
            if (who != people.end()) {
                remember(zone_, -1);
                people.erase(who);
            }
            return;
        }
        if (who != people.end() || character_ < 0) {
            return; // clicking someone selects them; the panel changes them
        }
        remember(zone_, -1);
        data::ZoneMonster m;
        m.character = static_cast<std::uint16_t>(character_);
        m.x = static_cast<std::uint16_t>(x);
        m.y = static_cast<std::uint16_t>(y);
        m.waypoints.fill(0xffff);
        people.push_back(m);
        return;
    }
    // Hotspots.
    if (!z.hotspots) {
        z.hotspots.emplace();
    }
    auto& spots = *z.hotspots;
    const auto here = std::find_if(spots.begin(), spots.end(), [&](const data::Hotspot& h) { return h.x == x && h.y == y; });
    if (tool_ == Tool::Pick) {
        if (here != spots.end()) {
            spotType_ = here->type;
        }
        return;
    }
    if (tool_ == Tool::Erase) {
        if (here != spots.end()) {
            remember(zone_, -1);
            spots.erase(here);
        }
        return;
    }
    if (here != spots.end()) {
        // Clicking a hotspot selects it; the panel on the right changes it.
        return;
    }
    remember(zone_, -1);
    data::Hotspot h;
    h.type = spotType_;
    h.x = static_cast<std::uint16_t>(x);
    h.y = static_cast<std::uint16_t>(y);
    h.enabled = 1;
    h.argument = kNoArgument;
    spots.push_back(h);
}

void Editor::fillRect(int x0, int y0, int x1, int y1) {
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    if (layer_ >= 3) {
        return;
    }
    remember(zone_, -1);
    const std::uint16_t value = tile_;
    for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y) {
        for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) {
            if (x >= 0 && y >= 0 && x < z.width && y < z.height) {
                z.cells[static_cast<std::size_t>(y * z.width + x)].layers[static_cast<std::size_t>(layer_)] = value;
            }
        }
    }
}

void Editor::zoneCanvas(const SDL_FRect& r) {
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    const SDL_FRect area{r.x + kPad, r.y + kPad, r.w - 2 * kPad, r.h - 2 * kPad};
    const Grid g = gridOf(z, area);
    const SDL_FRect zoneRect{g.x, g.y, g.cell * z.width, g.cell * z.height};
    const auto& in = gui_.input();

    // Layers, the ones above the layer being drawn on see-through.
    for (int layer = 0; layer < 3; ++layer) {
        if (!shown_[static_cast<std::size_t>(layer)]) {
            continue;
        }
        const Uint8 alpha = layer_ < 3 && layer > layer_ ? 110 : 255;
        for (int cy = 0; cy < z.height; ++cy) {
            for (int cx = 0; cx < z.width; ++cx) {
                drawTile(z.at(cx, cy).layers[static_cast<std::size_t>(layer)],
                         {g.x + static_cast<float>(cx) * g.cell, g.y + static_cast<float>(cy) * g.cell, g.cell, g.cell},
                         alpha);
            }
        }
    }
    // People, standing.
    if (shown_[4]) {
        for (const auto& m : z.monsters) {
            if (m.character < characters_.size()) {
                const SDL_FRect c{g.x + F(m.x) * g.cell, g.y + F(m.y) * g.cell, g.cell, g.cell};
                drawTile(characters_[m.character].frame(0, data::Facing::Down), c,
                         layer_ == 4 ? 255 : (layer_ == 3 ? 120 : 220));
                if (layer_ == 4) {
                    ui::outline(renderer_, {c.x + 1, c.y + 1, c.w - 2, c.h - 2}, ui::color::danger);
                }
            }
        }
    }
    ui::outline(renderer_, {zoneRect.x - 1, zoneRect.y - 1, zoneRect.w + 2, zoneRect.h + 2}, ui::color::border);
    if (shown_[3] && z.hotspots) {
        for (const auto& h : *z.hotspots) {
            const SDL_FRect c{g.x + F(h.x) * g.cell + 1, g.y + F(h.y) * g.cell + 1, g.cell - 2, g.cell - 2};
            const ui::Color col = layer_ == 3 ? ui::color::good : ui::Color{112, 190, 112, 140};
            ui::outline(renderer_, c, col);
            if (g.cell >= 2 * ui::kFont) {
                ui::text(renderer_, c.x + 2, c.y + 2, std::to_string(h.type), col);
            }
        }
    }

    // The cell under the mouse, and what it holds.
    std::optional<std::pair<int, int>> hover;
    if (gui_.hovered(zoneRect)) {
        const int hx = static_cast<int>((in.mouseX - g.x) / g.cell);
        const int hy = static_cast<int>((in.mouseY - g.y) / g.cell);
        if (hx >= 0 && hy >= 0 && hx < z.width && hy < z.height) {
            hover = std::make_pair(hx, hy);
        }
    }
    const bool clicked = gui_.area("canvas", zoneRect,
                                   "Draw on the zone with the tool. Right-click takes the tile under the mouse.");
    if (hover) {
        const auto [hx, hy] = *hover;
        const auto& cell = z.at(hx, hy);
        std::string text = "Cell " + std::to_string(hx) + "," + std::to_string(hy) + ".";
        for (int layer = 0; layer < 3; ++layer) {
            const auto t = cell.layers[static_cast<std::size_t>(layer)];
            if (t != data::kNoTile) {
                const std::string name = tileName(t);
                text += std::string(" ") + layerName(layer) + ": " + std::to_string(t) +
                        (name.empty() ? "" : " (" + name + ")") + ".";
            }
        }
        if (z.hotspots) {
            for (const auto& h : *z.hotspots) {
                if (h.x == hx && h.y == hy) {
                    text += " Hotspot: " + names::hotspot(h.type) +
                            (h.argument != kNoArgument ? " " + std::to_string(h.argument) : "") + ".";
                }
            }
        }
        for (const auto& m : z.monsters) {
            if (m.x == hx && m.y == hy) {
                text += " Person: " + characterName(m.character) + ".";
            }
        }
        gui_.setHelp(text);
        if (clicked && in.mousePressed) {
            applyTool(hx, hy, true);
        } else if (in.mouseDown && !in.mousePressed && (hx != cursorX_ || hy != cursorY_) && !fillFrom_) {
            applyTool(hx, hy, false);
        }
        if (in.rightPressed && layer_ == 4) {
            for (const auto& m : z.monsters) {
                if (m.x == hx && m.y == hy) {
                    character_ = m.character;
                }
            }
        }
        if (in.rightPressed && layer_ < 3) {
            const auto t = cell.layers[static_cast<std::size_t>(layer_)];
            if (t != data::kNoTile) {
                tile_ = t;
            }
        }
        ui::outline(renderer_, {g.x + F(hx) * g.cell, g.y + F(hy) * g.cell, g.cell, g.cell}, ui::color::text);
    }
    if (fillFrom_) {
        const int x1 = hover ? hover->first : cursorX_;
        const int y1 = hover ? hover->second : cursorY_;
        const auto [x0, y0] = *fillFrom_;
        const SDL_FRect box{g.x + F(std::min(x0, x1)) * g.cell, g.y + F(std::min(y0, y1)) * g.cell,
                            F(std::abs(x1 - x0) + 1) * g.cell, F(std::abs(y1 - y0) + 1) * g.cell};
        ui::outline(renderer_, box, ui::color::accent);
        if (in.mouseReleased) {
            fillRect(x0, y0, x1, y1);
            fillFrom_.reset();
        }
    }
    // The keyboard and gamepad cursor.
    if (gui_.focused("canvas")) {
        ui::outline(renderer_, {g.x + F(cursorX_) * g.cell, g.y + F(cursorY_) * g.cell, g.cell, g.cell}, ui::color::accent);
    }
}

float Editor::hotspotPanel(float x, float y, float w) {
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    section(renderer_, x, y, "Hotspot to place");
    y += 12;
    int type = static_cast<int>(spotType_);
    if (gui_.stepper("spot-type", {x, y, w, gui::kRow}, "Kind", type, 0, 15, names::hotspot(spotType_),
                     names::hotspotHelp(spotType_))) {
        spotType_ = static_cast<std::uint32_t>(type);
    }
    y += gui::kRow + kPad;
    y += gui_.paragraph(x, y, w, names::hotspotHelp(spotType_), ui::color::dim, 4) + kPad;

    // The hotspot under the cursor.
    data::Hotspot* spot = nullptr;
    if (z.hotspots) {
        for (auto& h : *z.hotspots) {
            if (h.x == cursorX_ && h.y == cursorY_) {
                spot = &h;
            }
        }
    }
    section(renderer_, x, y, "At " + std::to_string(cursorX_) + "," + std::to_string(cursorY_));
    y += 12;
    if (!spot) {
        y += gui_.paragraph(x, y, w, "No hotspot here. Click a cell with Paint to place one; click a hotspot to change it here.", ui::color::faint);
        return y;
    }
    int spotKind = static_cast<int>(spot->type);
    if (gui_.stepper("spot-kind", {x, y, w, gui::kRow}, "Kind", spotKind, 0, 15, names::hotspot(spot->type),
                     "What this hotspot is. " + names::hotspotHelp(spot->type))) {
        remember(zone_, -1);
        spot->type = static_cast<std::uint32_t>(spotKind);
    }
    y += gui::kRow + 2;
    int argument = spot->argument == kNoArgument ? -1 : spot->argument;
    const std::string shown = argument < 0 ? "None" : std::to_string(argument);
    const int maxArgument = spot->type == 9 || spot->type == 14 || spot->type == 15
                                ? static_cast<int>(host_.data->zones.size()) - 1
                                : static_cast<int>(host_.data->tiles.size()) - 1;
    if (gui_.stepper("spot-argument", {x, y, w, gui::kRow}, "Argument", argument, -1, maxArgument, shown,
                     names::hotspotHelp(spot->type), 1)) {
        remember(zone_, -1);
        spot->argument = argument < 0 ? kNoArgument : static_cast<std::uint16_t>(argument);
    }
    y += gui::kRow + 2;
    bool enabled = spot->enabled != 0;
    if (gui_.checkbox("spot-enabled", {x, y, w, gui::kRow}, "Active at the start", enabled,
                      "Whether the hotspot works from the start. Scripts can switch it on later.")) {
        remember(zone_, -1);
        spot->enabled = enabled ? 1 : 0;
    }
    y += gui::kRow + 2;
    if (spot->type == 6 || spot->type == 8) {
        if (gui_.button("spot-use-hand", {x, y, w, gui::kRow}, "Use the tile in hand",
                        "Make the tile in hand (" + std::to_string(tile_) + ") the argument of this hotspot.",
                        spot->argument == tile_)) {
            remember(zone_, -1);
            spot->argument = tile_;
        }
        y += gui::kRow + 4;
        if (spot->argument != kNoArgument && spot->argument < host_.data->tiles.size()) {
            drawTile(spot->argument, {x, y, 32, 32});
            ui::text(renderer_, x + 38, y + 12, tileName(spot->argument), ui::color::dim);
            y += 36;
        }
    } else if (spot->argument != kNoArgument && (spot->type == 9 || spot->type == 14 || spot->type == 15) &&
               spot->argument < host_.data->zones.size()) {
        const auto& target = host_.data->zones[spot->argument];
        if (gui_.button("goto-zone", {x, y, w, gui::kRow}, "Go to zone " + std::to_string(spot->argument),
                        "Open the zone this hotspot leads to: " + names::zoneType(target.type) + ".")) {
            zone_ = spot->argument;
        }
    }
    return y;
}

float Editor::peoplePanel(float x, float y, float w) {
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    std::vector<int> placeable;
    for (std::size_t i = 0; i < characters_.size(); ++i) {
        if (characters_[i].type != 1 && characters_[i].type != 4) {
            placeable.push_back(static_cast<int>(i));
        }
    }
    const auto describe = [&](int character) {
        if (character < 0 || static_cast<std::size_t>(character) >= characters_.size()) {
            return std::string("A character this game does not have.");
        }
        const auto& c = characters_[static_cast<std::size_t>(character)];
        return characterName(character) + " is " + names::characterKind(c.type) + " and " +
               names::movement(c.movement) + ".";
    };
    // A stepper over the characters that can be placed; returns true on a change.
    const auto chooser = [&](std::string_view id, float cy, int& character, std::string_view help) {
        auto it = std::find(placeable.begin(), placeable.end(), character);
        int index = it == placeable.end() ? 0 : static_cast<int>(it - placeable.begin());
        const int before = index;
        const std::string shown = it == placeable.end() ? characterName(character) : characterName(placeable[static_cast<std::size_t>(index)]);
        gui_.stepper(id, {x, cy, w, gui::kRow}, "Who", index, 0, static_cast<int>(placeable.size()) - 1, shown, help);
        if (index != before) {
            character = placeable[static_cast<std::size_t>(index)];
            return true;
        }
        return false;
    };

    section(renderer_, x, y, "Person to place");
    y += 12;
    if (placeable.empty()) {
        return y + gui_.paragraph(x, y, w, "This game has no characters to place.", ui::color::faint);
    }
    chooser("person-type", y, character_, "The character that Paint puts on the zone. Left and right go through all of them.");
    y += gui::kRow + 4;
    if (character_ >= 0 && static_cast<std::size_t>(character_) < characters_.size()) {
        drawTile(characters_[static_cast<std::size_t>(character_)].frame(0, data::Facing::Down), {x, y, 32, 32});
        y += std::max(36.0f, gui_.paragraph(x + 38, y, w - 38, describe(character_), ui::color::dim, 5));
    }
    y += kPad;

    section(renderer_, x, y, "At " + std::to_string(cursorX_) + "," + std::to_string(cursorY_));
    y += 12;
    const auto who = std::find_if(z.monsters.begin(), z.monsters.end(),
                                  [&](const data::ZoneMonster& m) { return m.x == cursorX_ && m.y == cursorY_; });
    if (who == z.monsters.end()) {
        return y + gui_.paragraph(x, y, w,
                                  "Nobody here. Click a cell with Paint to place the person above; click someone "
                                  "to change them here.",
                                  ui::color::faint);
    }
    int character = who->character;
    if (chooser("person-here", y, character, "Who stands here. Left and right change it.")) {
        remember(zone_, -1);
        who->character = static_cast<std::uint16_t>(character);
    }
    y += gui::kRow + 4;
    y += gui_.paragraph(x, y, w, describe(who->character), ui::color::dim, 4) + 4;
    if (gui_.button("person-remove", {x, y, w, gui::kRow}, "Remove", "Take this person out of the zone.")) {
        remember(zone_, -1);
        z.monsters.erase(who);
    }
    return y + gui::kRow + 2;
}

float Editor::tileList(std::string_view id, float x, float y, float w, std::string_view title, std::string_view help,
                       std::vector<std::uint16_t>& list, bool people) {
    const float top = y;
    section(renderer_, x, y, title);
    y += 12;
    y += gui_.paragraph(x, y, w, help, ui::color::dim, 4) + 2;
    const float icon = 20;
    float ix = x;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (ix + icon > x + w) {
            ix = x;
            y += icon + 2;
        }
        const SDL_FRect cell{ix, y, icon, icon};
        const std::string name = tileName(list[i]);
        const std::string label = std::to_string(list[i]) + (name.empty() ? "" : " (" + name + ")");
        ui::fill(renderer_, cell, ui::color::background);
        drawTile(list[i], cell);
        if (gui_.area(std::string(id) + "-" + std::to_string(i), cell, label + ". Click to take it off this list.")) {
            remember(zone_, -1);
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
        if (gui_.hovered(cell)) {
            ui::outline(renderer_, cell, ui::color::danger);
        }
        ix += icon + 2;
    }
    if (list.empty()) {
        ui::text(renderer_, x, y + 6, "Empty", ui::color::faint);
    }
    y += icon + 4;
    const bool already = std::find(list.begin(), list.end(), tile_) != list.end();
    const auto flags = tile_ < host_.data->tiles.size() ? host_.data->tiles[tile_].flags : 0u;
    const bool fits = people ? (flags & data::tile_flags::kCharacter) != 0 : (flags & data::tile_flags::kItem) != 0;
    std::string addHelp = "Add the tile in hand (" + std::to_string(tile_) + ") to this list. Choose it below.";
    if (!fits) {
        addHelp += people ? " Careful: it is not marked as a person." : " Careful: it is not marked as an item.";
    }
    if (gui_.button(std::string(id) + "-add", {x, y, w, gui::kRow},
                    already ? "The tile in hand is on the list" : "Add the tile in hand", addHelp, false, !already)) {
        remember(zone_, -1);
        list.push_back(tile_);
    }
    y += gui::kRow + kPad;
    return y - top;
}

void Editor::zoneStory(float x, float y, float w, const SDL_FRect& r) {
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    y += gui_.paragraph(x, y, w,
                        names::zoneType(z.type) + ": " + names::zoneTypeHelp(z.type) +
                            " The world generator picks items from these lists to build the story; the lists of the rooms "
                            "behind the zone's doors count too.",
                        ui::color::text, 9) +
         kPad;
    y += tileList("asks", x, y, w, "Asks for",
                  "Items the zone can ask the hero to bring. One is picked for each game.", z.requiredItems, false);
    if (host_.data->layout() == data::Layout::Yoda) {
        y += tileList("asks2", x, y, w, "Asks for second",
                      "When the zone holds two puzzles, the second item it asks for comes from here.", z.goalItems,
                      false);
    }
    y += tileList("gives", x, y, w, "Gives", "Items the hero can find or get here.", z.providedItems, false);
    y += tileList("people", x, y, w, "People", "People who can stand on this zone's person spots.", z.npcs, true);

    section(renderer_, x, y, "Tile in hand");
    y += 12;
    if (const auto picked = tileBrowser({r.x, y, r.w, r.y + r.h - y}, "story-tiles", 24)) {
        tile_ = *picked;
    }
}

// --- the inspector ---------------------------------------------------------------

void Editor::zoneInspector(const SDL_FRect& r) {
    ui::fill(renderer_, r, ui::color::panel);
    ui::fill(renderer_, {r.x, r.y, 1, r.h}, ui::color::border);
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    const float x = r.x + kPad;
    const float w = r.w - 2 * kPad;
    float y = r.y + kPad;

    section(renderer_, x, y, "Zone " + std::to_string(zone_));
    y += 12;
    {
        const float half = (w - 4) / 2;
        if (gui_.button("view-draw", {x, y, half, gui::kRow}, "Draw",
                        "Draw the zone: tiles, hotspots and people.", !storyView_)) {
            storyView_ = false;
        }
        if (gui_.button("view-story", {x + half + 4, y, half, gui::kRow}, "Story",
                        "What the zone asks for and gives: the lists the world generator builds the story from.",
                        storyView_)) {
            storyView_ = true;
        }
        y += gui::kRow + kPad;
    }
    if (storyView_) {
        zoneStory(x, y, w, r);
        return;
    }
    // Kind of zone.
    int typeIndex = 0;
    for (std::size_t i = 0; i < std::size(kZoneTypes); ++i) {
        if (kZoneTypes[i] == z.type) {
            typeIndex = static_cast<int>(i);
        }
    }
    const bool knownType = kZoneTypes[static_cast<std::size_t>(typeIndex)] == z.type;
    int pick = typeIndex;
    if (gui_.stepper("zone-type", {x, y, w, gui::kRow}, "Kind", pick, 0, static_cast<int>(std::size(kZoneTypes)) - 1,
                     names::zoneType(z.type), "What the zone is for. " + names::zoneTypeHelp(z.type)) ||
        (!knownType && pick != typeIndex)) {
        remember(zone_, -1);
        z.type = kZoneTypes[static_cast<std::size_t>(pick)];
    }
    y += gui::kRow + 2;
    int planetIndex = 0;
    for (std::size_t i = 0; i < std::size(kPlanets); ++i) {
        if (kPlanets[i] == z.planet) {
            planetIndex = static_cast<int>(i);
        }
    }
    int planetPick = planetIndex;
    if (gui_.stepper("zone-planet", {x, y, w, gui::kRow}, "Planet", planetPick, 0,
                     static_cast<int>(std::size(kPlanets)) - 1, names::planet(z.planet),
                     "Where the zone can be used: the world generator only takes zones of the planet it builds.")) {
        remember(zone_, -1);
        z.planet = static_cast<std::uint16_t>(kPlanets[static_cast<std::size_t>(planetPick)]);
    }
    y += gui::kRow + 2;
    const float half = (w - 4) / 2;
    if (gui_.button("size-9", {x, y, half, gui::kRow}, "9 x 9", "Make the zone 9 x 9 tiles (cuts off the rest).",
                    z.width == 9 && z.height == 9)) {
        resizeZone(9, 9);
    }
    if (gui_.button("size-18", {x + half + 4, y, half, gui::kRow}, "18 x 18",
                    "Make the zone 18 x 18 tiles (new cells get the tile in hand).", z.width == 18 && z.height == 18)) {
        resizeZone(18, 18);
    }
    y += gui::kRow + 2;
    const auto scripts = z.actions ? z.actions->size() : 0;
    const std::string scriptLabel = "Edit scripts (" + std::to_string(scripts) + ")";
    if (gui_.button("scripts", {x, y, w, gui::kRow}, scriptLabel,
                    "Scripts make things happen: people talk, doors open, items appear. This opens them as text in "
                    "your text editor (see docs/formats/script-text.md).")) {
        openScripts();
    }
    y += gui::kRow + kPad + 2;

    // Tools.
    section(renderer_, x, y, "Tool");
    y += 12;
    struct ToolButton {
        Tool tool;
        const char* label;
        const char* help;
    };
    static constexpr ToolButton tools[] = {
        {Tool::Paint, "Paint", "Paint: put the tile in hand on the cells you click or drag over."},
        {Tool::Erase, "Erase", "Erase: clear the cells on this layer."},
        {Tool::Pick, "Pick", "Pick: take the tile (or hotspot kind) under the mouse in hand. Right-click does it too."},
        {Tool::Fill, "Fill", "Fill: drag a rectangle to fill it with the tile in hand."},
    };
    const float quarter = (w - 6) / 4;
    for (std::size_t i = 0; i < std::size(tools); ++i) {
        if (gui_.button(std::string("tool-") + tools[i].label,
                        {x + static_cast<float>(i) * (quarter + 2), y, quarter, gui::kRow}, tools[i].label, tools[i].help,
                        tool_ == tools[i].tool)) {
            tool_ = tools[i].tool;
            fillFrom_.reset();
        }
    }
    y += gui::kRow + kPad;

    // Layers.
    section(renderer_, x, y, "Layer");
    y += 12;
    for (int layer = 0; layer < 5; ++layer) {
        bool shown = shown_[static_cast<std::size_t>(layer)];
        if (gui_.checkbox("show-" + std::to_string(layer), {x, y, 16, gui::kRow}, "", shown,
                          "Show or hide this layer while drawing.")) {
            shown_[static_cast<std::size_t>(layer)] = shown;
        }
        if (gui_.button(std::string("layer-") + layerName(layer), {x + 18, y, w - 18, gui::kRow}, layerName(layer),
                        layerHelp(layer), layer_ == layer)) {
            layer_ = layer;
            shown_[static_cast<std::size_t>(layer)] = true;
        }
        y += gui::kRow + 2;
    }
    y += kPad - 2;

    if (layer_ == 3) {
        hotspotPanel(x, y, w);
        return;
    }
    if (layer_ == 4) {
        peoplePanel(x, y, w);
        return;
    }

    // The tile in hand and the tiles to choose from.
    section(renderer_, x, y, "Tile in hand");
    y += 12;
    drawTile(tile_, {x, y, 32, 32});
    ui::outline(renderer_, {x - 1, y - 1, 34, 34}, ui::color::border);
    ui::text(renderer_, x + 40, y + 4, std::to_string(tile_), ui::color::text);
    const std::string name = tileName(tile_);
    if (!name.empty()) {
        ui::text(renderer_, x + 40, y + 16, ui::fit(name, w - 40),
                 ui::color::dim);
    }
    y += 32 + kPad;
    if (const auto picked = tileBrowser({r.x, y, r.w, r.y + r.h - y}, "zone-tiles", 32)) {
        tile_ = *picked;
        if (tool_ == Tool::Erase || tool_ == Tool::Pick) {
            tool_ = Tool::Paint;
        }
    }
}

} // namespace das::app
