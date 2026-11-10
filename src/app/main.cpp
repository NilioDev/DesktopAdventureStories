// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 0 placeholder: opens a window, scales a 9x9 tile play field to the
// screen and lets a marker walk around with keyboard or gamepad. This proves
// the build, rendering and input pipeline on every platform before any game
// data is loaded.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "das/constants.hpp"
#include "das/input.hpp"
#include "das/version.hpp"
#include "input_bindings.hpp"

#include <algorithm>
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
};

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
    SDL_RenderDebugText(r, kMargin, textY + 28, "Move, A/Space use, B/X attack");

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

    SDL_RenderPresent(r);
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[]) {
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
