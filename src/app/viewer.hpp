// SPDX-License-Identifier: GPL-3.0-or-later
//
// The front end: the title screen and its menus, the game with its panel
// (health, weapon, inventory), the pause menu, the options and the map. It
// also keeps the data viewer for zones and tiles. Keyboard, gamepad, mouse
// and touch all reach it as actions or pointer presses.
#pragma once

#include "das/data/character.hpp"
#include "das/data/game_data.hpp"
#include "das/data/palette.hpp"
#include "das/input.hpp"
#include "das/pack/mod.hpp"
#include "das/pack/pack.hpp"
#include "das/world/world.hpp"

#include "audio.hpp"
#include "input_bindings.hpp"
#include "settings.hpp"
#include "ui.hpp"

#include <SDL3/SDL.h>

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace das::app {

enum class ViewMode { Title, Zone, Tiles, Play, Edit };

// The device the player used last; the hints on screen follow it.
enum class InputDevice { Keyboard, Gamepad, Touch };

struct LoadedGame {
    data::Layout game = data::Layout::Yoda;
    data::GameData data;
    std::optional<data::Character> hero;
    data::Palette palette;
    bool paletteFromGame = false;
    SDL_Texture* atlas = nullptr; // all tiles, kAtlasColumns per row
    SDL_Texture* setup = nullptr; // 288 x 288 title screen
    std::vector<data::Bytes> sounds; // by sound index; empty when missing
    pack::Pack base;                 // as imported, before the mods
    std::vector<std::string> mods;   // ids of the mods that were applied
    std::string modProblem;          // why a switched-on mod was left out

    LoadedGame() = default;
    LoadedGame(const LoadedGame&) = delete;
    LoadedGame& operator=(const LoadedGame&) = delete;
    ~LoadedGame();
};

class Viewer {
public:
    // Shows a game from one of our packs, replacing an earlier copy of the
    // same game. Throws when the pack cannot be used.
    void add(SDL_Renderer* renderer, pack::Pack pack);

    bool empty() const { return games_.empty(); }

    void setMode(ViewMode mode) { mode_ = mode; }
    void selectGame(data::Layout game);
    void selectZone(int zone);

    // Starts walking around in the selected zone.
    void play();
    // Starts a new game in a generated world.
    void newGame(std::uint32_t seed, world::WorldSize size);
    // Puts the player on a cell of the current zone; for testing.
    void placePlayer(int x, int y);
    // Walks a scripted path ("UDLR" letters) in play mode; for testing.
    void walk(const std::string& path);
    // Opens the pause menu (or the options from it); for testing.
    void openMenu(bool options);
    void openMap() { showMap_ = true; }
    void openControls() {
        page_ = Page::Controls;
        menuItem_ = 0;
    }
    void openMods();
    // The zone editor (docs/editor.md), on the selected zone.
    void openEditor();
    // Where mods are kept; without it the menu has no Mods page.
    void setModsFolder(std::filesystem::path folder);

    void setAudio(Audio* audio) { audio_ = audio; }
    // The settings the options menu changes; owned by the app.
    void setSettings(Settings* settings) { settings_ = settings; }
    // Sets the tile filter of the settings on every game's textures.
    void applyFilter();
    // True once after the options menu changed a setting.
    bool takeSettingsChanged();
    bool quitRequested() const { return quit_; }
    // True once after the player chose Add games in the menu.
    bool takeAddGames() {
        const bool asked = addGames_;
        addGames_ = false;
        return asked;
    }
    // The controls, for showing them and changing them in the menu.
    void setBindings(const InputBindings* bindings) { bindings_ = bindings; }
    // The action waiting for a new key or button, if any. The app hands
    // the next key or button to the bindings and then calls endCapture().
    std::optional<Action> capturing() const { return capture_; }
    void endCapture() { capture_.reset(); }

    // Where games are saved; without it nothing is saved.
    void setSaveFolder(std::filesystem::path folder) { saveFolder_ = std::move(folder); }
    // Saves a game in progress so that Continue picks it up.
    void autosave();
    bool hasSave() const;
    bool continueGame();

    void setInputDevice(InputDevice device) { device_ = device; }
    InputDevice inputDevice() const { return device_; }
    // Whether the game itself is on screen and takes moves (no menu, map or
    // ending in the way): the time for the touch pad.
    bool playing() const { return mode_ == ViewMode::Play && page_ == Page::None && !showMap_ && banner_.empty(); }
    // A mouse click or a tap, in interface units.
    void pointerPressed(float x, float y);
    // The mouse moved with its button held: the editor paints on.
    void pointerDragged(float x, float y) { drag_ = SDL_FPoint{x, y}; }

    void update(const ActionState& actions, std::optional<Step> step, Uint64 now);
    void render(SDL_Renderer* renderer, const ui::Layout& layout, Uint64 now) const;

private:
    // Menus: the title screen's, the pause menu and the options.
    enum class Page { None, Main, Pause, Options, Controls, Mods, Editor };
    enum class RowId {
        Continue,
        NewGame,
        Game,
        Size,
        Difficulty,
        Options,
        AddGames,
        DataViewer,
        Quit,
        Resume,
        Map,
        SaveAndQuit,
        Fullscreen,
        Scaling,
        Filter,
        Volume,
        Music,
        TouchPad,
        Controls,
        Bind,
        ResetControls,
        Mods,
        Mod,
        OpenModsFolder,
        Editor,
        EditZone,
        EditLayer,
        EditTool,
        EditTile,
        EditSpotType,
        EditSpotArgument,
        EditZoneType,
        EditScripts,
        EditUndo,
        EditNewZone,
        EditTest,
        EditSave,
        EditLeave,
        Back,
    };
    struct Row {
        RowId id;
        std::string label;
        std::string value; // empty for a plain button
        Action action = Action::Count; // Bind: the action to change
        int index = 0;                 // Mod: which of mods_
    };
    // A place on screen the pointer can press, found while drawing.
    struct Hit {
        enum class Kind { Row, RowLeft, Field, Slot, Menu, Map, Picker } kind;
        SDL_FRect rect;
        int index = 0;
    };

    LoadedGame& game() { return *games_[current_]; }
    const LoadedGame& game() const { return *games_[current_]; }

    // viewer.cpp: state and input.
    // Makes the game's data from its base pack and the mods switched on.
    void rebuild(LoadedGame& game);
    // Puts a pack's content in the game: data, sounds and textures.
    void install(LoadedGame& game, pack::Pack pack);
    void rescanMods();
    // The mods of the current game, in the order they are applied.
    std::vector<const pack::Mod*> gameMods() const;
    void startPlay();
    void leavePlay();
    std::vector<Row> menuRows() const;
    void updateMenu(const ActionState& actions, std::optional<Step> step);
    void menuActivate(const Row& row, int change);
    void menuBack();
    void startCapture(Action action);
    void updatePlay(const ActionState& actions, std::optional<Step> step, Uint64 now);
    void handleClick(Uint64 now);
    std::optional<Step> stepTowards(world::Position target) const;
    // Shows what the scripts asked for; resumes them when it is done.
    void handleEffects(const ActionState& actions, Uint64 now);
    void useSelected(Uint64 now);
    std::string itemName(std::uint16_t tile) const;
    // Plays the game's song whose file name has `name` in it, if it has one.
    void playSong(const char* name);
    std::filesystem::path savePath() const;
    std::string gameId() const;
    int difficultyValue() const;

    // viewer_edit.cpp: the zone editor.
    struct EditState {
        int layer = 0; // 0 floor, 1 objects, 2 roof, 3 hotspots
        int tool = 0;  // 0 paint, 1 erase, 2 pick
        std::uint16_t tile = 0;
        std::uint32_t spotType = 9; // the hotspot type to place
        int x = 0;                  // the cursor, in cells
        int y = 0;
        bool picking = false; // choosing a tile
        int pickPage = 0;
        int pickIndex = 0; // the picker's cursor on its page
        std::vector<std::pair<int, data::Zone>> undo;
        bool dirty = false;
        bool leaveArmed = false; // leaving with unsaved changes asks once more
        bool testing = false;    // test play started from the editor
        pack::ModInfo info;      // of the mod being edited
        pack::Files sounds;      // the base sounds with the mod's
        std::vector<int> scriptZones; // zones whose scripts were opened as text
    };
    std::vector<Row> editorRows() const;
    void updateEditor(const ActionState& actions, std::optional<Step> step);
    void editorActivate(const Row& row, int change);
    void editorClick(const Hit& hit, SDL_FPoint at, bool dragged);
    // Applies the tool at a cell; `record` keeps the zone for Undo first.
    void editAt(int x, int y, bool record);
    void editUndo();
    void editNewZone();
    void editSave();
    std::filesystem::path editFolder() const;
    // Opens the zone's scripts as text in the system's text editor.
    void editScripts();
    // Reads back the scripts opened as text; false (with a notice) when one
    // has a mistake.
    bool syncScripts();
    void leaveEditor();
    data::Zone& editZone() { return game().data.zones[static_cast<std::size_t>(zone_)]; }
    const data::Zone& editZone() const { return game().data.zones[static_cast<std::size_t>(zone_)]; }
    void renderEditor(SDL_Renderer* renderer, const SDL_FRect& field) const;
    void renderEditorPanel(SDL_Renderer* renderer, const SDL_FRect& panel) const;

    // viewer_draw.cpp: drawing.
    void renderTile(SDL_Renderer* renderer, std::uint16_t tile, const SDL_FRect& dst) const;
    void renderZone(SDL_Renderer* renderer, const SDL_FRect& field) const;
    void renderTiles(SDL_Renderer* renderer, const SDL_FRect& field) const;
    void renderPlay(SDL_Renderer* renderer, const SDL_FRect& field, Uint64 now) const;
    void renderScanlines(SDL_Renderer* renderer, const SDL_FRect& field, float scale) const;
    void renderBubble(SDL_Renderer* renderer, const SDL_FRect& field, float cell, int camX, int camY) const;
    void renderBanner(SDL_Renderer* renderer, const SDL_FRect& field) const;
    void renderMap(SDL_Renderer* renderer, const SDL_FRect& field) const;
    void renderHud(SDL_Renderer* renderer, const SDL_FRect& panel) const;
    void renderMenu(SDL_Renderer* renderer, const SDL_FRect& area, const char* heading) const;
    void renderTitlePanel(SDL_Renderer* renderer, const SDL_FRect& panel) const;
    void renderViewerPanel(SDL_Renderer* renderer, const SDL_FRect& panel) const;
    void renderHints(SDL_Renderer* renderer, const SDL_FRect& panel) const;
    std::string hintText() const;
    void addHit(Hit::Kind kind, const SDL_FRect& rect, int index = 0) const;

    std::vector<std::unique_ptr<LoadedGame>> games_;
    SDL_Renderer* renderer_ = nullptr; // for remaking textures after a mod change
    std::filesystem::path modsFolder_;
    std::vector<pack::Mod> mods_; // every mod found, of both games
    std::vector<std::string> modScanProblems_;
    std::string notice_; // a short message under the title menu
    EditState edit_;
    std::optional<SDL_FPoint> drag_;
    std::size_t current_ = 0;
    ViewMode mode_ = ViewMode::Title;
    int zone_ = 0;
    int tilePage_ = 0;
    std::optional<world::World> world_;
    Uint64 lastStepAt_ = 0;
    Uint64 lastUpdate_ = 0; // for letting game time pass
    Audio* audio_ = nullptr;
    Settings* settings_ = nullptr;
    std::deque<world::Effect> effects_;
    std::optional<world::Effect> bubble_;
    int bubblePage_ = 0;
    Uint64 waitUntil_ = 0;
    std::string banner_; // "You won" / "You lost"
    bool generated_ = false; // playing a generated world, not a single zone
    std::size_t selected_ = 0; // inventory slot
    bool showMap_ = false;

    // Menus and options.
    Page page_ = Page::Main;
    Page optionsFrom_ = Page::Main;
    int menuItem_ = 0;
    std::filesystem::path saveFolder_;
    world::GeneratorOptions options_; // of the game being played
    world::WorldSize size_ = world::WorldSize::Medium;
    int difficulty_ = 1; // 0 easy, 1 normal, 2 hard
    bool settingsChanged_ = false;
    const InputBindings* bindings_ = nullptr;
    std::optional<Action> capture_;
    Uint64 captureUntil_ = 0;
    bool quit_ = false;
    bool addGames_ = false;
    bool titleSong_ = false; // the title song was started on this visit

    // Pointer input and walking to a tapped cell.
    InputDevice device_ = InputDevice::Keyboard;
    std::optional<SDL_FPoint> click_;
    std::optional<world::Position> walkTarget_;
    Uint64 nextWalkAt_ = 0;

    // Filled while drawing, read by the next update.
    mutable std::vector<Hit> hits_;
    mutable int camX_ = 0;
    mutable int camY_ = 0;
    mutable float cell_ = 32;
    mutable std::size_t bubbleColumns_ = 33;
    mutable std::size_t bubbleLines_ = 12;
};

} // namespace das::app
