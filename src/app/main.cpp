// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opens a window with a play field scaled in whole pixels. With the original
// game files it shows the data viewer; without them, a marker that walks
// around with keyboard or gamepad.
//
// Usage: DesktopAdventureStories [game folder or data file]
//          [--game yoda|indy] [--mode title|zone|tiles] [--zone N]
//          [--screenshot out.bmp]
// Without a path it uses DAS_GAMEDATA_DIR, or else a "gamedata" folder next
// to the program or in the current folder. A folder can also be dropped on
// the window.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "das/constants.hpp"
#include "das/input.hpp"
#include "das/version.hpp"
#include "input_bindings.hpp"
#include "viewer.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Logical canvas: the play field plus a margin and a status area below it.
// It is scaled up in whole pixels so the pixel art stays sharp.
constexpr int kMargin = 16;
constexpr int kStatusHeight = 56;
constexpr int kCanvasWidth = das::kViewPixels + 2 * kMargin;
constexpr int kCanvasHeight = das::kViewPixels + 2 * kMargin + kStatusHeight;

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::vector<SDL_Gamepad*> gamepads;

    das::ActionState actions;
    das::app::InputBindings bindings;
    das::MoveRepeater mover;

    int playerX = das::kViewTiles / 2;
    int playerY = das::kViewTiles / 2;
    bool paused = false;
    Uint64 lastTicks = 0;
    Uint64 useFlashUntil = 0;
    Uint64 attackFlashUntil = 0;

    das::app::Viewer viewer;
    std::string loadMessage;
    std::string screenshotPath; // when set: render one frame, save it, quit
};

void loadGames(App& app, const std::filesystem::path& where) {
    app.loadMessage = app.viewer.load(app.renderer, where);
    SDL_Log("%s", app.loadMessage.c_str());
}

void saveScreenshot(App& app) {
    SDL_Surface* shot = SDL_RenderReadPixels(app.renderer, nullptr);
    if (!shot) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Screenshot failed: %s", SDL_GetError());
        return;
    }
    if (!SDL_SaveBMP(shot, app.screenshotPath.c_str())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not save %s: %s", app.screenshotPath.c_str(), SDL_GetError());
    }
    SDL_DestroySurface(shot);
}

void openGamepad(App& app, SDL_JoystickID id) {
    if (SDL_Gamepad* pad = SDL_OpenGamepad(id)) {
        app.gamepads.push_back(pad);
        SDL_Log("Gamepad connected: %s", SDL_GetGamepadName(pad));
    }
}

void closeGamepad(App& app, SDL_JoystickID id) {
    auto it = std::find_if(app.gamepads.begin(), app.gamepads.end(),
                           [id](SDL_Gamepad* pad) { return SDL_GetGamepadID(pad) == id; });
    if (it != app.gamepads.end()) {
        SDL_Log("Gamepad disconnected: %s", SDL_GetGamepadName(*it));
        SDL_CloseGamepad(*it);
        app.gamepads.erase(it);
    }
}

void update(App& app, Uint64 now, Uint32 elapsedMs) {
    if (!app.viewer.empty()) {
        app.viewer.update(app.actions, app.mover.update(das::heldDirection(app.actions), elapsedMs));
        return;
    }
    if (app.actions.wasPressed(das::Action::Menu)) {
        app.paused = !app.paused;
    }
    if (app.paused) {
        app.mover.update(std::nullopt, elapsedMs);
        return;
    }
    if (const auto step = app.mover.update(das::heldDirection(app.actions), elapsedMs)) {
        app.playerX = std::clamp(app.playerX + step->dx, 0, das::kViewTiles - 1);
        app.playerY = std::clamp(app.playerY + step->dy, 0, das::kViewTiles - 1);
    }
    if (app.actions.wasPressed(das::Action::Use)) {
        app.useFlashUntil = now + 150;
    }
    if (app.actions.wasPressed(das::Action::Attack)) {
        app.attackFlashUntil = now + 150;
    }
}

void render(App& app, Uint64 now) {
    SDL_Renderer* r = app.renderer;
    SDL_SetRenderDrawColor(r, 18, 20, 26, 255);
    SDL_RenderClear(r);

    if (!app.viewer.empty()) {
        app.viewer.render(r, kMargin, kMargin, das::kViewPixels, das::kViewPixels + 2 * kMargin);
        return;
    }

    // Play field: a checkerboard of 32x32 tiles until real zones are loaded.
    for (int y = 0; y < das::kViewTiles; ++y) {
        for (int x = 0; x < das::kViewTiles; ++x) {
            const bool light = (x + y) % 2 == 0;
            SDL_SetRenderDrawColor(r, light ? 58 : 50, light ? 66 : 57, light ? 54 : 47, 255);
            const SDL_FRect tile{static_cast<float>(kMargin + x * das::kTileSize),
                                 static_cast<float>(kMargin + y * das::kTileSize),
                                 static_cast<float>(das::kTileSize), static_cast<float>(das::kTileSize)};
            SDL_RenderFillRect(r, &tile);
        }
    }

    // Player marker.
    const bool usePulse = now < app.useFlashUntil;
    const bool attackPulse = now < app.attackFlashUntil;
    if (attackPulse) {
        SDL_SetRenderDrawColor(r, 230, 90, 70, 255);
    } else if (usePulse) {
        SDL_SetRenderDrawColor(r, 120, 200, 255, 255);
    } else {
        SDL_SetRenderDrawColor(r, 235, 196, 90, 255);
    }
    const SDL_FRect player{static_cast<float>(kMargin + app.playerX * das::kTileSize + 6),
                           static_cast<float>(kMargin + app.playerY * das::kTileSize + 6),
                           static_cast<float>(das::kTileSize - 12), static_cast<float>(das::kTileSize - 12)};
    SDL_RenderFillRect(r, &player);

    // Status area. SDL's built-in debug font is 8x8 pixels.
    const float textY = static_cast<float>(das::kViewPixels + 2 * kMargin);
    SDL_SetRenderDrawColor(r, 220, 222, 228, 255);
    const std::string title = std::string(das::kProjectName) + " " + std::string(das::kVersion);
    SDL_RenderDebugText(r, kMargin, textY, title.c_str());

    SDL_SetRenderDrawColor(r, 150, 154, 165, 255);
    const std::string pad = app.gamepads.empty()
                                ? std::string("No gamepad connected")
                                : std::string("Gamepad: ") + SDL_GetGamepadName(app.gamepads.front());
    SDL_RenderDebugText(r, kMargin, textY + 14, pad.c_str());
    SDL_RenderDebugText(r, kMargin, textY + 28,
                        app.loadMessage.empty() ? "Drop your game folder here" : app.loadMessage.c_str());

    if (app.paused) {
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 160);
        const SDL_FRect field{static_cast<float>(kMargin), static_cast<float>(kMargin),
                              static_cast<float>(das::kViewPixels), static_cast<float>(das::kViewPixels)};
        SDL_RenderFillRect(r, &field);
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        const float cx = kMargin + das::kViewPixels / 2.0f;
        const float cy = kMargin + das::kViewPixels / 2.0f;
        SDL_RenderDebugText(r, cx - 3 * 8, cy - 4, "Paused");
    }
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    SDL_SetAppMetadata("DesktopAdventureStories", std::string(das::kVersion).c_str(),
                       "org.desktopadventurestories.app");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto* app = new App();
    *appstate = app;

    if (!SDL_CreateWindowAndRenderer("DesktopAdventureStories", kCanvasWidth * 2, kCanvasHeight * 2,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &app->window,
                                     &app->renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app->renderer, 1);
    SDL_SetRenderLogicalPresentation(app->renderer, kCanvasWidth, kCanvasHeight,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    // Command line.
    std::string dataPath;
    std::string game;
    std::string mode;
    int zone = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--game" && hasValue) {
            game = argv[++i];
        } else if (arg == "--mode" && hasValue) {
            mode = argv[++i];
        } else if (arg == "--zone" && hasValue) {
            zone = std::atoi(argv[++i]);
        } else if (arg == "--screenshot" && hasValue) {
            app->screenshotPath = argv[++i];
        } else if (!arg.empty() && arg[0] != '-') {
            dataPath = arg;
        }
    }
    if (dataPath.empty()) {
        if (const char* env = SDL_getenv("DAS_GAMEDATA_DIR")) {
            dataPath = env;
        }
    }
    if (dataPath.empty()) {
        // Default: a "gamedata" folder next to the program or in the
        // current folder (the repository root during development).
        std::vector<std::filesystem::path> candidates;
        if (const char* base = SDL_GetBasePath()) {
            candidates.emplace_back(std::filesystem::path(base) / "gamedata");
        }
        candidates.emplace_back("gamedata");
        for (const auto& candidate : candidates) {
            std::error_code ec;
            if (std::filesystem::is_directory(candidate, ec)) {
                dataPath = candidate.string();
                break;
            }
        }
    }
    if (!dataPath.empty()) {
        loadGames(*app, dataPath);
    }
    if (game == "indy") {
        app->viewer.selectGame(das::data::Layout::Indy);
    } else if (game == "yoda") {
        app->viewer.selectGame(das::data::Layout::Yoda);
    }
    if (mode == "zone") {
        app->viewer.setMode(das::app::ViewMode::Zone);
    } else if (mode == "tiles") {
        app->viewer.setMode(das::app::ViewMode::Tiles);
    }
    app->viewer.selectZone(zone);

    // Gamepads that are already plugged in also arrive as GAMEPAD_ADDED
    // events, so they are opened in SDL_AppEvent.
    app->lastTicks = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto& app = *static_cast<App*>(appstate);
    switch (event->type) {
    case SDL_EVENT_QUIT: return SDL_APP_SUCCESS;
    case SDL_EVENT_GAMEPAD_ADDED: openGamepad(app, event->gdevice.which); break;
    case SDL_EVENT_GAMEPAD_REMOVED: closeGamepad(app, event->gdevice.which); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: app.bindings.reset(app.actions); break;
    case SDL_EVENT_DROP_FILE:
        if (event->drop.data) {
            loadGames(app, event->drop.data);
        }
        break;
    default: break;
    }
    app.bindings.handleEvent(*event, app.actions);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto& app = *static_cast<App*>(appstate);
    const Uint64 now = SDL_GetTicks();
    const auto elapsed = static_cast<Uint32>(std::min<Uint64>(now - app.lastTicks, 250));
    app.lastTicks = now;

    update(app, now, elapsed);
    app.actions.endFrame();
    render(app, now);
    if (!app.screenshotPath.empty()) {
        saveScreenshot(app);
        return SDL_APP_SUCCESS;
    }
    SDL_RenderPresent(app.renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/) {
    auto* app = static_cast<App*>(appstate);
    if (!app) {
        return;
    }
    for (SDL_Gamepad* pad : app->gamepads) {
        SDL_CloseGamepad(pad);
    }
    // SDL destroys the window and renderer itself after this callback.
    delete app;
}
