// SPDX-License-Identifier: GPL-3.0-or-later
//
// The front end's state and input. Drawing is in viewer_draw.cpp.
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
constexpr Uint64 kWalkStepMs = 150; // walking to a tapped cell

#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
constexpr bool kMobile = true;
#else
constexpr bool kMobile = false;
#endif

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

const char* sizeName(world::WorldSize size) {
    switch (size) {
    case world::WorldSize::Small: return "Small";
    case world::WorldSize::Large: return "Large";
    default: return "Medium";
    }
}

const char* difficultyName(int d) { return d == 0 ? "Easy" : d == 2 ? "Hard" : "Normal"; }

const char* filterName(Filter f) {
    switch (f) {
    case Filter::Smooth: return "Smooth";
    case Filter::Scanlines: return "Scanlines";
    default: return "Sharp";
    }
}

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

int wrapAround(int value, int count) { return ((value % count) + count) % count; }

} // namespace

// Splits text into lines of at most `width` characters, at spaces.
std::vector<std::string> wrapText(const std::string& text, std::size_t width) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::string paragraph =
            text.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
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
    mode_ = mode_ == ViewMode::Play ? ViewMode::Title : mode_;
    page_ = Page::Main;
    zone_ = 0;
    tilePage_ = 0;
    applyFilter();
}

void Viewer::applyFilter() {
    const bool smooth = settings_ && settings_->filter == Filter::Smooth;
    for (const auto& g : games_) {
        for (SDL_Texture* t : {g->atlas, g->setup}) {
            if (t) {
                SDL_SetTextureScaleMode(t, smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
            }
        }
    }
}

bool Viewer::takeSettingsChanged() {
    const bool changed = settingsChanged_;
    settingsChanged_ = false;
    return changed;
}

void Viewer::startPlay() {
    effects_.clear();
    bubble_.reset();
    waitUntil_ = 0;
    banner_.clear();
    lastUpdate_ = 0;
    mode_ = ViewMode::Play;
    page_ = Page::None;
    selected_ = 0;
    showMap_ = false;
    walkTarget_.reset();
}

void Viewer::play() {
    if (games_.empty()) {
        return;
    }
    world_.emplace(game().data);
    world_->start(zone_);
    startPlay();
    generated_ = false;
}

void Viewer::newGame(std::uint32_t seed, world::WorldSize size) {
    if (games_.empty()) {
        return;
    }
    world::GeneratorOptions options;
    options.seed = seed;
    options.size = size;
    world::WorldMap map;
    try {
        map = world::generateWorld(game().data, options);
    } catch (const std::exception& e) {
        SDL_Log("Could not make a world: %s", e.what());
        return;
    }
    SDL_Log("New world: seed %u, goal puzzle %d, %d puzzles", static_cast<unsigned>(seed), map.goalPuzzle,
            map.puzzleCount);
    options_ = options;
    world_.emplace(game().data);
    world_->setRandomSeed(seed);
    world_->setDifficulty(difficultyValue());
    world_->setMap(std::move(map));
    world_->begin();
    startPlay();
    generated_ = true;
}

void Viewer::leavePlay() {
    autosave();
    zone_ = world_->zoneIndex();
    world_.reset();
    mode_ = generated_ ? ViewMode::Title : ViewMode::Zone;
    page_ = Page::Main;
    menuItem_ = 0;
    generated_ = false;
    showMap_ = false;
    walkTarget_.reset();
}

void Viewer::openMenu(bool options) {
    if (mode_ == ViewMode::Play) {
        page_ = Page::Pause;
    }
    if (options) {
        optionsFrom_ = page_;
        page_ = Page::Options;
    }
    menuItem_ = 0;
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
        world_.emplace(game().data);
        world::restoreGame(*world_, game().data, save);
        options_ = save.options;
        startPlay();
        generated_ = true;
        return true;
    } catch (const std::exception& e) {
        SDL_Log("Could not load the saved game: %s", e.what());
        world_.reset();
        return false;
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
        mode_ = ViewMode::Title;
        page_ = Page::Main;
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

void Viewer::pointerPressed(float x, float y) { click_ = SDL_FPoint{x, y}; }

// --- menus -----------------------------------------------------------------

std::vector<Viewer::Row> Viewer::menuRows() const {
    std::vector<Row> rows;
    switch (page_) {
    case Page::Main:
        if (hasSave()) {
            rows.push_back({RowId::Continue, "Continue", ""});
        }
        rows.push_back({RowId::NewGame, "New game", ""});
        if (games_.size() > 1) {
            rows.push_back({RowId::Game, "Game", game().game == data::Layout::Indy ? "Indy" : "Yoda"});
        }
        rows.push_back({RowId::Size, "World size", sizeName(size_)});
        rows.push_back({RowId::Difficulty, "Difficulty", difficultyName(difficulty_)});
        rows.push_back({RowId::Options, "Options", ""});
        if (!kMobile) {
            rows.push_back({RowId::DataViewer, "Data viewer", ""});
            rows.push_back({RowId::Quit, "Quit", ""});
        }
        break;
    case Page::Pause:
        rows.push_back({RowId::Resume, "Resume", ""});
        if (generated_) {
            rows.push_back({RowId::Map, "Map", ""});
        }
        rows.push_back({RowId::Options, "Options", ""});
        rows.push_back({RowId::SaveAndQuit, generated_ ? "Save and quit" : "Back to viewer", ""});
        break;
    case Page::Options: {
        const Settings s = settings_ ? *settings_ : Settings{};
        if (!kMobile) {
            rows.push_back({RowId::Fullscreen, "Fullscreen", s.fullscreen ? "On" : "Off"});
        }
        rows.push_back({RowId::Scaling, "Scaling", s.pixelPerfect ? "Pixel" : "Fill"});
        rows.push_back({RowId::Filter, "Filter", filterName(s.filter)});
        rows.push_back({RowId::Volume, "Sound", std::to_string(s.volume) + "%"});
        rows.push_back({RowId::Back, "Back", ""});
        break;
    }
    case Page::None: break;
    }
    return rows;
}

void Viewer::updateMenu(const ActionState& actions, std::optional<Step> step) {
    const auto rows = menuRows();
    if (rows.empty()) {
        return;
    }
    const int count = static_cast<int>(rows.size());
    menuItem_ = std::clamp(menuItem_, 0, count - 1);
    if (actions.wasPressed(Action::Menu) || actions.wasPressed(Action::Attack)) {
        menuBack();
        return;
    }
    if (mode_ == ViewMode::Title && page_ == Page::Main && actions.wasPressed(Action::Map) && !kMobile) {
        mode_ = ViewMode::Zone;
        return;
    }
    if (step && step->dy != 0) {
        menuItem_ = wrapAround(menuItem_ + step->dy, count);
        return;
    }
    const Row& row = rows[static_cast<std::size_t>(menuItem_)];
    if (step && step->dx != 0 && !row.value.empty()) {
        menuActivate(row.id, step->dx);
        return;
    }
    if (actions.wasPressed(Action::Use)) {
        menuActivate(row.id, 1);
    }
}

void Viewer::menuActivate(RowId id, int change) {
    auto changeSetting = [&](auto&& apply) {
        if (settings_) {
            apply(*settings_);
            settingsChanged_ = true;
        }
    };
    switch (id) {
    case RowId::Continue: continueGame(); break;
    case RowId::NewGame: newGame(static_cast<std::uint32_t>(SDL_GetPerformanceCounter()), size_); break;
    case RowId::Game:
        current_ = static_cast<std::size_t>(wrapAround(static_cast<int>(current_) + change, static_cast<int>(games_.size())));
        zone_ = 0;
        tilePage_ = 0;
        break;
    case RowId::Size: size_ = static_cast<world::WorldSize>(wrapAround(static_cast<int>(size_) + change, 3)); break;
    case RowId::Difficulty: difficulty_ = wrapAround(difficulty_ + change, 3); break;
    case RowId::Options:
        optionsFrom_ = page_;
        page_ = Page::Options;
        menuItem_ = 0;
        break;
    case RowId::DataViewer: mode_ = ViewMode::Zone; break;
    case RowId::Quit: quit_ = true; break;
    case RowId::Resume: page_ = Page::None; break;
    case RowId::Map:
        page_ = Page::None;
        showMap_ = true;
        break;
    case RowId::SaveAndQuit: leavePlay(); break;
    case RowId::Fullscreen: changeSetting([](Settings& s) { s.fullscreen = !s.fullscreen; }); break;
    case RowId::Scaling: changeSetting([](Settings& s) { s.pixelPerfect = !s.pixelPerfect; }); break;
    case RowId::Filter:
        changeSetting([&](Settings& s) { s.filter = static_cast<Filter>(wrapAround(static_cast<int>(s.filter) + change, 3)); });
        applyFilter();
        break;
    case RowId::Volume:
        changeSetting([&](Settings& s) {
            // Pressing on 100% starts again at 0; left and right stop at the ends.
            if (change > 0 && s.volume >= 100) {
                s.volume = 0;
            } else {
                s.volume = std::clamp(s.volume + 10 * change, 0, 100);
            }
        });
        break;
    case RowId::Back: menuBack(); break;
    }
}

void Viewer::menuBack() {
    switch (page_) {
    case Page::Options:
        page_ = optionsFrom_;
        menuItem_ = 0;
        break;
    case Page::Pause: page_ = Page::None; break;
    case Page::Main:
    case Page::None: break;
    }
}

// --- input -----------------------------------------------------------------

void Viewer::update(const ActionState& actions, std::optional<Step> step, Uint64 now) {
    if (games_.empty()) {
        click_.reset();
        return;
    }
    if (click_) {
        handleClick(now);
    }
    if (mode_ == ViewMode::Play) {
        updatePlay(actions, step, now);
        return;
    }
    if (mode_ == ViewMode::Title) {
        updateMenu(actions, step);
        return;
    }
    // The data viewer.
    if (mode_ == ViewMode::Zone && actions.wasPressed(Action::Use)) {
        play();
        return;
    }
    if (actions.wasPressed(Action::Map)) {
        mode_ = mode_ == ViewMode::Zone ? ViewMode::Tiles : ViewMode::Zone;
    }
    if (actions.wasPressed(Action::Menu) || actions.wasPressed(Action::Attack)) {
        mode_ = ViewMode::Title;
        page_ = Page::Main;
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
        zone_ = wrapAround(zone_ + delta, static_cast<int>(game().data.zones.size()));
    } else if (mode_ == ViewMode::Tiles) {
        const int pages = (static_cast<int>(game().data.tiles.size()) + kTilesPerPage - 1) / kTilesPerPage;
        tilePage_ = wrapAround(tilePage_ + delta, pages);
    }
}

void Viewer::handleClick(Uint64 now) {
    const SDL_FPoint at = *click_;
    click_.reset();
    const Hit* hit = nullptr;
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (ui::contains(it->rect, at.x, at.y)) {
            hit = &*it;
            break;
        }
    }
    const bool inMenu = mode_ == ViewMode::Title || (mode_ == ViewMode::Play && page_ != Page::None);
    if (inMenu) {
        if (hit && (hit->kind == Hit::Kind::Row || hit->kind == Hit::Kind::RowLeft)) {
            const auto rows = menuRows();
            if (hit->index >= 0 && hit->index < static_cast<int>(rows.size())) {
                menuItem_ = hit->index;
                menuActivate(rows[static_cast<std::size_t>(hit->index)].id, hit->kind == Hit::Kind::RowLeft ? -1 : 1);
            }
        }
        return;
    }
    if (mode_ == ViewMode::Zone) {
        if (hit && hit->kind == Hit::Kind::Field) {
            play();
        }
        return;
    }
    if (mode_ != ViewMode::Play || !world_) {
        return;
    }
    if (hit && hit->kind == Hit::Kind::Menu) {
        if (generated_) {
            openMenu(false);
        } else {
            leavePlay();
        }
        return;
    }
    if (hit && hit->kind == Hit::Kind::Map) {
        showMap_ = !showMap_;
        return;
    }
    if (bubble_) {
        ActionState tap; // a tap anywhere moves the speech on
        tap.press(Action::Use);
        handleEffects(tap, now);
        return;
    }
    if (showMap_) {
        showMap_ = false;
        return;
    }
    if (!hit) {
        return;
    }
    if (hit->kind == Hit::Kind::Slot) {
        const auto index = static_cast<std::size_t>(hit->index);
        if (index == selected_) {
            useSelected(now);
        } else if (index < world_->inventory().size()) {
            selected_ = index;
        }
        return;
    }
    if (hit->kind == Hit::Kind::Field && waitUntil_ == 0) {
        const world::Position target{camX_ + static_cast<int>(std::floor((at.x - hit->rect.x) / cell_)),
                                     camY_ + static_cast<int>(std::floor((at.y - hit->rect.y) / cell_))};
        if (world_->inside(target)) {
            walkTarget_ = target;
            nextWalkAt_ = 0;
        }
    }
}

std::optional<Step> Viewer::stepTowards(world::Position target) const {
    const auto& w = *world_;
    const world::Position from = w.player();
    if (from == target) {
        return std::nullopt;
    }
    const int dx = target.x - from.x;
    const int dy = target.y - from.y;
    if (std::abs(dx) + std::abs(dy) == 1) {
        return Step{dx, dy}; // next to it: step (or bump) into it
    }
    // Breadth-first search over walkable cells, to the target or, when the
    // target cannot be walked on, to a cell next to it.
    const int width = w.zone().width;
    const int height = w.zone().height;
    const auto index = [&](world::Position p) { return static_cast<std::size_t>(p.y * width + p.x); };
    std::vector<int> came(static_cast<std::size_t>(width * height), -1);
    std::deque<world::Position> queue{from};
    came[index(from)] = static_cast<int>(index(from));
    const bool targetWalkable = w.walkable(target);
    constexpr Step kSteps[4] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    std::optional<world::Position> end;
    while (!queue.empty() && !end) {
        const world::Position p = queue.front();
        queue.pop_front();
        for (const Step s : kSteps) {
            const world::Position n{p.x + s.dx, p.y + s.dy};
            if (!w.inside(n) || came[index(n)] >= 0) {
                continue;
            }
            if (n == target && !targetWalkable) {
                end = p;
                break;
            }
            if (!w.walkable(n)) {
                continue;
            }
            came[index(n)] = static_cast<int>(index(p));
            if (n == target) {
                end = n;
                break;
            }
            queue.push_back(n);
        }
    }
    if (!end) {
        return std::nullopt;
    }
    if (*end == from) {
        return Step{dx, dy}; // cannot happen: the target would be next to us
    }
    // Back from the end to the first step.
    world::Position p = *end;
    for (;;) {
        const int prev = came[index(p)];
        const world::Position q{prev % width, prev / width};
        if (q == from) {
            return Step{p.x - from.x, p.y - from.y};
        }
        p = q;
    }
}

void Viewer::useSelected(Uint64 now) {
    const auto& inventory = world_->inventory();
    if (inventory.empty() || bubble_ || waitUntil_ != 0) {
        return;
    }
    selected_ = std::min(selected_, inventory.size() - 1);
    const Step ahead = facingStep(world_->facing());
    const world::Position at{world_->player().x + ahead.dx, world_->player().y + ahead.dy};
    const std::uint16_t item = inventory[selected_];
    // Using a weapon on nothing takes it in hand.
    if (!world_->useItem(item, at) && world_->isWeapon(item)) {
        world_->equip(item);
    }
    handleEffects(ActionState{}, now);
}

void Viewer::updatePlay(const ActionState& actions, std::optional<Step> step, Uint64 now) {
    if (mode_ != ViewMode::Play || !world_) {
        return;
    }
    if (page_ != Page::None) {
        updateMenu(actions, step); // the game waits while a menu is open
        lastUpdate_ = now;
        return;
    }
    if (actions.wasPressed(Action::Menu)) {
        if (showMap_) {
            showMap_ = false;
        } else if (generated_) {
            openMenu(false);
            walkTarget_.reset();
        } else {
            leavePlay();
        }
        return;
    }
    if (!generated_ && actions.wasPressed(Action::Map)) {
        leavePlay();
        return;
    }
    if (actions.wasPressed(Action::Map)) {
        showMap_ = !showMap_;
    }
    const bool hadBubble = bubble_.has_value();
    handleEffects(actions, now);
    if (showMap_) {
        if (actions.wasPressed(Action::Attack) || actions.wasPressed(Action::Use)) {
            showMap_ = false;
        }
        lastUpdate_ = now;
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
        walkTarget_.reset();
        return;
    }
    if (actions.wasPressed(Action::Attack)) {
        walkTarget_.reset();
        world_->attack();
        handleEffects(ActionState{}, now);
        return;
    }
    if (actions.wasPressed(Action::Use) && !inventory.empty()) {
        walkTarget_.reset();
        useSelected(now);
        return;
    }
    if (step) {
        walkTarget_.reset();
        if (world_->move(*step) != world::MoveResult::Blocked) {
            lastStepAt_ = now;
        }
        return;
    }
    if (walkTarget_ && now >= nextWalkAt_) {
        const int zoneBefore = world_->zoneIndex();
        const auto next = stepTowards(*walkTarget_);
        if (!next) {
            walkTarget_.reset();
            return;
        }
        const auto result = world_->move(*next);
        if (result != world::MoveResult::Blocked) {
            lastStepAt_ = now;
        }
        nextWalkAt_ = now + kWalkStepMs;
        if (result == world::MoveResult::Blocked || world_->zoneIndex() != zoneBefore ||
            world_->player() == *walkTarget_) {
            walkTarget_.reset();
        }
        handleEffects(ActionState{}, now);
    }
}

void Viewer::handleEffects(const ActionState& actions, Uint64 now) {
    for (auto& e : world_->takeEffects()) {
        effects_.push_back(std::move(e));
    }
    if (bubble_ && (actions.wasPressed(Action::Use) || actions.wasPressed(Action::Attack))) {
        const std::size_t lines = wrapText(bubble_->text, bubbleColumns_).size();
        if (static_cast<std::size_t>(bubblePage_ + 1) * bubbleLines_ < lines) {
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
            bubble_ = world::Effect{world::Effect::Kind::Speech,
                                    "Found: " + itemName(static_cast<std::uint16_t>(e.value)), std::nullopt, 0};
            bubblePage_ = 0;
            break;
        }
        }
    }
}

} // namespace das::app
