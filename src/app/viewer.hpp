// SPDX-License-Identifier: GPL-3.0-or-later
//
// Data viewer: shows the title screen, zones and tiles of the original games
// the player pointed us at. This is the phase 1 front end; it grows into the
// game and, later, the editor.
#pragma once

#include "das/data/game_data.hpp"
#include "das/data/palette.hpp"
#include "das/data/character.hpp"
#include "das/input.hpp"
#include "das/pack/pack.hpp"
#include "das/world/world.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace das::app {

enum class ViewMode { Title, Zone, Tiles, Play };

struct LoadedGame {
    data::Layout game = data::Layout::Yoda;
    data::GameData data;
    std::optional<data::Character> hero;
    data::Palette palette;
    bool paletteFromGame = false;
    SDL_Texture* atlas = nullptr; // all tiles, kAtlasColumns per row
    SDL_Texture* setup = nullptr; // 288 x 288 title screen

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
    // Walks a scripted path ("UDLR" letters) in play mode; for testing.
    void walk(const std::string& path);

    void update(const ActionState& actions, std::optional<Step> step, Uint64 now);
    // Draws the view into a square area of the logical canvas, and the status
    // lines below it.
    void render(SDL_Renderer* renderer, float x, float y, float size, float statusY, Uint64 now) const;

private:
    LoadedGame& game() { return *games_[current_]; }
    const LoadedGame& game() const { return *games_[current_]; }

    void renderTile(SDL_Renderer* renderer, std::uint16_t tile, const SDL_FRect& dst) const;
    void renderZone(SDL_Renderer* renderer, float x, float y, float size) const;
    void renderTiles(SDL_Renderer* renderer, float x, float y, float size) const;
    void renderPlay(SDL_Renderer* renderer, float x, float y, float size, Uint64 now) const;

    std::vector<std::unique_ptr<LoadedGame>> games_;
    std::size_t current_ = 0;
    ViewMode mode_ = ViewMode::Title;
    int zone_ = 0;
    int tilePage_ = 0;
    std::optional<world::World> world_;
    Uint64 lastStepAt_ = 0;
};

} // namespace das::app
