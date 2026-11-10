// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor: its own full-window screen for changing a game and saving the
// changes as a mod (docs/editor.md). Tabs for zones and tiles; every control
// explains itself in the help bar at the bottom.
#pragma once

#include "das/data/character.hpp"
#include "das/data/game_data.hpp"
#include "das/data/palette.hpp"
#include "das/data/puzzle.hpp"
#include "das/input.hpp"
#include "das/pack/mod.hpp"

#include "gui.hpp"
#include "ui.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace das::app {

class Editor {
public:
    // What the editor needs from the app.
    struct Host {
        data::GameData* data = nullptr; // the game, changed in place
        const data::Palette* palette = nullptr;
        SDL_Texture* const* atlas = nullptr; // all tiles, 64 per row
        const pack::Pack* base = nullptr;    // the game before any mod
        pack::Files sounds;                  // the base sounds with the mod's
        std::filesystem::path modsFolder;
        std::filesystem::path modFolder; // where the mod is (or will be) saved
        pack::ModInfo info;
        std::vector<int> scriptZones; // zones whose scripts the mod keeps as text
        std::function<void()> tilesChanged; // remake the atlas
        std::function<void(int)> tileChanged; // redraw one tile of the atlas
        std::function<void(const data::Bytes&)> playSound;
        std::function<void(const pack::Files&)> soundsChanged;
        std::function<void(const pack::Mod&)> saved;
    };

    explicit Editor(Host host, int zone);

    // Raw events (mouse positions in interface units).
    void event(const SDL_Event& e) { collector_.event(e); }
    // Keyboard and gamepad, from the game's controls.
    void actions(const ActionState& actions, std::optional<Step> step);

    // Draws the editor and handles what was done since the last frame.
    void frame(SDL_Renderer* renderer, const ui::Layout& layout);

    // The interface scale for a window; the editor has its own, which the
    // player can change.
    float scaleFor(int pixelWidth, int pixelHeight) const;

    bool typing() const { return gui_.typing(); }
    int zone() const { return zone_; }
    bool dirty() const { return dirty_; }

    // Requests for the app, taken once.
    std::optional<int> takeTryOut();
    bool takeLeave();

    // Reads back scripts opened as text; false (with a notice) on a mistake.
    bool syncScripts();

    // A file chosen in the system's open dialog, which answers later.
    struct FileRequest {
        std::mutex mutex;
        std::string path;
        bool done = false;
        int replace = -1; // the sound to replace, or -1 to add one
    };

private:
    enum class Tab { Zones, Tiles, Scripts, Characters, Puzzles, Sounds, Check };
    enum class Tool { Paint, Erase, Pick, Fill };

    // What a change replaced, for Undo and Redo. Undoing swaps it with
    // the data, so the same snapshot then serves Redo.
    struct Snapshot {
        int zone = -1; // one zone
        std::optional<data::Zone> zoneData;
        int tile = -1; // one tile with its name
        std::optional<data::Tile> tileData;
        std::string tileName;
        // When zones or tiles were added: all of them.
        std::optional<std::vector<data::Zone>> zones;
        std::optional<std::vector<data::Tile>> tiles;
        std::optional<std::vector<data::TileName>> names;
        // Characters and puzzles: all of them.
        std::optional<std::vector<data::TaggedEntry>> characters;
        std::optional<std::vector<data::CharacterValues>> weapons;
        std::optional<std::vector<data::CharacterValues>> aux;
        std::optional<std::vector<data::TaggedEntry>> puzzles;
        // One sound file, and the list of sound names.
        bool sound = false;
        std::vector<std::string> soundNames;
        std::string soundKey;
        std::optional<data::Bytes> soundBytes;
    };

    // editor.cpp
    void topBar(const SDL_FRect& r);
    // The tour for beginners, drawn over everything; takes the input while
    // it runs.
    void tour(const gui::Input& in, float width, float height);
    void helpBar(const SDL_FRect& r);
    // Call before a change: `zone` and `tile` (or -1) are what it changes;
    // `structure` when zones or tiles are added.
    void remember(int zone, int tile, bool structure = false);
    void swapSnapshot(Snapshot& s);
    // Call before changing characters or puzzles.
    void rememberStory();
    void readCharacters();
    void undo();
    void redo();
    void save();
    void openScripts();
    std::string tileName(std::uint16_t tile) const;
    void setTileName(std::uint16_t tile, const std::string& name);
    void drawTile(std::uint16_t tile, const SDL_FRect& dst, Uint8 alpha = 255) const;

    // editor_zones.cpp
    void zonesTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right);
    void zoneList(const SDL_FRect& r);
    void zoneCanvas(const SDL_FRect& r);
    void zoneInspector(const SDL_FRect& r);
    void applyTool(int x, int y, bool startOfStroke);
    void fillRect(int x0, int y0, int x1, int y1);
    void newZone(bool copy);
    void resizeZone(int width, int height);
    void zoneExamples(const SDL_FRect& r);
    // The right panel's parts.
    float hotspotPanel(float x, float y, float w);
    float peoplePanel(float x, float y, float w);
    void zoneStory(float x, float y, float w, const SDL_FRect& r);
    // One of a zone's lists of tiles: shown, removed by clicking, added from
    // the tile in hand. Returns the height used.
    float tileList(std::string_view id, float x, float y, float w, std::string_view title, std::string_view help,
                   std::vector<std::uint16_t>& list, bool people);
    std::string characterName(int character) const;

    // editor_scripts.cpp
    void scriptsTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right);
    void scriptCards(const SDL_FRect& r);
    // The controls of the chosen line; returns the height used.
    float lineEditor(const std::string& id, float x, float y, float w, bool condition, data::ScriptLine& line);
    void scriptMap(const SDL_FRect& r);
    // Ready-made scripts to start from; returns the height used.
    float scriptTemplates(float x, float y, float w);
    // Call before changing scripts; changes with the same key in a row are
    // one step for Undo (typing, stepping a number).
    void changeScripts(const std::string& mergeKey = {});
    std::string argText(int kind, int value) const;
    std::string lineSentence(bool condition, const data::ScriptLine& line) const;
    // For the check: per number of a line, x y t(ile) z(one) p(erson)
    // h(otspot) s(ound) or '-'; and whether the line has text.
    std::string lineArgKinds(bool condition, std::uint16_t opcode) const;
    bool lineHasText(bool condition, std::uint16_t opcode) const;

    // editor_check.cpp
    struct Problem {
        bool mistake = false; // else a tip
        Tab tab = Tab::Zones;
        int zone = -1;
        int x = -1;
        int y = -1;
        int layer = -1;
        bool story = false; // shown in the zone's Story view
        int script = -1;
        int line = -1;
        int index = -1; // a character or puzzle
        std::string text;
    };
    void runCheck();
    void checkTab(const SDL_FRect& r);
    std::vector<std::pair<char, int>> lineArgs(bool condition, const data::ScriptLine& line) const;

    // editor_characters.cpp
    void charactersTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right);
    void characterList(const SDL_FRect& r);
    void characterInspector(const SDL_FRect& r);
    // Writes characters_[i] back into the data.
    void storeCharacter(int i);
    // The CHWP or CAUX entry of a character, made when missing.
    data::CharacterValues& characterValues(std::vector<data::CharacterValues>& list, int character);

    // editor_sounds.cpp
    void soundsTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right);
    void chooseSoundFile(int replace);
    void takeSoundFile();
    void rememberSound(const std::string& key);

    // editor_puzzles.cpp
    void puzzlesTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right);
    void readPuzzles();
    void storePuzzle(int i);

    // editor_tiles.cpp
    void tilesTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right);
    // A scrolling, filtered grid of tiles; returns the tile clicked.
    std::optional<std::uint16_t> tileBrowser(const SDL_FRect& r, std::string_view id, float cell);
    void tileInspector(const SDL_FRect& r);
    void addTile();
    // The tile's pixels, large, to draw on; and the colours to draw with.
    void pixelCanvas(const SDL_FRect& r);
    float paletteBox(float x, float y, float w);

    Host host_;
    gui::InputCollector collector_;
    gui::Input pending_; // keyboard and gamepad since the last frame
    gui::Gui gui_;
    SDL_Renderer* renderer_ = nullptr;
    int uiScale_ = 0; // 0: chosen by the window size

    Tab tab_ = Tab::Zones;
    int zone_ = 0;
    std::uint16_t tile_ = 0; // the tile in hand (also the tile on the Tiles tab)
    int layer_ = 0;           // 0 floor, 1 objects, 2 roof, 3 hotspots, 4 people
    std::array<bool, 5> shown_{true, true, true, true, true};
    bool storyView_ = false;  // the right panel shows what the zone asks and gives
    int character_ = -1;      // the character Paint places on the People layer
    std::vector<data::Character> characters_; // all characters, read once
    Tool tool_ = Tool::Paint;
    std::uint32_t spotType_ = 9;
    int cursorX_ = 0;
    int cursorY_ = 0;
    std::optional<std::pair<int, int>> fillFrom_; // first corner of a Fill
    std::string zoneSearch_;
    float zoneScroll_ = 0;
    std::string tileSearch_;
    int tileFilter_ = 0; // 0 all, then by kind
    float tileScroll_ = 0;
    float tileScroll2_ = 0;

    int scriptsZone_ = -1; // the zone the Scripts tab shows
    int selScript_ = -1;
    int selLine_ = -1;      // When lines first, then Then lines
    int pickPair_ = 0;      // the place a map click sets: 0 first, 1 second
    float scriptScroll_ = 0;
    float scriptHeight_ = 0;
    std::string mergeKey_;
    // The scripts text last written or read per zone, to see whether the
    // player changed the file.
    std::map<int, std::string> scriptText_;

    bool examplesOpen_ = false;
    bool templatesOpen_ = false;
    int tourStep_ = -1; // -1: no tour
    SDL_FRect leftRect_{}, centerRect_{}, rightRect_{};
    std::vector<Problem> problems_;
    std::size_t checkedStamp_ = static_cast<std::size_t>(-1);
    float checkScroll_ = 0;
    int sound_ = 0;
    float soundScroll_ = 0;
    std::shared_ptr<FileRequest> fileRequest_;
    std::vector<data::Puzzle> puzzles_; // all puzzles, read when needed
    int puzzle_ = 0;
    std::string puzzleSearch_;
    float puzzleScroll_ = 0;
    std::string characterSearch_;
    float characterScroll_ = 0;

    // Drawing tiles.
    enum class Brush { Pencil, Fill, Pick };
    Brush brush_ = Brush::Pencil;
    int color_ = 1;          // palette index; 0 is see-through
    bool stroke_ = false;    // the mouse is drawing on the tile

    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    bool dirty_ = false;
    std::size_t changes_ = 0; // counts every change, undo and redo
    // The zones that use the tile on the Tiles tab, recounted after changes.
    std::uint16_t usersTile_ = 0xffff;
    std::size_t usersStamp_ = static_cast<std::size_t>(-1);
    std::vector<int> users_;
    bool leaveArmed_ = false;
    std::string notice_;
    Uint64 noticeAt_ = 0;
    std::optional<int> tryOut_;
    bool leave_ = false;
};

// Plain names for numbers in the data, for the editor and its help.
namespace names {
std::string zoneType(std::uint32_t type);
std::string zoneTypeHelp(std::uint32_t type);
std::string planet(int planet);
std::string hotspot(std::uint32_t type);
std::string hotspotHelp(std::uint32_t type);
std::string characterKind(std::uint16_t type);
std::string movement(std::uint16_t movement);
} // namespace names

} // namespace das::app
