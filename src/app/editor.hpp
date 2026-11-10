// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor: its own full-window screen for changing a game and saving the
// changes as a mod (docs/editor.md). Tabs for zones and tiles; every control
// explains itself in the help bar at the bottom.
#pragma once

#include "das/data/game_data.hpp"
#include "das/data/palette.hpp"
#include "das/input.hpp"
#include "das/pack/mod.hpp"

#include "gui.hpp"
#include "ui.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
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

private:
    enum class Tab { Zones, Tiles };
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
    };

    // editor.cpp
    void topBar(const SDL_FRect& r);
    void helpBar(const SDL_FRect& r);
    // Call before a change: `zone` and `tile` (or -1) are what it changes;
    // `structure` when zones or tiles are added.
    void remember(int zone, int tile, bool structure = false);
    void swapSnapshot(Snapshot& s);
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

    // editor_tiles.cpp
    void tilesTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right);
    // A scrolling, filtered grid of tiles; returns the tile clicked.
    std::optional<std::uint16_t> tileBrowser(const SDL_FRect& r, std::string_view id, float cell);
    void tileInspector(const SDL_FRect& r);
    void addTile();

    Host host_;
    gui::InputCollector collector_;
    gui::Input pending_; // keyboard and gamepad since the last frame
    gui::Gui gui_;
    SDL_Renderer* renderer_ = nullptr;
    int uiScale_ = 0; // 0: chosen by the window size

    Tab tab_ = Tab::Zones;
    int zone_ = 0;
    std::uint16_t tile_ = 0; // the tile in hand (also the tile on the Tiles tab)
    int layer_ = 0;           // 0 floor, 1 objects, 2 roof, 3 hotspots
    std::array<bool, 4> shown_{true, true, true, true};
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
} // namespace names

} // namespace das::app
