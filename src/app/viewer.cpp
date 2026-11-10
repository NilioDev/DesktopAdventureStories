// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace das::app {

namespace {

constexpr int kAtlasColumns = 64;
constexpr int kTile = 32;
constexpr int kTilesPerPage = 9 * 9;

SDL_Texture* makeTexture(SDL_Renderer* renderer, int width, int height, const std::vector<std::uint32_t>& rgba) {
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
    if (!texture) {
        return nullptr;
    }
    SDL_UpdateTexture(texture, nullptr, rgba.data(), width * 4);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

std::uint32_t rgba(const data::Palette& palette, std::uint8_t index, bool transparentZero) {
    if (transparentZero && index == 0) {
        return 0;
    }
    const auto& c = palette.colors[index];
    // SDL_PIXELFORMAT_RGBA32 is R, G, B, A in memory order.
    const std::uint8_t bytes[4] = {c.r, c.g, c.b, 0xff};
    std::uint32_t value = 0;
    std::memcpy(&value, bytes, 4);
    return value;
}

SDL_Texture* buildAtlas(SDL_Renderer* renderer, const data::GameData& data, const data::Palette& palette) {
    const int count = static_cast<int>(data.tiles.size());
    const int rows = std::max(1, (count + kAtlasColumns - 1) / kAtlasColumns);
    const int width = kAtlasColumns * kTile;
    const int height = rows * kTile;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    for (int t = 0; t < count; ++t) {
        const auto& tile = data.tiles[static_cast<std::size_t>(t)];
        const int ox = (t % kAtlasColumns) * kTile;
        const int oy = (t / kAtlasColumns) * kTile;
        for (int y = 0; y < kTile; ++y) {
            for (int x = 0; x < kTile; ++x) {
                pixels[static_cast<std::size_t>((oy + y) * width + ox + x)] =
                    rgba(palette, tile.pixels[static_cast<std::size_t>(y * kTile + x)], true);
            }
        }
    }
    return makeTexture(renderer, width, height, pixels);
}

SDL_Texture* buildSetup(SDL_Renderer* renderer, const data::GameData& data, const data::Palette& palette) {
    if (data.setupScreen.empty()) {
        return nullptr;
    }
    std::vector<std::uint32_t> pixels(data.setupScreen.size());
    std::transform(data.setupScreen.begin(), data.setupScreen.end(), pixels.begin(),
                   [&](std::uint8_t index) { return rgba(palette, index, false); });
    return makeTexture(renderer, data::kSetupScreenSize, data::kSetupScreenSize, pixels);
}

void text(SDL_Renderer* renderer, float x, float y, const std::string& line, Uint8 shade) {
    SDL_SetRenderDrawColor(renderer, shade, shade, static_cast<Uint8>(std::min(255, shade + 8)), 255);
    SDL_RenderDebugText(renderer, x, y, line.c_str());
}

} // namespace

LoadedGame::~LoadedGame() {
    if (atlas) {
        SDL_DestroyTexture(atlas);
    }
    if (setup) {
        SDL_DestroyTexture(setup);
    }
}

std::string Viewer::load(SDL_Renderer* renderer, const std::filesystem::path& where) {
    const auto found = data::findGameFiles(where);
    if (found.empty()) {
        return "No game files found in " + where.string();
    }
    std::string summary;
    for (const auto& files : found) {
        try {
            auto game = std::make_unique<LoadedGame>();
            game->files = files;
            game->data = data::parseGameData(data::readFile(files.dataFile));
            if (!files.executable.empty()) {
                if (auto palette = data::findPalette(data::readFile(files.executable))) {
                    game->palette = *palette;
                    game->paletteFromGame = true;
                }
            }
            if (!game->paletteFromGame) {
                game->palette = data::greyscalePalette();
            }
            game->atlas = buildAtlas(renderer, game->data, game->palette);
            game->setup = buildSetup(renderer, game->data, game->palette);

            // Replace an earlier copy of the same game.
            std::erase_if(games_, [&](const auto& g) { return g->files.game == files.game; });
            games_.push_back(std::move(game));
            SDL_Log("Loaded %s from %s", std::string(data::layoutName(files.game)).c_str(),
                    files.dataFile.string().c_str());
            summary += std::string(summary.empty() ? "" : ", ") + std::string(data::layoutName(files.game));
        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load %s: %s", files.dataFile.string().c_str(),
                         e.what());
            summary += std::string(summary.empty() ? "" : ", ") + "error: " + e.what();
        }
    }
    current_ = games_.empty() ? 0 : games_.size() - 1;
    zone_ = 0;
    tilePage_ = 0;
    return summary;
}

void Viewer::selectGame(data::Layout which) {
    for (std::size_t i = 0; i < games_.size(); ++i) {
        if (games_[i]->files.game == which) {
            current_ = i;
        }
    }
}

void Viewer::selectZone(int zone) {
    if (!games_.empty()) {
        zone_ = std::clamp(zone, 0, static_cast<int>(game().data.zones.size()) - 1);
    }
}

void Viewer::update(const ActionState& actions, std::optional<Step> step) {
    if (games_.empty()) {
        return;
    }
    if (actions.wasPressed(Action::Map)) {
        mode_ = mode_ == ViewMode::Title ? ViewMode::Zone : mode_ == ViewMode::Zone ? ViewMode::Tiles : ViewMode::Title;
    }
    if (actions.wasPressed(Action::Menu)) {
        mode_ = ViewMode::Title;
    }
    if (games_.size() > 1 &&
        (actions.wasPressed(Action::InventoryNext) || actions.wasPressed(Action::InventoryPrev))) {
        current_ = (current_ + 1) % games_.size();
        zone_ = 0;
        tilePage_ = 0;
    }
    if (!step) {
        return;
    }
    const int delta = step->dx + step->dy * 10; // left/right: 1, up/down: 10
    if (mode_ == ViewMode::Zone) {
        const int count = static_cast<int>(game().data.zones.size());
        zone_ = ((zone_ + delta) % count + count) % count;
    } else if (mode_ == ViewMode::Tiles) {
        const int pages = (static_cast<int>(game().data.tiles.size()) + kTilesPerPage - 1) / kTilesPerPage;
        tilePage_ = ((tilePage_ + delta) % pages + pages) % pages;
    }
}

void Viewer::renderTile(SDL_Renderer* renderer, std::uint16_t tile, const SDL_FRect& dst) const {
    if (tile == data::kNoTile || tile >= game().data.tiles.size()) {
        return;
    }
    const SDL_FRect src{static_cast<float>((tile % kAtlasColumns) * kTile),
                        static_cast<float>((tile / kAtlasColumns) * kTile), kTile, kTile};
    SDL_RenderTexture(renderer, game().atlas, &src, &dst);
}

void Viewer::renderZone(SDL_Renderer* renderer, float x, float y, float size) const {
    const auto& zone = game().data.zones[static_cast<std::size_t>(zone_)];
    const int span = std::max<int>(zone.width, zone.height);
    const float cell = std::floor(size / static_cast<float>(span));
    const float ox = x + (size - cell * zone.width) / 2;
    const float oy = y + (size - cell * zone.height) / 2;
    for (int layer = 0; layer < 3; ++layer) {
        for (int cy = 0; cy < zone.height; ++cy) {
            for (int cx = 0; cx < zone.width; ++cx) {
                const SDL_FRect dst{ox + static_cast<float>(cx) * cell, oy + static_cast<float>(cy) * cell, cell, cell};
                renderTile(renderer, zone.at(cx, cy).layers[static_cast<std::size_t>(layer)], dst);
            }
        }
    }
}

void Viewer::renderTiles(SDL_Renderer* renderer, float x, float y, float size) const {
    const float cell = size / 9;
    const int first = tilePage_ * kTilesPerPage;
    for (int i = 0; i < kTilesPerPage; ++i) {
        const int tile = first + i;
        if (tile >= static_cast<int>(game().data.tiles.size())) {
            break;
        }
        const SDL_FRect dst{x + static_cast<float>(i % 9) * cell, y + static_cast<float>(i / 9) * cell, cell, cell};
        renderTile(renderer, static_cast<std::uint16_t>(tile), dst);
    }
}

void Viewer::render(SDL_Renderer* renderer, float x, float y, float size, float statusY) const {
    if (games_.empty()) {
        return;
    }
    const auto& g = game();
    char line[128];
    switch (mode_) {
    case ViewMode::Title: {
        if (g.setup) {
            const SDL_FRect dst{x, y, size, size};
            SDL_RenderTexture(renderer, g.setup, nullptr, &dst);
        }
        std::snprintf(line, sizeof line, "M/Back: zones and tiles");
        break;
    }
    case ViewMode::Zone: {
        renderZone(renderer, x, y, size);
        const auto& zone = g.data.zones[static_cast<std::size_t>(zone_)];
        std::snprintf(line, sizeof line, "Zone %d/%zu  %dx%d  type %u", zone_, g.data.zones.size(), zone.width,
                      zone.height, static_cast<unsigned>(zone.type));
        break;
    }
    case ViewMode::Tiles: {
        renderTiles(renderer, x, y, size);
        const int first = tilePage_ * kTilesPerPage;
        const int last = std::min<int>(first + kTilesPerPage, static_cast<int>(g.data.tiles.size())) - 1;
        std::snprintf(line, sizeof line, "Tiles %d-%d of %zu", first, last, g.data.tiles.size());
        break;
    }
    }
    text(renderer, x, statusY, g.files.game == data::Layout::Indy ? "Indiana Jones" : "Yoda Stories", 220);
    text(renderer, x, statusY + 14, line, 150);
    if (!g.paletteFromGame) {
        text(renderer, x, statusY + 28, "No .EXE found: showing greyscale", 150);
    }
}

} // namespace das::app
