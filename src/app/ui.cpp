// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.hpp"

#include "das/constants.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace das::app::ui {

namespace {

constexpr float kMargin = 8;
constexpr float kPanelWidth = 200;
constexpr float kField = static_cast<float>(kViewPixels); // 288
// The smallest screen in units the interface is laid out for.
constexpr float kMinShort = 320;

float g_scale = 1;

void setColor(SDL_Renderer* r, Color c) {
    SDL_SetRenderDrawBlendMode(r, c.a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

} // namespace

namespace {

Layout layoutAt(float scale, int pixelWidth, int pixelHeight, bool pixelPerfect, float bottomRoom) {
    Layout out;
    const auto pw = static_cast<float>(std::max(1, pixelWidth));
    const auto ph = static_cast<float>(std::max(1, pixelHeight));
    out.scale = scale;
    out.width = pw / out.scale;
    out.height = ph / out.scale;
    out.portrait = out.height > out.width;

    // The space the play field can take, in units.
    float areaW = 0;
    float areaH = 0;
    if (out.portrait) {
        areaW = out.width - 2 * kMargin;
        areaH = out.height - 2 * kMargin - 150; // keep room for the panel
    } else {
        areaW = out.width - 3 * kMargin - kPanelWidth;
        areaH = out.height - 2 * kMargin;
    }
    const float room = std::max(64.0f, std::min(areaW, areaH)) * out.scale; // in pixels
    float fieldPixels = room;
    if (pixelPerfect && room >= kField) {
        fieldPixels = std::floor(room / kField) * kField;
    }
    const float field = fieldPixels / out.scale;

    if (out.portrait) {
        const float x = std::floor((out.width - field) / 2);
        out.field = {x, kMargin, field, field};
        const float panelY = out.field.y + field + kMargin;
        out.panel = {kMargin, panelY, out.width - 2 * kMargin,
                     std::max(120.0f, out.height - panelY - kMargin - bottomRoom)};
    } else {
        const float total = field + kMargin + kPanelWidth;
        const float x = std::floor((out.width - total) / 2);
        const float y = std::floor((out.height - field) / 2);
        out.field = {x, y, field, field};
        out.panel = {x + field + kMargin, y, kPanelWidth, field};
    }
    return out;
}

} // namespace

Layout layout(int pixelWidth, int pixelHeight, bool pixelPerfect, float bottomRoom) {
    // The largest interface that does not make the play field smaller.
    const float most = std::max(1.0f, std::floor(static_cast<float>(std::min(pixelWidth, pixelHeight)) / kMinShort));
    Layout best = layoutAt(1, pixelWidth, pixelHeight, pixelPerfect, bottomRoom);
    for (float scale = 2; scale <= most; ++scale) {
        const Layout next = layoutAt(scale, pixelWidth, pixelHeight, pixelPerfect, bottomRoom);
        if (next.field.w * next.scale >= best.field.w * best.scale - 0.5f) {
            best = next;
        }
    }
    return best;
}

Layout plainLayout(int pixelWidth, int pixelHeight, float scale) {
    Layout l;
    l.scale = std::max(1.0f, scale);
    l.width = std::floor(static_cast<float>(pixelWidth) / l.scale);
    l.height = std::floor(static_cast<float>(pixelHeight) / l.scale);
    return l;
}

void begin(SDL_Renderer* renderer, const Layout& layout) {
    g_scale = layout.scale;
    SDL_SetRenderScale(renderer, g_scale, g_scale);
}

void fill(SDL_Renderer* renderer, const SDL_FRect& rect, Color c) {
    setColor(renderer, c);
    SDL_RenderFillRect(renderer, &rect);
}

void outline(SDL_Renderer* renderer, const SDL_FRect& rect, Color c) {
    setColor(renderer, c);
    SDL_RenderRect(renderer, &rect);
}

void panel(SDL_Renderer* renderer, const SDL_FRect& rect, Color fillColor) {
    fill(renderer, {rect.x + 2, rect.y + 2, rect.w, rect.h}, {0, 0, 0, 70});
    fill(renderer, rect, fillColor);
    outline(renderer, rect, color::border);
}

void text(SDL_Renderer* renderer, float x, float y, std::string_view line, Color c, float size) {
    setColor(renderer, c);
    const std::string s(line);
    if (size == 1) {
        SDL_RenderDebugText(renderer, std::round(x), std::round(y), s.c_str());
        return;
    }
    SDL_SetRenderScale(renderer, g_scale * size, g_scale * size);
    SDL_RenderDebugText(renderer, std::round(x * g_scale) / (g_scale * size), std::round(y * g_scale) / (g_scale * size),
                        s.c_str());
    SDL_SetRenderScale(renderer, g_scale, g_scale);
}

float textWidth(std::string_view line, float size) { return static_cast<float>(line.size()) * kFont * size; }

void textRight(SDL_Renderer* renderer, float right, float y, std::string_view line, Color c, float size) {
    text(renderer, right - textWidth(line, size), y, line, c, size);
}

void textCentered(SDL_Renderer* renderer, float centerX, float y, std::string_view line, Color c, float size) {
    text(renderer, std::floor(centerX - textWidth(line, size) / 2), y, line, c, size);
}

void bar(SDL_Renderer* renderer, const SDL_FRect& rect, float ratio, Color c) {
    fill(renderer, rect, {12, 13, 18});
    const float r = std::clamp(ratio, 0.0f, 1.0f);
    if (r > 0) {
        fill(renderer, {rect.x + 1, rect.y + 1, (rect.w - 2) * r, rect.h - 2}, c);
    }
    outline(renderer, rect, color::border);
}

bool contains(const SDL_FRect& rect, float x, float y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
}

} // namespace das::app::ui
