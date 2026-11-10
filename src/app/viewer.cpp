// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewer.hpp"

#include "das/world/savegame.hpp"

#include "das/util/text.hpp"

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
    lastUpdate_ = 0;
    world_->start(zone_);
    mode_ = ViewMode::Play;
    generated_ = false;
    selected_ = 0;
    showMap_ = false;
}

void Viewer::newGame(std::uint32_t seed, world::WorldSize size) {
    if (games_.empty()) {
        return;
    }
    world::GeneratorOptions options;
    options.seed = seed;
    options.size = size;
    options_ = options;
    world::WorldMap map;
    try {
        map = world::generateWorld(game().data, options);
    } catch (const std::exception& e) {
        SDL_Log("Could not make a world: %s", e.what());
        return;
    }
    SDL_Log("New world: seed %u, goal puzzle %d, %d puzzles", static_cast<unsigned>(seed), map.goalPuzzle,
            map.puzzleCount);
    effects_.clear();
    bubble_.reset();
    waitUntil_ = 0;
    banner_.clear();
    world_.emplace(game().data);
    lastUpdate_ = 0;
    world_->setRandomSeed(seed);
    world_->setDifficulty(difficultyValue());
    world_->setMap(std::move(map));
    world_->begin();
    mode_ = ViewMode::Play;
    generated_ = true;
    selected_ = 0;
    showMap_ = false;
}

std::string Viewer::gameId() const { return game().game == data::Layout::Indy ? "indy" : "yoda"; }

int Viewer::difficultyValue() const { return difficulty_ == 0 ? 20 : difficulty_ == 2 ? 80 : 50; }

std::filesystem::path Viewer::savePath() const {
    return saveFolder_.empty() ? std::filesystem::path{} : saveFolder_ / (gameId() + ".save.json");
}

bool Viewer::hasSave() const {
    std::error_code ec;
    return !games_.empty() && !saveFolder_.empty() && std::filesystem::is_regular_file(savePath(), ec);
}

void Viewer::autosave() {
    if (!world_ || !generated_ || saveFolder_.empty()) {
        return;
    }
    std::error_code ec;
    if (world_->outcome() != world::Outcome::Playing) {
        std::filesystem::remove(savePath(), ec); // a finished game is not continued
        return;
    }
    if (!world_->canSave()) {
        return;
    }
    try {
        std::filesystem::create_directories(saveFolder_, ec);
        const std::string text = world::writeSaveGame(*world_, options_, gameId());
        const auto temp = savePath().string() + ".new";
        if (FILE* f = std::fopen(temp.c_str(), "wb")) {
            const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
            std::fclose(f);
            if (ok) {
                std::filesystem::rename(temp, savePath(), ec);
            }
        }
    } catch (const std::exception& e) {
        SDL_Log("Could not save the game: %s", e.what());
    }
}

bool Viewer::continueGame() {
    if (!hasSave()) {
        return false;
    }
    try {
        std::string text;
        if (FILE* f = std::fopen(savePath().string().c_str(), "rb")) {
            char buffer[65536];
            std::size_t n = 0;
            while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) {
                text.append(buffer, n);
            }
            std::fclose(f);
        }
        const world::SaveGame save = world::readSaveGame(text);
        if (save.game != gameId()) {
            return false;
        }
        effects_.clear();
        bubble_.reset();
        waitUntil_ = 0;
        banner_.clear();
        world_.emplace(game().data);
        lastUpdate_ = 0;
        world::restoreGame(*world_, game().data, save);
        options_ = save.options;
        mode_ = ViewMode::Play;
        generated_ = true;
        selected_ = 0;
        showMap_ = false;
        return true;
    } catch (const std::exception& e) {
        SDL_Log("Could not load the saved game: %s", e.what());
        world_.reset();
        return false;
    }
}

namespace {

enum class MenuItem { Continue, NewGame, Size, Difficulty };

std::vector<MenuItem> menuItems(bool canContinue) {
    std::vector<MenuItem> items;
    if (canContinue) {
        items.push_back(MenuItem::Continue);
    }
    items.insert(items.end(), {MenuItem::NewGame, MenuItem::Size, MenuItem::Difficulty});
    return items;
}

const char* sizeName(world::WorldSize size) {
    switch (size) {
    case world::WorldSize::Small: return "Small";
    case world::WorldSize::Large: return "Large";
    default: return "Medium";
    }
}

const char* difficultyName(int d) { return d == 0 ? "Easy" : d == 2 ? "Hard" : "Normal"; }

} // namespace

void Viewer::updateTitle(const ActionState& actions, std::optional<Step> step) {
    const auto items = menuItems(hasSave());
    const int count = static_cast<int>(items.size());
    menuItem_ = std::clamp(menuItem_, 0, count - 1);
    if (step && step->dy != 0) {
        menuItem_ = (menuItem_ + step->dy + count) % count;
        return;
    }
    const MenuItem item = items[static_cast<std::size_t>(menuItem_)];
    const int change = step ? step->dx : (actions.wasPressed(Action::Use) ? 1 : 0);
    if (change == 0) {
        return;
    }
    switch (item) {
    case MenuItem::Continue:
        if (actions.wasPressed(Action::Use)) {
            continueGame();
        }
        break;
    case MenuItem::NewGame:
        if (actions.wasPressed(Action::Use)) {
            newGame(static_cast<std::uint32_t>(SDL_GetPerformanceCounter()), size_);
        }
        break;
    case MenuItem::Size: size_ = static_cast<world::WorldSize>((static_cast<int>(size_) + change + 3) % 3); break;
    case MenuItem::Difficulty: difficulty_ = (difficulty_ + change + 3) % 3; break;
    }
}

void Viewer::renderTitleMenu(SDL_Renderer* renderer, float x, float y, float size) const {
    const auto items = menuItems(hasSave());
    std::vector<std::string> labels;
    for (const auto item : items) {
        switch (item) {
        case MenuItem::Continue: labels.emplace_back("Continue"); break;
        case MenuItem::NewGame: labels.emplace_back("New game"); break;
        case MenuItem::Size: labels.push_back(std::string("World size  < ") + sizeName(size_) + " >"); break;
        case MenuItem::Difficulty: labels.push_back(std::string("Difficulty  < ") + difficultyName(difficulty_) + " >"); break;
        }
    }
    std::size_t longest = 1;
    for (const auto& l : labels) {
        longest = std::max(longest, l.size());
    }
    // As large as fits the view, at most one and a half times the font.
    constexpr auto kFont = static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
    const float kScale = std::min(1.5f, (size - 56) / (kFont * static_cast<float>(longest)));
    const float kChar = kFont * kScale;
    const float rowH = 22;
    const float boxW = kChar * static_cast<float>(longest) + 40;
    const float boxH = rowH * static_cast<float>(items.size()) + 20;
    const SDL_FRect box{x + (size - boxW) / 2, y + size - boxH - 24, boxW, boxH};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 14, 16, 24, 215);
    SDL_RenderFillRect(renderer, &box);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const float ry = box.y + 10 + rowH * static_cast<float>(i);
        const bool active = static_cast<int>(i) == std::clamp(menuItem_, 0, static_cast<int>(items.size()) - 1);
        if (active) {
            SDL_SetRenderDrawColor(renderer, 235, 196, 90, 60);
            const SDL_FRect bar{box.x + 8, ry, box.w - 16, rowH};
            SDL_RenderFillRect(renderer, &bar);
        }
        SDL_SetRenderScale(renderer, kScale, kScale);
        SDL_SetRenderDrawColor(renderer, active ? 245 : 190, active ? 215 : 194, active ? 120 : 205, 255);
        SDL_RenderDebugText(renderer, (box.x + 20) / kScale, (ry + (rowH - kChar) / 2) / kScale, labels[i].c_str());
        SDL_SetRenderScale(renderer, 1, 1);
    }
}

std::string Viewer::itemName(std::uint16_t tile) const {
    for (const auto& n : game().data.tileNames) {
        if (n.tile == tile) {
            return text::windows1252ToUtf8(n.name);
        }
    }
    return "item " + std::to_string(tile);
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
        updatePlay(actions, step, now);
        return;
    }
    if (mode_ == ViewMode::Title && (actions.wasPressed(Action::Use) || step)) {
        updateTitle(actions, step);
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

Step facingStep(data::Facing facing) {
    switch (facing) {
    case data::Facing::Up: return {0, -1};
    case data::Facing::Down: return {0, 1};
    case data::Facing::Left:
    case data::Facing::UpLeft:
    case data::Facing::DownLeft: return {-1, 0};
    default: return {1, 0};
    }
}

} // namespace

void Viewer::updatePlay(const ActionState& actions, std::optional<Step> step, Uint64 now) {
    if (actions.wasPressed(Action::Menu) || (!generated_ && actions.wasPressed(Action::Map))) {
        autosave();
        zone_ = world_->zoneIndex();
        world_.reset();
        mode_ = generated_ ? ViewMode::Title : ViewMode::Zone;
        generated_ = false;
        return;
    }
    if (actions.wasPressed(Action::Map)) {
        showMap_ = !showMap_;
    }
    const bool hadBubble = bubble_.has_value();
    handleEffects(actions, now);
    if (showMap_) {
        return;
    }
    const auto& inventory = world_->inventory();
    if (!inventory.empty()) {
        const std::size_t count = inventory.size();
        if (actions.wasPressed(Action::InventoryNext)) {
            selected_ = (selected_ + 1) % count;
        }
        if (actions.wasPressed(Action::InventoryPrev)) {
            selected_ = (selected_ + count - 1) % count;
        }
        selected_ = std::min(selected_, count - 1);
    }
    const int elapsed = lastUpdate_ == 0 ? 0 : static_cast<int>(std::min<Uint64>(now - lastUpdate_, 100));
    lastUpdate_ = now;
    if (hadBubble || bubble_ || waitUntil_ != 0) {
        return;
    }
    // Game time: monsters move and attack, shots fly.
    world_->update(elapsed);
    handleEffects(ActionState{}, now);
    if (bubble_ || waitUntil_ != 0) {
        return;
    }
    if (actions.wasPressed(Action::Attack)) {
        world_->attack();
        handleEffects(ActionState{}, now);
        return;
    }
    if (actions.wasPressed(Action::Use) && !inventory.empty()) {
        const Step ahead = facingStep(world_->facing());
        const world::Position at{world_->player().x + ahead.dx, world_->player().y + ahead.dy};
        const std::uint16_t item = inventory[selected_];
        // Using a weapon on nothing takes it in hand.
        if (!world_->useItem(item, at) && world_->isWeapon(item)) {
            world_->equip(item);
        }
        handleEffects(ActionState{}, now);
        return;
    }
    if (step) {
        const auto result = world_->move(*step);
        if (result != world::MoveResult::Blocked) {
            lastStepAt_ = now;
        }
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
        case world::Effect::Kind::ItemFound: {
            const auto& inventory = world_->inventory();
            const auto it = std::find(inventory.begin(), inventory.end(), static_cast<std::uint16_t>(e.value));
            if (it != inventory.end()) {
                selected_ = static_cast<std::size_t>(it - inventory.begin());
            }
            bubble_ = world::Effect{world::Effect::Kind::Speech, "Found: " + itemName(static_cast<std::uint16_t>(e.value)),
                                    std::nullopt, 0};
            bubblePage_ = 0;
            break;
        }
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
    for (const auto& m : w.monsters()) {
        if (m.at.x >= camX && m.at.x < endX && m.at.y >= camY && m.at.y < endY) {
            renderTile(renderer, m.tile, dst(m.at.x, m.at.y));
        }
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
        if (shot.at.x >= camX && shot.at.x < endX && shot.at.y >= camY && shot.at.y < endY) {
            renderTile(renderer, shot.tile, dst(shot.at.x, shot.at.y));
        }
    }
    for (int cy = camY; cy < endY; ++cy) {
        for (int cx = camX; cx < endX; ++cx) {
            renderTile(renderer, w.tile({cx, cy}, 2), dst(cx, cy));
        }
    }
    if (bubble_) {
        renderBubble(renderer, x, y, size, cell, camX, camY);
    }
    if (showMap_) {
        renderMap(renderer, x, y, size);
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


void Viewer::renderInventory(SDL_Renderer* renderer, float x, float y) const {
    const auto& inventory = world_->inventory();
    constexpr float kIcon = 16;
    constexpr std::size_t kMax = 18;
    const std::size_t first = selected_ >= kMax ? selected_ - kMax + 1 : 0;
    for (std::size_t i = first; i < inventory.size() && i < first + kMax; ++i) {
        const SDL_FRect dst{x + static_cast<float>(i - first) * (kIcon + 0), y, kIcon, kIcon};
        if (i == selected_) {
            SDL_SetRenderDrawColor(renderer, 235, 196, 90, 255);
            const SDL_FRect frame{dst.x - 1, dst.y - 1, kIcon + 2, kIcon + 2};
            SDL_RenderRect(renderer, &frame);
        }
        renderTile(renderer, inventory[i], dst);
    }
    if (inventory.empty()) {
        SDL_SetRenderDrawColor(renderer, 110, 114, 125, 255);
        SDL_RenderDebugText(renderer, x, y + 4, "Nothing carried");
    } else {
        SDL_SetRenderDrawColor(renderer, 150, 154, 165, 255);
        SDL_RenderDebugText(renderer, x, y + kIcon + 4, itemName(inventory[selected_]).c_str());
    }
}

void Viewer::renderHealth(SDL_Renderer* renderer, float x, float y) const {
    // The weapon in hand, then a health bar that turns from green to red.
    const SDL_FRect icon{x, y, 16, 16};
    renderTile(renderer, world_->weapon(), icon);
    const float ratio = static_cast<float>(world_->health()) / static_cast<float>(world::kMaxHealth);
    const SDL_FRect back{x + 22, y + 4, 74, 8};
    SDL_SetRenderDrawColor(renderer, 40, 44, 54, 255);
    SDL_RenderFillRect(renderer, &back);
    const auto red = static_cast<Uint8>(std::clamp(255.0f * (1.0f - ratio) * 1.6f, 60.0f, 235.0f));
    const auto green = static_cast<Uint8>(std::clamp(255.0f * ratio * 1.2f, 50.0f, 200.0f));
    SDL_SetRenderDrawColor(renderer, red, green, 70, 255);
    const SDL_FRect bar{back.x, back.y, back.w * ratio, back.h};
    SDL_RenderFillRect(renderer, &bar);
}

void Viewer::renderMap(SDL_Renderer* renderer, float x, float y, float size) const {
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
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 14, 20, 225);
    const SDL_FRect panel{x, y, size, size};
    SDL_RenderFillRect(renderer, &panel);
    const float cell = std::floor((size - 32) / static_cast<float>(std::max(map->width, map->height)));
    const float ox = x + (size - cell * static_cast<float>(map->width)) / 2;
    const float oy = y + (size - cell * static_cast<float>(map->height)) / 2;
    for (int cy = 0; cy < map->height; ++cy) {
        for (int cx = 0; cx < map->width; ++cx) {
            const auto& site = map->at(cx, cy);
            if (site.kind == world::SiteKind::None) {
                continue;
            }
            const bool solved = world_->goal(site.zone).solved;
            switch (site.kind) {
            case world::SiteKind::Town: SDL_SetRenderDrawColor(renderer, 90, 150, 230, 255); break;
            case world::SiteKind::Goal: SDL_SetRenderDrawColor(renderer, 220, 80, 70, 255); break;
            case world::SiteKind::Puzzle:
                if (solved) {
                    SDL_SetRenderDrawColor(renderer, 110, 190, 110, 255);
                } else {
                    SDL_SetRenderDrawColor(renderer, 230, 180, 80, 255);
                }
                break;
            default: SDL_SetRenderDrawColor(renderer, 70, 80, 72, 255); break;
            }
            const SDL_FRect box{ox + static_cast<float>(cx) * cell + 1, oy + static_cast<float>(cy) * cell + 1,
                                cell - 2, cell - 2};
            SDL_RenderFillRect(renderer, &box);
            if (cx == hereX && cy == hereY) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                const SDL_FRect mark{box.x + cell / 2 - 4, box.y + cell / 2 - 4, 6, 6};
                SDL_RenderFillRect(renderer, &mark);
            }
        }
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
        renderTitleMenu(renderer, x, y, size);
        std::snprintf(line, sizeof line, "Arrows: choose  Space/A: start  M: zones");
        break;
    }
    case ViewMode::Play: {
        renderPlay(renderer, x, y, size, now);
        if (generated_) {
            text(renderer, x, statusY, g.game == data::Layout::Indy ? "Indiana Jones" : "Yoda Stories", 220);
            char where[64];
            std::snprintf(where, sizeof where, "Zone %d", world_->zoneIndex());
            text(renderer, x + size - static_cast<float>(std::strlen(where) * 8), statusY, where, 110);
            renderInventory(renderer, x, statusY + 14);
            renderHealth(renderer, x + size - 96, statusY + 14);
            return;
        }
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
