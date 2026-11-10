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

struct Settings {
    bool fullscreen = false;
    // Pixel perfect scales the play field in whole steps only; otherwise it
    // fills the space it has.
    bool pixelPerfect = true;
    Filter filter = Filter::Sharp;
    int volume = 80; // sound effects, 0 to 100
    int music = 70;  // 0 to 100
    TouchPad touchPad = TouchPad::Auto;
    // Controls, per action (by its actionName): keys by SDL scancode name
    // and gamepad buttons by SDL button name. A missing action keeps its
    // default controls.
    std::map<std::string, std::vector<std::string>> keys;
    std::map<std::string, std::vector<std::string>> buttons;

    friend bool operator==(const Settings&, const Settings&) = default;
};

// Missing or damaged files give the defaults.
Settings loadSettings(const std::filesystem::path& file);
void saveSettings(const Settings& settings, const std::filesystem::path& file);

} // namespace das::app
