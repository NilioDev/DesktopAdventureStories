// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game picker: the first screen, with a card for each game. A card shows
// the game's own title screen from the player's files, when the game was
// last saved, and buttons to continue or to start. Indy's card comes first.
#include "viewer.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace das::app {

namespace {

#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
constexpr bool kMobile = true;
#else
constexpr bool kMobile = false;
#endif

constexpr int kCards = 2;
constexpr int kCardHit = 9; // a press on a card, not on one of its buttons
constexpr float kButtonH = 20;
constexpr float kRadius = 12;

data::Layout layoutOfCard(int card) { return card == 0 ? data::Layout::Indy : data::Layout::Yoda; }
const char* idOfCard(int card) { return card == 0 ? "indy" : "yoda"; }

// The colours of a card, after its game.
struct CardColors {
    ui::Color accent;
    ui::Color ink;
    ui::Color fill;
    ui::Color line;
    ui::Color chip;
    ui::Color sub;
};

CardColors cardColors(int card) {
    if (card == 0) {
        return {ui::kIndyAccent, ui::kIndyInk, {27, 20, 13}, {64, 50, 36}, {46, 35, 23}, {201, 180, 154}};
    }
    return {ui::kYodaAccent, ui::kYodaInk, {14, 28, 31}, {38, 64, 68}, {27, 50, 55}, {169, 196, 182}};
}

SDL_FRect grow(const SDL_FRect& r, float by) { return {r.x - by, r.y - by, r.w + 2 * by, r.h + 2 * by}; }

} // namespace

int Viewer::gameOfCard(int card) const {
    for (std::size_t i = 0; i < games_.size(); ++i) {
        if (games_[i]->game == layoutOfCard(card)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Viewer::pickCard() const {
    if (pickCard_ >= 0) {
        return pickCard_;
    }
    return games_.empty() || game().game == data::Layout::Indy ? 0 : 1;
}

std::vector<Viewer::PickButton> Viewer::pickButtons(int card) const {
    using What = PickButton::What;
    std::vector<PickButton> buttons;
    if (card == kPickTop) {
        buttons.push_back({What::AddGames, "Add games"});
        buttons.push_back({What::Options, "Options"});
        if (!kMobile) {
            buttons.push_back({What::Quit, "Quit"});
        }
        return buttons;
    }
    if (gameOfCard(card) < 0) {
        buttons.push_back({What::AddGames, "Add game files", true});
    } else if (newestSlotFor(idOfCard(card)) >= 0) {
        buttons.push_back({What::Continue, "Continue", true});
        buttons.push_back({What::Play, "New adventure"});
    } else {
        buttons.push_back({What::Play, "Start adventure", true});
    }
    return buttons;
}

void Viewer::updatePicker(const ActionState& actions, std::optional<Step> step) {
    const int card = pickCard();
    const int count = static_cast<int>(pickButtons(pickRow_ == kPickTop ? kPickTop : card).size());
    pickButton_ = std::clamp(pickButton_, 0, std::max(0, count - 1));
    if ((actions.wasPressed(Action::Menu) || actions.wasPressed(Action::Attack)) && pickRow_ == kPickTop) {
        pickRow_ = 0;
        pickButton_ = 0;
        return;
    }
    if (step && step->dx != 0) {
        if (pickRow_ == kPickTop) {
            pickButton_ = std::clamp(pickButton_ + step->dx, 0, count - 1);
        } else {
            pickActivate(std::clamp(card + step->dx, 0, kCards - 1), kCardHit);
        }
        return;
    }
    if (step && step->dy != 0) {
        if (pickRow_ == kPickTop) {
            if (step->dy > 0) {
                pickRow_ = 0;
                pickButton_ = 0;
            }
        } else if (step->dy < 0 && pickButton_ == 0) {
            pickRow_ = kPickTop;
            pickButton_ = 0;
        } else {
            pickButton_ = std::clamp(pickButton_ + step->dy, 0, count - 1);
        }
        return;
    }
    if (actions.wasPressed(Action::Use)) {
        pickActivate(pickRow_ == kPickTop ? kPickTop : card, pickButton_);
    }
}

void Viewer::pickActivate(int card, int button) {
    using What = PickButton::What;
    notice_.clear();
    noticeGood_ = false;
    if (card != kPickTop) {
        pickCard_ = card;
        pickRow_ = 0;
        const int index = gameOfCard(card);
        if (index >= 0) {
            current_ = static_cast<std::size_t>(index); // its song, its colours
        }
        if (button == kCardHit) {
            pickButton_ = 0;
            return;
        }
    } else {
        pickRow_ = kPickTop;
    }
    const auto buttons = pickButtons(card);
    if (button < 0 || button >= static_cast<int>(buttons.size())) {
        return;
    }
    pickButton_ = button;
    switch (buttons[static_cast<std::size_t>(button)].what) {
    case What::Continue:
        if (!continueGame()) {
            page_ = Page::Main;
            menuItem_ = 0;
            if (notice_.empty()) {
                notice_ = "This save could not be read";
            }
        }
        break;
    case What::Play: {
        page_ = Page::Main;
        const auto rows = menuRows();
        const auto it = std::find_if(rows.begin(), rows.end(), [](const Row& r) { return r.id == RowId::NewGame; });
        menuItem_ = it == rows.end() ? 0 : static_cast<int>(it - rows.begin());
        break;
    }
    case What::AddGames: addGames_ = true; break;
    case What::Options:
        optionsFrom_ = Page::Pick;
        page_ = Page::Options;
        menuItem_ = 0;
        break;
    case What::Quit: quit_ = true; break;
    }
}

void Viewer::renderPicker(SDL_Renderer* renderer, const ui::Layout& layout) const {
    const float w = layout.width;
    const float h = layout.height;
    const float m = std::min(w, h) < 360 ? 8.0f : 14.0f;
    const bool showFocus = device_ != InputDevice::Touch;
    const int chosen = pickCard();

    // The name, and the buttons that are not about one game.
    float y = m;
    ui::text(renderer, m, y + 4, "Desktop Adventure Stories", ui::color::text, 1.3f);
    {
        const auto buttons = pickButtons(kPickTop);
        float right = w - m;
        for (int i = static_cast<int>(buttons.size()) - 1; i >= 0; --i) {
            const auto& b = buttons[static_cast<std::size_t>(i)];
            const float bw = ui::textWidth(b.label) + 20;
            const SDL_FRect r{right - bw, y, bw, kButtonH};
            ui::fillRounded(renderer, r, kButtonH / 2, {23, 26, 34});
            ui::outlineRounded(renderer, r, kButtonH / 2, {58, 63, 76});
            if (showFocus && pickRow_ == kPickTop && pickButton_ == i) {
                ui::outlineRounded(renderer, grow(r, 2), kButtonH / 2 + 2, ui::color::accent);
            }
            ui::textCentered(renderer, r.x + r.w / 2, r.y + (r.h - ui::kFont) / 2, b.label, ui::color::text);
            addHit(Hit::Kind::Pick, r, (kPickTop + 1) * 10 + i);
            right -= bw + 6;
        }
    }
    y += kButtonH + m;
    if (h >= 300) {
        ui::text(renderer, m, y, "Which adventure today?", ui::color::text, 2);
        y += 28;
    }

    // The hints along the bottom.
    const float hintY = h - m - ui::kFont;
    std::string hint;
    switch (device_) {
    case InputDevice::Touch: hint = "Tap a game, then Continue or Start"; break;
    case InputDevice::Gamepad:
        hint = "D-pad: choose    " + controlName(Action::Use) + ": select    Up: more";
        break;
    case InputDevice::Keyboard:
        hint = "Arrows: choose    " + controlName(Action::Use) + ": select    Up: more";
        break;
    }
    ui::text(renderer, m, hintY, ui::fit(hint, w - 2 * m), ui::color::dim);

    // The cards: side by side, or above each other on a tall screen.
    const float top = y;
    const float bottom = hintY - m;
    const bool stacked = layout.portrait;
    for (int card = 0; card < kCards; ++card) {
        SDL_FRect r{};
        if (stacked) {
            const float ch = std::floor((bottom - top - m) / 2);
            r = {m, top + static_cast<float>(card) * (ch + m), w - 2 * m, ch};
        } else {
            const float cw = std::floor((w - 3 * m) / 2);
            r = {m + static_cast<float>(card) * (cw + m), top, cw, bottom - top};
        }
        const bool on = card == chosen;
        const CardColors c = cardColors(card);
        const int index = gameOfCard(card);
        if (on) {
            ui::fillRounded(renderer, grow(r, 4), kRadius + 4, {c.accent.r, c.accent.g, c.accent.b, 40});
        }
        ui::fillRounded(renderer, r, kRadius, c.fill);
        ui::outlineRounded(renderer, r, kRadius, on ? c.accent : c.line);
        if (on) {
            ui::outlineRounded(renderer, grow(r, -1), kRadius - 1, c.accent);
        }
        addHit(Hit::Kind::Pick, r, (card + 1) * 10 + kCardHit);

        // The game's own title screen, cut to the shape of the room.
        const bool wide = r.w > r.h * 1.4f; // a short, wide card: the picture at the left
        SDL_FRect art{};
        if (wide) {
            art = {r.x + 6, r.y + 6, r.h - 12, r.h - 12};
        } else {
            art = {r.x + 6, r.y + 6, r.w - 12, std::floor(std::min((r.w - 12) * 0.7f, r.h * 0.5f))};
        }
        const SDL_Texture* setup = index >= 0 ? games_[static_cast<std::size_t>(index)]->setup : nullptr;
        if (setup) {
            // All of it, on black: the game's name is part of the picture.
            float tw = 0;
            float th = 0;
            SDL_GetTextureSize(const_cast<SDL_Texture*>(setup), &tw, &th);
            ui::fillRounded(renderer, art, kRadius - 4, {0, 0, 0, 255});
            const float scale = std::min(art.w / tw, art.h / th);
            const SDL_FRect dst{std::floor(art.x + (art.w - tw * scale) / 2), art.y, std::floor(tw * scale),
                                std::floor(th * scale)};
            SDL_RenderTexture(renderer, const_cast<SDL_Texture*>(setup), nullptr, &dst);
        } else {
            ui::fillRounded(renderer, art, kRadius - 4, {0, 0, 0, 70});
            const char* missing = index >= 0 ? "No title screen" : "Not added yet";
            ui::textCentered(renderer, art.x + art.w / 2, art.y + (art.h - ui::kFont) / 2, missing, c.sub);
        }

        // The name, the year and when it was last played.
        const float tx = wide ? art.x + art.w + 10 : r.x + 12;
        const float tw = r.x + r.w - 12 - tx;
        float ty = wide ? r.y + 10 : art.y + art.h + 10;
        const float nameSize = r.w < 220 ? 1.3f : 1.6f;
        std::string name = card == 0 ? "Indiana Jones and his Desktop Adventures" : "Yoda Stories";
        std::string year = card == 0 ? "1996" : "Star Wars: Yoda Stories, 1997";
        auto lines = ui::wrap(name, tw, nameSize);
        if (lines.size() > 2) {
            // Too long for this card: the rest of the name goes under it.
            lines = ui::wrap("Indiana Jones", tw, nameSize);
            year = "Desktop Adventures, 1996";
        }
        for (std::size_t i = 0; i < lines.size() && i < 2; ++i) {
            ui::text(renderer, tx, ty, lines[i], ui::color::text, nameSize);
            ty += ui::kFont * nameSize + 4;
        }
        ui::text(renderer, tx, ty, ui::fit(year, tw), c.sub);
        ty += 14;
        std::string chip = "Not added yet";
        if (index >= 0) {
            const std::string when = savedWhen(idOfCard(card));
            chip = when.empty() ? "No save yet" : "Saved " + when;
        }
        chip = ui::fit(chip, tw - 12);
        const SDL_FRect chipRect{tx, ty, ui::textWidth(chip) + 12, 14};
        ui::fillRounded(renderer, chipRect, 7, c.chip);
        ui::text(renderer, chipRect.x + 6, chipRect.y + 3, chip, ui::color::text);

        // The buttons, at the bottom of the card.
        const auto buttons = pickButtons(card);
        float by = r.y + r.h - 10 - static_cast<float>(buttons.size()) * (kButtonH + 6) + 6;
        by = std::max(by, chipRect.y + chipRect.h + 8);
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const auto& b = buttons[i];
            const SDL_FRect br{tx, by, std::min(tw, std::max(120.0f, ui::textWidth(b.label) + 28)), kButtonH};
            if (b.primary) {
                ui::fillRounded(renderer, br, kButtonH / 2, c.accent);
            } else {
                ui::outlineRounded(renderer, br, kButtonH / 2, c.line);
            }
            if (showFocus && on && pickRow_ == 0 && pickButton_ == static_cast<int>(i)) {
                ui::outlineRounded(renderer, grow(br, 2), kButtonH / 2 + 2, ui::color::text);
            }
            ui::textCentered(renderer, br.x + br.w / 2, br.y + (br.h - ui::kFont) / 2, b.label,
                             b.primary ? c.ink : ui::color::text);
            addHit(Hit::Kind::Pick, br, (card + 1) * 10 + static_cast<int>(i));
            by += kButtonH + 6;
        }
    }

    if (!notice_.empty()) {
        ui::textRight(renderer, w - m, hintY, ui::fit(notice_, w / 2), noticeGood_ ? ui::color::good : ui::color::danger);
    }
}

} // namespace das::app
