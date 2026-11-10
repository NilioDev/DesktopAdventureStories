// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's frame: top bar, tabs, help bar, Undo and saving.
#include "editor.hpp"

#include "das/script/script_text.hpp"
#include "das/util/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace das::app {

namespace {

constexpr int kAtlasColumns = 64;
constexpr float kTile = 32;
constexpr std::size_t kUndoSteps = 200;
constexpr float kTop = 22;
constexpr float kBottom = 36;

std::string scriptFile(int zone) {
    char name[48];
    std::snprintf(name, sizeof name, "scripts/%04d.txt", zone);
    return name;
}

} // namespace

// --- names -------------------------------------------------------------------

namespace names {

std::string zoneType(std::uint32_t type) {
    switch (type) {
    case 0: return "None";
    case 1: return "Open area";
    case 2: return "Blockade north";
    case 3: return "Blockade south";
    case 4: return "Blockade east";
    case 5: return "Blockade west";
    case 6: return "Travel start";
    case 7: return "Travel end";
    case 8: return "Room";
    case 9: return "Loading screen";
    case 10: return "Goal";
    case 11: return "Town";
    case 13: return "Won screen";
    case 14: return "Lost screen";
    case 15: return "Use an item";
    case 16: return "Trade";
    case 17: return "Find an item";
    case 18: return "Find a weapon";
    default: return "Type " + std::to_string(type);
    }
}

std::string zoneTypeHelp(std::uint32_t type) {
    switch (type) {
    case 1: return "An open area: the world generator puts it anywhere to fill the map.";
    case 2:
    case 3:
    case 4:
    case 5: return "A blockade: the way on in this direction opens only with the right item.";
    case 6: return "Where a vehicle leaves for another part of the map.";
    case 7: return "Where a vehicle arrives.";
    case 8: return "A room: only reached through a door (hotspot Door in) of another zone.";
    case 10: return "The goal of a game: the last puzzle is solved here.";
    case 11: return "The town where a game starts.";
    case 13:
    case 14: return "Shown at the end of a game.";
    case 15: return "A puzzle: use an item on something here to get another item.";
    case 16: return "A puzzle: give an item to someone here to get another item.";
    case 17: return "An item to find, which a puzzle elsewhere asks for.";
    case 18: return "A weapon to find.";
    default: return "The world generator chooses zones by their type.";
    }
}

std::string planet(int planet) {
    switch (planet) {
    case 0: return "None";
    case 1: return "Desert";
    case 2: return "Snow";
    case 3: return "Forest";
    case 5: return "Swamp";
    default: return "Planet " + std::to_string(planet);
    }
}

std::string hotspot(std::uint32_t type) {
    static const char* const list[] = {"Item spot", "Person spot", "Weapon spot", "Vehicle to", "Vehicle back",
                                       "Locator",   "Item",        "Person",      "Weapon",     "Door in",
                                       "Door out",  "Unused",      "Lock",        "Teleporter", "Ship out",
                                       "Ship in"};
    return type < std::size(list) ? list[type] : "Type " + std::to_string(type);
}

std::string hotspotHelp(std::uint32_t type) {
    switch (type) {
    case 0: return "Item spot: where the world generator puts the item that this zone's puzzle hands out.";
    case 1: return "Person spot: where a trading person stands.";
    case 2: return "Weapon spot: where a weapon to find is put.";
    case 3: return "Vehicle to: a vehicle that takes the hero away.";
    case 4: return "Vehicle back: a vehicle that brings the hero back.";
    case 5: return "Locator: where the map item lies.";
    case 6: return "Item: an item lying here or hidden in the object. Argument: the item.";
    case 7: return "Person: a person stands here.";
    case 8: return "Weapon: a weapon lies here. Argument: the weapon.";
    case 9: return "Door in: walking here enters another zone. Argument: the zone behind the door.";
    case 10: return "Door out: walking here goes back to where the hero came from.";
    case 12: return "Lock: a door that needs an item.";
    case 13: return "Teleporter: takes the hero to another teleporter.";
    case 14: return "Ship out: flies to another part of the world. Argument: the zone it flies to.";
    case 15: return "Ship in: flies back. Argument: the zone it flies to.";
    default: return "A hotspot type the games do not use.";
    }
}

std::string characterKind(std::uint16_t type) {
    switch (type) {
    case 1: return "the hero";
    case 2: return "a monster or person";
    case 4: return "a weapon";
    default: return "kind " + std::to_string(type);
    }
}

std::string movement(std::uint16_t movement) {
    switch (movement) {
    case 0: return "stands still";
    case 1: return "goes for the hero";
    case 2: return "goes for the hero when close, else wanders";
    case 3: return "wanders";
    case 4: return "stays put and attacks what comes close";
    case 5: return "rolls after the hero";
    case 6: return "goes for the hero when close, else stays";
    case 7: return "moves like a droid";
    case 8: return "hovers";
    case 9: return "wanders and shoots";
    case 10: return "patrols";
    case 11: return "keeps away from the hero";
    case 12: return "is part of the zone: never moves or attacks";
    default: return "moves in way " + std::to_string(movement);
    }
}

} // namespace names

// --- setup -------------------------------------------------------------------

Editor::Editor(Host host, int zone) : host_(std::move(host)) {
    zone_ = std::clamp(zone, 0, static_cast<int>(host_.data->zones.size()) - 1);
    const auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    cursorX_ = z.width / 2;
    cursorY_ = z.height / 2;
    const auto t = z.at(cursorX_, cursorY_).layers[0];
    tile_ = t == data::kNoTile ? 0 : t;
    readCharacters();
    for (std::size_t i = 0; i < characters_.size(); ++i) {
        if (characters_[i].type != 1 && characters_[i].type != 4) {
            character_ = static_cast<int>(i);
            break;
        }
    }
}

void Editor::readCharacters() {
    characters_.clear();
    for (const auto& entry : host_.data->characters) {
        try {
            characters_.push_back(data::parseCharacter(entry.body, host_.data->layout()));
        } catch (const std::exception&) {
            data::Character unknown;
            for (auto& set : unknown.frames) {
                set.fill(data::kNoTile);
            }
            characters_.push_back(unknown);
        }
    }
    character_ = std::min(character_, static_cast<int>(characters_.size()) - 1);
}

std::string Editor::characterName(int character) const {
    if (character < 0 || static_cast<std::size_t>(character) >= characters_.size()) {
        return "Character " + std::to_string(character);
    }
    const auto& name = characters_[static_cast<std::size_t>(character)].name;
    return name.empty() ? "Character " + std::to_string(character) : name;
}

float Editor::scaleFor(int pixelWidth, int pixelHeight) const {
    if (uiScale_ > 0) {
        return static_cast<float>(uiScale_);
    }
    // Room for the panels and a zone of 18 x 18 tiles of 16 units or more.
    const int w = pixelWidth / 760;
    const int h = pixelHeight / 470;
    return static_cast<float>(std::clamp(std::min(w, h), 1, 4));
}

void Editor::actions(const ActionState& actions, std::optional<Step> step) {
    if (step) {
        pending_.navX += step->dx;
        pending_.navY += step->dy;
    }
    if (actions.wasPressed(Action::Use)) {
        pending_.activate = true;
    }
    if (actions.wasPressed(Action::Menu) || actions.wasPressed(Action::Attack)) {
        pending_.back = true;
    }
}

std::optional<int> Editor::takeTryOut() {
    auto z = tryOut_;
    tryOut_.reset();
    return z;
}

bool Editor::takeLeave() {
    const bool l = leave_;
    leave_ = false;
    return l;
}

// --- the frame ---------------------------------------------------------------

void Editor::frame(SDL_Renderer* renderer, const ui::Layout& layout) {
    renderer_ = renderer;
    takeSoundFile();
    gui::Input in = collector_.take();
    in.activate = in.activate || pending_.activate || (in.enter && !gui_.typing());
    in.back = (in.back || pending_.back) && !gui_.typing();
    if (tab_ == Tab::Zones && gui_.focused("canvas") && !gui_.typing()) {
        // On the canvas the arrows and the stick move the cursor; Tab still
        // moves on, and so does going back.
        const auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
        cursorX_ = std::clamp(cursorX_ + pending_.navX, 0, std::max(0, z.width - 1));
        cursorY_ = std::clamp(cursorY_ + pending_.navY, 0, std::max(0, z.height - 1));
        if (in.activate) {
            if (tool_ == Tool::Fill && fillFrom_) {
                fillRect(fillFrom_->first, fillFrom_->second, cursorX_, cursorY_);
                fillFrom_.reset();
            } else {
                applyTool(cursorX_, cursorY_, true);
            }
            in.activate = false;
        }
        if (in.back) {
            in.back = false;
            if (fillFrom_) {
                fillFrom_.reset();
            } else {
                in.navY += 1;
            }
        }
    } else {
        in.navX += pending_.navX;
        in.navY += pending_.navY;
    }
    pending_ = gui::Input{};

    // Shortcuts.
    if (!gui_.typing()) {
        switch (in.shortcut) {
        case 'z': in.shift ? redo() : undo(); break;
        case 'y': redo(); break;
        case 's': save(); break;
        default: break;
        }
    }

    gui_.begin(renderer, in);
    const float w = layout.width;
    const float h = layout.height;
    ui::fill(renderer, {0, 0, w, h}, ui::color::background);
    const float body = h - kTop - kBottom;
    if (tab_ == Tab::Zones) {
        const float left = std::min(170.0f, w * 0.22f);
        const float right = std::min(230.0f, w * 0.3f);
        zonesTab({0, kTop, left, body}, {left, kTop, w - left - right, body}, {w - right, kTop, right, body});
    } else if (tab_ == Tab::Sounds) {
        const float left = std::min(240.0f, w * 0.28f);
        const float right = std::min(260.0f, w * 0.3f);
        soundsTab({0, kTop, left, body}, {left, kTop, w - left - right, body}, {w - right, kTop, right, body});
    } else if (tab_ == Tab::Puzzles) {
        const float left = std::min(240.0f, w * 0.28f);
        const float right = std::min(260.0f, w * 0.3f);
        puzzlesTab({0, kTop, left, body}, {left, kTop, w - left - right, body}, {w - right, kTop, right, body});
    } else if (tab_ == Tab::Characters) {
        const float left = std::min(220.0f, w * 0.26f);
        const float right = std::min(260.0f, w * 0.3f);
        charactersTab({0, kTop, left, body}, {left, kTop, w - left - right, body}, {w - right, kTop, right, body});
    } else if (tab_ == Tab::Scripts) {
        const float left = std::min(170.0f, w * 0.22f);
        const float right = std::min(260.0f, w * 0.3f);
        scriptsTab({0, kTop, left, body}, {left, kTop, w - left - right, body}, {w - right, kTop, right, body});
    } else {
        const float left = std::min(300.0f, w * 0.38f);
        const float right = std::min(240.0f, w * 0.3f);
        tilesTab({0, kTop, left, body}, {left, kTop, w - left - right, body}, {w - right, kTop, right, body});
    }
    topBar({0, 0, w, kTop});
    helpBar({0, h - kBottom, w, kBottom});
    gui_.end();
}

void Editor::topBar(const SDL_FRect& r) {
    ui::fill(renderer_, r, ui::color::panel);
    ui::fill(renderer_, {r.x, r.y + r.h - 1, r.w, 1}, ui::color::border);
    float x = r.x + 4;
    const float y = r.y + 4;
    const float bh = r.h - 8;
    const auto button = [&](std::string_view id, std::string_view label, std::string_view help, bool selected = false,
                            bool enabled = true) {
        const float bw = ui::textWidth(label) + 12;
        const bool pressed = gui_.button(id, {x, y, bw, bh}, label, help, selected, enabled);
        x += bw + 3;
        return pressed;
    };
    if (button("tab-zones", "Zones", "Draw zones: the screens the hero walks through.", tab_ == Tab::Zones)) {
        tab_ = Tab::Zones;
    }
    if (button("tab-tiles", "Tiles", "The tiles: their names and what they do (floor, wall, item...).",
               tab_ == Tab::Tiles)) {
        tab_ = Tab::Tiles;
    }
    if (button("tab-scripts", "Scripts",
               "Scripts: what happens in a zone, such as people talking, doors opening and items appearing.",
               tab_ == Tab::Scripts)) {
        tab_ = Tab::Scripts;
    }
    if (button("tab-characters", "Characters",
               "Characters: the hero, monsters, people and weapons; how they look, move and fight.",
               tab_ == Tab::Characters)) {
        tab_ = Tab::Characters;
    }
    if (button("tab-puzzles", "Puzzles",
               "Puzzles: the items a game is built from, and what people say about them.", tab_ == Tab::Puzzles)) {
        tab_ = Tab::Puzzles;
    }
    if (button("tab-sounds", "Sounds", "Sounds: listen to them, replace them and add your own.",
               tab_ == Tab::Sounds)) {
        tab_ = Tab::Sounds;
    }
    x += 8;
    if (button("undo", "Undo", "Take back the last change (Ctrl+Z).", false, !undo_.empty())) {
        undo();
    }
    if (button("redo", "Redo", "Do again what Undo took back (Ctrl+Y).", false, !redo_.empty())) {
        redo();
    }
    if (button("save", dirty_ ? "Save*" : "Save",
               "Save all changes as the mod \"" + host_.info.id + "\" (Ctrl+S). The original game is never changed.")) {
        save();
    }
    if (button("try", "Try it out", "Play the zone as it is now. Press Esc in the game to come back.")) {
        if (syncScripts()) {
            tryOut_ = zone_;
        }
    }

    // Right side: size of the interface and leaving.
    float rx = r.x + r.w - 4;
    const auto rightButton = [&](std::string_view id, std::string_view label, std::string_view help) {
        const float bw = ui::textWidth(label) + 12;
        rx -= bw;
        const bool pressed = gui_.button(id, {rx, y, bw, bh}, label, help);
        rx -= 3;
        return pressed;
    };
    if (rightButton("leave", "Leave", "Back to the title menu.")) {
        if (dirty_ && !leaveArmed_) {
            leaveArmed_ = true;
            notice_ = "There are unsaved changes. Save them, or press Leave again to drop them.";
            noticeAt_ = SDL_GetTicks();
        } else {
            leave_ = true;
        }
    }
    const int current = uiScale_ > 0 ? uiScale_ : 0;
    if (rightButton("bigger", "A+", "Make the editor larger.")) {
        uiScale_ = std::min(4, (current > 0 ? current : 1) + 1);
    }
    if (rightButton("smaller", "A-", "Make the editor smaller.")) {
        uiScale_ = std::max(1, (current > 0 ? current : 2) - 1);
    }
    const std::string title = "Mod: " + host_.info.id;
    if (rx - x > ui::textWidth(title) + 8) {
        ui::text(renderer_, rx - ui::textWidth(title) - 6, y + (bh - ui::kFont) / 2, title, ui::color::dim);
    }
}

void Editor::helpBar(const SDL_FRect& r) {
    ui::fill(renderer_, r, ui::color::panel);
    ui::fill(renderer_, {r.x, r.y, r.w, 1}, ui::color::border);
    std::string text = gui_.help();
    ui::Color c = ui::color::dim;
    if (!notice_.empty() && SDL_GetTicks() - noticeAt_ < 8000) {
        text = notice_;
        c = ui::color::accent;
    } else if (text.empty()) {
        text = tab_ == Tab::Zones
                   ? "Choose a zone on the left, a tile on the right, and draw on the zone. Point at anything to see "
                     "what it does."
                   : tab_ == Tab::Sounds
                   ? "Choose a sound on the left to listen to it or replace it. Point at anything to see what it does."
                   : tab_ == Tab::Puzzles
                   ? "Choose a puzzle on the left: its item and what people say about it. Point at anything to see "
                     "what it does."
                   : tab_ == Tab::Characters
                   ? "Choose a character on the left. Click a picture to give it the tile in hand. Point at anything "
                     "to see what it does."
                   : tab_ == Tab::Scripts
                   ? "Choose a zone on the left; its scripts are in the middle. Click a line to change it. Point at "
                     "anything to see what it does."
                   : "Choose a tile on the left to see and change what it does. Point at anything to see what it does.";
    }
    gui_.paragraph(r.x + 6, r.y + 6, r.w - 12, text, c, 3);
}

// --- tiles and names -----------------------------------------------------------

void Editor::drawTile(std::uint16_t tile, const SDL_FRect& dst, Uint8 alpha) const {
    if (tile == data::kNoTile || tile >= host_.data->tiles.size() || !*host_.atlas) {
        return;
    }
    const SDL_FRect src{static_cast<float>(tile % kAtlasColumns) * kTile,
                        static_cast<float>(tile / kAtlasColumns) * kTile, kTile, kTile};
    SDL_SetTextureAlphaMod(*host_.atlas, alpha);
    SDL_RenderTexture(renderer_, *host_.atlas, &src, &dst);
    SDL_SetTextureAlphaMod(*host_.atlas, 255);
}

std::string Editor::tileName(std::uint16_t tile) const {
    for (const auto& n : host_.data->tileNames) {
        if (n.tile == tile) {
            return text::windows1252ToUtf8(n.name);
        }
    }
    return {};
}

void Editor::setTileName(std::uint16_t tile, const std::string& name) {
    auto& names = host_.data->tileNames;
    const auto it = std::find_if(names.begin(), names.end(), [&](const data::TileName& n) { return n.tile == tile; });
    std::string bytes;
    try {
        bytes = text::utf8ToWindows1252(name);
    } catch (const std::exception&) {
        notice_ = "That name has a character the game cannot show.";
        noticeAt_ = SDL_GetTicks();
        return;
    }
    if (bytes.empty()) {
        if (it != names.end()) {
            names.erase(it);
        }
    } else if (it != names.end()) {
        it->name = bytes;
    } else {
        const auto at = std::find_if(names.begin(), names.end(), [&](const data::TileName& n) { return n.tile > tile; });
        names.insert(at, {tile, bytes});
    }
}

// --- undo ----------------------------------------------------------------------

void Editor::remember(int zone, int tile, bool structure) {
    mergeKey_.clear();
    Snapshot s;
    auto& d = *host_.data;
    if (structure) {
        s.zones = d.zones;
        s.tiles = d.tiles;
        s.names = d.tileNames;
    } else {
        if (zone >= 0 && static_cast<std::size_t>(zone) < d.zones.size()) {
            s.zone = zone;
            s.zoneData = d.zones[static_cast<std::size_t>(zone)];
        }
        if (tile >= 0 && static_cast<std::size_t>(tile) < d.tiles.size()) {
            s.tile = tile;
            s.tileData = d.tiles[static_cast<std::size_t>(tile)];
            s.tileName = tileName(static_cast<std::uint16_t>(tile));
        }
    }
    undo_.push_back(std::move(s));
    if (undo_.size() > kUndoSteps) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    dirty_ = true;
    ++changes_;
    leaveArmed_ = false;
}

void Editor::rememberStory() {
    remember(-1, -1);
    auto& s = undo_.back();
    const auto& d = *host_.data;
    s.characters = d.characters;
    s.weapons = d.characterWeapons;
    s.aux = d.characterAux;
    s.puzzles = d.puzzles;
}

void Editor::swapSnapshot(Snapshot& s) {
    auto& d = *host_.data;
    bool tiles = false;
    if (s.characters) {
        std::swap(*s.characters, d.characters);
        std::swap(*s.weapons, d.characterWeapons);
        std::swap(*s.aux, d.characterAux);
        std::swap(*s.puzzles, d.puzzles);
        readCharacters();
        readPuzzles();
    }
    if (s.sound) {
        std::swap(s.soundNames, d.sounds);
        const auto it = host_.sounds.find(s.soundKey);
        std::optional<data::Bytes> now;
        if (it != host_.sounds.end()) {
            now = std::move(it->second);
            host_.sounds.erase(it);
        }
        if (s.soundBytes) {
            host_.sounds[s.soundKey] = std::move(*s.soundBytes);
        }
        s.soundBytes = std::move(now);
        if (host_.soundsChanged) {
            host_.soundsChanged(host_.sounds);
        }
    }
    if (s.zones) {
        std::swap(*s.zones, d.zones);
    }
    if (s.tiles) {
        std::swap(*s.tiles, d.tiles);
        tiles = true;
    }
    if (s.names) {
        std::swap(*s.names, d.tileNames);
    }
    if (s.zoneData && static_cast<std::size_t>(s.zone) < d.zones.size()) {
        std::swap(*s.zoneData, d.zones[static_cast<std::size_t>(s.zone)]);
        zone_ = s.zone;
    }
    if (s.tileData && static_cast<std::size_t>(s.tile) < d.tiles.size()) {
        std::swap(*s.tileData, d.tiles[static_cast<std::size_t>(s.tile)]);
        const std::string name = tileName(static_cast<std::uint16_t>(s.tile));
        setTileName(static_cast<std::uint16_t>(s.tile), s.tileName);
        s.tileName = name;
        tiles = true;
    }
    zone_ = std::clamp(zone_, 0, static_cast<int>(d.zones.size()) - 1);
    tile_ = static_cast<std::uint16_t>(std::min<std::size_t>(tile_, d.tiles.size() - 1));
    if (tiles && host_.tilesChanged) {
        host_.tilesChanged();
    }
    dirty_ = true;
    ++changes_;
}

void Editor::undo() {
    if (undo_.empty()) {
        return;
    }
    Snapshot s = std::move(undo_.back());
    undo_.pop_back();
    swapSnapshot(s);
    redo_.push_back(std::move(s));
}

void Editor::redo() {
    if (redo_.empty()) {
        return;
    }
    Snapshot s = std::move(redo_.back());
    redo_.pop_back();
    swapSnapshot(s);
    undo_.push_back(std::move(s));
}

// --- saving and scripts ----------------------------------------------------------

bool Editor::syncScripts() {
    for (const int zone : host_.scriptZones) {
        if (zone < 0 || static_cast<std::size_t>(zone) >= host_.data->zones.size()) {
            continue;
        }
        std::ifstream in(host_.modFolder / scriptFile(zone), std::ios::binary);
        if (!in) {
            continue;
        }
        std::ostringstream text;
        text << in.rdbuf();
        auto& z = host_.data->zones[static_cast<std::size_t>(zone)];
        const auto layout = host_.data->layout();
        try {
            // Only a file the player changed counts: changes made on the
            // Scripts tab since it was written are kept.
            const auto known = scriptText_.find(zone);
            if (known != scriptText_.end() && known->second == text.str()) {
                continue;
            }
            auto actions = script::readScriptText(text.str(), layout);
            const std::string now = z.actions ? script::writeScriptText(*z.actions, layout) : std::string();
            if (script::writeScriptText(actions, layout) != now) {
                remember(zone, -1);
                z.actions = std::move(actions);
            }
            scriptText_[zone] = text.str();
        } catch (const std::exception& e) {
            notice_ = scriptFile(zone) + ": " + e.what();
            noticeAt_ = SDL_GetTicks();
            zone_ = zone;
            return false;
        }
    }
    return true;
}

void Editor::save() {
    if (!syncScripts()) {
        return;
    }
    pack::Pack changed;
    changed.game = host_.base->game;
    changed.data = *host_.data;
    changed.palette = *host_.palette;
    changed.sounds = host_.sounds;
    try {
        pack::Mod mod = pack::makeMod(*host_.base, changed, host_.info);
        const auto layout = host_.data->layout();
        for (const int zone : host_.scriptZones) {
            if (zone < 0 || static_cast<std::size_t>(zone) >= host_.data->zones.size()) {
                continue;
            }
            const auto& actions = host_.data->zones[static_cast<std::size_t>(zone)].actions;
            const std::string text = actions ? script::writeScriptText(*actions, layout) : std::string();
            mod.files[scriptFile(zone)] = data::Bytes(text.begin(), text.end());
            scriptText_[zone] = text;
        }
        pack::writeMod(mod, host_.modFolder);
        if (host_.saved) {
            host_.saved(mod);
        }
        dirty_ = false;
        leaveArmed_ = false;
        notice_ = "Saved as the mod \"" + host_.info.id + "\" (" + std::to_string(mod.files.size() - 1) +
                  " changed files). It is switched on.";
    } catch (const std::exception& e) {
        notice_ = std::string("Not saved: ") + e.what();
    }
    noticeAt_ = SDL_GetTicks();
}

void Editor::openScripts() {
    if (!syncScripts()) {
        return;
    }
    if (dirty_ || !std::filesystem::is_directory(host_.modFolder)) {
        save();
        if (dirty_) {
            return;
        }
    }
    const auto path = host_.modFolder / scriptFile(zone_);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream out(path, std::ios::binary);
        const auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
        out << "# The scripts of zone " << zone_ << ". Save this file, then choose Try it out or Save\n"
            << "# in the editor. See docs/formats/script-text.md.\n\n"
            << (z.actions ? script::writeScriptText(*z.actions, host_.data->layout()) : std::string());
    }
    if (std::find(host_.scriptZones.begin(), host_.scriptZones.end(), zone_) == host_.scriptZones.end()) {
        host_.scriptZones.push_back(zone_);
    }
    const std::string url = "file://" + path.generic_string();
    notice_ = SDL_OpenURL(url.c_str()) ? "Opened " + scriptFile(zone_) + " in your text editor."
                                       : "Edit the file " + path.string();
    noticeAt_ = SDL_GetTicks();
}

} // namespace das::app
