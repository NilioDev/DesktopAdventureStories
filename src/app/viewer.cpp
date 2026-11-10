// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace das::app {

namespace {

constexpr int kAtlasColumns = 64;
constexpr int kTile = 32;
constexpr int kTilesPerPage = 9 * 9;
// Speech bubble text: characters per line (8 px font) and lines per page.
constexpr std::size_t kBubbleColumns = 33;
constexpr std::size_t kBubbleLines = 12;

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

void Viewer::add(SDL_Renderer* renderer, pack::Pack pack) {
    auto game = std::make_unique<LoadedGame>();
    game->game = pack.data.layout();
    game->data = std::move(pack.data);
    game->palette = pack.palette;
    game->paletteFromGame = pack.palette.colors != data::greyscalePalette().colors;
    try {
        game->hero = data::findHero(game->data);
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No hero sprite: %s", e.what());
    }
    for (const auto& name : game->data.sounds) {
        const auto slash = name.find_last_of("\\/:");
        std::string file = slash == std::string::npos ? name : name.substr(slash + 1);
        std::transform(file.begin(), file.end(), file.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto it = pack.sounds.find("sounds/" + file);
        game->sounds.push_back(it != pack.sounds.end() ? it->second : data::Bytes{});
    }
    game->atlas = buildAtlas(renderer, game->data, game->palette);
    game->setup = buildSetup(renderer, game->data, game->palette);

    std::erase_if(games_, [&](const auto& g) { return g->game == game->game; });
    games_.push_back(std::move(game));
    current_ = games_.size() - 1;
    world_.reset();
    mode_ = mode_ == ViewMode::Play ? ViewMode::Zone : mode_;
    zone_ = 0;
    tilePage_ = 0;
}

void Viewer::play() {
    if (games_.empty()) {
        return;
    }
    effects_.clear();
    bubble_.reset();
    waitUntil_ = 0;
    banner_.clear();
    world_.emplace(game().data);
    world_->start(zone_);
    mode_ = ViewMode::Play;
}

void Viewer::placePlayer(int x, int y) {
    if (world_) {
        world_->enter(world_->zoneIndex(), {x, y});
    }
}

void Viewer::walk(const std::string& path) {
    if (!world_) {
        return;
    }
    for (const char c : path) {
        // Skip through what earlier steps said or waited for.
        for (int guard = 0; guard < 256 && world_->paused(); ++guard) {
            world_->takeEffects();
            world_->resume();
        }
        world_->takeEffects();
        const Step step = c == 'U' ? Step{0, -1} : c == 'D' ? Step{0, 1} : c == 'L' ? Step{-1, 0} : Step{1, 0};
        world_->move(step);
    }
    effects_.clear();
    handleEffects(ActionState{}, SDL_GetTicks());
}

void Viewer::selectGame(data::Layout which) {
    world_.reset();
    if (mode_ == ViewMode::Play) {
        mode_ = ViewMode::Zone;
    }
    for (std::size_t i = 0; i < games_.size(); ++i) {
        if (games_[i]->game == which) {
            current_ = i;
        }
    }
}

void Viewer::selectZone(int zone) {
    if (!games_.empty()) {
        zone_ = std::clamp(zone, 0, static_cast<int>(game().data.zones.size()) - 1);
    }
}

void Viewer::update(const ActionState& actions, std::optional<Step> step, Uint64 now) {
    if (games_.empty()) {
        return;
    }
    if (mode_ == ViewMode::Play) {
        if (actions.wasPressed(Action::Menu) || actions.wasPressed(Action::Map)) {
            zone_ = world_->zoneIndex();
            world_.reset();
            mode_ = ViewMode::Zone;
            return;
        }
        handleEffects(actions, now);
        if (step && !bubble_ && waitUntil_ == 0) {
            const auto result = world_->move(*step);
            if (result != world::MoveResult::Blocked) {
                lastStepAt_ = now;
            }
        }
        return;
    }
    if (mode_ == ViewMode::Zone && actions.wasPressed(Action::Use)) {
        play();
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

namespace {

// Splits text into lines of at most `width` characters, at spaces.
std::vector<std::string> wrap(const std::string& text, std::size_t width) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string paragraph = text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
        std::string line;
        std::size_t i = 0;
        while (i < paragraph.size()) {
            std::size_t end = paragraph.find(' ', i);
            if (end == std::string::npos) {
                end = paragraph.size();
            }
            const std::string word = paragraph.substr(i, end - i);
            if (!line.empty() && line.size() + 1 + word.size() > width) {
                lines.push_back(line);
                line.clear();
            }
            line += (line.empty() ? "" : " ") + word;
            i = end + 1;
        }
        lines.push_back(line);
        if (newline == std::string::npos) {
            break;
        }
        start = newline + 1;
    }
    return lines;
}

} // namespace

void Viewer::handleEffects(const ActionState& actions, Uint64 now) {
    for (auto& e : world_->takeEffects()) {
        effects_.push_back(std::move(e));
    }
    if (bubble_ && (actions.wasPressed(Action::Use) || actions.wasPressed(Action::Attack))) {
        const std::size_t lines = wrap(bubble_->text, kBubbleColumns).size();
        if (static_cast<std::size_t>(bubblePage_ + 1) * kBubbleLines < lines) {
            ++bubblePage_;
        } else {
            bubble_.reset();
            bubblePage_ = 0;
            world_->resume();
        }
    }
    if (waitUntil_ != 0 && now >= waitUntil_) {
        waitUntil_ = 0;
        world_->resume();
    }
    // Pick up what the scripts did, up to the next thing that takes time.
    for (int guard = 0; guard < 64 && !bubble_ && waitUntil_ == 0; ++guard) {
        if (effects_.empty()) {
            for (auto& e : world_->takeEffects()) {
                effects_.push_back(std::move(e));
            }
            if (effects_.empty()) {
                break;
            }
        }
        world::Effect e = std::move(effects_.front());
        effects_.pop_front();
        switch (e.kind) {
        case world::Effect::Kind::Speech:
            bubble_ = std::move(e);
            bubblePage_ = 0;
            break;
        case world::Effect::Kind::Wait: waitUntil_ = now + static_cast<Uint64>(std::max(1, e.value)); break;
        case world::Effect::Kind::Sound:
            if (audio_ && e.value >= 0 && static_cast<std::size_t>(e.value) < game().sounds.size()) {
                audio_->play(game().sounds[static_cast<std::size_t>(e.value)]);
            }
            break;
        case world::Effect::Kind::StopSound:
            if (audio_) {
                audio_->stopAll();
            }
            break;
        case world::Effect::Kind::Won: banner_ = "You won!"; break;
        case world::Effect::Kind::Lost: banner_ = "You lost."; break;
        }
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

void Viewer::renderPlay(SDL_Renderer* renderer, float x, float y, float size, Uint64 now) const {
    const auto& w = *world_;
    const auto& zone = w.zone();
    constexpr int kView = 9;
    const float cell = size / kView;
    // Camera: keep the player in the middle, without showing past the edges.
    const int camX = std::clamp(w.player().x - kView / 2, 0, std::max(0, zone.width - kView));
    const int camY = std::clamp(w.player().y - kView / 2, 0, std::max(0, zone.height - kView));
    const auto dst = [&](int cx, int cy) {
        return SDL_FRect{x + static_cast<float>(cx - camX) * cell, y + static_cast<float>(cy - camY) * cell, cell, cell};
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
        if (at.x >= camX && at.x < endX && at.y >= camY && at.y < endY) {
            renderTile(renderer, t, dst(at.x, at.y));
        }
    }
    if (game().hero && w.heroVisible()) {
        const bool walking = now - lastStepAt_ < 250;
        const int set = walking ? 1 + w.stepCount() % 2 : 0;
        renderTile(renderer, game().hero->frame(set, w.facing()), dst(w.player().x, w.player().y));
    }
    for (int cy = camY; cy < endY; ++cy) {
        for (int cx = camX; cx < endX; ++cx) {
            renderTile(renderer, w.tile({cx, cy}, 2), dst(cx, cy));
        }
    }
    if (bubble_) {
        renderBubble(renderer, x, y, size, cell, camX, camY);
    }
    if (!banner_.empty()) {
        const float w2 = static_cast<float>(banner_.size() * 16 + 32);
        const SDL_FRect box{x + (size - w2) / 2, y + size / 2 - 24, w2, 48};
        SDL_SetRenderDrawColor(renderer, 16, 18, 28, 230);
        SDL_RenderFillRect(renderer, &box);
        SDL_SetRenderScale(renderer, 2, 2);
        text(renderer, (box.x + 16) / 2, (box.y + 16) / 2, banner_, 240);
        SDL_SetRenderScale(renderer, 1, 1);
    }
}


void Viewer::renderBubble(SDL_Renderer* renderer, float x, float y, float size, float cell, int camX,
                          int camY) const {
    const world::Position speaker = bubble_->at.value_or(world_->player());
    constexpr float kLine = 11;
    constexpr float kPad = 8;
    const float width = size - 8;
    const auto all = wrap(bubble_->text, kBubbleColumns);
    const std::size_t first = static_cast<std::size_t>(bubblePage_) * kBubbleLines;
    const std::vector<std::string> lines(all.begin() + static_cast<std::ptrdiff_t>(std::min(first, all.size())),
                                         all.begin() + static_cast<std::ptrdiff_t>(std::min(first + kBubbleLines, all.size())));
    const bool more = first + kBubbleLines < all.size();
    const float height = static_cast<float>(lines.size()) * kLine + 2 * kPad;
    const float sx = x + (static_cast<float>(speaker.x - camX) + 0.5f) * cell;
    const float sy = y + static_cast<float>(speaker.y - camY) * cell;
    // Above the speaker when there is room, otherwise below.
    float by = sy - height - 10;
    const bool below = by < y;
    if (below) {
        by = sy + cell + 10;
    }
    const float bx = std::clamp(sx - width / 2, x + 4, x + size - width - 4);
    by = std::clamp(by, y + 4, y + size - height - 4);

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 90);
    const SDL_FRect shadow{bx + 3, by + 3, width, height};
    SDL_RenderFillRect(renderer, &shadow);
    SDL_SetRenderDrawColor(renderer, 250, 248, 240, 245);
    const SDL_FRect box{bx, by, width, height};
    SDL_RenderFillRect(renderer, &box);
    // A small tail pointing at the speaker.
    for (int i = 0; i < 6; ++i) {
        const float tx = std::clamp(sx, bx + 10, bx + width - 10);
        const float ty = below ? by - static_cast<float>(i) - 1 : by + height + static_cast<float>(i);
        const SDL_FRect row{tx - static_cast<float>(6 - i), ty, static_cast<float>(2 * (6 - i)), 1};
        SDL_RenderFillRect(renderer, &row);
    }
    SDL_SetRenderDrawColor(renderer, 30, 32, 44, 255);
    SDL_RenderRect(renderer, &box);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        SDL_SetRenderDrawColor(renderer, 30, 32, 44, 255);
        SDL_RenderDebugText(renderer, bx + kPad, by + kPad + static_cast<float>(i) * kLine, lines[i].c_str());
    }
    if (more) {
        // A small arrow: there is another page.
        SDL_SetRenderDrawColor(renderer, 30, 32, 44, 255);
        for (int i = 0; i < 4; ++i) {
            const SDL_FRect row{bx + width - 14 + static_cast<float>(i), by + height - 9 + static_cast<float>(i),
                                static_cast<float>(8 - 2 * i), 1};
            SDL_RenderFillRect(renderer, &row);
        }
    }
}

void Viewer::render(SDL_Renderer* renderer, float x, float y, float size, float statusY, Uint64 now) const {
    if (games_.empty()) {
        return;
    }
    const auto& g = game();
    char line[128];
    char detail[128] = "";
    switch (mode_) {
    case ViewMode::Title: {
        if (g.setup) {
            const SDL_FRect dst{x, y, size, size};
            SDL_RenderTexture(renderer, g.setup, nullptr, &dst);
        }
        std::snprintf(line, sizeof line, "M/Back: zones and tiles");
        break;
    }
    case ViewMode::Play: {
        renderPlay(renderer, x, y, size, now);
        std::snprintf(line, sizeof line, "Zone %d  (%d,%d)", world_->zoneIndex(), world_->player().x,
                      world_->player().y);
        std::snprintf(detail, sizeof detail, "Esc/Start: back to the viewer");
        break;
    }
    case ViewMode::Zone: {
        renderZone(renderer, x, y, size);
        const auto& zone = g.data.zones[static_cast<std::size_t>(zone_)];
        std::snprintf(line, sizeof line, "Zone %d/%zu  %dx%d  type %u", zone_, g.data.zones.size(), zone.width,
                      zone.height, static_cast<unsigned>(zone.type));
        std::snprintf(detail, sizeof detail, "Space/A: walk here.  %zu hotspots, %zu scripts",
                      zone.hotspots ? zone.hotspots->size() : 0, zone.actions ? zone.actions->size() : 0);
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
    text(renderer, x, statusY, g.game == data::Layout::Indy ? "Indiana Jones" : "Yoda Stories", 220);
    text(renderer, x, statusY + 14, line, 150);
    if (!g.paletteFromGame) {
        text(renderer, x, statusY + 28, "No .EXE found: showing greyscale", 150);
    } else if (detail[0] != '\0') {
        text(renderer, x, statusY + 28, detail, 150);
    }
}

} // namespace das::app
