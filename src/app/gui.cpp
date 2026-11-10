// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui.hpp"

#include <algorithm>
#include <cmath>

namespace das::app::gui {

namespace {

constexpr ui::Color kField{16, 18, 24};
constexpr ui::Color kHover{48, 53, 68};
constexpr ui::Color kSelected{70, 62, 34};

SDL_Rect pixelRect(const SDL_FRect& r) {
    return {static_cast<int>(std::floor(r.x)), static_cast<int>(std::floor(r.y)), static_cast<int>(std::ceil(r.w)),
            static_cast<int>(std::ceil(r.h))};
}

} // namespace

std::string fitText(std::string_view text, float width) {
    const auto chars = static_cast<std::size_t>(std::max(0.0f, width / ui::kFont));
    if (text.size() <= chars) {
        return std::string(text);
    }
    std::size_t cut = chars > 2 ? chars - 2 : chars;
    // Not in the middle of a UTF-8 letter.
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xc0) == 0x80) {
        --cut;
    }
    return std::string(text.substr(0, cut)) + (chars > 2 ? ".." : "");
}

std::vector<std::string> wrap(std::string_view text, std::size_t columns) {
    std::vector<std::string> lines;
    columns = std::max<std::size_t>(columns, 4);
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        std::string_view paragraph =
            text.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        std::string line;
        while (!paragraph.empty()) {
            std::size_t cut = paragraph.find(' ');
            std::string_view word = paragraph.substr(0, cut);
            while (word.size() > columns) { // a very long word is broken
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                }
                lines.emplace_back(word.substr(0, columns));
                word.remove_prefix(columns);
            }
            if (!line.empty() && line.size() + 1 + word.size() > columns) {
                lines.push_back(line);
                line.clear();
            }
            if (!word.empty()) {
                line += (line.empty() ? "" : " ") + std::string(word);
            }
            paragraph = cut == std::string_view::npos ? std::string_view{} : paragraph.substr(cut + 1);
        }
        lines.push_back(line);
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    return lines;
}

// --- input -------------------------------------------------------------------

void InputCollector::event(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
        input_.mouseX = e.motion.x;
        input_.mouseY = e.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        input_.mouseX = e.button.x;
        input_.mouseY = e.button.y;
        if (e.button.button == SDL_BUTTON_LEFT) {
            input_.mouseDown = true;
            input_.mousePressed = true;
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
            input_.rightPressed = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) {
            input_.mouseDown = false;
            input_.mouseReleased = true;
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL: input_.wheel += e.wheel.y; break;
    case SDL_EVENT_TEXT_INPUT: input_.typed += e.text.text; break;
    case SDL_EVENT_KEY_DOWN: {
        const SDL_Keycode k = e.key.key;
        input_.ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
        input_.shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
        if (k == SDLK_BACKSPACE) input_.backspace = true;
        if (k == SDLK_DELETE) input_.deleteKey = true;
        if (k == SDLK_RETURN || k == SDLK_KP_ENTER) input_.enter = true;
        if (k == SDLK_ESCAPE) input_.escape = true;
        if (k == SDLK_LEFT) input_.left = true;
        if (k == SDLK_RIGHT) input_.right = true;
        if (k == SDLK_HOME) input_.home = true;
        if (k == SDLK_END) input_.end = true;
        if (k == SDLK_TAB) input_.navY += input_.shift ? -1 : 1;
        if (input_.ctrl && k >= SDLK_A && k <= SDLK_Z) {
            input_.shortcut = static_cast<char>('a' + (k - SDLK_A));
        }
        break;
    }
    case SDL_EVENT_KEY_UP:
        input_.ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
        input_.shift = (e.key.mod & SDL_KMOD_SHIFT) != 0;
        break;
    default: break;
    }
}

Input InputCollector::take() {
    Input out = input_;
    // Only what is held carries over.
    Input next;
    next.mouseX = input_.mouseX;
    next.mouseY = input_.mouseY;
    next.mouseDown = input_.mouseDown;
    next.ctrl = input_.ctrl;
    next.shift = input_.shift;
    input_ = next;
    return out;
}

// --- frame -------------------------------------------------------------------

void Gui::begin(SDL_Renderer* renderer, const Input& input) {
    renderer_ = renderer;
    input_ = input;
    lastOrder_ = std::move(order_);
    order_.clear();
    help_.clear();
    focusHelp_.clear();
    scrolls_.clear();
    if (!typing() && input_.navY != 0 && !lastOrder_.empty()) {
        auto it = std::find(lastOrder_.begin(), lastOrder_.end(), focus_);
        int index = it == lastOrder_.end() ? (input_.navY > 0 ? -1 : 0) : static_cast<int>(it - lastOrder_.begin());
        const int count = static_cast<int>(lastOrder_.size());
        index = ((index + input_.navY) % count + count) % count;
        focus_ = lastOrder_[static_cast<std::size_t>(index)];
    }
}

void Gui::end() {
    if (help_.empty()) {
        help_ = focusHelp_;
    }
    if (input_.mouseReleased) {
        pressedOn_.clear();
    }
    // A field that is no longer shown is no longer typed in.
    if (!editing_.empty() && std::find(order_.begin(), order_.end(), editing_) == order_.end()) {
        editing_.clear();
    }
}

bool Gui::hovered(const SDL_FRect& r) const {
    if (!ui::contains(r, input_.mouseX, input_.mouseY)) {
        return false;
    }
    return scrolls_.empty() || ui::contains(scrolls_.back().rect, input_.mouseX, input_.mouseY);
}

bool Gui::visible(const SDL_FRect& r) const {
    if (scrolls_.empty()) {
        return true;
    }
    const SDL_FRect& c = scrolls_.back().rect;
    return r.y + r.h > c.y && r.y < c.y + c.h;
}

bool Gui::interact(std::string_view id, const SDL_FRect& r, std::string_view help) {
    order_.emplace_back(id);
    const bool over = hovered(r);
    if (over && !help.empty()) {
        help_ = std::string(help);
    }
    if (focus_ == id && !help.empty()) {
        focusHelp_ = std::string(help);
    }
    if (over && input_.mousePressed) {
        pressedOn_ = std::string(id);
        focus_ = std::string(id);
        return true;
    }
    return focus_ == id && input_.activate && !typing();
}

void Gui::focusRing(std::string_view id, const SDL_FRect& r) {
    if (focus_ == id) {
        ui::outline(renderer_, {r.x - 1, r.y - 1, r.w + 2, r.h + 2}, ui::color::accent);
    }
}

// --- widgets -----------------------------------------------------------------

bool Gui::button(std::string_view id, const SDL_FRect& r, std::string_view label, std::string_view help, bool selected,
                 bool enabled) {
    if (!visible(r)) {
        order_.emplace_back(id);
        return false;
    }
    const bool clicked = interact(id, r, help) && enabled;
    const bool over = hovered(r);
    ui::fill(renderer_, r, selected ? kSelected : over && enabled ? kHover : ui::color::panelLight);
    ui::outline(renderer_, r, selected ? ui::color::accent : ui::color::border);
    const std::string shown = fitText(label, r.w - 4);
    ui::textCentered(renderer_, r.x + r.w / 2, std::floor(r.y + (r.h - ui::kFont) / 2), shown,
                     !enabled ? ui::color::faint : selected ? ui::color::accent : ui::color::text);
    focusRing(id, r);
    return clicked;
}

bool Gui::checkbox(std::string_view id, const SDL_FRect& r, std::string_view label, bool& value, std::string_view help) {
    if (!visible(r)) {
        order_.emplace_back(id);
        return false;
    }
    const bool clicked = interact(id, r, help);
    if (hovered(r)) {
        ui::fill(renderer_, r, kHover);
    }
    const SDL_FRect box{r.x + 2, r.y + (r.h - 10) / 2, 10, 10};
    ui::fill(renderer_, box, kField);
    ui::outline(renderer_, box, ui::color::border);
    if (value) {
        ui::fill(renderer_, {box.x + 2, box.y + 2, 6, 6}, ui::color::accent);
    }
    ui::text(renderer_, r.x + 16, std::floor(r.y + (r.h - ui::kFont) / 2), fitText(label, r.w - 18), ui::color::text);
    focusRing(id, r);
    if (clicked) {
        value = !value;
    }
    return clicked;
}

bool Gui::stepper(std::string_view id, const SDL_FRect& r, std::string_view label, int& value, int min, int max,
                  std::string_view shown, std::string_view help, int step) {
    if (!visible(r)) {
        order_.emplace_back(id);
        return false;
    }
    const bool clicked = interact(id, r, help);
    const bool over = hovered(r);
    if (over) {
        ui::fill(renderer_, r, kHover);
    }
    const float ty = std::floor(r.y + (r.h - ui::kFont) / 2);
    ui::text(renderer_, r.x + 2, ty, label, ui::color::dim);
    const float labelW = ui::textWidth(label) + 8;
    const SDL_FRect less{r.x + labelW, r.y, 12, r.h};
    const SDL_FRect more{r.x + r.w - 12, r.y, 12, r.h};
    ui::text(renderer_, less.x + 2, ty, "<", ui::color::accent);
    ui::text(renderer_, more.x + 2, ty, ">", ui::color::accent);
    const std::string text = fitText(shown, more.x - less.x - less.w - 4);
    ui::textCentered(renderer_, (less.x + less.w + more.x) / 2, ty, text, ui::color::text);
    focusRing(id, r);
    int change = 0;
    if (clicked && input_.mousePressed) {
        if (ui::contains(less, input_.mouseX, input_.mouseY)) {
            change = -step;
        } else if (ui::contains(more, input_.mouseX, input_.mouseY)) {
            change = step;
        }
    }
    if (focus_ == id && !typing()) {
        change += input_.navX * step;
    }
    if (over && input_.wheel != 0) {
        change += (input_.wheel > 0 ? 1 : -1) * step;
    }
    if (change == 0) {
        return false;
    }
    const int next = std::clamp(value + change, min, max);
    if (next == value) {
        return false;
    }
    value = next;
    return true;
}

bool Gui::textField(std::string_view id, const SDL_FRect& r, std::string& text, std::string_view help,
                    std::size_t maxLength, std::string_view placeholder) {
    if (!visible(r)) {
        order_.emplace_back(id);
        return false;
    }
    const bool clicked = interact(id, r, help);
    if (clicked) {
        editing_ = std::string(id);
        caret_ = text.size();
    }
    if (editing_ == id && input_.mousePressed && !hovered(r)) {
        editing_.clear(); // a press elsewhere ends the typing
    }
    const bool active = editing_ == id;
    bool changed = false;
    if (active) {
        caret_ = std::min(caret_, text.size());
        if (!input_.typed.empty()) {
            std::string add;
            for (const char c : input_.typed) {
                if (static_cast<unsigned char>(c) >= 0x20) {
                    add += c;
                }
            }
            if (text.size() + add.size() <= maxLength) {
                text.insert(caret_, add);
                caret_ += add.size();
                changed = true;
            }
        }
        // Whole UTF-8 characters are removed.
        const auto isContinuation = [&](std::size_t i) { return (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80; };
        if (input_.backspace && caret_ > 0) {
            std::size_t from = caret_ - 1;
            while (from > 0 && isContinuation(from)) {
                --from;
            }
            text.erase(from, caret_ - from);
            caret_ = from;
            changed = true;
        }
        if (input_.deleteKey && caret_ < text.size()) {
            std::size_t to = caret_ + 1;
            while (to < text.size() && isContinuation(to)) {
                ++to;
            }
            text.erase(caret_, to - caret_);
            changed = true;
        }
        if (input_.left && caret_ > 0) {
            --caret_;
            while (caret_ > 0 && isContinuation(caret_)) {
                --caret_;
            }
        }
        if (input_.right && caret_ < text.size()) {
            ++caret_;
            while (caret_ < text.size() && isContinuation(caret_)) {
                ++caret_;
            }
        }
        if (input_.home) {
            caret_ = 0;
        }
        if (input_.end) {
            caret_ = text.size();
        }
        if (input_.enter || input_.escape) {
            editing_.clear();
        }
    }
    ui::fill(renderer_, r, kField);
    ui::outline(renderer_, r, active ? ui::color::accent : hovered(r) ? ui::color::dim : ui::color::border);
    const float ty = std::floor(r.y + (r.h - ui::kFont) / 2);
    const auto columns = static_cast<std::size_t>(std::max(1.0f, (r.w - 6) / ui::kFont));
    if (text.empty() && !active) {
        ui::text(renderer_, r.x + 3, ty, fitText(placeholder, r.w - 6), ui::color::faint);
    } else {
        // Show the end of a long text, or the part around the caret.
        const std::size_t caretAt = active ? caret_ : 0;
        const std::size_t first = caretAt > columns - 1 ? caretAt - (columns - 1) : 0;
        ui::text(renderer_, r.x + 3, ty, text.substr(first, columns), ui::color::text);
        if (active && (SDL_GetTicks() / 500) % 2 == 0) {
            ui::fill(renderer_, {r.x + 3 + static_cast<float>(caretAt - first) * ui::kFont, r.y + 2, 1, r.h - 4},
                     ui::color::accent);
        }
    }
    focusRing(id, r);
    return changed;
}

bool Gui::area(std::string_view id, const SDL_FRect& r, std::string_view help) {
    const bool clicked = interact(id, r, help);
    return clicked;
}

void Gui::label(float x, float y, std::string_view text, ui::Color c) { ui::text(renderer_, x, y, text, c); }

float Gui::paragraph(float x, float y, float width, std::string_view text, ui::Color c, int maxLines) {
    const auto lines = wrap(text, static_cast<std::size_t>(std::max(1.0f, width / ui::kFont)));
    float h = 0;
    int count = 0;
    for (const auto& line : lines) {
        if (count++ >= maxLines) {
            break;
        }
        ui::text(renderer_, x, y + h, line, c);
        h += 10;
    }
    return h;
}

void Gui::beginScroll(const SDL_FRect& r, float& scroll, float contentHeight) {
    if (hovered(r) && input_.wheel != 0) {
        scroll -= input_.wheel * 24;
    }
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, contentHeight - r.h));
    scrolls_.push_back({r, scroll});
    const SDL_Rect clip = pixelRect(r);
    SDL_SetRenderClipRect(renderer_, &clip);
    // A thin bar shows where the view is.
    if (contentHeight > r.h) {
        const float h = std::max(10.0f, r.h * r.h / contentHeight);
        const float y = r.y + (r.h - h) * (scroll / (contentHeight - r.h));
        ui::fill(renderer_, {r.x + r.w - 3, y, 2, h}, ui::color::faint);
    }
}

void Gui::endScroll() {
    scrolls_.pop_back();
    if (scrolls_.empty()) {
        SDL_SetRenderClipRect(renderer_, nullptr);
    } else {
        const SDL_Rect clip = pixelRect(scrolls_.back().rect);
        SDL_SetRenderClipRect(renderer_, &clip);
    }
}

} // namespace das::app::gui
