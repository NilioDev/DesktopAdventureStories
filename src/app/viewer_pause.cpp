// SPDX-License-Identifier: GPL-3.0-or-later
//
// The pause screen of a generated game: tabs for the menu, the map and the
// backpack, switched with the item keys or buttons (LB/RB) or a press on a
// tab. The game waits under it.
#include "viewer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace das::app {

namespace {

constexpr float kMargin = 8;
constexpr float kTabH = 22;
constexpr float kCardPad = 12;
constexpr float kTile = 32;
constexpr ui::Color kWell{0, 0, 0, 90};

const char* tabName(int tab) { return tab == 0 ? "Menu" : tab == 1 ? "Map" : "Backpack"; }

const char* worldSizeName(world::WorldSize size) {
    switch (size) {
    case world::WorldSize::Small: return "Small";
    case world::WorldSize::Medium: return "Medium";
    case world::WorldSize::Large: return "Large";
    }
    return "";
}

} // namespace

std::vector<Viewer::PauseTab> Viewer::pauseTabs() const {
    return {PauseTab::Menu, PauseTab::Map, PauseTab::Backpack};
}

void Viewer::switchTab(int change) {
    const auto tabs = pauseTabs();
    const auto it = std::find(tabs.begin(), tabs.end(), pauseTab_);
    const int at = it == tabs.end() ? 0 : static_cast<int>(it - tabs.begin());
    const int count = static_cast<int>(tabs.size());
    pauseTab_ = tabs[static_cast<std::size_t>(((at + change) % count + count) % count)];
    backpackCursor_ = selected_;
    menuItem_ = 0;
    notice_.clear();
}

void Viewer::updatePauseTab(const ActionState& actions, std::optional<Step> step) {
    if (actions.wasPressed(Action::Menu) || actions.wasPressed(Action::Attack)) {
        page_ = Page::None; // back to the game
        return;
    }
    if (actions.wasPressed(Action::Map)) {
        if (pauseTab_ == PauseTab::Map) {
            page_ = Page::None;
        } else {
            openMap();
        }
        return;
    }
    if (pauseTab_ == PauseTab::Map) {
        if (actions.wasPressed(Action::Use)) {
            page_ = Page::None;
        }
        return;
    }
    // The backpack: move through the items, Use takes one in hand.
    const auto count = static_cast<int>(world_->inventory().size());
    if (count == 0) {
        if (actions.wasPressed(Action::Use)) {
            page_ = Page::None;
        }
        return;
    }
    int cursor = std::clamp(static_cast<int>(backpackCursor_), 0, count - 1);
    if (step) {
        cursor += step->dx + step->dy * std::max(1, backpackColumns_);
        cursor = std::clamp(cursor, 0, count - 1);
    }
    backpackCursor_ = static_cast<std::size_t>(cursor);
    if (actions.wasPressed(Action::Use)) {
        selected_ = backpackCursor_;
        page_ = Page::None;
    }
}

void Viewer::renderPause(SDL_Renderer* renderer, const ui::Layout& layout) const {
    const float w = layout.width;
    const float h = layout.height;
    const bool keys = device_ != InputDevice::Touch;

    // The map and the backpack hide the game: they are full pages.
    if (pauseTab_ != PauseTab::Menu) {
        const auto bg = ui::color::background;
        ui::fill(renderer, {0, 0, w, h}, {bg.r, bg.g, bg.b, 245});
    }

    // The tabs, in the middle at the top.
    const auto tabs = pauseTabs();
    float tabsW = 6;
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        tabsW += ui::textWidth(tabName(static_cast<int>(tabs[i]))) + 24 + 4;
    }
    tabsW += 2;
    const SDL_FRect strip{std::floor((w - tabsW) / 2), kMargin, tabsW, kTabH + 8};
    ui::fillRounded(renderer, strip, strip.h / 2, ui::color::panel);
    ui::outlineRounded(renderer, strip, strip.h / 2, ui::color::border);
    float tx = strip.x + 4;
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        const char* name = tabName(static_cast<int>(tabs[i]));
        const SDL_FRect tab{tx, strip.y + 4, ui::textWidth(name) + 24, kTabH};
        const bool on = tabs[i] == pauseTab_;
        if (on) {
            ui::fillRounded(renderer, tab, kTabH / 2, ui::color::accent);
        }
        ui::textCentered(renderer, tab.x + tab.w / 2, tab.y + (tab.h - ui::kFont) / 2, name,
                         on ? ui::color::accentInk : ui::color::text);
        addHit(Hit::Kind::Tab, tab, static_cast<int>(i));
        tx += tab.w + 4;
    }
    if (keys) {
        const auto label = [&](float x, const std::string& name) {
            const SDL_FRect box{x, strip.y + (strip.h - 14) / 2, ui::textWidth(name) + 8, 14};
            ui::outlineRounded(renderer, box, 4, ui::color::border);
            ui::text(renderer, box.x + 4, box.y + 3, name, ui::color::text);
        };
        const std::string prev = controlName(Action::InventoryPrev);
        const std::string next = controlName(Action::InventoryNext);
        label(strip.x - ui::textWidth(prev) - 8 - 6, prev);
        label(strip.x + strip.w + 6, next);
    }

    // What the buttons do here, along the bottom.
    std::string hint;
    const std::string use = controlName(Action::Use);
    const std::string back = controlName(device_ == InputDevice::Gamepad ? Action::Attack : Action::Menu);
    if (pauseTab_ == PauseTab::Map) {
        hint = keys ? use + " or " + back + ": back to the game" : "Tap Menu, then Resume, to go back";
    } else if (pauseTab_ == PauseTab::Backpack) {
        hint = keys ? "Arrows: choose    " + use + ": take in hand    " + back + ": back to the game"
                    : "Tap an item, and again to take it in hand";
    }
    const float hintY = h - kMargin - ui::kFont;
    if (!hint.empty()) {
        ui::textCentered(renderer, w / 2, hintY, ui::fit(hint, w - 2 * kMargin), ui::color::dim);
    }

    const float top = strip.y + strip.h + kMargin + 4;
    const float areaW = std::min(w - 4 * kMargin, 640.0f);
    const SDL_FRect area{std::floor((w - areaW) / 2), top, areaW, hintY - kMargin - top};
    switch (pauseTab_) {
    case PauseTab::Menu: renderMenuCard(renderer, area); break;
    case PauseTab::Map: renderMapTab(renderer, area); break;
    case PauseTab::Backpack: renderBackpack(renderer, area); break;
    }
}

void Viewer::renderMapTab(SDL_Renderer* renderer, const SDL_FRect& area) const {
    const auto& w = *world_;
    const bool tall = area.h > area.w;
    const MapHere here = mapHere();
    const int span = std::max(1, std::max(here.map->width, here.map->height));

    // The map, as large as it fits.
    const float side = std::floor(tall ? std::min(area.w, area.h * 0.6f) : std::min(area.h, area.w * 0.58f));
    const SDL_FRect mapCard{tall ? std::floor(area.x + (area.w - side) / 2) : area.x, area.y, side, side};
    ui::panel(renderer, mapCard);
    ui::text(renderer, mapCard.x + kCardPad, mapCard.y + 10, "Map", ui::color::text, 1.3f);
    const float room = side - 2 * kCardPad - 18;
    const float cell = std::min(40.0f, std::floor(room / static_cast<float>(span)));
    const float gridW = cell * static_cast<float>(here.map->width);
    const float gridH = cell * static_cast<float>(here.map->height);
    renderMapCells(renderer, here, std::floor(mapCard.x + (side - gridW) / 2),
                   std::floor(mapCard.y + 18 + kCardPad + (room - gridH) / 2), cell);

    // How far the player is, and what the colours mean.
    SDL_FRect info = tall ? SDL_FRect{area.x, mapCard.y + side + kMargin, area.w, area.y + area.h - side - kMargin - area.y}
                          : SDL_FRect{mapCard.x + side + 2 * kMargin, area.y, area.w - side - 2 * kMargin, area.h};
    if (info.w < 120 || info.h < 60) {
        return;
    }
    int solved = 0;
    int total = 0;
    for (const auto& site : w.map().world.sites) {
        if (world::isChainPuzzle(site)) {
            ++total;
            solved += w.goal(site.zone).solved ? 1 : 0;
        }
    }
    float y = info.y + 4;
    const float x = info.x + 4;
    const float width = info.w - 8;
    ui::text(renderer, x, y, "YOUR ADVENTURE", ui::color::accent);
    y += 14;
    const auto title = ui::wrap("Solve every puzzle to reach the final goal", width, 1.6f);
    for (std::size_t i = 0; i < title.size() && i < 3; ++i) {
        ui::text(renderer, x, y, title[i], ui::color::text, 1.6f);
        y += 17;
    }
    y += 8;

    // Three numbers in small cards.
    const float statW = std::floor((info.w - 2 * 6) / 3);
    const auto stat = [&](int i, const char* label, const std::string& value) {
        const SDL_FRect r{info.x + static_cast<float>(i) * (statW + 6), y, statW, 38};
        ui::panel(renderer, r);
        ui::text(renderer, r.x + 8, r.y + 7, ui::fit(label, r.w - 16), ui::color::dim);
        ui::text(renderer, r.x + 8, r.y + 19, ui::fit(value, r.w - 16), ui::color::text, 1.3f);
    };
    char puzzles[32];
    std::snprintf(puzzles, sizeof puzzles, "%d / %d", solved, total);
    stat(0, "Puzzles", puzzles);
    stat(1, "World", worldSizeName(options_.size));
    stat(2, "Difficulty", difficulty_ == 0 ? "Easy" : difficulty_ == 2 ? "Hard" : "Normal");
    y += 38 + 10;
    const SDL_FRect track{info.x, y, info.w, 6};
    ui::fillRounded(renderer, track, 3, {255, 255, 255, 26});
    if (total > 0 && solved > 0) {
        ui::fillRounded(renderer,
                        {track.x, track.y, std::max(6.0f, track.w * static_cast<float>(solved) / static_cast<float>(total)),
                         track.h},
                        3, ui::color::accent);
    }
    y += 18;

    // The legend.
    struct Entry {
        ui::Color c;
        const char* name;
    };
    const Entry entries[] = {{ui::color::accent, "Open puzzle"}, {mapcolor::kSolved, "Solved"},
                             {mapcolor::kGoal, "Final goal"},    {mapcolor::kTown, "Town"},
                             {mapcolor::kPlain, "Other places"}, {ui::color::text, "You are here"}};
    float lx = x;
    for (const auto& e : entries) {
        const float ew = 12 + ui::textWidth(e.name) + 14;
        if (lx + ew > info.x + info.w) {
            lx = x;
            y += 14;
        }
        if (y + 10 > info.y + info.h) {
            break;
        }
        const SDL_FRect swatch{lx, y, 9, 9};
        if (&e == &entries[std::size(entries) - 1]) {
            // The hero's cell: an outline with a dot, like on the map.
            ui::outlineRounded(renderer, swatch, 2, ui::color::text);
            ui::fillRounded(renderer, {lx + 3, y + 3, 3, 3}, 1, ui::color::text);
        } else {
            ui::fillRounded(renderer, swatch, 2, e.c);
        }
        if (&e == &entries[1]) {
            drawMapCheck(renderer, swatch);
        }
        ui::text(renderer, lx + 13, y + 1, e.name, ui::color::dim);
        lx += ew;
    }
}

void Viewer::renderBackpack(SDL_Renderer* renderer, const SDL_FRect& area) const {
    const auto& inventory = world_->inventory();
    const bool tall = area.h > area.w;
    constexpr float kSlot = 40;
    constexpr float kGap = 6;

    // The items in a grid.
    const SDL_FRect grid = tall ? SDL_FRect{area.x, area.y, area.w, std::floor(area.h * 0.55f)}
                                : SDL_FRect{area.x, area.y, std::floor(area.w * 0.58f), area.h};
    ui::panel(renderer, grid);
    ui::text(renderer, grid.x + kCardPad, grid.y + 10, "Backpack", ui::color::text, 1.3f);
    char count[32];
    std::snprintf(count, sizeof count, "%zu %s", inventory.size(), inventory.size() == 1 ? "item" : "items");
    ui::textRight(renderer, grid.x + grid.w - kCardPad, grid.y + 12, count, ui::color::dim);
    const float top = grid.y + 30;
    const int columns = std::max(1, static_cast<int>((grid.w - 2 * kCardPad + kGap) / (kSlot + kGap)));
    const int rows = std::max(1, static_cast<int>((grid.y + grid.h - kCardPad - top + kGap) / (kSlot + kGap)));
    backpackColumns_ = columns;
    const int itemCount = static_cast<int>(inventory.size());
    const int cursor = std::clamp(static_cast<int>(backpackCursor_), 0, std::max(0, itemCount - 1));
    const int totalRows = std::max(1, (itemCount + columns - 1) / columns);
    const int firstRow = std::clamp(cursor / columns - rows + 1, 0, std::max(0, totalRows - rows));
    const float gx = std::floor(grid.x + (grid.w - static_cast<float>(columns) * (kSlot + kGap) + kGap) / 2);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            const int index = (firstRow + r) * columns + c;
            const SDL_FRect slot{gx + static_cast<float>(c) * (kSlot + kGap), top + static_cast<float>(r) * (kSlot + kGap),
                                 kSlot, kSlot};
            const bool used = index < itemCount;
            const bool on = used && index == cursor;
            ui::fillRounded(renderer, slot, 8,
                            on ? ui::Color{255, 255, 255, 36} : used ? ui::Color{255, 255, 255, 16} : ui::Color{255, 255, 255, 6});
            if (!used) {
                continue;
            }
            renderTile(renderer, inventory[static_cast<std::size_t>(index)], {slot.x + 4, slot.y + 4, kTile, kTile});
            if (on) {
                ui::outlineRounded(renderer, slot, 8, ui::color::accent);
                ui::outlineRounded(renderer, {slot.x + 1, slot.y + 1, slot.w - 2, slot.h - 2}, 7, ui::color::accent);
            }
            if (static_cast<std::size_t>(index) == selected_) {
                ui::fillRounded(renderer, {slot.x + slot.w - 9, slot.y + 3, 6, 6}, 3, ui::color::text); // in hand
            }
            addHit(Hit::Kind::Slot, slot, index);
        }
    }

    // The chosen item, large, with its name.
    const SDL_FRect info = tall ? SDL_FRect{area.x, grid.y + grid.h + kMargin, area.w, area.h - grid.h - kMargin}
                                : SDL_FRect{grid.x + grid.w + 2 * kMargin, area.y, area.w - grid.w - 2 * kMargin, area.h};
    if (info.w < 100 || info.h < 60) {
        return;
    }
    if (inventory.empty()) {
        ui::text(renderer, info.x + 4, info.y + 4, "Nothing carried yet", ui::color::dim, 1.3f);
        const auto lines = ui::wrap("Pick up what you find and it shows up here.", info.w - 8);
        float y = info.y + 24;
        for (const auto& line : lines) {
            ui::text(renderer, info.x + 4, y, line, ui::color::faint);
            y += 11;
        }
        return;
    }
    const auto item = inventory[static_cast<std::size_t>(cursor)];
    const float big = tall ? 56.0f : 76.0f;
    const SDL_FRect well{info.x + 4, info.y + 4, big, big};
    ui::fillRounded(renderer, well, 12, kWell);
    ui::outlineRounded(renderer, well, 12, ui::color::border);
    const float tile = big >= 72 ? 2 * kTile : kTile + 16;
    renderTile(renderer, item, {well.x + (big - tile) / 2, well.y + (big - tile) / 2, tile, tile});
    float y = well.y + big + 10;
    if (static_cast<std::size_t>(cursor) == selected_) {
        ui::text(renderer, info.x + 4, y, "IN HAND", ui::color::accent);
        y += 13;
    } else if (world_->isWeapon(item)) {
        ui::text(renderer, info.x + 4, y, "WEAPON", ui::color::accent);
        y += 13;
    }
    const auto name = ui::wrap(itemName(item), info.w - 8, 1.6f);
    for (std::size_t i = 0; i < name.size() && i < 2; ++i) {
        ui::text(renderer, info.x + 4, y, name[i], ui::color::text, 1.6f);
        y += 17;
    }
    y += 6;
    const char* what = world_->isWeapon(item)
                           ? "Take it in hand and use it on nothing to fight with it."
                           : "Take it in hand, then use it on someone or something in front of you.";
    const auto lines = ui::wrap(what, info.w - 8);
    for (const auto& line : lines) {
        if (y + 10 > info.y + info.h) {
            break;
        }
        ui::text(renderer, info.x + 4, y, line, ui::color::dim);
        y += 11;
    }
}

} // namespace das::app
