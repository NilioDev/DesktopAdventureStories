// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui.hpp"

#include "das/constants.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>

// Built in by cmake/DasEmbed.cmake: Fira Sans Medium and PxPlus IBM CGA.
extern const unsigned char kInterfaceFont[];
extern const std::size_t kInterfaceFontSize;
extern const unsigned char kStoryFont[];
extern const std::size_t kStoryFontSize;

namespace das::app::ui {

namespace {

constexpr float kMargin = 8;
constexpr float kPanelWidth = 200;
constexpr float kSide = 128; // a column of cards beside the field in play
constexpr float kBar = 48;   // the item bar under the field in play
constexpr float kField = static_cast<float>(kViewPixels); // 288
// The smallest screen in units the interface is laid out for.
constexpr float kMinShort = 320;

float g_scale = 1;

// --- the font ---------------------------------------------------------------
//
// Letters are drawn at the screen's own resolution: the font is opened at
// the size in screen pixels, and drawn with the render scale at 1.

// Both fonts have capitals about 7 units high, on a baseline 7 units below
// the top of a line, like SDL's 8x8 letters they replace.
constexpr float kBaseline = 7;
float fontSize(Font font) { return font == Font::Story ? 8.0f : 10.0f; } // in units
constexpr Uint64 kKeepFrames = 120; // drawn texts unused this long are freed

struct CachedText {
    TTF_Text* text = nullptr;
    Uint64 used = 0;
};

struct Fonts {
    bool ready = false;
    TTF_TextEngine* engine = nullptr;
    std::unordered_map<int, TTF_Font*> bySize; // by font and size in screen pixels
    std::unordered_map<std::string, CachedText> texts; // by size and text
    Uint64 frame = 0;
};

Fonts g_fonts;

// The font's size in screen pixels. The pixel font only at whole multiples
// of its 8 pixels, so its letters stay square.
int pixelSize(float size, Font font) {
    if (font == Font::Story) {
        return std::max(1, static_cast<int>(std::lround(size * g_scale))) * 8;
    }
    return std::max(1, static_cast<int>(std::lround(fontSize(font) * size * g_scale)));
}

// A key for a font at a size.
int fontKey(int pixels, Font font) { return pixels * 2 + (font == Font::Story ? 1 : 0); }

TTF_Font* fontAt(int pixels, Font which) {
    if (!g_fonts.ready) {
        return nullptr;
    }
    const int key = fontKey(pixels, which);
    if (const auto it = g_fonts.bySize.find(key); it != g_fonts.bySize.end()) {
        return it->second;
    }
    const bool story = which == Font::Story;
    TTF_Font* font = nullptr;
    if (SDL_IOStream* io = story ? SDL_IOFromConstMem(kStoryFont, kStoryFontSize)
                                 : SDL_IOFromConstMem(kInterfaceFont, kInterfaceFontSize)) {
        font = TTF_OpenFontIO(io, true, static_cast<float>(pixels));
    }
    if (font) {
        // The pixel font without smoothing: every pixel stays a sharp square.
        TTF_SetFontHinting(font, story ? TTF_HINTING_MONO : TTF_HINTING_LIGHT);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not open the font: %s", SDL_GetError());
    }
    g_fonts.bySize[key] = font;
    return font;
}

TTF_Text* cachedText(TTF_Font* font, int fontKey, std::string_view line) {
    std::string key = std::to_string(fontKey);
    key += '\n';
    key += line;
    auto& entry = g_fonts.texts[key];
    if (!entry.text) {
        entry.text = TTF_CreateText(g_fonts.engine, font, line.data(), line.size());
    }
    entry.used = g_fonts.frame;
    return entry.text;
}

// Steps back over one UTF-8 letter.
std::size_t previousLetter(std::string_view s, std::size_t at) {
    if (at == 0) {
        return 0;
    }
    --at;
    while (at > 0 && (static_cast<unsigned char>(s[at]) & 0xc0) == 0x80) {
        --at;
    }
    return at;
}

void setColor(SDL_Renderer* r, Color c) {
    SDL_SetRenderDrawBlendMode(r, c.a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

} // namespace

namespace {

Layout layoutAt(float scale, int pixelWidth, int pixelHeight, bool pixelPerfect, float bottomRoom, bool hud) {
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
    } else if (hud) {
        areaW = out.width - 4 * kMargin - 2 * kSide;
        areaH = out.height - 3 * kMargin - kBar;
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
        if (hud) {
            // Two columns side by side, the bar under them.
            const SDL_FRect all = out.panel;
            const float columnH = std::max(40.0f, all.h - kBar - kMargin);
            const float half = std::floor((all.w - kMargin) / 2);
            out.left = {all.x, all.y, half, columnH};
            out.panel = {all.x + half + kMargin, all.y, all.w - half - kMargin, columnH};
            out.bar = {all.x, all.y + columnH + kMargin, all.w, kBar};
        }
    } else if (hud) {
        const float total = field + 2 * (kMargin + kSide);
        const float x = std::floor((out.width - total) / 2);
        const float y = std::floor((out.height - field - kMargin - kBar) / 2);
        out.left = {x, y, kSide, field};
        out.field = {x + kSide + kMargin, y, field, field};
        out.panel = {out.field.x + field + kMargin, y, kSide, field};
        out.bar = {out.field.x, y + field + kMargin, field, kBar};
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

SDL_FRect hudArea(const Layout& layout) {
    if (layout.left.w <= 0) {
        return layout.panel;
    }
    const float x0 = std::min({layout.left.x, layout.panel.x, layout.bar.x});
    const float y0 = std::min({layout.left.y, layout.panel.y, layout.bar.y});
    const float x1 = std::max({layout.left.x + layout.left.w, layout.panel.x + layout.panel.w, layout.bar.x + layout.bar.w});
    const float y1 = std::max({layout.left.y + layout.left.h, layout.panel.y + layout.panel.h, layout.bar.y + layout.bar.h});
    return {x0, y0, x1 - x0, y1 - y0};
}

Layout layout(int pixelWidth, int pixelHeight, bool pixelPerfect, float bottomRoom, bool hud) {
    // The largest interface that does not make the play field smaller.
    const float most = std::max(1.0f, std::floor(static_cast<float>(std::min(pixelWidth, pixelHeight)) / kMinShort));
    Layout best = layoutAt(1, pixelWidth, pixelHeight, pixelPerfect, bottomRoom, hud);
    for (float scale = 2; scale <= most; ++scale) {
        const Layout next = layoutAt(scale, pixelWidth, pixelHeight, pixelPerfect, bottomRoom, hud);
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

void init(SDL_Renderer* renderer) {
    if (g_fonts.ready) {
        return;
    }
    if (!TTF_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not start SDL_ttf: %s", SDL_GetError());
        return;
    }
    g_fonts.engine = TTF_CreateRendererTextEngine(renderer);
    if (!g_fonts.engine) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not set up text drawing: %s", SDL_GetError());
        TTF_Quit();
        return;
    }
    g_fonts.ready = true;
}

void shutdown() {
    for (auto& [key, entry] : g_fonts.texts) {
        TTF_DestroyText(entry.text);
    }
    for (auto& [key, font] : g_fonts.bySize) {
        if (font) {
            TTF_CloseFont(font);
        }
    }
    if (g_fonts.engine) {
        TTF_DestroyRendererTextEngine(g_fonts.engine);
    }
    const bool started = g_fonts.ready || g_fonts.engine;
    g_fonts = Fonts{};
    if (started) {
        TTF_Quit();
    }
}

void begin(SDL_Renderer* renderer, const Layout& layout) {
    g_scale = layout.scale;
    SDL_SetRenderScale(renderer, g_scale, g_scale);
    // Free the texts that were not drawn for a while.
    ++g_fonts.frame;
    if (g_fonts.frame % 60 == 0) {
        for (auto it = g_fonts.texts.begin(); it != g_fonts.texts.end();) {
            if (g_fonts.frame - it->second.used > kKeepFrames) {
                TTF_DestroyText(it->second.text);
                it = g_fonts.texts.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void fill(SDL_Renderer* renderer, const SDL_FRect& rect, Color c) {
    setColor(renderer, c);
    SDL_RenderFillRect(renderer, &rect);
}

void setAccent(Color accent, Color ink) {
    color::accent = accent;
    color::accentInk = ink;
}

namespace {

// The edge of a rounded rectangle, clockwise from the top left corner.
std::vector<SDL_FPoint> roundedEdge(const SDL_FRect& rect, float radius) {
    const float r = std::clamp(radius, 0.0f, std::min(rect.w, rect.h) / 2);
    constexpr int kSteps = 6; // per corner
    constexpr float kQuarter = 1.5707964f;
    const SDL_FPoint centres[4] = {{rect.x + r, rect.y + r},
                                   {rect.x + rect.w - r, rect.y + r},
                                   {rect.x + rect.w - r, rect.y + rect.h - r},
                                   {rect.x + r, rect.y + rect.h - r}};
    std::vector<SDL_FPoint> points;
    points.reserve(4 * (kSteps + 1));
    for (int corner = 0; corner < 4; ++corner) {
        const float start = static_cast<float>(corner + 2) * kQuarter; // 180, 270, 0, 90 degrees
        for (int i = 0; i <= kSteps; ++i) {
            const float a = start + kQuarter * static_cast<float>(i) / kSteps;
            points.push_back({centres[corner].x + r * std::cos(a), centres[corner].y + r * std::sin(a)});
        }
    }
    return points;
}

} // namespace

void fillRounded(SDL_Renderer* renderer, const SDL_FRect& rect, float radius, Color c) {
    if (radius < 1 || rect.w < 2 || rect.h < 2) {
        fill(renderer, rect, c);
        return;
    }
    const auto edge = roundedEdge(rect, radius);
    const SDL_FColor fc{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(edge.size() + 1);
    vertices.push_back({{rect.x + rect.w / 2, rect.y + rect.h / 2}, fc, {0, 0}});
    for (const auto& p : edge) {
        vertices.push_back({p, fc, {0, 0}});
    }
    std::vector<int> indices;
    indices.reserve(edge.size() * 3);
    const int n = static_cast<int>(edge.size());
    for (int i = 0; i < n; ++i) {
        indices.push_back(0);
        indices.push_back(1 + i);
        indices.push_back(1 + (i + 1) % n);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(),
                       static_cast<int>(indices.size()));
}

void outlineRounded(SDL_Renderer* renderer, const SDL_FRect& rect, float radius, Color c) {
    // A band one unit wide between the edge and the same shape one unit in,
    // drawn as triangles so that its curves are smooth.
    const auto outer = roundedEdge(rect, radius);
    const auto inner = roundedEdge({rect.x + 1, rect.y + 1, rect.w - 2, rect.h - 2}, std::max(0.0f, radius - 1));
    const SDL_FColor fc{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(outer.size() * 2);
    for (std::size_t i = 0; i < outer.size(); ++i) {
        vertices.push_back({outer[i], fc, {0, 0}});
        vertices.push_back({inner[i], fc, {0, 0}});
    }
    std::vector<int> indices;
    const int n = static_cast<int>(outer.size());
    indices.reserve(static_cast<std::size_t>(n) * 6);
    for (int i = 0; i < n; ++i) {
        const int a0 = 2 * i;
        const int b0 = 2 * ((i + 1) % n);
        indices.insert(indices.end(), {a0, a0 + 1, b0, b0, a0 + 1, b0 + 1});
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(),
                       static_cast<int>(indices.size()));
}

void outline(SDL_Renderer* renderer, const SDL_FRect& rect, Color c) {
    setColor(renderer, c);
    SDL_RenderRect(renderer, &rect);
}

void panel(SDL_Renderer* renderer, const SDL_FRect& rect, Color fillColor) {
    constexpr float kRadius = 8;
    fillRounded(renderer, {rect.x - 1, rect.y + 2, rect.w + 2, rect.h + 2}, kRadius + 1, {0, 0, 0, 60});
    fillRounded(renderer, rect, kRadius, fillColor);
    outlineRounded(renderer, rect, kRadius, color::border);
}

void text(SDL_Renderer* renderer, float x, float y, std::string_view line, Color c, float size, Font which) {
    if (line.empty()) {
        return;
    }
    const int pixels = pixelSize(size, which);
    TTF_Font* font = fontAt(pixels, which);
    TTF_Text* drawn = font ? cachedText(font, fontKey(pixels, which), line) : nullptr;
    if (!drawn) {
        // Without the font: SDL's built-in 8x8 letters.
        setColor(renderer, c);
        const std::string s(line);
        SDL_SetRenderScale(renderer, g_scale * size, g_scale * size);
        SDL_RenderDebugText(renderer, std::round(x * g_scale) / (g_scale * size),
                            std::round(y * g_scale) / (g_scale * size), s.c_str());
        SDL_SetRenderScale(renderer, g_scale, g_scale);
        return;
    }
    TTF_SetTextColor(drawn, c.r, c.g, c.b, c.a);
    // Draw in screen pixels. The clip rectangle is in units and SDL scales it
    // with the render scale, so it is scaled by hand while the scale is 1.
    const bool clipped = SDL_RenderClipEnabled(renderer);
    SDL_Rect clip{};
    if (clipped) {
        SDL_GetRenderClipRect(renderer, &clip);
    }
    SDL_SetRenderScale(renderer, 1, 1);
    if (clipped) {
        const auto scaled = [](int v) { return static_cast<int>(std::lround(static_cast<float>(v) * g_scale)); };
        const SDL_Rect pixelClip{scaled(clip.x), scaled(clip.y), scaled(clip.w), scaled(clip.h)};
        SDL_SetRenderClipRect(renderer, &pixelClip);
    }
    const float baseline = std::round((y + kBaseline * size) * g_scale);
    TTF_DrawRendererText(drawn, std::round(x * g_scale), baseline - static_cast<float>(TTF_GetFontAscent(font)));
    SDL_SetRenderScale(renderer, g_scale, g_scale);
    if (clipped) {
        SDL_SetRenderClipRect(renderer, &clip);
    }
}

float textWidth(std::string_view line, float size, Font which) {
    if (line.empty()) {
        return 0;
    }
    TTF_Font* font = fontAt(pixelSize(size, which), which);
    int w = 0;
    int h = 0;
    if (!font || !TTF_GetStringSize(font, line.data(), line.size(), &w, &h)) {
        return static_cast<float>(line.size()) * kFont * size; // the 8x8 letters
    }
    return static_cast<float>(w) / g_scale;
}

std::string fit(std::string_view line, float width, float size, Font font) {
    if (textWidth(line, size, font) <= width) {
        return std::string(line);
    }
    const std::string_view more = g_fonts.ready ? "\xE2\x80\xA6" : "..";
    const float room = width - textWidth(more, size, font);
    // The longest start that fits: find it by halving.
    std::size_t low = 0;
    std::size_t high = line.size();
    while (low < high) {
        const std::size_t mid = (low + high + 1) / 2;
        if (textWidth(line.substr(0, mid), size, font) <= room) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    // Not in the middle of a UTF-8 letter.
    while (low > 0 && low < line.size() && (static_cast<unsigned char>(line[low]) & 0xc0) == 0x80) {
        --low;
    }
    std::string out(line.substr(0, low));
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out + std::string(more);
}

std::vector<std::string> wrap(std::string_view text, float width, float size, Font font) {
    std::vector<std::string> lines;
    const auto fits = [&](std::string_view s) { return textWidth(s, size, font) <= width; };
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        std::string_view paragraph =
            text.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
        std::string line;
        while (!paragraph.empty()) {
            const std::size_t cut = paragraph.find(' ');
            std::string_view word = paragraph.substr(0, cut);
            paragraph = cut == std::string_view::npos ? std::string_view{} : paragraph.substr(cut + 1);
            if (word.empty()) {
                continue;
            }
            if (!line.empty() && fits(line + " " + std::string(word))) {
                line += " ";
                line += word;
                continue;
            }
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
            // A word longer than a line is broken where it has to be.
            while (!word.empty() && !fits(word)) {
                std::size_t end = word.size();
                while (end > 1 && !fits(word.substr(0, end))) {
                    end = previousLetter(word, end);
                }
                end = std::max<std::size_t>(end, 1);
                while (end < word.size() && (static_cast<unsigned char>(word[end]) & 0xc0) == 0x80) {
                    ++end; // keep a letter whole
                }
                lines.emplace_back(word.substr(0, end));
                word.remove_prefix(end);
            }
            line = std::string(word);
        }
        lines.push_back(line);
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    return lines;
}

void textRight(SDL_Renderer* renderer, float right, float y, std::string_view line, Color c, float size) {
    text(renderer, right - textWidth(line, size), y, line, c, size);
}

void textCentered(SDL_Renderer* renderer, float centerX, float y, std::string_view line, Color c, float size) {
    text(renderer, std::floor(centerX - textWidth(line, size) / 2), y, line, c, size);
}

void ring(SDL_Renderer* renderer, float cx, float cy, float radius, float thickness, float ratio, Color c,
          Color track) {
    constexpr int kSteps = 64;
    constexpr float kTurn = 6.2831853f;
    const float r = std::clamp(ratio, 0.0f, 1.0f);
    const auto colorOf = [](Color k) { return SDL_FColor{k.r / 255.0f, k.g / 255.0f, k.b / 255.0f, k.a / 255.0f}; };
    const SDL_FColor on = colorOf(c);
    const SDL_FColor off = colorOf(track);
    const float inner = std::max(0.0f, radius - thickness);
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    vertices.reserve(kSteps * 4);
    indices.reserve(kSteps * 6);
    for (int i = 0; i < kSteps; ++i) {
        const float a0 = kTurn * static_cast<float>(i) / kSteps - kTurn / 4;
        const float a1 = kTurn * static_cast<float>(i + 1) / kSteps - kTurn / 4;
        const SDL_FColor fc = (static_cast<float>(i) + 0.5f) / kSteps <= r ? on : off;
        const int base = static_cast<int>(vertices.size());
        vertices.push_back({{cx + radius * std::cos(a0), cy + radius * std::sin(a0)}, fc, {0, 0}});
        vertices.push_back({{cx + inner * std::cos(a0), cy + inner * std::sin(a0)}, fc, {0, 0}});
        vertices.push_back({{cx + radius * std::cos(a1), cy + radius * std::sin(a1)}, fc, {0, 0}});
        vertices.push_back({{cx + inner * std::cos(a1), cy + inner * std::sin(a1)}, fc, {0, 0}});
        indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(),
                       static_cast<int>(indices.size()));
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
