// SPDX-License-Identifier: GPL-3.0-or-later
//
// The zone editor: paints tiles on a zone's three layers, places hotspots,
// adds zones, tries a zone out and saves the changes as a mod
// (docs/editor.md, docs/formats/mod.md).
#include "viewer.hpp"

#include "das/util/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace das::app {

std::vector<std::string> wrapText(const std::string& text, std::size_t width); // viewer.cpp

namespace {

constexpr float kTile = 32;
constexpr int kPickSide = 9; // tiles across the picker
constexpr int kPickPage = kPickSide * kPickSide;
constexpr float kPad = 8;
constexpr float kRow = 18;
constexpr std::size_t kUndoSteps = 100;
constexpr std::uint16_t kNoArgument = 0xffff;

const char* layerName(int layer) {
    switch (layer) {
    case 0: return "Floor";
    case 1: return "Objects";
    case 2: return "Roof";
    default: return "Hotspots";
    }
}

const char* toolName(int tool) { return tool == 1 ? "Erase" : tool == 2 ? "Pick" : "Paint"; }

// Our own short names for the hotspot types (see docs/editor.md).
std::string spotName(std::uint32_t type) {
    static const char* const names[] = {"Item spot", "Person spot", "Weapon spot", "Vehicle to", "Vehicle back",
                                        "Locator",   "Item",        "Person",      "Weapon",     "Door in",
                                        "Door out",  "Unused",      "Lock",        "Teleporter", "Ship out",
                                        "Ship in"};
    return type < std::size(names) ? names[type] : "Type " + std::to_string(type);
}

int wrapAround(int value, int count) { return ((value % count) + count) % count; }

std::string fit(const std::string& line, float width) {
    const auto chars = static_cast<std::size_t>(std::max(0.0f, width / ui::kFont));
    if (line.size() <= chars) {
        return line;
    }
    return chars > 2 ? line.substr(0, chars - 2) + ".." : line.substr(0, chars);
}

// Where a zone sits in the field: the size of a cell and the top left.
struct Grid {
    float cell;
    float x;
    float y;
};

Grid gridOf(const data::Zone& zone, const SDL_FRect& field) {
    const int span = std::max<int>(1, std::max<int>(zone.width, zone.height));
    const float cell = field.w / static_cast<float>(span);
    return {cell, field.x + (field.w - cell * zone.width) / 2, field.y + (field.h - cell * zone.height) / 2};
}

} // namespace

// --- state -------------------------------------------------------------------

void Viewer::openEditor() {
    if (games_.empty()) {
        return;
    }
    if (modsFolder_.empty()) {
        notice_ = "The editor needs a library folder";
        return;
    }
    LoadedGame& g = game();
    rescanMods();
    // The editor works on the base game with only its own mod on top, so
    // that saving stores just what was changed here.
    edit_ = EditState{};
    edit_.info.id = "my-" + g.base.game;
    edit_.info.game = g.base.game;
    edit_.info.name = "My changes";
    edit_.info.description = "Made with the editor.";
    pack::Pack pack = g.base;
    for (const auto& mod : mods_) {
        if (mod.info.id == edit_.info.id && mod.info.game == g.base.game) {
            try {
                pack::applyMod(pack, mod);
                edit_.info = mod.info;
            } catch (const std::exception& e) {
                notice_ = mod.info.name + ": " + e.what();
                return;
            }
        }
    }
    edit_.sounds = pack.sounds;
    install(g, std::move(pack));
    g.mods = {edit_.info.id};
    world_.reset();
    zone_ = std::clamp(zone_, 0, static_cast<int>(g.data.zones.size()) - 1);
    edit_.x = editZone().width / 2;
    edit_.y = editZone().height / 2;
    edit_.tile = editZone().at(edit_.x, edit_.y).layers[0];
    if (edit_.tile == data::kNoTile) {
        edit_.tile = 0;
    }
    mode_ = ViewMode::Edit;
    page_ = Page::None;
    menuItem_ = 0;
    notice_.clear();
}

void Viewer::leaveEditor() {
    if (edit_.dirty && !edit_.leaveArmed) {
        edit_.leaveArmed = true;
        notice_ = "Not saved. Leave again to drop the changes.";
        return;
    }
    edit_ = EditState{};
    rebuild(game()); // back to the mods that are switched on
    mode_ = ViewMode::Title;
    page_ = Page::Main;
    menuItem_ = 0;
    notice_.clear();
}

void Viewer::editAt(int x, int y, bool record) {
    data::Zone& zone = editZone();
    if (x < 0 || y < 0 || x >= zone.width || y >= zone.height) {
        return;
    }
    edit_.x = x;
    edit_.y = y;
    edit_.leaveArmed = false;
    const data::Zone before = zone;
    if (edit_.layer < 3) {
        auto& slot = zone.cells[static_cast<std::size_t>(y * zone.width + x)].layers[static_cast<std::size_t>(edit_.layer)];
        if (edit_.tool == 2) {
            if (slot != data::kNoTile) {
                edit_.tile = slot;
            }
            return;
        }
        const std::uint16_t value = edit_.tool == 1 ? data::kNoTile : edit_.tile;
        if (slot == value) {
            return;
        }
        if (record) {
            edit_.undo.emplace_back(zone_, before);
        }
        slot = value;
    } else {
        if (!zone.hotspots) {
            zone.hotspots.emplace(); // an Indy zone that had none listed
        }
        auto& spots = *zone.hotspots;
        const auto here = std::find_if(spots.begin(), spots.end(), [&](const data::Hotspot& h) {
            return h.x == x && h.y == y;
        });
        if (edit_.tool == 2) {
            if (here != spots.end()) {
                edit_.spotType = here->type;
            }
            return;
        }
        if (edit_.tool == 1) {
            if (here == spots.end()) {
                return;
            }
            if (record) {
                edit_.undo.emplace_back(zone_, before);
            }
            spots.erase(here);
        } else {
            if (here != spots.end() && here->type == edit_.spotType) {
                return;
            }
            if (record) {
                edit_.undo.emplace_back(zone_, before);
            }
            if (here != spots.end()) {
                here->type = edit_.spotType;
            } else {
                data::Hotspot h;
                h.type = edit_.spotType;
                h.x = static_cast<std::uint16_t>(x);
                h.y = static_cast<std::uint16_t>(y);
                h.enabled = 1;
                h.argument = kNoArgument;
                spots.push_back(h);
            }
        }
    }
    if (edit_.undo.size() > kUndoSteps) {
        edit_.undo.erase(edit_.undo.begin());
    }
    edit_.dirty = true;
}

void Viewer::editUndo() {
    if (edit_.undo.empty()) {
        return;
    }
    auto [zone, before] = std::move(edit_.undo.back());
    edit_.undo.pop_back();
    if (zone >= 0 && static_cast<std::size_t>(zone) < game().data.zones.size()) {
        game().data.zones[static_cast<std::size_t>(zone)] = std::move(before);
        zone_ = zone;
    }
    edit_.dirty = true;
}

void Viewer::editNewZone() {
    auto& zones = game().data.zones;
    const data::Zone& like = editZone();
    data::Zone zone;
    zone.width = like.width;
    zone.height = like.height;
    zone.type = like.type;
    zone.planet = like.planet;
    zone.izonField = like.izonField;
    zone.izonPlanet = like.izonPlanet;
    zone.index = static_cast<std::uint16_t>(zones.size());
    zone.cells.resize(static_cast<std::size_t>(zone.width) * zone.height);
    for (auto& c : zone.cells) {
        c.layers[0] = edit_.tile;
    }
    zone.hotspots.emplace();
    zone.actions.emplace();
    zones.push_back(std::move(zone));
    zone_ = static_cast<int>(zones.size()) - 1;
    edit_.undo.clear(); // undo steps keep zone numbers; keep them simple
    edit_.dirty = true;
    edit_.leaveArmed = false;
    notice_ = "Zone " + std::to_string(zone_) + " added";
}

void Viewer::editSave() {
    LoadedGame& g = game();
    pack::Pack changed;
    changed.game = g.base.game;
    changed.data = g.data;
    changed.palette = g.palette;
    changed.sounds = edit_.sounds;
    try {
        pack::Mod mod = pack::makeMod(g.base, changed, edit_.info);
        const auto existing = std::find_if(mods_.begin(), mods_.end(), [&](const pack::Mod& m) {
            return m.info.id == edit_.info.id && m.info.game == edit_.info.game;
        });
        std::filesystem::path folder = modsFolder_ / edit_.info.id;
        if (existing != mods_.end() && std::filesystem::is_directory(existing->location)) {
            folder = existing->location;
        }
        pack::writeMod(mod, folder);
        if (settings_) {
            auto& on = settings_->mods[g.base.game];
            if (std::find(on.begin(), on.end(), edit_.info.id) == on.end()) {
                on.push_back(edit_.info.id);
                settingsChanged_ = true;
            }
        }
        rescanMods();
        edit_.dirty = false;
        edit_.leaveArmed = false;
        notice_ = "Saved as mod \"" + edit_.info.id + "\" (" + std::to_string(mod.files.size() - 1) + " files)";
    } catch (const std::exception& e) {
        notice_ = std::string("Not saved: ") + e.what();
    }
}

// --- menu ------------------------------------------------------------------

std::vector<Viewer::Row> Viewer::editorRows() const {
    std::vector<Row> rows;
    const auto& zones = game().data.zones;
    rows.push_back({RowId::EditZone, "Zone", std::to_string(zone_) + "/" + std::to_string(zones.size() - 1)});
    rows.push_back({RowId::EditLayer, "Layer", layerName(edit_.layer)});
    rows.push_back({RowId::EditTool, "Tool", toolName(edit_.tool)});
    if (edit_.layer < 3) {
        rows.push_back({RowId::EditTile, "Tile", std::to_string(edit_.tile)});
    } else {
        rows.push_back({RowId::EditSpotType, "Type", spotName(edit_.spotType)});
        std::string argument = "-";
        if (const auto& spots = editZone().hotspots) {
            for (const auto& h : *spots) {
                if (h.x == edit_.x && h.y == edit_.y) {
                    argument = h.argument == kNoArgument ? "None" : std::to_string(h.argument);
                }
            }
        }
        rows.push_back({RowId::EditSpotArgument, "Argument", argument});
    }
    rows.push_back({RowId::EditZoneType, "Zone type", std::to_string(editZone().type)});
    rows.push_back({RowId::EditUndo, "Undo", edit_.undo.empty() ? "" : std::to_string(edit_.undo.size())});
    rows.push_back({RowId::EditNewZone, "New zone", ""});
    rows.push_back({RowId::EditTest, "Try it out", ""});
    rows.push_back({RowId::EditSave, "Save mod", edit_.dirty ? "*" : ""});
    rows.push_back({RowId::EditLeave, "Leave editor", ""});
    return rows;
}

void Viewer::editorActivate(const Row& row, int change) {
    edit_.leaveArmed = row.id == RowId::EditLeave && edit_.leaveArmed;
    switch (row.id) {
    case RowId::EditZone:
        zone_ = wrapAround(zone_ + change, static_cast<int>(game().data.zones.size()));
        edit_.x = std::min(edit_.x, editZone().width - 1);
        edit_.y = std::min(edit_.y, editZone().height - 1);
        break;
    case RowId::EditLayer: edit_.layer = wrapAround(edit_.layer + change, 4); break;
    case RowId::EditTool: edit_.tool = wrapAround(edit_.tool + change, 3); break;
    case RowId::EditTile:
        // Left and right step through the tiles; pressing opens the picker.
        if (change != 0) {
            edit_.tile = static_cast<std::uint16_t>(
                wrapAround(edit_.tile + change, static_cast<int>(game().data.tiles.size())));
        } else {
            edit_.picking = true;
            edit_.pickPage = edit_.tile / kPickPage;
            edit_.pickIndex = edit_.tile % kPickPage;
            page_ = Page::None;
        }
        break;
    case RowId::EditSpotType: edit_.spotType = static_cast<std::uint32_t>(wrapAround(static_cast<int>(edit_.spotType) + change, 16)); break;
    case RowId::EditSpotArgument:
        if (auto& spots = editZone().hotspots) {
            for (auto& h : *spots) {
                if (h.x == edit_.x && h.y == edit_.y) {
                    edit_.undo.emplace_back(zone_, editZone());
                    // One below 0 is "none" (0xFFFF), as in the data.
                    const int value = h.argument == kNoArgument ? -1 : h.argument;
                    const int next = std::clamp(value + change, -1, 0xfffe);
                    h.argument = next < 0 ? kNoArgument : static_cast<std::uint16_t>(next);
                    edit_.dirty = true;
                }
            }
        }
        break;
    case RowId::EditZoneType:
        edit_.undo.emplace_back(zone_, editZone());
        editZone().type = static_cast<std::uint32_t>(std::clamp(static_cast<int>(editZone().type) + change, 0, 30));
        edit_.dirty = true;
        break;
    case RowId::EditUndo: editUndo(); break;
    case RowId::EditNewZone: editNewZone(); break;
    case RowId::EditTest:
        world_.emplace(game().data);
        world_->start(zone_);
        startPlay();
        generated_ = false;
        edit_.testing = true;
        break;
    case RowId::EditSave: editSave(); break;
    case RowId::EditLeave: leaveEditor(); break;
    default: break;
    }
}

// --- input -----------------------------------------------------------------

void Viewer::updateEditor(const ActionState& actions, std::optional<Step> step) {
    if (drag_) {
        const SDL_FPoint at = *drag_;
        drag_.reset();
        for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
            if (it->kind == Hit::Kind::Field && ui::contains(it->rect, at.x, at.y)) {
                editorClick(*it, at, true);
                break;
            }
        }
    }
    if (edit_.picking) {
        const int count = static_cast<int>(game().data.tiles.size());
        const int pages = std::max(1, (count + kPickPage - 1) / kPickPage);
        if (actions.wasPressed(Action::Menu) || actions.wasPressed(Action::Attack) || actions.wasPressed(Action::Map)) {
            edit_.picking = false;
            return;
        }
        if (actions.wasPressed(Action::InventoryNext) || actions.wasPressed(Action::InventoryPrev)) {
            edit_.pickPage = wrapAround(edit_.pickPage + (actions.wasPressed(Action::InventoryNext) ? 1 : -1), pages);
        }
        if (step) {
            const int x = wrapAround(edit_.pickIndex % kPickSide + step->dx, kPickSide);
            const int y = edit_.pickIndex / kPickSide + step->dy;
            if (y < 0 || y >= kPickSide) {
                edit_.pickPage = wrapAround(edit_.pickPage + (y < 0 ? -1 : 1), pages);
            }
            edit_.pickIndex = wrapAround(y, kPickSide) * kPickSide + x;
        }
        if (actions.wasPressed(Action::Use)) {
            const int tile = edit_.pickPage * kPickPage + edit_.pickIndex;
            if (tile < count) {
                edit_.tile = static_cast<std::uint16_t>(tile);
                edit_.picking = false;
            }
        }
        return;
    }
    if (page_ == Page::Editor) {
        if (actions.wasPressed(Action::Menu) || actions.wasPressed(Action::Attack)) {
            page_ = Page::None;
            return;
        }
        const auto rows = editorRows();
        menuItem_ = std::clamp(menuItem_, 0, static_cast<int>(rows.size()) - 1);
        if (step && step->dy != 0) {
            menuItem_ = wrapAround(menuItem_ + step->dy, static_cast<int>(rows.size()));
            return;
        }
        const Row& row = rows[static_cast<std::size_t>(menuItem_)];
        if (step && step->dx != 0 && !row.value.empty()) {
            editorActivate(row, step->dx);
            return;
        }
        if (actions.wasPressed(Action::Use)) {
            editorActivate(row, row.id == RowId::EditTile ? 0 : 1);
        }
        return;
    }
    // The field has the focus: the cursor moves, Use applies the tool.
    if (actions.wasPressed(Action::Menu)) {
        page_ = Page::Editor;
        return;
    }
    if (actions.wasPressed(Action::Map)) {
        edit_.picking = true;
        edit_.pickPage = edit_.tile / kPickPage;
        edit_.pickIndex = edit_.tile % kPickPage;
        return;
    }
    if (actions.wasPressed(Action::InventoryNext) || actions.wasPressed(Action::InventoryPrev)) {
        edit_.layer = wrapAround(edit_.layer + (actions.wasPressed(Action::InventoryNext) ? 1 : -1), 4);
    }
    if (step) {
        edit_.x = std::clamp(edit_.x + step->dx, 0, editZone().width - 1);
        edit_.y = std::clamp(edit_.y + step->dy, 0, editZone().height - 1);
    }
    if (actions.wasPressed(Action::Use)) {
        editAt(edit_.x, edit_.y, true);
    }
    if (actions.wasPressed(Action::Attack)) {
        const int tool = edit_.tool;
        edit_.tool = 2; // Attack picks what is under the cursor
        editAt(edit_.x, edit_.y, false);
        edit_.tool = tool;
    }
}

void Viewer::editorClick(const Hit& hit, SDL_FPoint at, bool dragged) {
    if (hit.kind == Hit::Kind::Picker) {
        if (!dragged) {
            edit_.tile = static_cast<std::uint16_t>(hit.index);
            edit_.picking = false;
        }
        return;
    }
    if (hit.kind == Hit::Kind::Field && !edit_.picking) {
        const Grid g = gridOf(editZone(), hit.rect);
        const int x = static_cast<int>(std::floor((at.x - g.x) / g.cell));
        const int y = static_cast<int>(std::floor((at.y - g.y) / g.cell));
        if (dragged && x == edit_.x && y == edit_.y) {
            return;
        }
        // A press starts a stroke, which Undo takes back as a whole.
        editAt(x, y, !dragged);
        page_ = Page::None;
        return;
    }
    if ((hit.kind == Hit::Kind::Row || hit.kind == Hit::Kind::RowLeft) && !dragged) {
        const auto rows = editorRows();
        if (hit.index >= 0 && hit.index < static_cast<int>(rows.size())) {
            menuItem_ = hit.index;
            const Row& row = rows[static_cast<std::size_t>(hit.index)];
            editorActivate(row, hit.kind == Hit::Kind::RowLeft ? -1 : row.id == RowId::EditTile ? 0 : 1);
        }
    }
}

// --- drawing ---------------------------------------------------------------

void Viewer::renderEditor(SDL_Renderer* renderer, const SDL_FRect& field) const {
    const auto& g = game();
    if (edit_.picking) {
        const float cell = field.w / kPickSide;
        const int first = edit_.pickPage * kPickPage;
        for (int i = 0; i < kPickPage; ++i) {
            const int tile = first + i;
            if (tile >= static_cast<int>(g.data.tiles.size())) {
                break;
            }
            const SDL_FRect dst{field.x + static_cast<float>(i % kPickSide) * cell,
                                field.y + static_cast<float>(i / kPickSide) * cell, cell, cell};
            renderTile(renderer, static_cast<std::uint16_t>(tile), dst);
            addHit(Hit::Kind::Picker, dst, tile);
            if (tile == edit_.tile) {
                ui::outline(renderer, dst, ui::color::accent);
            }
            if (i == edit_.pickIndex) {
                ui::outline(renderer, {dst.x + 1, dst.y + 1, dst.w - 2, dst.h - 2}, ui::color::text);
            }
        }
        return;
    }
    const auto& zone = editZone();
    const Grid grid = gridOf(zone, field);
    for (int layer = 0; layer < 3; ++layer) {
        // Layers above the one being edited are seen through.
        const bool above = edit_.layer < 3 && layer > edit_.layer;
        SDL_SetTextureAlphaMod(g.atlas, above ? 90 : 255);
        for (int cy = 0; cy < zone.height; ++cy) {
            for (int cx = 0; cx < zone.width; ++cx) {
                const SDL_FRect dst{grid.x + static_cast<float>(cx) * grid.cell,
                                    grid.y + static_cast<float>(cy) * grid.cell, grid.cell, grid.cell};
                renderTile(renderer, zone.at(cx, cy).layers[static_cast<std::size_t>(layer)], dst);
            }
        }
    }
    SDL_SetTextureAlphaMod(g.atlas, 255);
    addHit(Hit::Kind::Field, field);
    if (zone.hotspots) {
        const bool spots = edit_.layer == 3;
        for (const auto& h : *zone.hotspots) {
            const SDL_FRect r{grid.x + static_cast<float>(h.x) * grid.cell + 1,
                              grid.y + static_cast<float>(h.y) * grid.cell + 1, grid.cell - 2, grid.cell - 2};
            ui::outline(renderer, r, spots ? ui::color::good : ui::color::faint);
            if (spots && grid.cell >= 2 * ui::kFont) {
                ui::text(renderer, r.x + 1, r.y + 1, std::to_string(h.type), ui::color::good);
            }
        }
    }
    // The cursor, for keyboard and gamepad.
    const SDL_FRect cursor{grid.x + static_cast<float>(edit_.x) * grid.cell, grid.y + static_cast<float>(edit_.y) * grid.cell,
                           grid.cell, grid.cell};
    ui::outline(renderer, cursor, page_ == Page::None ? ui::color::accent : ui::color::dim);
}

void Viewer::renderEditorPanel(SDL_Renderer* renderer, const SDL_FRect& panel) const {
    ui::panel(renderer, panel);
    const auto& g = game();
    const float x = panel.x + kPad;
    const float w = panel.w - 2 * kPad;
    float y = panel.y + kPad + 2;
    ui::text(renderer, x, y, "Zone editor", ui::color::accent);
    y += 11;
    ui::text(renderer, x, y, fit("Mod: " + edit_.info.id, w), ui::color::dim);
    y += 16;

    // The tile in hand, with its name.
    if (edit_.layer < 3) {
        const SDL_FRect swatch{x, y, kTile, kTile};
        ui::fill(renderer, swatch, ui::color::background);
        renderTile(renderer, edit_.tile, swatch);
        ui::outline(renderer, swatch, ui::color::border);
        std::string name;
        for (const auto& n : g.data.tileNames) {
            if (n.tile == edit_.tile) {
                name = text::windows1252ToUtf8(n.name);
            }
        }
        ui::text(renderer, x + kTile + 6, y + 6, "Tile " + std::to_string(edit_.tile), ui::color::text);
        if (!name.empty()) {
            ui::text(renderer, x + kTile + 6, y + 18, fit(name, w - kTile - 6), ui::color::dim);
        }
        y += kTile + 8;
    }

    const auto rows = editorRows();
    renderMenu(renderer, {x, y, w, static_cast<float>(rows.size()) * kRow}, nullptr);
    y += static_cast<float>(rows.size()) * kRow + 4;

    const auto& zone = editZone();
    char line[96];
    std::snprintf(line, sizeof line, "%dx%d, cell %d,%d", zone.width, zone.height, edit_.x, edit_.y);
    ui::text(renderer, x, y, line, ui::color::dim);
    y += 12;
    if (!notice_.empty()) {
        for (const auto& l : wrapText(notice_, static_cast<std::size_t>(w / ui::kFont))) {
            ui::text(renderer, x, y, l, ui::color::accent);
            y += 10;
        }
        y += 2;
    }
    std::string hint;
    if (edit_.picking) {
        const int pages = static_cast<int>((g.data.tiles.size() + kPickPage - 1) / kPickPage);
        hint = "Page " + std::to_string(edit_.pickPage + 1) + " of " + std::to_string(pages) +
               ". Choose a tile; " + (device_ == InputDevice::Gamepad ? "LB/RB" : "Q/E") + " turns pages.";
    } else if (device_ == InputDevice::Touch || device_ == InputDevice::Keyboard) {
        hint = page_ == Page::None ? "Click or drag to draw. Keys: arrows move, Space draws, Ctrl picks, M tiles, "
                                     "Q/E layer, Esc menu."
                                   : "Esc: back to the zone.";
    } else {
        hint = page_ == Page::None ? "A draws, B picks, Back tiles, LB/RB layer, Start menu." : "B: back to the zone.";
    }
    for (const auto& l : wrapText(hint, static_cast<std::size_t>(w / ui::kFont))) {
        if (y + 10 > panel.y + panel.h - kPad) {
            break;
        }
        ui::text(renderer, x, y, l, ui::color::faint);
        y += 10;
    }
}

} // namespace das::app
