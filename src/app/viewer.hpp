// SPDX-License-Identifier: GPL-3.0-or-later
//
// The front end: the game picker, the title screen and its menus, the game with its panel
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
#include "das/world/snapshot.hpp"
#include "das/world/world.hpp"

#include "audio.hpp"
#include "editor.hpp"
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
    // A short message under the menu (cleared by the next menu choice): a
    // problem, or with `good` news such as a game that is ready.
    void setNotice(std::string text, bool good = false) {
        notice_ = std::move(text);
        noticeGood_ = good;
    }
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

    // Bug reports (docs/bug-snapshots.md). F8, both sticks pressed in, the
    // pause menu or Options, Report a problem ask for one: the next frame is
    // drawn without a menu and handed back as a screenshot, then the "Report
    // a bug" page opens.
    void requestBugReport();
    bool wantsScreenshot() const { return capturePending_; }
    void setBugScreenshot(data::Bytes png);
    enum class BugAction { None, Save, Report };
    // What the player chose on the bug page; the app writes the snapshot.
    BugAction takeBugAction() {
        const BugAction a = bugAction_;
        bugAction_ = BugAction::None;
        return a;
    }
    // The snapshot as a zip file, with what the viewer knows filled in on
    // top of `info` (version, system and log come from the app).
    data::Bytes bugSnapshot(world::SnapshotInfo info) const;
    // The state of the game for the report form, and the player's words.
    std::string bugSummary() const;
    std::string bugWhat() const;
    bool bugInGame() const { return mode_ == ViewMode::Play && world_.has_value(); }
    // The note on the bug page takes typed text while this is true.
    bool typingNote() const { return noteTyping_; }
    void noteText(const char* utf8);
    // Backspace takes a letter away; Enter and Esc end the typing.
    void noteKey(SDL_Scancode key);
    void endTyping() { noteTyping_ = false; }
    // Loads a save game given as text (a snapshot's save.json), switching to
    // its game. Returns false (with a notice) when it cannot be used.
    bool loadSaveText(const std::string& text);

    // Where games are saved; without it nothing is saved.
    void setSaveFolder(std::filesystem::path folder) { saveFolder_ = std::move(folder); }
    // Saves a game in progress in the autosave slot, so that Continue picks
    // it up. Returns whether it was written (not while a script waits).
    bool autosave();
    bool hasSave() const;
    // Loads the newest save of the current game.
    bool continueGame();
    // The app went to the background (a phone may close it there without
    // warning): save, and pause so the player comes back to the menu.
    void enterBackground();

    void setInputDevice(InputDevice device) { device_ = device; }
    InputDevice inputDevice() const { return device_; }
    // Whether the game itself is on screen and takes moves (no menu, map or
    // ending in the way): the time for the touch pad.
    // In a game, menu or not: the screen has the cards around the field.
    bool inPlay() const { return mode_ == ViewMode::Play && world_.has_value(); }
    bool playing() const { return mode_ == ViewMode::Play && page_ == Page::None && !showMap_ && banner_.empty(); }
    // A mouse click or a tap, in interface units.
    void pointerPressed(float x, float y);
    // Events for the editor while it is open (mouse positions in units).
    void editorEvent(const SDL_Event& e);
    bool editing() const { return mode_ == ViewMode::Edit && editor_ != nullptr; }
    float editorScale(int pixelWidth, int pixelHeight) const;
    // Whether the editor wants typed text (a text field has the focus).
    bool editorTyping() const { return editing() && editor_->typing(); }

    void update(const ActionState& actions, std::optional<Step> step, Uint64 now);
    void render(SDL_Renderer* renderer, const ui::Layout& layout, Uint64 now) const;

private:
    // Menus: the title screen's, the pause menu and the options.
    // Pick: the cards to choose a game with, the first screen.
    enum class Page { None, Pick, Main, Pause, Options, Controls, Mods, Slots, Help, Bug };
    enum class RowId {
        Continue,
        NewGame,
        Size,
        Difficulty,
        Options,
        AddGames,
        DataViewer,
        Quit,
        Resume,
        Map,
        SaveAndQuit,
        SaveGame,
        LoadGame,
        Slot,
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
        Report,
        HowToPlay,
        Editor,
        ReportBug,
        BugKind,
        BugNote,
        BugSave,
        BugSend,
        Coordinates,
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
        enum class Kind { Row, RowLeft, Field, Slot, Menu, Map, Pick } kind;
        SDL_FRect rect;
        int index = 0;
    };

    // The game picker. Card 0 is Indy, card 1 Yoda; the row above the
    // cards (kPickTop) holds Add games, Options and Quit.
    static constexpr int kPickTop = -1;
    struct PickButton {
        enum class What { Continue, Play, AddGames, Options, Quit } what;
        std::string label;
        bool primary = false;
    };
    std::vector<PickButton> pickButtons(int card) const;
    int pickCard() const; // the card chosen now
    void updatePicker(const ActionState& actions, std::optional<Step> step);
    void pickActivate(int card, int button);
    void renderPicker(SDL_Renderer* renderer, const ui::Layout& layout) const;
    // The loaded game of a card, or -1.
    int gameOfCard(int card) const;

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
    // Slot 0 is the autosave, 1 to kSaveSlots the player's own.
    static constexpr int kSaveSlots = 3;
    std::filesystem::path savePath(int slot) const { return savePathFor(gameId(), slot); }
    std::filesystem::path savePathFor(const std::string& id, int slot) const;
    // The newest slot of a game ("indy" or "yoda") with a save, or -1.
    int newestSlotFor(const std::string& id) const;
    // When a game was last saved, as "11 Nov 19:20", or "".
    std::string savedWhen(const std::string& id) const;
    bool writeSave(int slot);
    bool loadGame(int slot);
    // Loads a save game's text into the current game.
    bool applySave(const std::string& text);
    // The newest slot with a save, or -1.
    int newestSlot() const;
    void openSlots(bool saving);
    std::string gameId() const;
    int difficultyValue() const;

    // The editor (editor.hpp) and trying a zone out from it.
    void leaveEditor();

    // viewer_draw.cpp: drawing.
    void renderTile(SDL_Renderer* renderer, std::uint16_t tile, const SDL_FRect& dst) const;
    void renderZone(SDL_Renderer* renderer, const SDL_FRect& field) const;
    void renderTiles(SDL_Renderer* renderer, const SDL_FRect& field) const;
    void renderPlay(SDL_Renderer* renderer, const SDL_FRect& field, Uint64 now) const;
    void renderScanlines(SDL_Renderer* renderer, const SDL_FRect& field, float scale) const;
    void renderBubble(SDL_Renderer* renderer, const SDL_FRect& field, float cell, int camX, int camY) const;
    void renderBanner(SDL_Renderer* renderer, const SDL_FRect& field) const;
    void renderMap(SDL_Renderer* renderer, const SDL_FRect& field) const;
    // The map the player is on now (the world or the Dagobah map) and the
    // cell of the current zone on it, -1 when it is not on it.
    struct MapHere {
        const world::Map* map = nullptr;
        int x = -1;
        int y = -1;
    };
    MapHere mapHere() const;
    // The map's sites as rounded cells, `cell` units each, from (x, y).
    void renderMapCells(SDL_Renderer* renderer, const MapHere& here, float x, float y, float cell) const;
    void renderHud(SDL_Renderer* renderer, const ui::Layout& layout) const;
    void renderMenu(SDL_Renderer* renderer, const SDL_FRect& area, const char* heading) const;
    void renderTitlePanel(SDL_Renderer* renderer, const SDL_FRect& panel) const;
    void renderViewerPanel(SDL_Renderer* renderer, const SDL_FRect& panel) const;
    void renderHints(SDL_Renderer* renderer, const SDL_FRect& panel) const;
    std::string hintText() const;
    // The first key or button of an action, for the device used last.
    std::string controlName(Action action) const;
    // The "How to play" card: the aim of the game and the controls.
    std::string howToPlayText() const;
    // The text above the rows of the bug page.
    std::string bugText() const;
    void recordInput(const ActionState& actions, Uint64 now);
    void addHit(Hit::Kind kind, const SDL_FRect& rect, int index = 0) const;

    std::vector<std::unique_ptr<LoadedGame>> games_;
    SDL_Renderer* renderer_ = nullptr; // for remaking textures after a mod change
    std::filesystem::path modsFolder_;
    std::vector<pack::Mod> mods_; // every mod found, of both games
    std::vector<std::string> modScanProblems_;
    std::string notice_; // a short message under the title menu
    bool noticeGood_ = false;
    std::unique_ptr<Editor> editor_;
    bool testing_ = false; // playing a zone from the editor
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
    Page page_ = Page::Pick;
    int pickCard_ = -1;  // -1: the card of the current game
    int pickRow_ = 0;    // kPickTop or 0 (the card's buttons)
    int pickButton_ = 0; // in the row
    Page optionsFrom_ = Page::Main;
    Page helpFrom_ = Page::None;
    int menuItem_ = 0;
    std::filesystem::path saveFolder_;
    world::GeneratorOptions options_; // of the game being played
    Uint64 nextAutosaveAt_ = 0;     // in play, the next timed autosave
    bool slotsSaving_ = false;      // the Slots page saves (else loads)
    int confirmSlot_ = -1;          // a slot pressed once, to overwrite
    std::vector<std::string> slotDates_; // when each slot was saved, "" for empty
    world::WorldSize size_ = world::WorldSize::Medium;
    int difficulty_ = 1; // 0 easy, 1 normal, 2 hard
    bool settingsChanged_ = false;
    const InputBindings* bindings_ = nullptr;
    std::optional<Action> capture_;
    Uint64 captureUntil_ = 0;
    bool quit_ = false;
    bool addGames_ = false;
    bool titleSong_ = false; // the title song was started on this visit

    // The bug page.
    bool capturePending_ = false; // the next frame becomes the screenshot
    data::Bytes bugShot_;
    Page bugFrom_ = Page::Main;
    int bugKind_ = 0;
    std::string note_;
    bool noteTyping_ = false;
    BugAction bugAction_ = BugAction::None;
    bool showCoordinates_ = false; // zone and position over the play field
    std::deque<std::string> inputs_; // the last controls pressed

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
    mutable float bubbleWidth_ = 264; // the text's width in a speech bubble
    mutable std::size_t bubbleLines_ = 12;
};

} // namespace das::app
