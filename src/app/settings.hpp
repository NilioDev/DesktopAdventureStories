// SPDX-License-Identifier: GPL-3.0-or-later
//
// The player's settings, kept as a small JSON file in the app's data folder.
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace das::app {

enum class Filter { Sharp, Smooth, Scanlines };
// The on-screen pad: Auto shows it once the screen was touched.
enum class TouchPad { Auto, On, Off };

// What the originals keep between games to vary the next world.
struct GameHistory {
    int gamesWon = 0;
    // Yoda Stories: the planet of the last game (the original starts from 1).
    int planet = 1;
    // The goals of the last games, oldest first, per planet (Indy: planet 1).
    std::map<int, std::vector<int>> goals;
    // Indy: the zones the last game used.
    std::vector<int> zones;

    friend bool operator==(const GameHistory&, const GameHistory&) = default;
};

struct Settings {
    bool fullscreen = false;
    // Pixel perfect scales the play field in whole steps only; otherwise it
    // fills the space it has.
    bool pixelPerfect = true;
    Filter filter = Filter::Sharp;
    int volume = 80; // sound effects, 0 to 100
    int music = 70;  // 0 to 100
    TouchPad touchPad = TouchPad::Auto;
    // The "How to play" card was shown once, at the first game.
    bool howToPlaySeen = false;
    // Controls, per action (by its actionName): keys by SDL scancode name
    // and gamepad buttons by SDL button name. A missing action keeps its
    // default controls.
    std::map<std::string, std::vector<std::string>> keys;
    std::map<std::string, std::vector<std::string>> buttons;
    // The mods switched on, by game id ("yoda", "indy"), as mod ids.
    std::map<std::string, std::vector<std::string>> mods;
    // By game id.
    std::map<std::string, GameHistory> history;

    friend bool operator==(const Settings&, const Settings&) = default;
};

// Missing or damaged files give the defaults.
Settings loadSettings(const std::filesystem::path& file);
void saveSettings(const Settings& settings, const std::filesystem::path& file);

} // namespace das::app
