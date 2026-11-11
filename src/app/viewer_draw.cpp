// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing the front end: the play field and our own interface around it.
#include "viewer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace das::app {

std::vector<std::string> wrapText(const std::string& text, float width); // viewer.cpp

namespace {

constexpr int kAtlasColumns = 64;
constexpr float kTile = 32;
constexpr int kTilesPerPage = 9 * 9;
constexpr int kView = 9; // cells across the play field
constexpr float kPad = 8;
constexpr float kRow = 18; // a menu row

std::string fit(const std::string& line, float width) { return ui::fit(line, width); }

ui::Color healthColor(float ratio) {
    if (ratio > 0.5f) {
        return ui::color::good;
    }
    if (ratio > 0.25f) {
        return ui::color::accent;
    }
    return ui::color::danger;
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
    // Each game has its own colour; on the cards it is the chosen card's.
    const bool picking = mode_ == ViewMode::Title && page_ == Page::Pick;
    const bool indy = picking ? pickCard() == 0 : game().game == data::Layout::Indy;
    ui::setAccent(indy ? ui::kIndyAccent : ui::kYodaAccent, indy ? ui::kIndyInk : ui::kYodaInk);
    if (picking) {
        renderPicker(renderer, layout);
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
        renderHud(renderer, layout);
        if (showCoordinates_ && !capturePending_) {
            const std::string where = "zone " + std::to_string(world_->zoneIndex()) + "  " +
                                      std::to_string(world_->player().x) + "," + std::to_string(world_->player().y);
            ui::fill(renderer, {field.x, field.y, ui::textWidth(where) + 8, 12}, ui::color::shade);
            ui::text(renderer, field.x + 4, field.y + 2, where, ui::color::accent);
        }
        // A bug report's screenshot is taken without the menu over the game.
        if (page_ != Page::None && !capturePending_) {
            // The game waits under a dimmed screen.
            ui::fill(renderer, {0, 0, layout.width, layout.height}, ui::color::shade);
            hits_.clear(); // only the menu takes presses now
            if (page_ == Page::Pause && generated_) {
                renderPause(renderer, layout);
            } else {
                renderMenuCard(renderer, field);
            }
        }
        break;
    }
}

void Viewer::renderMenuCard(SDL_Renderer* renderer, const SDL_FRect& field) const {
    const bool help = page_ == Page::Help;
    const bool card = help || page_ == Page::Bug; // a text above the rows
    const float w = std::min(field.w - 2 * kPad, card ? 272.0f : 240.0f);
    const auto rows = static_cast<float>(menuRows().size());
    auto cardLines =
        card ? wrapText(help ? howToPlayText() : bugText(), w - 2 * kPad)
             : std::vector<std::string>{};
    if (!help && cardLines.size() > 9) {
        cardLines.erase(cardLines.begin() + 1, cardLines.end() - 8); // the end of a long note
    }
    const float textH = static_cast<float>(cardLines.size()) * 10 + (card ? 6 : 0);
    const float h = (card ? 2 * kPad + 18 + 2 : 40) + rows * kRow + textH + (notice_.empty() ? 0 : 14);
    const SDL_FRect box{std::floor(field.x + (field.w - w) / 2),
                        std::floor(std::max(field.y, field.y + (field.h - h) / 2)), w, h};
    ui::panel(renderer, box);
    const char* heading = page_ == Page::Options    ? "Options"
                          : page_ == Page::Controls ? "Controls"
                          : page_ == Page::Slots    ? (slotsSaving_ ? "Save game" : "Load game")
                          : help                    ? "How to play"
                          : card                    ? "Report a bug"
                                                    : "Paused";
    if (card) {
        float ty = box.y + kPad;
        ui::text(renderer, box.x + kPad, ty, heading, ui::color::accent);
        ty += 18;
        for (const auto& line : cardLines) {
            ui::text(renderer, box.x + kPad, ty, line, help ? ui::color::text : ui::color::dim);
            ty += 10;
        }
        renderMenu(renderer, {box.x + kPad, ty + 6, box.w - 2 * kPad, rows * kRow}, nullptr);
    } else {
        renderMenu(renderer, {box.x + kPad, box.y + kPad, box.w - 2 * kPad, box.h - 2 * kPad}, heading);
    }
    if (!notice_.empty()) {
        ui::text(renderer, box.x + kPad + 8, box.y + box.h - kPad - 14,
                 fit(notice_, box.w - 2 * kPad - 8), noticeGood_ ? ui::color::good : ui::color::accent);
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

namespace {

// The colours of the map's sites.
constexpr ui::Color kTown{110, 160, 230};
constexpr ui::Color kSolved{96, 112, 128};
constexpr ui::Color kGoal{244, 211, 94};
constexpr ui::Color kPlain{78, 88, 100};

} // namespace

Viewer::MapHere Viewer::mapHere() const {
    const auto& wm = world_->map();
    // The map of the current zone: the one listing it, or the map with the
    // site the zone belongs to.
    MapHere here{&wm.world};
    for (const world::Map* m : {&wm.world, &wm.dagobah}) {
        for (int cy = 0; cy < m->height; ++cy) {
            for (int cx = 0; cx < m->width; ++cx) {
                const auto& zones = m->at(cx, cy).zones;
                if (std::find(zones.begin(), zones.end(), world_->zoneIndex()) != zones.end()) {
                    here = {m, cx, cy};
                }
            }
        }
    }
    return here;
}

void Viewer::renderMapCells(SDL_Renderer* renderer, const MapHere& here, float x, float y, float cell) const {
    const world::Map& map = *here.map;
    const bool blink = (SDL_GetTicks() / 400) % 2 == 0;
    const float gap = cell >= 12 ? 2.0f : 1.0f;
    const float radius = std::floor(cell / 4);
    for (int cy = 0; cy < map.height; ++cy) {
        for (int cx = 0; cx < map.width; ++cx) {
            const auto& site = map.at(cx, cy);
            const SDL_FRect box{x + static_cast<float>(cx) * cell, y + static_cast<float>(cy) * cell, cell - gap,
                                cell - gap};
            if (site.kind == world::SiteKind::None) {
                ui::fillRounded(renderer, box, radius, {255, 255, 255, 10});
                continue;
            }
            ui::Color c = kPlain;
            switch (site.kind) {
            case world::SiteKind::Town: c = kTown; break;
            case world::SiteKind::Goal: c = kGoal; break;
            case world::SiteKind::Puzzle: c = world_->goal(site.zone).solved ? kSolved : ui::color::accent; break;
            default: break;
            }
            ui::fillRounded(renderer, box, radius, c);
            if (cx == here.x && cy == here.y) {
                ui::outlineRounded(renderer, {box.x - 1, box.y - 1, box.w + 2, box.h + 2}, radius + 1,
                                   ui::color::text);
                if (blink) {
                    const float m = std::floor(cell / 3);
                    ui::fillRounded(renderer, {box.x + m - 1, box.y + m - 1, box.w - 2 * m + 2, box.h - 2 * m + 2},
                                    radius, ui::color::text);
                }
            }
        }
    }
}

void Viewer::renderMap(SDL_Renderer* renderer, const SDL_FRect& field) const {
    const MapHere here = mapHere();
    const world::Map& map = *here.map;
    ui::fill(renderer, field, {12, 14, 20, 235});
    ui::text(renderer, field.x + kPad + 2, field.y + kPad + 2, "Map", ui::color::accent, 1.4f);
    const float legendH = 14;
    const float area = std::min(field.w, field.h - 2 * legendH) - 4 * kPad;
    const float cell = std::floor(area / static_cast<float>(std::max(1, std::max(map.width, map.height))));
    const float ox = std::floor(field.x + (field.w - cell * static_cast<float>(map.width)) / 2);
    const float oy = std::floor(field.y + 2 * kPad + 8 + (area - cell * static_cast<float>(map.height)) / 2);
    renderMapCells(renderer, here, ox, oy, cell);
    // The legend.
    struct Entry {
        ui::Color c;
        const char* name;
    };
    const Entry entries[] = {{kTown, "Town"}, {ui::color::accent, "Puzzle"}, {kSolved, "Done"}, {kGoal, "Goal"}};
    float lx = field.x + kPad;
    const float ly = field.y + field.h - kPad - 8;
    for (const auto& e : entries) {
        ui::fillRounded(renderer, {lx, ly, 8, 8}, 2, e.c);
        ui::text(renderer, lx + 11, ly, e.name, ui::color::dim);
        lx += 11 + ui::textWidth(e.name) + 12;
    }
}

void Viewer::renderBubble(SDL_Renderer* renderer, const SDL_FRect& field, float cell, int camX, int camY) const {
    const world::Position speaker = bubble_->at.value_or(world_->player());
    constexpr float kLine = 11;
    const float width = field.w - 8;
    bubbleWidth_ = std::max(64.0f, width - 2 * kPad);
    bubbleLines_ = static_cast<std::size_t>(std::max(3.0f, (field.h * 0.6f) / kLine));
    const auto all = ui::wrap(bubble_->text, bubbleWidth_, 1, ui::Font::Story);
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
        ui::text(renderer, bx + kPad, by + kPad + static_cast<float>(i) * kLine, lines[i], ink, 1, ui::Font::Story);
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

std::string Viewer::controlName(Action action) const {
    if (!bindings_) {
        return "?";
    }
    const std::string names =
        device_ == InputDevice::Gamepad ? bindings_->buttonNames(action) : bindings_->keyNames(action);
    return names.substr(0, names.find(','));
}

std::string Viewer::hintText() const {
    const bool menu = mode_ == ViewMode::Title || page_ != Page::None;
    if (capture_) {
        return device_ == InputDevice::Gamepad ? "Press the new button" : "Press the new key\nEsc cancels";
    }
    const auto key = [&](Action a) { return controlName(a); };
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

std::string Viewer::howToPlayText() const {
    std::string text =
        "Explore, talk to everyone and pick up what you find. Most people want something before they help you: "
        "bring it and use it on them. The map shows the puzzles still open; solve them all to reach the final one.\n\n";
    const auto key = [&](Action a) { return controlName(a); };
    switch (device_) {
    case InputDevice::Touch:
        text += "Tap a place to walk there, or a person to talk. Tap an item to pick it, and again to use it. "
                "The pad and Hit button walk and attack.";
        break;
    case InputDevice::Gamepad:
        text += "Stick or D-pad: walk and push.\n" + key(Action::Use) + ": talk and use the item.\n" +
                key(Action::Attack) + ": attack.\n" + key(Action::InventoryPrev) + "/" + key(Action::InventoryNext) +
                ": choose an item.\n" + key(Action::Map) + ": map.  " + key(Action::Menu) + ": menu and save.";
        break;
    case InputDevice::Keyboard:
        text += "Arrows: walk and push, or click where to go.\n" + key(Action::Use) + ": talk and use the item.\n" +
                key(Action::Attack) + ": attack.\n" + key(Action::InventoryPrev) + "/" + key(Action::InventoryNext) +
                ": choose an item.\n" + key(Action::Map) + ": map.  " + key(Action::Menu) + ": menu and save.";
        break;
    }
    return text;
}

void Viewer::renderHints(SDL_Renderer* renderer, const SDL_FRect& area) const {
    const std::string hint = hintText();
    const auto lines = wrapText(hint, area.w);
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
            ui::fillRounded(renderer, rect, rect.h / 2, ui::color::panelLight);
            ui::fillRounded(renderer, {rect.x + 3, rect.y + 4, 3, rect.h - 8}, 1.5f, ui::color::accent);
        }
        const float ty = rect.y + (rect.h - ui::kFont) / 2;
        ui::text(renderer, rect.x + 8, ty, row.label, on ? ui::color::accent : ui::color::text);
        addHit(Hit::Kind::Row, rect, static_cast<int>(i));
        if (!row.value.empty()) {
            // Long values (a list of keys) are cut to the room left.
            // A save slot's date is not a value to step through.
            const bool arrows = on && row.id != RowId::Slot;
            const float room = rect.w - 6 - 8 - ui::textWidth(row.label) - ui::textWidth(arrows ? "<  >" : "  ") - ui::kFont;
            const std::string shown = fit(row.value, room);
            const std::string value = arrows ? "< " + shown + " >" : shown + "  ";
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
                          : page_ == Page::Slots    ? "Load game"
                          : page_ == Page::Bug      ? "Report a bug"
                                                    : nullptr;
    renderMenu(renderer, {x, y, w, rows * kRow}, heading);
    y += rows * kRow + (heading ? 18 : 0) + kPad;
    const auto paragraph = [&](const std::string& text, ui::Color color, std::size_t maxLines) {
        const auto lines = wrapText(text, w);
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
        paragraph(notice_, noticeGood_ ? ui::color::good : ui::color::danger, 4);
    }
    if (page_ == Page::Bug) {
        paragraph(bugText(), ui::color::dim, 12);
        return;
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
    const auto lines = wrapText(hint, w);
    float hy = panel.y + panel.h - kPad - static_cast<float>(lines.size()) * 10;
    for (const auto& l : lines) {
        ui::text(renderer, x, hy, l, ui::color::faint);
        hy += 10;
    }
}

namespace {

constexpr float kCardPad = 8;
constexpr ui::Color kWell{14, 16, 22}; // behind an item's picture

// A card of the play screen, `h` high at the top of what is left of a
// column; returns false (and draws nothing) when it does not fit.
bool card(SDL_Renderer* r, SDL_FRect& column, float h, SDL_FRect& out) {
    if (h > column.h) {
        return false;
    }
    out = {column.x, column.y, column.w, h};
    ui::panel(r, out);
    column.y += h + 6;
    column.h -= h + 6;
    return true;
}

// A small rounded label for a key or button, such as LB.
float keyLabel(SDL_Renderer* r, float x, float centerY, const std::string& name) {
    const float w = ui::textWidth(name) + 8;
    const SDL_FRect box{x, std::floor(centerY - 7), w, 14};
    ui::outlineRounded(r, box, 4, {74, 79, 92});
    ui::text(r, box.x + 4, box.y + 3, name, ui::color::text);
    return w;
}

} // namespace

void Viewer::renderHud(SDL_Renderer* renderer, const ui::Layout& layout) const {
    const auto& w = *world_;
    const bool indy = game().game == data::Layout::Indy;

    // --- the left column: health, weapon, buttons and hints ----------------
    SDL_FRect left = layout.left;
    SDL_FRect box{};
    const float ratio = static_cast<float>(w.health()) / static_cast<float>(world::kMaxHealth);
    if (card(renderer, left, 44, box)) {
        const float radius = 15;
        const float cx = box.x + kCardPad + radius;
        const float cy = box.y + box.h / 2;
        ui::ring(renderer, cx, cy, radius, 5, ratio, healthColor(ratio), {255, 255, 255, 30});
        const float tx = cx + radius + 8;
        ui::text(renderer, tx, box.y + 9, "Health", ui::color::dim);
        char percent[16];
        std::snprintf(percent, sizeof percent, "%d%%", static_cast<int>(std::ceil(ratio * 100)));
        ui::text(renderer, tx, box.y + 21, percent, ui::color::text, 1.5f);
    }
    if (card(renderer, left, 48, box)) {
        const SDL_FRect well{box.x + kCardPad, box.y + 6, 36, 36};
        ui::fillRounded(renderer, well, 6, kWell);
        if (w.weapon() != data::kNoTile) {
            renderTile(renderer, w.weapon(), {well.x + 2, well.y + 2, kTile, kTile});
        }
        const float tx = well.x + well.w + 8;
        const float tw = box.x + box.w - kCardPad - tx;
        ui::text(renderer, tx, box.y + 12, "Weapon", ui::color::dim);
        ui::text(renderer, tx, box.y + 25, fit(w.weapon() != data::kNoTile ? itemName(w.weapon()) : "None", tw),
                 ui::color::text);
    }
    // Map and Menu at the bottom of the column, hints above them.
    if (left.h >= kRow + 4) {
        const float y = left.y + left.h - kRow;
        const float half = std::floor((left.w - 6) / 2);
        const SDL_FRect mapButton{left.x, y, half, kRow};
        const SDL_FRect menuButton{left.x + left.w - half, y, half, kRow};
        const auto pill = [&](const SDL_FRect& r, const char* label) {
            ui::fillRounded(renderer, r, r.h / 2, ui::color::panelLight);
            ui::outlineRounded(renderer, r, r.h / 2, ui::color::border);
            ui::textCentered(renderer, r.x + r.w / 2, r.y + (r.h - ui::kFont) / 2, label, ui::color::text);
        };
        if (generated_) {
            pill(mapButton, "Map");
            addHit(Hit::Kind::Map, mapButton);
        }
        pill(menuButton, generated_ ? "Menu" : "Back");
        addHit(Hit::Kind::Menu, menuButton);
        left.h -= kRow + 6;
        if (left.h >= 40 && device_ != InputDevice::Touch) {
            renderHints(renderer, {left.x + 2, left.y, left.w - 2, left.h});
        }
    }

    // --- the right column: the map and how far the player is ---------------
    SDL_FRect right = layout.panel;
    if (generated_) {
        const MapHere here = mapHere();
        const int span = std::max(1, std::max(here.map->width, here.map->height));
        // Small maps (Dagobah) keep small cells, in the middle of the card.
        const float cell = std::min(14.0f, std::floor((right.w - 2 * kCardPad) / static_cast<float>(span)));
        const float mapH = cell * static_cast<float>(here.map->height);
        if (cell >= 4 && card(renderer, right, 24 + mapH + kCardPad, box)) {
            ui::text(renderer, box.x + kCardPad, box.y + 7, "Map", ui::color::text);
            if (device_ != InputDevice::Touch) {
                const std::string key = controlName(Action::Map);
                ui::textRight(renderer, box.x + box.w - kCardPad, box.y + 7, fit(key, box.w / 2), ui::color::faint);
            }
            const float mapW = cell * static_cast<float>(here.map->width);
            renderMapCells(renderer, here, std::floor(box.x + (box.w - mapW) / 2), box.y + 22, cell);
            addHit(Hit::Kind::Map, box);
        }
        int solved = 0;
        int total = 0;
        for (const auto& site : w.map().world.sites) {
            if (site.kind == world::SiteKind::Puzzle || site.kind == world::SiteKind::Goal) {
                ++total;
                solved += w.goal(site.zone).solved ? 1 : 0;
            }
        }
        if (card(renderer, right, 46, box)) {
            ui::text(renderer, box.x + kCardPad, box.y + 8, indy ? "Indiana Jones" : "Yoda Stories", ui::color::accent);
            char line[48];
            std::snprintf(line, sizeof line, "%d of %d puzzles", solved, total);
            ui::text(renderer, box.x + kCardPad, box.y + 20, line, ui::color::text);
            const SDL_FRect track{box.x + kCardPad, box.y + 34, box.w - 2 * kCardPad, 5};
            ui::fillRounded(renderer, track, 2.5f, {255, 255, 255, 26});
            if (total > 0 && solved > 0) {
                ui::fillRounded(renderer,
                                {track.x, track.y, std::max(5.0f, track.w * static_cast<float>(solved) / static_cast<float>(total)), track.h},
                                2.5f, ui::color::accent);
            }
        }
    } else if (card(renderer, right, 34, box)) {
        char line[48];
        std::snprintf(line, sizeof line, "Zone %d", w.zoneIndex());
        ui::text(renderer, box.x + kCardPad, box.y + 7, "Zone test", ui::color::accent);
        ui::text(renderer, box.x + kCardPad, box.y + 19, line, ui::color::dim);
    }
    // The selected item, with its name.
    const auto& inventory = w.inventory();
    if (card(renderer, right, 48, box)) {
        const SDL_FRect well{box.x + kCardPad, box.y + 6, 36, 36};
        ui::fillRounded(renderer, well, 6, kWell);
        const float tx = well.x + well.w + 8;
        const float tw = box.x + box.w - kCardPad - tx;
        ui::text(renderer, tx, box.y + 12, "In hand", ui::color::dim);
        if (inventory.empty()) {
            ui::text(renderer, tx, box.y + 25, fit("Nothing yet", tw), ui::color::faint);
        } else {
            const auto item = inventory[std::min(selected_, inventory.size() - 1)];
            renderTile(renderer, item, {well.x + 2, well.y + 2, kTile, kTile});
            const auto lines = ui::wrap(itemName(item), tw);
            ui::text(renderer, tx, box.y + 25, fit(lines.empty() ? "" : lines[0], tw), ui::color::text);
        }
    }

    // --- the item bar under the field --------------------------------------
    const SDL_FRect bar = layout.bar;
    if (bar.w <= 0) {
        return;
    }
    constexpr float kCell = 36; // an item's place in the bar
    constexpr float kGap = 4;
    const bool keys = device_ != InputDevice::Touch && !inventory.empty();
    const std::string prev = keys ? controlName(Action::InventoryPrev) : "";
    const std::string next = keys ? controlName(Action::InventoryNext) : "";
    const float labels = keys ? ui::textWidth(prev) + ui::textWidth(next) + 2 * 8 + 2 * 6 : 0;
    const int fits = std::max(1, static_cast<int>((bar.w - labels - 2 * 6 + kGap) / (kCell + kGap)));
    const int count = static_cast<int>(inventory.size());
    const int shown = std::max(1, std::min(fits, count));
    // Scroll to keep the selected item in sight.
    const int first = std::clamp(static_cast<int>(selected_) - shown / 2, 0, std::max(0, count - shown));
    const float inner = static_cast<float>(shown) * (kCell + kGap) - kGap;
    const SDL_FRect strip{std::floor(bar.x + (bar.w - inner - 12) / 2), bar.y + std::floor((bar.h - kCell - 12) / 2),
                          inner + 12, kCell + 12};
    ui::panel(renderer, strip);
    for (int i = 0; i < shown; ++i) {
        const int index = first + i;
        const SDL_FRect slot{strip.x + 6 + static_cast<float>(i) * (kCell + kGap), strip.y + 6, kCell, kCell};
        const bool on = index == static_cast<int>(selected_) && index < count;
        ui::fillRounded(renderer, slot, 7, on ? ui::Color{255, 255, 255, 36} : ui::Color{255, 255, 255, 12});
        if (index >= count) {
            continue;
        }
        renderTile(renderer, inventory[static_cast<std::size_t>(index)], {slot.x + 2, slot.y + 2, kTile, kTile});
        if (on) {
            ui::outlineRounded(renderer, slot, 7, ui::color::accent);
            ui::outlineRounded(renderer, {slot.x + 1, slot.y + 1, slot.w - 2, slot.h - 2}, 6, ui::color::accent);
        }
        addHit(Hit::Kind::Slot, slot, index);
    }
    if (keys) {
        const float cy = strip.y + strip.h / 2;
        keyLabel(renderer, strip.x - 6 - ui::textWidth(prev) - 8, cy, prev);
        keyLabel(renderer, strip.x + strip.w + 6, cy, next);
    }
    if (first > 0 || first + shown < count) {
        char more[24];
        std::snprintf(more, sizeof more, "%zu / %d", selected_ + 1, count);
        ui::textRight(renderer, bar.x + bar.w, bar.y + bar.h - ui::kFont, more, ui::color::faint);
    }
}

} // namespace das::app
