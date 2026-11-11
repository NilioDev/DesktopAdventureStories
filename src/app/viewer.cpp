// SPDX-License-Identifier: GPL-3.0-or-later
//
// The front end's state and input. Drawing is in viewer_draw.cpp.
#include "viewer.hpp"

#include "das/pack/names.hpp"

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

// Splits text into lines of at most `width` characters, at spaces. A word
// longer than a line (a file path) is cut.
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
            std::string word = paragraph.substr(i, end - i);
            if (!line.empty() && line.size() + 1 + word.size() > width) {
                lines.push_back(line);
                line.clear();
            }
            while (width > 0 && word.size() > width) {
                lines.push_back(word.substr(0, width));
                word.erase(0, width);
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
    renderer_ = renderer;
    auto game = std::make_unique<LoadedGame>();
    game->game = pack.data.layout();
    game->base = std::move(pack);
    rebuild(*game);

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

void Viewer::rebuild(LoadedGame& g) {
    pack::Pack pack = g.base;
    g.mods.clear();
    g.modProblem.clear();
    if (settings_) {
        const auto wanted = settings_->mods.find(g.base.game);
        if (wanted != settings_->mods.end()) {
            for (const auto& id : wanted->second) {
                const auto it = std::find_if(mods_.begin(), mods_.end(), [&](const pack::Mod& m) {
                    return m.info.id == id && m.info.game == g.base.game;
                });
                if (it == mods_.end()) {
                    g.modProblem = "Mod \"" + id + "\" is missing";
                }
            }
        }
        for (const pack::Mod& mod : mods_) {
            if (mod.info.game != g.base.game || wanted == settings_->mods.end() ||
                std::find(wanted->second.begin(), wanted->second.end(), mod.info.id) == wanted->second.end()) {
                continue;
            }
            pack::Pack modded = pack;
            try {
                pack::applyMod(modded, mod);
                pack = std::move(modded);
                g.mods.push_back(mod.info.id);
            } catch (const std::exception& e) {
                g.modProblem = mod.info.name + ": " + e.what();
                SDL_Log("Mod %s left out: %s", mod.info.id.c_str(), e.what());
            }
        }
    }

    install(g, std::move(pack));
}

void Viewer::install(LoadedGame& g, pack::Pack pack) {
    g.data = std::move(pack.data);
    g.palette = pack.palette;
    g.paletteFromGame = pack.palette.colors != data::greyscalePalette().colors;
    g.hero.reset();
    try {
        g.hero = data::findHero(g.data);
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No hero sprite: %s", e.what());
    }
    g.sounds.clear();
    for (const auto& name : g.data.sounds) {
        const auto slash = name.find_last_of("\\/:");
        std::string file = slash == std::string::npos ? name : name.substr(slash + 1);
        std::transform(file.begin(), file.end(), file.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const auto it = pack.sounds.find("sounds/" + file);
        g.sounds.push_back(it != pack.sounds.end() ? it->second : data::Bytes{});
    }
    for (SDL_Texture** t : {&g.atlas, &g.setup}) {
        if (*t) {
            SDL_DestroyTexture(*t);
            *t = nullptr;
        }
    }
    if (renderer_) {
        g.atlas = buildAtlas(renderer_, g.data, g.palette);
        g.setup = buildSetup(renderer_, g.data, g.palette);
    }
    zone_ = std::clamp(zone_, 0, std::max(0, static_cast<int>(g.data.zones.size()) - 1));
    applyFilter();
}

void Viewer::setModsFolder(std::filesystem::path folder) {
    modsFolder_ = std::move(folder);
    rescanMods();
}

void Viewer::rescanMods() {
    modScanProblems_.clear();
    mods_ = modsFolder_.empty() ? std::vector<pack::Mod>{} : pack::findMods(modsFolder_, &modScanProblems_);
    for (const auto& p : modScanProblems_) {
        SDL_Log("Mod not read: %s", p.c_str());
    }
}

std::vector<const pack::Mod*> Viewer::gameMods() const {
    std::vector<const pack::Mod*> out;
    if (games_.empty()) {
        return out;
    }
    for (const auto& m : mods_) {
        if (m.info.game == game().base.game) {
            out.push_back(&m);
        }
    }
    return out;
}

void Viewer::openMods() {
    if (mode_ != ViewMode::Title || modsFolder_.empty()) {
        return;
    }
    rescanMods();
    page_ = Page::Mods;
    menuItem_ = 0;
}

void Viewer::openEditor() {
    if (games_.empty()) {
        return;
    }
    if (modsFolder_.empty()) {
        notice_ = "The editor needs a library folder";
        return;
    }
    LoadedGame& g = game();
    rescanMods();
    // The editor works on the base game with only its own mod on top, so
    // that saving stores just what was changed in it.
    Editor::Host host;
    host.info.id = "my-" + g.base.game;
    host.info.game = g.base.game;
    host.info.name = "My changes";
    host.info.description = "Made with the editor.";
    host.modFolder = modsFolder_ / host.info.id;
    pack::Pack pack = g.base;
    for (const auto& mod : mods_) {
        if (mod.info.id != host.info.id || mod.info.game != g.base.game) {
            continue;
        }
        try {
            pack::applyMod(pack, mod);
        } catch (const std::exception& e) {
            notice_ = mod.info.name + ": " + e.what();
            return;
        }
        host.info = mod.info;
        if (std::filesystem::is_directory(mod.location)) {
            host.modFolder = mod.location;
        }
        // Its scripts as text may still be edited: they are read again
        // before trying out and saving.
        for (const auto& [name, bytes] : mod.files) {
            int zone = -1;
            if (std::sscanf(name.c_str(), "scripts/%d.txt", &zone) == 1 && zone >= 0) {
                host.scriptZones.push_back(zone);
            }
        }
    }
    host.sounds = pack.sounds;
    install(g, std::move(pack));
    g.mods = {host.info.id};
    world_.reset();

    host.data = &g.data;
    host.palette = &g.palette;
    host.atlas = &g.atlas;
    host.base = &g.base;
    host.modsFolder = modsFolder_;
    LoadedGame* loaded = &g;
    host.tileChanged = [loaded](int tile) {
        if (!loaded->atlas || tile < 0 || static_cast<std::size_t>(tile) >= loaded->data.tiles.size()) {
            return;
        }
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kTile * kTile));
        const auto& t = loaded->data.tiles[static_cast<std::size_t>(tile)];
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = rgba(loaded->palette, t.pixels[i], true);
        }
        const SDL_Rect r{(tile % kAtlasColumns) * kTile, (tile / kAtlasColumns) * kTile, kTile, kTile};
        SDL_UpdateTexture(loaded->atlas, &r, pixels.data(), kTile * 4);
    };
    host.playSound = [this](const data::Bytes& sound) {
        if (audio_) {
            audio_->play(sound);
        }
    };
    host.soundsChanged = [loaded](const pack::Files& files) {
        loaded->sounds.clear();
        for (const auto& name : loaded->data.sounds) {
            const auto it = files.find("sounds/" + pack::detail::soundFileName(name));
            loaded->sounds.push_back(it != files.end() ? it->second : data::Bytes{});
        }
    };
    host.tilesChanged = [this, loaded] {
        if (loaded->atlas) {
            SDL_DestroyTexture(loaded->atlas);
        }
        loaded->atlas = renderer_ ? buildAtlas(renderer_, loaded->data, loaded->palette) : nullptr;
        applyFilter();
    };
    host.saved = [this, loaded](const pack::Mod& mod) {
        if (settings_) {
            auto& on = settings_->mods[loaded->base.game];
            if (std::find(on.begin(), on.end(), mod.info.id) == on.end()) {
                on.push_back(mod.info.id);
                settingsChanged_ = true;
            }
        }
        rescanMods();
    };
    editor_ = std::make_unique<Editor>(std::move(host), zone_);
    mode_ = ViewMode::Edit;
    page_ = Page::None;
    notice_.clear();
}

void Viewer::leaveEditor() {
    if (editor_) {
        zone_ = editor_->zone();
    }
    editor_.reset();
    rebuild(game()); // back to the mods that are switched on
    mode_ = ViewMode::Title;
    page_ = Page::Main;
    menuItem_ = 0;
}

void Viewer::editorEvent(const SDL_Event& e) {
    if (editing() && !testing_) {
        editor_->event(e);
    }
}

float Viewer::editorScale(int pixelWidth, int pixelHeight) const {
    return editor_ ? editor_->scaleFor(pixelWidth, pixelHeight) : 1.0f;
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
    nextAutosaveAt_ = 0; // set on the first frame of play
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
    if (settings_ && !settings_->howToPlaySeen) {
        // The very first game starts with how to play it.
        settings_->howToPlaySeen = true;
        settingsChanged_ = true;
        helpFrom_ = Page::None;
        page_ = Page::Help;
        menuItem_ = 0;
    }
}

void Viewer::leavePlay() {
    if (testing_) {
        // Back to the editor.
        world_.reset();
        testing_ = false;
        mode_ = ViewMode::Edit;
        page_ = Page::None;
        showMap_ = false;
        walkTarget_.reset();
        return;
    }
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

std::filesystem::path Viewer::savePath(int slot) const {
    if (saveFolder_.empty()) {
        return {};
    }
    // The autosave keeps the name saves had before there were slots.
    return saveFolder_ / (gameId() + (slot == 0 ? "" : ".slot" + std::to_string(slot)) + ".save.json");
}

int Viewer::newestSlot() const {
    if (games_.empty() || saveFolder_.empty()) {
        return -1;
    }
    int newest = -1;
    SDL_Time newestTime = 0;
    for (int slot = 0; slot <= kSaveSlots; ++slot) {
        SDL_PathInfo info{};
        if (SDL_GetPathInfo(savePath(slot).string().c_str(), &info) && info.type == SDL_PATHTYPE_FILE &&
            (newest < 0 || info.modify_time > newestTime)) {
            newest = slot;
            newestTime = info.modify_time;
        }
    }
    return newest;
}

bool Viewer::hasSave() const { return newestSlot() >= 0; }

bool Viewer::autosave() {
    if (!world_ || !generated_ || saveFolder_.empty()) {
        return false;
    }
    if (world_->outcome() != world::Outcome::Playing) {
        std::error_code ec;
        std::filesystem::remove(savePath(0), ec); // a finished game is not continued
        return false;
    }
    return writeSave(0);
}

bool Viewer::writeSave(int slot) {
    if (!world_ || !generated_ || saveFolder_.empty() || !world_->canSave()) {
        return false;
    }
    try {
        std::error_code ec;
        std::filesystem::create_directories(saveFolder_, ec);
        const std::string text = world::writeSaveGame(*world_, options_, gameId(), game().mods);
        const auto path = savePath(slot);
        const auto temp = path.string() + ".new";
        bool ok = false;
        if (FILE* f = std::fopen(temp.c_str(), "wb")) {
            ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
            ok = std::fclose(f) == 0 && ok;
        }
        if (ok) {
            std::filesystem::rename(temp, path, ec);
            ok = !ec;
        }
        return ok;
    } catch (const std::exception& e) {
        SDL_Log("Could not save the game: %s", e.what());
        return false;
    }
}

void Viewer::enterBackground() {
    if (mode_ != ViewMode::Play || !world_ || !generated_) {
        return;
    }
    autosave();
    if (page_ == Page::None && world_->outcome() == world::Outcome::Playing) {
        openMenu(false);
        walkTarget_.reset();
    }
}

bool Viewer::continueGame() {
    const int slot = newestSlot();
    return slot >= 0 && loadGame(slot);
}

bool Viewer::loadGame(int slot) {
    if (games_.empty() || saveFolder_.empty()) {
        return false;
    }
    try {
        std::string text;
        if (FILE* f = std::fopen(savePath(slot).string().c_str(), "rb")) {
            char buffer[65536];
            std::size_t n = 0;
            while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) {
                text.append(buffer, n);
            }
            std::fclose(f);
        }
        if (text.empty()) {
            return false;
        }
        const world::SaveGame save = world::readSaveGame(text);
        if (save.game != gameId()) {
            return false;
        }
        if (save.mods != game().mods) {
            // Another set of mods makes another world: the save would not fit.
            std::string list;
            for (const auto& m : save.mods) {
                list += (list.empty() ? "" : ", ") + m;
            }
            notice_ = list.empty() ? "This save was made without mods" : "This save needs the mods " + list;
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
        notice_ = "This save could not be read";
        world_.reset();
        return false;
    }
}

void Viewer::openSlots(bool saving) {
    slotsSaving_ = saving;
    confirmSlot_ = -1;
    slotDates_.assign(static_cast<std::size_t>(kSaveSlots) + 1, "");
    for (int slot = 0; slot <= kSaveSlots; ++slot) {
        SDL_PathInfo info{};
        SDL_DateTime t{};
        if (SDL_GetPathInfo(savePath(slot).string().c_str(), &info) && info.type == SDL_PATHTYPE_FILE &&
            SDL_TimeToDateTime(info.modify_time, &t, true)) {
            static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            char text[32];
            SDL_snprintf(text, sizeof text, "%d %s %02d:%02d", t.day, kMonths[(t.month + 11) % 12], t.hour, t.minute);
            slotDates_[static_cast<std::size_t>(slot)] = text;
        }
    }
    page_ = Page::Slots;
    menuItem_ = 0;
}

std::string Viewer::itemName(std::uint16_t tile) const {
    for (const auto& n : game().data.tileNames) {
        if (n.tile == tile) {
            return text::windows1252ToUtf8(n.name);
        }
    }
    return "item " + std::to_string(tile);
}

void Viewer::playSong(const char* name) {
    if (!audio_) {
        return;
    }
    for (std::size_t i = 0; i < game().data.sounds.size() && i < game().sounds.size(); ++i) {
        std::string file = game().data.sounds[i];
        std::transform(file.begin(), file.end(), file.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (file.find(name) != std::string::npos && file.find(".mid") != std::string::npos) {
            audio_->play(game().sounds[i]);
            return;
        }
    }
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
        if (hasSave()) {
            rows.push_back({RowId::LoadGame, "Load game", ""});
        }
        if (games_.size() > 1) {
            rows.push_back({RowId::Game, "Game", game().game == data::Layout::Indy ? "Indy" : "Yoda"});
        }
        rows.push_back({RowId::Size, "World size", sizeName(size_)});
        rows.push_back({RowId::Difficulty, "Difficulty", difficultyName(difficulty_)});
        rows.push_back({RowId::Options, "Options", ""});
        if (!modsFolder_.empty()) {
            const auto on = game().mods.size();
            rows.push_back({RowId::Mods, "Mods", on > 0 ? std::to_string(on) + " on" : ""});
            rows.push_back({RowId::Editor, "Editor", ""});
        }
        rows.push_back({RowId::AddGames, "Add games", ""});
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
        if (generated_) {
            rows.push_back({RowId::SaveGame, "Save game", ""});
            if (hasSave()) {
                rows.push_back({RowId::LoadGame, "Load game", ""});
            }
        }
        rows.push_back({RowId::HowToPlay, "How to play", ""});
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
        rows.push_back({RowId::Music, "Music", std::to_string(s.music) + "%"});
        rows.push_back({RowId::TouchPad, "Touch pad",
                        s.touchPad == TouchPad::On ? "On" : s.touchPad == TouchPad::Off ? "Off" : "Auto"});
        if (bindings_) {
            rows.push_back({RowId::Controls, "Controls", ""});
        }
        rows.push_back({RowId::Report, "Report a problem", ""});
        rows.push_back({RowId::Back, "Back", ""});
        break;
    }
    case Page::Controls: {
        struct Named {
            Action action;
            const char* label;
        };
        static constexpr Named kActions[] = {
            {Action::MoveUp, "Up"},          {Action::MoveDown, "Down"},     {Action::MoveLeft, "Left"},
            {Action::MoveRight, "Right"},    {Action::Use, "Use"},           {Action::Attack, "Attack"},
            {Action::InventoryPrev, "Prev item"}, {Action::InventoryNext, "Next item"}, {Action::Map, "Map"},
            {Action::Menu, "Menu"},
        };
        const bool pad = device_ == InputDevice::Gamepad;
        for (const auto& a : kActions) {
            std::string value;
            if (capture_ == a.action) {
                value = pad ? "Press a button" : "Press a key";
            } else if (bindings_) {
                value = pad ? bindings_->buttonNames(a.action) : bindings_->keyNames(a.action);
            }
            rows.push_back({RowId::Bind, a.label, value, a.action});
        }
        rows.push_back({RowId::ResetControls, "Reset controls", ""});
        rows.push_back({RowId::Back, "Back", ""});
        break;
    }
    case Page::Mods: {
        const auto list = gameMods();
        const auto& on = game().mods;
        const std::vector<std::string>* wanted = nullptr;
        if (settings_) {
            if (const auto it = settings_->mods.find(game().base.game); it != settings_->mods.end()) {
                wanted = &it->second;
            }
        }
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto& id = list[i]->info.id;
            const bool applied = std::find(on.begin(), on.end(), id) != on.end();
            const bool switchedOn = wanted && std::find(wanted->begin(), wanted->end(), id) != wanted->end();
            Row row{RowId::Mod, list[i]->info.name, applied ? "On" : switchedOn ? "Error" : "Off"};
            row.index = static_cast<int>(i);
            rows.push_back(std::move(row));
        }
        if (!kMobile) {
            rows.push_back({RowId::OpenModsFolder, "Open mods folder", ""});
        }
        rows.push_back({RowId::Back, "Back", ""});
        break;
    }
    case Page::Slots:
        // Saving goes to the player's own slots; the autosave is only loaded.
        for (int slot = slotsSaving_ ? 1 : 0; slot <= kSaveSlots; ++slot) {
            const std::string& date =
                static_cast<std::size_t>(slot) < slotDates_.size() ? slotDates_[static_cast<std::size_t>(slot)] : "";
            if (!slotsSaving_ && date.empty()) {
                continue;
            }
            Row row{RowId::Slot, slot == 0 ? "Autosave" : "Slot " + std::to_string(slot),
                    confirmSlot_ == slot ? "Overwrite?" : date.empty() ? "Empty" : date};
            row.index = slot;
            rows.push_back(row);
        }
        rows.push_back({RowId::Back, "Back", ""});
        break;
    case Page::Help: rows.push_back({RowId::Back, helpFrom_ == Page::None ? "Let's go" : "Back", ""}); break;
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
    if (capture_) {
        return; // the next key or button goes to the bindings
    }
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
        menuActivate(row, step->dx);
        return;
    }
    if (actions.wasPressed(Action::Use)) {
        if (row.id == RowId::Bind) {
            startCapture(row.action);
        } else {
            menuActivate(row, 1);
        }
    }
}

void Viewer::startCapture(Action action) {
    capture_ = action;
    captureUntil_ = SDL_GetTicks() + 6000;
}

void Viewer::menuActivate(const Row& row, int change) {
    const RowId id = row.id;
    notice_.clear();
    noticeGood_ = false;
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
    case RowId::AddGames: addGames_ = true; break;
    case RowId::Quit: quit_ = true; break;
    case RowId::Resume: page_ = Page::None; break;
    case RowId::Map:
        page_ = Page::None;
        showMap_ = true;
        break;
    case RowId::SaveAndQuit: leavePlay(); break;
    case RowId::SaveGame:
        if (world_ && !world_->canSave()) {
            notice_ = "Finish the conversation first";
        } else {
            openSlots(true);
        }
        break;
    case RowId::LoadGame: openSlots(false); break;
    case RowId::Slot: {
        if (change != 1) {
            break; // left and right do not save or load
        }
        const int slot = row.index;
        if (!slotsSaving_) {
            const bool inGame = mode_ == ViewMode::Play;
            if (loadGame(slot)) {
                break;
            }
            if (notice_.empty()) {
                notice_ = "This save could not be read";
            }
            if (inGame && !world_) {
                // The game in play was dropped for the save that failed.
                mode_ = ViewMode::Title;
            }
            page_ = mode_ == ViewMode::Play ? Page::Pause : Page::Main;
            menuItem_ = 0;
            break;
        }
        const bool used = static_cast<std::size_t>(slot) < slotDates_.size() &&
                          !slotDates_[static_cast<std::size_t>(slot)].empty();
        if (used && confirmSlot_ != slot) {
            confirmSlot_ = slot; // press again to overwrite
            break;
        }
        if (writeSave(slot)) {
            notice_ = "Saved in slot " + std::to_string(slot);
            page_ = Page::Pause;
            menuItem_ = 0;
        } else {
            notice_ = "Could not save";
            confirmSlot_ = -1;
        }
        break;
    }
    case RowId::Fullscreen: changeSetting([](Settings& s) { s.fullscreen = !s.fullscreen; }); break;
    case RowId::Scaling: changeSetting([](Settings& s) { s.pixelPerfect = !s.pixelPerfect; }); break;
    case RowId::Filter:
        changeSetting([&](Settings& s) { s.filter = static_cast<Filter>(wrapAround(static_cast<int>(s.filter) + change, 3)); });
        applyFilter();
        break;
    case RowId::Volume:
    case RowId::Music:
        changeSetting([&](Settings& s) {
            int& level = id == RowId::Volume ? s.volume : s.music;
            // Pressing on 100% starts again at 0; left and right stop at the ends.
            if (change > 0 && level >= 100) {
                level = 0;
            } else {
                level = std::clamp(level + 10 * change, 0, 100);
            }
        });
        break;
    case RowId::TouchPad:
        changeSetting([&](Settings& s) { s.touchPad = static_cast<TouchPad>(wrapAround(static_cast<int>(s.touchPad) + change, 3)); });
        break;
    case RowId::Controls:
        page_ = Page::Controls;
        menuItem_ = 0;
        break;
    case RowId::Bind: break; // see updateMenu: it needs the row's action
    case RowId::ResetControls:
        changeSetting([](Settings& s) {
            s.keys.clear();
            s.buttons.clear();
        });
        break;
    case RowId::Mods: openMods(); break;
    case RowId::Editor: openEditor(); break;
    case RowId::Mod: {
        const auto list = gameMods();
        if (!settings_ || row.index < 0 || static_cast<std::size_t>(row.index) >= list.size()) {
            break;
        }
        const std::string& modId = list[static_cast<std::size_t>(row.index)]->info.id;
        auto& wanted = settings_->mods[game().base.game];
        const auto it = std::find(wanted.begin(), wanted.end(), modId);
        if (it != wanted.end()) {
            wanted.erase(it);
        } else {
            wanted.push_back(modId);
        }
        if (wanted.empty()) {
            settings_->mods.erase(game().base.game);
        }
        settingsChanged_ = true;
        rebuild(game());
        break;
    }
    case RowId::OpenModsFolder: {
        std::error_code ec;
        std::filesystem::create_directories(modsFolder_, ec);
        const std::string url = "file://" + modsFolder_.generic_string();
        if (!SDL_OpenURL(url.c_str())) {
            notice_ = "Mods are in " + modsFolder_.string();
        }
        break;
    }
    case RowId::Report: report_ = true; break;
    case RowId::HowToPlay:
        helpFrom_ = page_;
        page_ = Page::Help;
        menuItem_ = 0;
        break;
    case RowId::Back: menuBack(); break;
    }
}

void Viewer::menuBack() {
    switch (page_) {
    case Page::Controls:
        page_ = Page::Options;
        menuItem_ = 0;
        break;
    case Page::Options:
        page_ = optionsFrom_;
        menuItem_ = 0;
        break;
    case Page::Pause: page_ = Page::None; break;
    case Page::Mods:
        page_ = Page::Main;
        menuItem_ = 0;
        break;
    case Page::Slots:
        page_ = mode_ == ViewMode::Play ? Page::Pause : Page::Main;
        menuItem_ = 0;
        break;
    case Page::Help:
        page_ = helpFrom_;
        menuItem_ = 0;
        break;
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
    if (capture_ && now > captureUntil_) {
        capture_.reset(); // nothing pressed: keep the old controls
    }
    if (click_ && !capture_) {
        handleClick(now);
    }
    click_.reset();
    if (mode_ == ViewMode::Edit) {
        if (editor_) {
            editor_->actions(actions, step);
            if (const auto zone = editor_->takeTryOut()) {
                world_.emplace(game().data);
                world_->start(*zone);
                startPlay();
                generated_ = false;
                testing_ = true;
            } else if (editor_->takeLeave()) {
                leaveEditor();
            }
        }
        return;
    }
    if (mode_ == ViewMode::Play) {
        updatePlay(actions, step, now);
        return;
    }
    if (mode_ == ViewMode::Title) {
        if (!titleSong_) {
            titleSong_ = true;
            playSong("theme");
        }
        updateMenu(actions, step);
        if (mode_ != ViewMode::Title) {
            titleSong_ = false; // the next visit plays it again
        }
        return;
    }
    titleSong_ = false;
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
                const Row& row = rows[static_cast<std::size_t>(hit->index)];
                if (row.id == RowId::Bind) {
                    startCapture(row.action);
                } else {
                    menuActivate(row, hit->kind == Hit::Kind::RowLeft ? -1 : 1);
                }
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
    notice_.clear(); // a "Saved" note is for the pause menu only
    if (generated_) {
        // Saved every few minutes of play, at the first moment at rest.
        constexpr Uint64 kAutosaveEvery = 2 * 60 * 1000;
        if (nextAutosaveAt_ == 0) {
            nextAutosaveAt_ = now + kAutosaveEvery;
        } else if (now >= nextAutosaveAt_ && world_->canSave()) {
            autosave();
            nextAutosaveAt_ = now + kAutosaveEvery;
        }
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
        case world::Effect::Kind::Won:
            banner_ = "You won!";
            playSong("victory");
            break;
        case world::Effect::Kind::Lost:
            banner_ = "You lost.";
            playSong("defeat");
            break;
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
