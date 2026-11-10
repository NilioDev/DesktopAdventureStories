// SPDX-License-Identifier: GPL-3.0-or-later
//
// Opens the game window: the play field, scaled in whole pixels, with our
// own interface around it. Without game files it asks for them.
//
// Usage: DesktopAdventureStories [game folder, zip or data file]
//          [--game yoda|indy] [--mode title|zone|tiles|play|game|continue] [--zone N]
//          [--seed N] [--size small|medium|large]
//          [--at X,Y] [--walk UDLR...] [--library DIR] [--reimport]
//        For screenshots and tests:
//          [--screenshot out.bmp] [--window WxH] [--menu pause|options|controls|map]
//          [--device keyboard|gamepad|touch] [--tap X,Y]
//
// The first time a game is found it is imported into our own open format in
// the library folder (the app's data folder, or --library / DAS_LIBRARY_DIR);
// from then on only the library is used and the original files can go.
// Games are looked for in the given path, DAS_GAMEDATA_DIR, or else a
// "gamedata" folder next to the program or in the current folder. A folder
// or zip can also be dropped on the window.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "das/constants.hpp"
#include "das/input.hpp"
#include "das/pack/import.hpp"
#include "das/version.hpp"
#include "audio.hpp"
#include "input_bindings.hpp"
#include "settings.hpp"
#include "touch_controls.hpp"
#include "ui.hpp"
#include "viewer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;

// Files the system's picker handed over.
struct Picked {
    std::mutex mutex;
    bool open = false;
    std::vector<std::string> paths;
    std::string error;
};

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::vector<SDL_Gamepad*> gamepads;

    das::ActionState actions;
    das::app::InputBindings bindings;
    das::MoveRepeater mover;
    Uint64 lastTicks = 0;

    das::app::Audio audio;
    das::app::Viewer viewer;
    das::app::Settings settings;
    std::filesystem::path settingsFile;
    das::app::ui::Layout layout;
    int pixelWidth = 1; // of the last frame
    int pixelHeight = 1;
    das::app::TouchControls touch;
    Picked picked;
    SDL_FRect chooseButton{}; // on the welcome screen
    std::optional<das::pack::Library> library;
    std::string loadMessage;
    std::string screenshotPath; // when set: render one frame, save it, quit
};

std::string gameTitle(const std::string& id) { return id == "indy" ? "Indiana Jones" : "Yoda Stories"; }

void note(App& app, const std::string& message) {
    app.loadMessage += (app.loadMessage.empty() ? "" : "; ") + message;
    SDL_Log("%s", message.c_str());
}

void show(App& app, das::pack::Pack pack) {
    const std::string id = pack.game;
    try {
        app.viewer.add(app.renderer, std::move(pack));
    } catch (const std::exception& e) {
        note(app, gameTitle(id) + ": " + e.what());
    }
}

void showLibrary(App& app) {
    if (!app.library) {
        return;
    }
    for (const auto& id : app.library->games()) {
        try {
            show(app, app.library->load(id));
        } catch (const std::exception& e) {
            note(app, "Could not read " + gameTitle(id) + " from the library: " + e.what());
        }
    }
}

// Imports the games of a source into the library and shows them. With
// `onlyMissing`, games that are already in the library are skipped.
void importFrom(App& app, const das::pack::Source& source, const std::string& where, bool onlyMissing) {
    try {
        std::vector<std::string> wanted = das::pack::gamesIn(source);
        if (onlyMissing && app.library) {
            std::erase_if(wanted, [&](const std::string& id) { return app.library->has(id); });
        }
        if (wanted.empty()) {
            if (!onlyMissing) {
                note(app, "No game files found in " + where);
            }
            return;
        }
        for (auto& pack : das::pack::importGames(source, wanted)) {
            const std::string id = pack.game;
            if (app.library) {
                try {
                    app.library->install(pack);
                    note(app, "Imported " + gameTitle(id));
                } catch (const std::exception& e) {
                    note(app, "Could not save " + gameTitle(id) + ": " + e.what());
                }
            }
            show(app, std::move(pack));
        }
    } catch (const std::exception& e) {
        note(app, "Import failed: " + std::string(e.what()));
    }
}

void importGames(App& app, const std::filesystem::path& where, bool onlyMissing) {
    try {
        const auto source = das::pack::openSource(where);
        if (!source) {
            note(app, "Nothing to import at " + where.string());
            return;
        }
        importFrom(app, *source, where.string(), onlyMissing);
    } catch (const std::exception& e) {
        note(app, "Import failed: " + std::string(e.what()));
    }
}

// --- choosing game files with the system's file picker ----------------------

// The file name in a path or in an Android content address such as
// "content://.../document/primary%3ADownload%2FYoda.zip".
std::string displayName(const std::string& path) {
    std::string decoded;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size()) {
            decoded += static_cast<char>(std::stoi(path.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            decoded += path[i];
        }
    }
    const auto cut = decoded.find_last_of("/\\:");
    return cut == std::string::npos ? decoded : decoded.substr(cut + 1);
}

void SDLCALL onPicked(void* userdata, const char* const* files, int /*filter*/) {
    auto& picked = *static_cast<Picked*>(userdata);
    std::lock_guard lock(picked.mutex);
    picked.open = false;
    if (!files) {
        picked.error = SDL_GetError();
        return;
    }
    for (; *files; ++files) {
        picked.paths.emplace_back(*files);
    }
}

void openPicker(App& app) {
    {
        std::lock_guard lock(app.picked.mutex);
        if (app.picked.open) {
            return;
        }
        app.picked.open = true;
    }
#if defined(SDL_PLATFORM_ANDROID)
    // Android hands out contents, not folders: a zip of the game folder, or
    // its files. No filter: the data files have no type Android knows.
    SDL_ShowOpenFileDialog(onPicked, &app.picked, app.window, nullptr, 0, nullptr, true);
#else
    SDL_ShowOpenFolderDialog(onPicked, &app.picked, app.window, nullptr, false);
#endif
}

// Imports what the picker handed over (it may answer on another thread).
void takePicked(App& app) {
    std::vector<std::string> paths;
    {
        std::lock_guard lock(app.picked.mutex);
        if (!app.picked.error.empty()) {
            note(app, "No files chosen: " + app.picked.error);
            app.picked.error.clear();
        }
        paths.swap(app.picked.paths);
    }
    if (paths.empty()) {
        return;
    }
    app.loadMessage.clear();
    std::error_code ec;
    if (paths.size() == 1 && std::filesystem::exists(paths.front(), ec)) {
        importGames(app, paths.front(), false); // a folder, zip or data file on disk
        return;
    }
    // Contents only: read each file through SDL, which knows Android's
    // content addresses.
    std::vector<std::pair<std::string, das::data::Bytes>> files;
    for (const auto& path : paths) {
        std::size_t size = 0;
        void* bytes = SDL_LoadFile(path.c_str(), &size);
        if (!bytes) {
            note(app, "Could not read " + displayName(path) + ": " + SDL_GetError());
            continue;
        }
        const auto* begin = static_cast<const std::uint8_t*>(bytes);
        das::data::Bytes content(begin, begin + size);
        SDL_free(bytes);
        if (das::pack::looksLikeZip(content)) {
            try {
                importFrom(app, *das::pack::zipSource(content), displayName(path), false);
            } catch (const std::exception& e) {
                note(app, displayName(path) + ": " + e.what());
            }
            continue;
        }
        files.emplace_back(displayName(path), std::move(content));
    }
    if (!files.empty()) {
        importFrom(app, *das::pack::filesSource(std::move(files)), "the chosen files", false);
    }
}

std::optional<das::pack::Library> openLibrary(const std::string& wanted) {
    std::filesystem::path root = wanted;
    if (root.empty()) {
        if (const char* env = SDL_getenv("DAS_LIBRARY_DIR")) {
            root = env;
        }
    }
    if (root.empty()) {
        char* pref = SDL_GetPrefPath("", "DesktopAdventureStories");
        if (!pref) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No data folder, games are not kept: %s", SDL_GetError());
            return std::nullopt;
        }
        root = std::filesystem::path(pref) / "library";
        SDL_free(pref);
    }
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    SDL_Log("Library: %s", root.string().c_str());
    return das::pack::Library(root);
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

void applySettings(App& app) {
    SDL_SetWindowFullscreen(app.window, app.settings.fullscreen);
    app.audio.setVolume(static_cast<float>(app.settings.volume) / 100.0f);
    app.audio.setMusicVolume(static_cast<float>(app.settings.music) / 100.0f);
    app.viewer.applyFilter();
    app.bindings.configure(app.settings);
}

void update(App& app, Uint64 now, Uint32 elapsedMs) {
    takePicked(app);
    if (app.viewer.takeAddGames() || (app.viewer.empty() && app.actions.wasPressed(das::Action::Use))) {
        openPicker(app);
    }
    using das::app::TouchPad;
    const bool wantPad = app.settings.touchPad == TouchPad::On ||
                         (app.settings.touchPad == TouchPad::Auto &&
                          app.viewer.inputDevice() == das::app::InputDevice::Touch);
    app.touch.setVisible(wantPad && app.viewer.playing(), app.actions);
    app.viewer.update(app.actions, app.mover.update(das::heldDirection(app.actions), elapsedMs), now);
    app.audio.update();
    if (app.viewer.takeSettingsChanged()) {
        applySettings(app);
        if (!app.settingsFile.empty()) {
            das::app::saveSettings(app.settings, app.settingsFile);
        }
    }
}

// Without games: say how to get them in.
void renderWelcome(App& app) {
    namespace ui = das::app::ui;
    SDL_Renderer* r = app.renderer;
    const auto& l = app.layout;
    const float w = std::min(l.width - 32, 360.0f);
    const SDL_FRect box{std::floor((l.width - w) / 2), std::floor(l.height / 2 - 84), w, 168};
    ui::panel(r, box);
    float y = box.y + 14;
    ui::text(r, box.x + 14, y, das::kProjectName, ui::color::accent);
    ui::textRight(r, box.x + box.w - 14, y, das::kVersion, ui::color::faint);
    y += 24;
#if defined(SDL_PLATFORM_ANDROID)
    const char* lines[] = {"Needs the files of the original", "Indiana Jones and His Desktop",
                           "Adventures or Yoda Stories:", "a zip of the game folder."};
#else
    const char* lines[] = {"Needs the files of the original", "Indiana Jones and His Desktop",
                           "Adventures or Yoda Stories.", "Choose the game folder, or drop", "it (or a zip) on this window."};
#endif
    for (const char* line : lines) {
        ui::text(r, box.x + 14, y, line, ui::color::text);
        y += 11;
    }
    app.chooseButton = {box.x + 14, box.y + box.h - 14 - 20, box.w - 28, 20};
    ui::fill(r, app.chooseButton, ui::color::panelLight);
    ui::outline(r, app.chooseButton, ui::color::accent);
    ui::textCentered(r, app.chooseButton.x + app.chooseButton.w / 2, app.chooseButton.y + 6, "Choose game files",
                     ui::color::accent);
    if (!app.loadMessage.empty()) {
        const auto& m = app.loadMessage;
        const std::size_t chars = static_cast<std::size_t>(std::max(8.0f, (l.width - 16) / ui::kFont));
        ui::text(r, 8, box.y + box.h + 10, m.size() > chars ? m.substr(0, chars) : m, ui::color::danger);
    }
}

// A press with no game installed: the welcome screen's button.
void welcomePress(App& app, float x, float y) {
    if (app.viewer.empty() && das::app::ui::contains(app.chooseButton, x, y)) {
        openPicker(app);
    }
}

void render(App& app, Uint64 now) {
    SDL_Renderer* r = app.renderer;
    int pw = 0;
    int ph = 0;
    SDL_GetCurrentRenderOutputSize(r, &pw, &ph);
    app.pixelWidth = pw;
    app.pixelHeight = ph;
    app.layout = das::app::ui::layout(pw, ph, app.settings.pixelPerfect,
                                      app.touch.visible() ? das::app::TouchControls::kBottomRoom : 0);
    app.touch.place(app.layout);
    SDL_SetRenderScale(r, 1, 1);
    const auto bg = das::app::ui::color::background;
    SDL_SetRenderDrawColor(r, bg.r, bg.g, bg.b, 255);
    SDL_RenderClear(r);
    das::app::ui::begin(r, app.layout);
    if (app.viewer.empty()) {
        renderWelcome(app);
    } else {
        app.viewer.render(r, app.layout, now);
        app.touch.render(r);
    }
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    SDL_SetAppMetadata("DesktopAdventureStories", std::string(das::kVersion).c_str(),
                       "org.desktopadventurestories.app");

    // Touches are handled as fingers (several at once), not as a mouse.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto* app = new App();
    *appstate = app;
    app->audio.open();
    app->viewer.setAudio(&app->audio);
    app->viewer.setSettings(&app->settings);
    app->viewer.setBindings(&app->bindings);

    // Command line.
    std::string dataPath;
    std::string game;
    std::string mode;
    int zone = 0;
    std::string walkPath;
    std::uint32_t seed = static_cast<std::uint32_t>(SDL_GetPerformanceCounter());
    auto size = das::world::WorldSize::Medium;
    std::string libraryPath;
    bool reimport = false;
    int atX = -1;
    int atY = -1;
    int windowW = kWindowWidth;
    int windowH = kWindowHeight;
    std::string menu;
    std::string device;
    std::optional<SDL_FPoint> tap;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--game" && hasValue) {
            game = argv[++i];
        } else if (arg == "--mode" && hasValue) {
            mode = argv[++i];
        } else if (arg == "--zone" && hasValue) {
            zone = std::atoi(argv[++i]);
        } else if (arg == "--seed" && hasValue) {
            seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--size" && hasValue) {
            const std::string value = argv[++i];
            size = value == "small"   ? das::world::WorldSize::Small
                   : value == "large" ? das::world::WorldSize::Large
                                      : das::world::WorldSize::Medium;
        } else if (arg == "--walk" && hasValue) {
            walkPath = argv[++i];
        } else if (arg == "--at" && hasValue) {
            std::sscanf(argv[++i], "%d,%d", &atX, &atY);
        } else if (arg == "--library" && hasValue) {
            libraryPath = argv[++i];
        } else if (arg == "--reimport") {
            reimport = true;
        } else if (arg == "--window" && hasValue) {
            std::sscanf(argv[++i], "%dx%d", &windowW, &windowH);
        } else if (arg == "--menu" && hasValue) {
            menu = argv[++i];
        } else if (arg == "--device" && hasValue) {
            device = argv[++i];
        } else if (arg == "--tap" && hasValue) {
            SDL_FPoint p{};
            if (std::sscanf(argv[++i], "%f,%f", &p.x, &p.y) == 2) {
                tap = p;
            }
        } else if (arg == "--screenshot" && hasValue) {
            app->screenshotPath = argv[++i];
        } else if (!arg.empty() && arg[0] != '-') {
            dataPath = arg;
        }
    }
    if (!SDL_CreateWindowAndRenderer("DesktopAdventureStories", windowW, windowH,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &app->window,
                                     &app->renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowMinimumSize(app->window, 480, 360);
    SDL_SetRenderVSync(app->renderer, 1);

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
    app->library = openLibrary(libraryPath);
    if (app->library) {
        // Next to the library, so that re-importing games keeps the saves.
        app->viewer.setSaveFolder(app->library->root().parent_path() / "saves");
        app->settingsFile = app->library->root().parent_path() / "settings.json";
        app->settings = das::app::loadSettings(app->settingsFile);
    }
    if (!app->screenshotPath.empty()) {
        app->settings.fullscreen = false;
    }
    applySettings(*app);
    showLibrary(*app);
    if (!dataPath.empty()) {
        importGames(*app, dataPath, !reimport);
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
    if (mode == "play" || mode == "game" || mode == "continue") {
        if (mode == "game") {
            app->viewer.newGame(seed, size);
        } else if (mode == "continue") {
            if (!app->viewer.continueGame()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No saved game to continue");
            }
        } else {
            app->viewer.play();
        }
        if (atX >= 0 && atY >= 0) {
            app->viewer.placePlayer(atX, atY);
        }
        app->viewer.walk(walkPath);
    }
    if (menu == "pause" || menu == "options" || menu == "controls") {
        app->viewer.openMenu(menu != "pause");
        if (menu == "controls") {
            app->viewer.openControls();
        }
    } else if (menu == "map") {
        app->viewer.openMap();
    }
    if (device == "gamepad") {
        app->viewer.setInputDevice(das::app::InputDevice::Gamepad);
    } else if (device == "touch") {
        app->viewer.setInputDevice(das::app::InputDevice::Touch);
    }
    if (device == "touch-pad") {
        // A finger held on the pad's right arm, for a screenshot.
        app->viewer.setInputDevice(das::app::InputDevice::Touch);
        render(*app, SDL_GetTicks());
        update(*app, SDL_GetTicks(), 0);
        const auto& f = app->layout.field;
        app->touch.fingerDown(1, f.x / 2 + 30, f.y + f.h - 40, app->actions);
    }
    if (tap) {
        // A tap needs the layout of a drawn frame: draw one first.
        render(*app, SDL_GetTicks());
        app->viewer.pointerPressed(tap->x, tap->y);
    }

    // Gamepads that are already plugged in also arrive as GAMEPAD_ADDED
    // events, so they are opened in SDL_AppEvent.
    app->lastTicks = SDL_GetTicks();
    return SDL_APP_CONTINUE;
}

// While the controls menu waits for a new key or button: takes it.
bool captureControl(App& app, const SDL_Event& event) {
    const auto action = app.viewer.capturing();
    if (!action) {
        return false;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.scancode != SDL_SCANCODE_ESCAPE && event.key.scancode != SDL_SCANCODE_AC_BACK) {
            app.bindings.bindKey(app.settings, *action, event.key.scancode);
        }
    } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        app.bindings.bindButton(app.settings, *action, static_cast<SDL_GamepadButton>(event.gbutton.button));
    } else {
        return false;
    }
    app.viewer.endCapture();
    app.bindings.configure(app.settings);
    if (!app.settingsFile.empty()) {
        das::app::saveSettings(app.settings, app.settingsFile);
    }
    return true;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto& app = *static_cast<App*>(appstate);
    if (captureControl(app, *event)) {
        return SDL_APP_CONTINUE;
    }
    switch (event->type) {
    case SDL_EVENT_QUIT: return SDL_APP_SUCCESS;
    case SDL_EVENT_GAMEPAD_ADDED: openGamepad(app, event->gdevice.which); break;
    case SDL_EVENT_GAMEPAD_REMOVED: closeGamepad(app, event->gdevice.which); break;
    case SDL_EVENT_WINDOW_FOCUS_LOST: app.bindings.reset(app.actions); break;
    case SDL_EVENT_DROP_FILE:
        if (event->drop.data) {
            app.loadMessage.clear();
            importGames(app, event->drop.data, false);
        }
        break;
    case SDL_EVENT_KEY_DOWN: app.viewer.setInputDevice(das::app::InputDevice::Keyboard); break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: app.viewer.setInputDevice(das::app::InputDevice::Gamepad); break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT && event->button.which != SDL_TOUCH_MOUSEID) {
            app.viewer.setInputDevice(das::app::InputDevice::Keyboard);
            SDL_ConvertEventToRenderCoordinates(app.renderer, event);
            app.viewer.pointerPressed(event->button.x, event->button.y);
            welcomePress(app, event->button.x, event->button.y);
        }
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        // Fingers come in parts of the window; we want interface units.
        const float x = event->tfinger.x * static_cast<float>(app.pixelWidth) / app.layout.scale;
        const float y = event->tfinger.y * static_cast<float>(app.pixelHeight) / app.layout.scale;
        const SDL_FingerID finger = event->tfinger.fingerID;
        if (event->type == SDL_EVENT_FINGER_DOWN) {
            app.viewer.setInputDevice(das::app::InputDevice::Touch);
            if (!app.touch.fingerDown(finger, x, y, app.actions)) {
                app.viewer.pointerPressed(x, y);
                welcomePress(app, x, y);
            }
        } else if (event->type == SDL_EVENT_FINGER_MOTION) {
            app.touch.fingerMoved(finger, x, y, app.actions);
        } else {
            app.touch.fingerUp(finger, app.actions);
        }
        break;
    }
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
    if (app.viewer.quitRequested()) {
        return SDL_APP_SUCCESS;
    }
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
    if (app->screenshotPath.empty()) {
        app->viewer.autosave();
    }
    for (SDL_Gamepad* pad : app->gamepads) {
        SDL_CloseGamepad(pad);
    }
    // SDL destroys the window and renderer itself after this callback.
    delete app;
}
