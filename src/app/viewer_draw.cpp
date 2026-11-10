// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing the front end: the play field and our own interface around it.
#include "viewer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace das::app {

std::vector<std::string> wrapText(const std::string& text, std::size_t width); // viewer.cpp

namespace {

constexpr int kAtlasColumns = 64;
constexpr float kTile = 32;
constexpr int kTilesPerPage = 9 * 9;
constexpr int kView = 9; // cells across the play field
constexpr float kPad = 8;
constexpr float kRow = 18; // a menu row
constexpr float kSlot = 36; // an inventory slot

std::string fit(const std::string& line, float width) {
    const auto chars = static_cast<std::size_t>(std::max(0.0f, width / ui::kFont));
    if (line.size() <= chars) {
        return line;
    }
    return chars > 2 ? line.substr(0, chars - 2) + ".." : line.substr(0, chars);
}

ui::Color healthColor(float ratio) {
    if (ratio > 0.5f) {
        return ui::color::good;
    }
    if (ratio > 0.25f) {
        return ui::color::accent;
    }
    return ui::color::danger;
}

// A button with a centred label.
void button(SDL_Renderer* r, const SDL_FRect& rect, const char* label) {
    ui::fill(r, rect, ui::color::panelLight);
    ui::outline(r, rect, ui::color::border);
    ui::textCentered(r, rect.x + rect.w / 2, rect.y + (rect.h - ui::kFont) / 2, label, ui::color::text);
}

} // namespace

void Viewer::addHit(Hit::Kind kind, const SDL_FRect& rect, int index) const { hits_.push_back({kind, rect, index}); }

void Viewer::renderTile(SDL_Renderer* renderer, std::uint16_t tile, const SDL_FRect& dst) const {
    if (tile == data::kNoTile || tile >= game().data.tiles.size()) {
        return;
    }
    const SDL_FRect src{static_cast<float>(tile % kAtlasColumns) * kTile,
                        static_cast<float>(tile / kAtlasColumns) * kTile, kTile, kTile};
    SDL_RenderTexture(renderer, game().atlas, &src, &dst);
}

void Viewer::render(SDL_Renderer* renderer, const ui::Layout& layout, Uint64 now) const {
    hits_.clear();
    if (games_.empty()) {
        return;
    }
    const SDL_FRect& field = layout.field;
    // A thin frame around the play field.
    ui::outline(renderer, {field.x - 1, field.y - 1, field.w + 2, field.h + 2}, ui::color::border);
    switch (mode_) {
    case ViewMode::Title:
        if (game().setup) {
            SDL_RenderTexture(renderer, game().setup, nullptr, &field);
        }
        renderScanlines(renderer, field, layout.scale);
        renderTitlePanel(renderer, layout.panel);
        break;
    case ViewMode::Zone:
        renderZone(renderer, field);
        addHit(Hit::Kind::Field, field);
        renderViewerPanel(renderer, layout.panel);
        break;
    case ViewMode::Tiles:
        renderTiles(renderer, field);
        renderViewerPanel(renderer, layout.panel);
        break;
    case ViewMode::Edit:
        if (editor_) {
            hits_.clear();
            editor_->frame(renderer, layout);
        }
        break;
    case ViewMode::Play:
        renderPlay(renderer, field, now);
        renderScanlines(renderer, field, layout.scale);
        renderHud(renderer, layout.panel);
        if (page_ != Page::None) {
            // The game waits under a dimmed screen.
            ui::fill(renderer, {0, 0, layout.width, layout.height}, ui::color::shade);
            hits_.clear(); // only the menu takes presses now
            const float w = std::min(field.w - 2 * kPad, 240.0f);
            const auto rows = static_cast<float>(menuRows().size());
            const float h = 40 + rows * kRow;
            const SDL_FRect box{std::floor(field.x + (field.w - w) / 2), std::floor(field.y + (field.h - h) / 2), w, h};
            ui::panel(renderer, box);
            renderMenu(renderer, {box.x + kPad, box.y + kPad, box.w - 2 * kPad, box.h - 2 * kPad},
                       page_ == Page::Options ? "Options" : page_ == Page::Controls ? "Controls" : "Paused");
        }
        break;
    }
}

// --- the play field ---------------------------------------------------------

void Viewer::renderZone(SDL_Renderer* renderer, const SDL_FRect& field) const {
    const auto& zone = game().data.zones[static_cast<std::size_t>(zone_)];
    const int span = std::max<int>(zone.width, zone.height);
    const float cell = field.w / static_cast<float>(span);
    const float ox = field.x + (field.w - cell * zone.width) / 2;
    const float oy = field.y + (field.h - cell * zone.height) / 2;
    for (int layer = 0; layer < 3; ++layer) {
        for (int cy = 0; cy < zone.height; ++cy) {
            for (int cx = 0; cx < zone.width; ++cx) {
                const SDL_FRect dst{ox + static_cast<float>(cx) * cell, oy + static_cast<float>(cy) * cell, cell, cell};
                renderTile(renderer, zone.at(cx, cy).layers[static_cast<std::size_t>(layer)], dst);
            }
        }
    }
}

void Viewer::renderTiles(SDL_Renderer* renderer, const SDL_FRect& field) const {
    const float cell = field.w / 9;
    const int first = tilePage_ * kTilesPerPage;
    for (int i = 0; i < kTilesPerPage; ++i) {
        const int tile = first + i;
        if (tile >= static_cast<int>(game().data.tiles.size())) {
            break;
        }
        const SDL_FRect dst{field.x + static_cast<float>(i % 9) * cell, field.y + static_cast<float>(i / 9) * cell,
                            cell, cell};
        renderTile(renderer, static_cast<std::uint16_t>(tile), dst);
    }
}

void Viewer::renderPlay(SDL_Renderer* renderer, const SDL_FRect& field, Uint64 now) const {
    const auto& w = *world_;
    const auto& zone = w.zone();
    const float cell = field.w / kView;
    // Camera: keep the player in the middle, without showing past the edges.
    const int camX = std::clamp(w.player().x - kView / 2, 0, std::max(0, zone.width - kView));
    const int camY = std::clamp(w.player().y - kView / 2, 0, std::max(0, zone.height - kView));
    camX_ = camX;
    camY_ = camY;
    cell_ = cell;
    addHit(Hit::Kind::Field, field);
    const auto dst = [&](int cx, int cy) {
        return SDL_FRect{field.x + static_cast<float>(cx - camX) * cell, field.y + static_cast<float>(cy - camY) * cell,
                         cell, cell};
    };
    const auto visible = [&](world::Position p) {
        return p.x >= camX && p.x < camX + kView && p.y >= camY && p.y < camY + kView;
    };
    const int endX = std::min<int>(zone.width, camX + kView);
    const int endY = std::min<int>(zone.height, camY + kView);
    for (int layer = 0; layer < 2; ++layer) {
        for (int cy = camY; cy < endY; ++cy) {
            for (int cx = camX; cx < endX; ++cx) {
                renderTile(renderer, w.tile({cx, cy}, layer), dst(cx, cy));
            }
        }
    }
    for (const auto& [at, t] : w.overlays()) {
        if (visible(at)) {
            renderTile(renderer, t, dst(at.x, at.y));
        }
    }
    for (const auto& m : w.monsters()) {
        if (visible(m.at)) {
            renderTile(renderer, m.tile, dst(m.at.x, m.at.y));
        }
    }
    if (walkTarget_ && visible(*walkTarget_)) {
        // Where a tap sends the hero.
        const SDL_FRect d = dst(walkTarget_->x, walkTarget_->y);
        const float inset = cell / 4;
        ui::outline(renderer, {d.x + inset, d.y + inset, d.w - 2 * inset, d.h - 2 * inset}, ui::color::accent);
    }
    if (game().hero && w.heroVisible()) {
        const bool walking = now - lastStepAt_ < 250;
        const int set = walking ? 1 + w.stepCount() % 2 : 0;
        std::uint16_t tile = game().hero->frame(set, w.facing());
        // Attacking: the weapon's own pose of the hero.
        if (w.attacking()) {
            for (const auto& f : w.fighters()) {
                if (f.character.type == 4 && f.item == w.weapon()) {
                    const std::uint16_t pose = f.character.frame(1, w.facing());
                    if (pose != data::kNoTile) {
                        tile = pose;
                    }
                    break;
                }
            }
        }
        renderTile(renderer, tile, dst(w.player().x, w.player().y));
    }
    for (const auto& shot : w.shots()) {
        if (visible(shot.at)) {
            renderTile(renderer, shot.tile, dst(shot.at.x, shot.at.y));
        }
    }
    for (int cy = camY; cy < endY; ++cy) {
        for (int cx = camX; cx < endX; ++cx) {
            renderTile(renderer, w.tile({cx, cy}, 2), dst(cx, cy));
        }
    }
    if (bubble_) {
        renderBubble(renderer, field, cell, camX, camY);
    }
    if (showMap_) {
        renderMap(renderer, field);
    }
    renderBanner(renderer, field);
}

void Viewer::renderScanlines(SDL_Renderer* renderer, const SDL_FRect& field, float scale) const {
    if (!settings_ || settings_->filter != Filter::Scanlines) {
        return;
    }
    // A dark line under every row of game pixels, at least one screen pixel.
    const float row = field.h / static_cast<float>(kView * static_cast<int>(kTile));
    const float line = std::max(1.0f / scale, row / 2);
    std::vector<SDL_FRect> lines;
    for (float y = field.y + row - line; y < field.y + field.h; y += row) {
        lines.push_back({field.x, y, field.w, line});
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 70);
    SDL_RenderFillRects(renderer, lines.data(), static_cast<int>(lines.size()));
}

void Viewer::renderBanner(SDL_Renderer* renderer, const SDL_FRect& field) const {
    if (banner_.empty()) {
        return;
    }
    const float w = ui::textWidth(banner_, 2) + 48;
    const SDL_FRect box{std::floor(field.x + (field.w - w) / 2), std::floor(field.y + field.h / 2 - 28), w, 56};
    ui::panel(renderer, box);
    ui::textCentered(renderer, box.x + box.w / 2, box.y + 12, banner_, ui::color::accent, 2);
    ui::textCentered(renderer, box.x + box.w / 2, box.y + 36, "Menu: back to the title", ui::color::dim);
}

void Viewer::renderMap(SDL_Renderer* renderer, const SDL_FRect& field) const {
    const auto& wm = world_->map();
    // The map of the current zone: the one listing it, or the map with the
    // site the zone belongs to.
    const world::Map* map = &wm.world;
    int hereX = -1;
    int hereY = -1;
    for (const world::Map* m : {&wm.world, &wm.dagobah}) {
        for (int cy = 0; cy < m->height; ++cy) {
            for (int cx = 0; cx < m->width; ++cx) {
                const auto& zones = m->at(cx, cy).zones;
                if (std::find(zones.begin(), zones.end(), world_->zoneIndex()) != zones.end()) {
                    map = m;
                    hereX = cx;
                    hereY = cy;
                }
            }
        }
    }
    ui::fill(renderer, field, {12, 14, 20, 235});
    ui::text(renderer, field.x + kPad, field.y + kPad, "Map", ui::color::accent);
    const float legendH = 14;
    const float area = std::min(field.w, field.h - 2 * legendH) - 4 * kPad;
    const float cell = std::floor(area / static_cast<float>(std::max(1, std::max(map->width, map->height))));
    const float ox = std::floor(field.x + (field.w - cell * static_cast<float>(map->width)) / 2);
    const float oy = std::floor(field.y + 2 * kPad + 8 + (area - cell * static_cast<float>(map->height)) / 2);
    const ui::Color town{90, 150, 230};
    const ui::Color open{230, 180, 80};
    const ui::Color goal{220, 80, 70};
    const ui::Color plain{62, 72, 66};
    const bool blink = (SDL_GetTicks() / 400) % 2 == 0;
    for (int cy = 0; cy < map->height; ++cy) {
        for (int cx = 0; cx < map->width; ++cx) {
            const auto& site = map->at(cx, cy);
            if (site.kind == world::SiteKind::None) {
                continue;
            }
            ui::Color c = plain;
            switch (site.kind) {
            case world::SiteKind::Town: c = town; break;
            case world::SiteKind::Goal: c = goal; break;
            case world::SiteKind::Puzzle: c = world_->goal(site.zone).solved ? ui::color::good : open; break;
            default: break;
            }
            const SDL_FRect box{ox + static_cast<float>(cx) * cell + 1, oy + static_cast<float>(cy) * cell + 1,
                                cell - 2, cell - 2};
            ui::fill(renderer, box, c);
            if (cx == hereX && cy == hereY) {
                ui::outline(renderer, {box.x - 1, box.y - 1, box.w + 2, box.h + 2}, ui::color::text);
                if (blink) {
                    const float m = std::floor(cell / 3);
                    ui::fill(renderer, {box.x + m - 1, box.y + m - 1, box.w - 2 * m + 2, box.h - 2 * m + 2},
                             ui::color::text);
                }
            }
        }
    }
    // The legend.
    struct Entry {
        ui::Color c;
        const char* name;
    };
    const Entry entries[] = {{town, "Town"}, {open, "Puzzle"}, {ui::color::good, "Done"}, {goal, "Goal"}};
    float lx = field.x + kPad;
    const float ly = field.y + field.h - kPad - 8;
    for (const auto& e : entries) {
        ui::fill(renderer, {lx, ly, 8, 8}, e.c);
        ui::text(renderer, lx + 11, ly, e.name, ui::color::dim);
        lx += 11 + ui::textWidth(e.name) + 12;
    }
}

void Viewer::renderBubble(SDL_Renderer* renderer, const SDL_FRect& field, float cell, int camX, int camY) const {
    const world::Position speaker = bubble_->at.value_or(world_->player());
    constexpr float kLine = 11;
    const float width = field.w - 8;
    bubbleColumns_ = static_cast<std::size_t>(std::max(8.0f, (width - 2 * kPad) / ui::kFont));
    bubbleLines_ = static_cast<std::size_t>(std::max(3.0f, (field.h * 0.6f) / kLine));
    const auto all = wrapText(bubble_->text, bubbleColumns_);
    const std::size_t first = static_cast<std::size_t>(bubblePage_) * bubbleLines_;
    const std::vector<std::string> lines(
        all.begin() + static_cast<std::ptrdiff_t>(std::min(first, all.size())),
        all.begin() + static_cast<std::ptrdiff_t>(std::min(first + bubbleLines_, all.size())));
    const bool more = first + bubbleLines_ < all.size();
    const float height = static_cast<float>(lines.size()) * kLine + 2 * kPad;
    const float sx = field.x + (static_cast<float>(speaker.x - camX) + 0.5f) * cell;
    const float sy = field.y + static_cast<float>(speaker.y - camY) * cell;
    // Above the speaker when there is room, otherwise below.
    float by = sy - height - 10;
    const bool below = by < field.y;
    if (below) {
        by = sy + cell + 10;
    }
    const float bx = std::floor(std::clamp(sx - width / 2, field.x + 4, field.x + field.w - width - 4));
    by = std::floor(std::clamp(by, field.y + 4, field.y + field.h - height - 4));

    const ui::Color paper{250, 248, 240, 245};
    const ui::Color ink{30, 32, 44};
    ui::fill(renderer, {bx + 3, by + 3, width, height}, {0, 0, 0, 90});
    ui::fill(renderer, {bx, by, width, height}, paper);
    // A small tail pointing at the speaker.
    for (int i = 0; i < 6; ++i) {
        const float tx = std::clamp(sx, bx + 10, bx + width - 10);
        const float ty = below ? by - static_cast<float>(i) - 1 : by + height + static_cast<float>(i);
        ui::fill(renderer, {tx - static_cast<float>(6 - i), ty, static_cast<float>(2 * (6 - i)), 1}, paper);
    }
    ui::outline(renderer, {bx, by, width, height}, ink);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        ui::text(renderer, bx + kPad, by + kPad + static_cast<float>(i) * kLine, lines[i], ink);
    }
    if (more) {
        // A small arrow: there is another page.
        for (int i = 0; i < 4; ++i) {
            ui::fill(renderer,
                     {bx + width - 14 + static_cast<float>(i), by + height - 9 + static_cast<float>(i),
                      static_cast<float>(8 - 2 * i), 1},
                     ink);
        }
    }
}

// --- the panel --------------------------------------------------------------

std::string Viewer::hintText() const {
    const bool menu = mode_ == ViewMode::Title || page_ != Page::None;
    if (capture_) {
        return device_ == InputDevice::Gamepad ? "Press the new button" : "Press the new key\nEsc cancels";
    }
    // The first key or button of an action.
    const auto key = [&](Action a) {
        if (!bindings_) {
            return std::string("?");
        }
        const std::string names = device_ == InputDevice::Gamepad ? bindings_->buttonNames(a) : bindings_->keyNames(a);
        return names.substr(0, names.find(','));
    };
    switch (device_) {
    case InputDevice::Touch: return menu ? "Tap to choose" : "Tap to walk and talk\nTap an item twice\nto use it";
    case InputDevice::Gamepad:
        return menu ? "D-pad choose\n" + key(Action::Use) + " select  " + key(Action::Attack) + " back"
                    : key(Action::Use) + " use  " + key(Action::Attack) + " attack\n" + key(Action::InventoryPrev) +
                          "/" + key(Action::InventoryNext) + " items\n" + key(Action::Map) + " map  " +
                          key(Action::Menu) + " menu";
    case InputDevice::Keyboard: break;
    }
    return menu ? "Arrows choose\n" + key(Action::Use) + " select  " + key(Action::Menu) + " back"
                : "Arrows walk  " + key(Action::Attack) + " attack\n" + key(Action::Use) + " use  " +
                      key(Action::InventoryPrev) + "/" + key(Action::InventoryNext) + " items\n" + key(Action::Map) +
                      " map  " + key(Action::Menu) + " menu";
}

void Viewer::renderHints(SDL_Renderer* renderer, const SDL_FRect& area) const {
    const std::string hint = hintText();
    const auto lines = wrapText(hint, static_cast<std::size_t>(area.w / ui::kFont));
    float y = area.y + area.h - static_cast<float>(lines.size()) * 10;
    if (y < area.y) {
        return; // no room
    }
    for (const auto& line : lines) {
        ui::text(renderer, area.x, y, line, ui::color::faint);
        y += 10;
    }
}

void Viewer::renderMenu(SDL_Renderer* renderer, const SDL_FRect& area, const char* heading) const {
    float y = area.y;
    if (heading) {
        ui::text(renderer, area.x, y, heading, ui::color::accent);
        y += 18;
    }
    const auto rows = menuRows();
    const int active = std::clamp(menuItem_, 0, static_cast<int>(rows.size()) - 1);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        const SDL_FRect rect{area.x, y, area.w, kRow - 2};
        const bool on = static_cast<int>(i) == active;
        if (on) {
            ui::fill(renderer, rect, ui::color::panelLight);
            ui::fill(renderer, {rect.x, rect.y, 2, rect.h}, ui::color::accent);
        }
        const float ty = rect.y + (rect.h - ui::kFont) / 2;
        ui::text(renderer, rect.x + 8, ty, row.label, on ? ui::color::accent : ui::color::text);
        addHit(Hit::Kind::Row, rect, static_cast<int>(i));
        if (!row.value.empty()) {
            // Long values (a list of keys) are cut to the room left.
            const float room = rect.w - 6 - 8 - ui::textWidth(row.label) - 5 * ui::kFont;
            const std::string shown = fit(row.value, room);
            const std::string value = on ? "< " + shown + " >" : shown + "  ";
            ui::textRight(renderer, rect.x + rect.w - 6, ty, value, on ? ui::color::text : ui::color::dim);
            // The left arrow (or the left part of the value) steps back.
            const float valueX = rect.x + rect.w - 6 - ui::textWidth(value);
            addHit(Hit::Kind::RowLeft, {valueX - 4, rect.y, 2 * ui::kFont + 4, rect.h}, static_cast<int>(i));
        }
        y += kRow;
    }
}

void Viewer::renderTitlePanel(SDL_Renderer* renderer, const SDL_FRect& panel) const {
    ui::panel(renderer, panel);
    const float x = panel.x + kPad;
    const float w = panel.w - 2 * kPad;
    float y = panel.y + kPad + 2;
    const bool indy = game().game == data::Layout::Indy;
    ui::text(renderer, x, y, indy ? "Indiana Jones" : "Yoda Stories", ui::color::accent);
    y += 11;
    ui::text(renderer, x, y, fit(indy ? "Desktop Adventures" : "Star Wars", w), ui::color::dim);
    y += 18;
    const float rows = static_cast<float>(menuRows().size());
    const char* heading = page_ == Page::Options    ? "Options"
                          : page_ == Page::Controls ? "Controls"
                          : page_ == Page::Mods     ? "Mods"
                                                    : nullptr;
    renderMenu(renderer, {x, y, w, rows * kRow}, heading);
    y += rows * kRow + (heading ? 18 : 0) + kPad;
    const auto paragraph = [&](const std::string& text, ui::Color color, std::size_t maxLines) {
        const auto lines = wrapText(text, static_cast<std::size_t>(w / ui::kFont));
        for (std::size_t i = 0; i < lines.size() && i < maxLines; ++i) {
            ui::text(renderer, x, y, lines[i], color);
            y += 10;
        }
        y += 4;
    };
    if (!game().paletteFromGame) {
        paragraph("No .EXE found: greyscale", ui::color::danger, 1);
    }
    if (!notice_.empty()) {
        paragraph(notice_, ui::color::danger, 3);
    }
    if (!game().modProblem.empty() && (page_ == Page::Main || page_ == Page::Mods)) {
        paragraph(game().modProblem, ui::color::danger, 6);
    }
    if (page_ == Page::Mods) {
        const auto list = gameMods();
        const auto menuRowsNow = menuRows();
        const int item = std::clamp(menuItem_, 0, static_cast<int>(menuRowsNow.size()) - 1);
        const Row& row = menuRowsNow[static_cast<std::size_t>(item)];
        if (list.empty()) {
            paragraph("No mods yet. Put a mod's folder or .zip file in the mods folder.",
                      ui::color::dim, 5);
        } else if (row.id == RowId::Mod && static_cast<std::size_t>(row.index) < list.size()) {
            const auto& info = list[static_cast<std::size_t>(row.index)]->info;
            std::string by = info.version.empty() ? "" : "Version " + info.version;
            if (!info.author.empty()) {
                by += (by.empty() ? "By " : ", by ") + info.author;
            }
            if (!by.empty()) {
                paragraph(by, ui::color::dim, 2);
            }
            if (!info.description.empty()) {
                paragraph(info.description, ui::color::text, 6);
            }
        }
        return;
    }
    renderHints(renderer, {x, y + 8, w, panel.y + panel.h - kPad - y - 8});
}

void Viewer::renderViewerPanel(SDL_Renderer* renderer, const SDL_FRect& panel) const {
    ui::panel(renderer, panel);
    const auto& g = game();
    const float x = panel.x + kPad;
    const float w = panel.w - 2 * kPad;
    float y = panel.y + kPad + 2;
    ui::text(renderer, x, y, "Data viewer", ui::color::accent);
    y += 18;
    char line[96];
    if (mode_ == ViewMode::Zone) {
        const auto& zone = g.data.zones[static_cast<std::size_t>(zone_)];
        std::snprintf(line, sizeof line, "Zone %d of %zu", zone_, g.data.zones.size());
        ui::text(renderer, x, y, line, ui::color::text);
        y += 12;
        std::snprintf(line, sizeof line, "%dx%d, type %u", zone.width, zone.height, static_cast<unsigned>(zone.type));
        ui::text(renderer, x, y, line, ui::color::dim);
        y += 12;
        std::snprintf(line, sizeof line, "%zu hotspots", zone.hotspots ? zone.hotspots->size() : 0);
        ui::text(renderer, x, y, line, ui::color::dim);
        y += 12;
        std::snprintf(line, sizeof line, "%zu scripts", zone.actions ? zone.actions->size() : 0);
        ui::text(renderer, x, y, line, ui::color::dim);
    } else {
        const int first = tilePage_ * kTilesPerPage;
        const int last = std::min<int>(first + kTilesPerPage, static_cast<int>(g.data.tiles.size())) - 1;
        std::snprintf(line, sizeof line, "Tiles %d-%d", first, last);
        ui::text(renderer, x, y, line, ui::color::text);
        y += 12;
        std::snprintf(line, sizeof line, "of %zu", g.data.tiles.size());
        ui::text(renderer, x, y, line, ui::color::dim);
    }
    const char* hint = mode_ == ViewMode::Zone ? "Arrows browse\nSpace walk here\nM tiles  Esc back"
                                               : "Arrows browse\nM zones  Esc back";
    const auto lines = wrapText(hint, static_cast<std::size_t>(w / ui::kFont));
    float hy = panel.y + panel.h - kPad - static_cast<float>(lines.size()) * 10;
    for (const auto& l : lines) {
        ui::text(renderer, x, hy, l, ui::color::faint);
        hy += 10;
    }
}

void Viewer::renderHud(SDL_Renderer* renderer, const SDL_FRect& panel) const {
    ui::panel(renderer, panel);
    const auto& w = *world_;
    const float x = panel.x + kPad;
    const float width = panel.w - 2 * kPad;
    const float bottom = panel.y + panel.h - kPad;
    float y = panel.y + kPad + 2;

    // Title and the puzzles solved so far.
    const bool indy = game().game == data::Layout::Indy;
    ui::text(renderer, x, y, generated_ ? (indy ? "Indiana Jones" : "Yoda Stories") : "Zone test", ui::color::accent);
    if (generated_) {
        int solved = 0;
        int total = 0;
        for (const auto& site : w.map().world.sites) {
            if (site.kind == world::SiteKind::Puzzle || site.kind == world::SiteKind::Goal) {
                ++total;
                solved += w.goal(site.zone).solved ? 1 : 0;
            }
        }
        char line[32];
        std::snprintf(line, sizeof line, "%d/%d", solved, total);
        ui::textRight(renderer, x + width, y, line, ui::color::dim);
    } else {
        char line[32];
        std::snprintf(line, sizeof line, "%d", w.zoneIndex());
        ui::textRight(renderer, x + width, y, line, ui::color::dim);
    }
    y += 16;

    // Health.
    const float ratio = static_cast<float>(w.health()) / static_cast<float>(world::kMaxHealth);
    ui::text(renderer, x, y, "Health", ui::color::dim);
    char percent[16];
    std::snprintf(percent, sizeof percent, "%d%%", static_cast<int>(std::ceil(ratio * 100)));
    ui::textRight(renderer, x + width, y, percent, ui::color::text);
    y += 11;
    ui::bar(renderer, {x, y, width, 8}, ratio, healthColor(ratio));
    y += 16;

    // The weapon in hand.
    const SDL_FRect weaponBox{x, y, kSlot - 2, kSlot - 2};
    ui::fill(renderer, weaponBox, {16, 18, 24});
    ui::outline(renderer, weaponBox, ui::color::border);
    if (w.weapon() != data::kNoTile) {
        renderTile(renderer, w.weapon(), {x + 1, y + 1, kTile, kTile});
    }
    ui::text(renderer, x + kSlot + 4, y + 6, "Weapon", ui::color::dim);
    ui::text(renderer, x + kSlot + 4, y + 18,
             fit(w.weapon() != data::kNoTile ? itemName(w.weapon()) : "None", width - kSlot - 4), ui::color::text);
    y += kSlot + 8;

    // The buttons at the bottom, and the hints above them.
    const float buttonY = bottom - kRow;
    const float half = std::floor((width - 4) / 2);
    const SDL_FRect mapButton{x, buttonY, half, kRow};
    const SDL_FRect menuButton{x + width - half, buttonY, half, kRow};
    if (generated_) {
        button(renderer, mapButton, "Map");
        addHit(Hit::Kind::Map, mapButton);
    }
    button(renderer, menuButton, generated_ ? "Menu" : "Back");
    addHit(Hit::Kind::Menu, menuButton);

    // The inventory as a grid; it scrolls to keep the selection in sight.
    const auto& inventory = w.inventory();
    char count[32];
    std::snprintf(count, sizeof count, "%zu", inventory.size());
    ui::text(renderer, x, y, "Inventory", ui::color::dim);
    ui::textRight(renderer, x + width, y, count, ui::color::faint);
    y += 12;
    const int columns = std::max(1, static_cast<int>(width / kSlot));
    const float nameH = 14;
    const float hintsH = 34;
    float gridRoom = buttonY - 6 - nameH - y;
    bool hints = gridRoom - hintsH >= 2 * kSlot;
    if (hints) {
        gridRoom -= hintsH;
    }
    const int rowCount = (static_cast<int>(inventory.size()) + columns - 1) / columns;
    // Not more empty rows than needed: at least two, then as many as fit.
    const int rowsVisible = std::max(1, std::min(static_cast<int>(gridRoom / kSlot), std::max(2, rowCount)));
    const int selectedRow = static_cast<int>(selected_) / columns;
    const int firstRow = std::clamp(selectedRow - rowsVisible + 1, 0, std::max(0, rowCount - rowsVisible));
    const float gridX = x + std::floor((width - static_cast<float>(columns) * kSlot) / 2);
    for (int r = 0; r < rowsVisible; ++r) {
        for (int c = 0; c < columns; ++c) {
            const auto i = static_cast<std::size_t>((firstRow + r) * columns + c);
            const SDL_FRect slot{gridX + static_cast<float>(c) * kSlot, y + static_cast<float>(r) * kSlot, kSlot - 2,
                                 kSlot - 2};
            ui::fill(renderer, slot, {16, 18, 24});
            if (i >= inventory.size()) {
                continue;
            }
            renderTile(renderer, inventory[i], {slot.x + 1, slot.y + 1, kTile, kTile});
            if (i == selected_) {
                ui::outline(renderer, slot, ui::color::accent);
                ui::outline(renderer, {slot.x + 1, slot.y + 1, slot.w - 2, slot.h - 2}, ui::color::accent);
            }
            addHit(Hit::Kind::Slot, slot, static_cast<int>(i));
        }
    }
    if (rowCount > rowsVisible) {
        // A thin scroll bar at the right of the grid.
        const float gridH = static_cast<float>(rowsVisible) * kSlot - 2;
        const float knob = gridH * static_cast<float>(rowsVisible) / static_cast<float>(rowCount);
        const float top = gridH * static_cast<float>(firstRow) / static_cast<float>(rowCount);
        ui::fill(renderer, {x + width - 1, y, 2, gridH}, {16, 18, 24});
        ui::fill(renderer, {x + width - 1, y + top, 2, knob}, ui::color::dim);
    }
    y += static_cast<float>(rowsVisible) * kSlot;
    if (inventory.empty()) {
        ui::text(renderer, x, y, "Nothing carried", ui::color::faint);
    } else {
        ui::text(renderer, x, y, fit(itemName(inventory[std::min(selected_, inventory.size() - 1)]), width),
                 ui::color::text);
    }
    y += nameH;
    if (hints) {
        renderHints(renderer, {x, y, width, buttonY - 6 - y});
    }
}

} // namespace das::app
