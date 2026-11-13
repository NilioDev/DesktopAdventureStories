// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"

#include "das/util/json.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace das::app {

namespace {

std::map<std::string, std::vector<std::string>> readControls(const json::Value& v) {
    std::map<std::string, std::vector<std::string>> out;
    for (const auto& [action, list] : v.asObject()) {
        auto& names = out[action];
        for (const auto& name : list.asArray()) {
            names.push_back(name.asString());
        }
    }
    return out;
}

json::Value writeControls(const std::map<std::string, std::vector<std::string>>& controls) {
    json::Object out;
    for (const auto& [action, names] : controls) {
        json::Array list;
        for (const auto& name : names) {
            list.emplace_back(name);
        }
        out.emplace_back(action, std::move(list));
    }
    return out;
}

std::vector<int> readInts(const json::Value& v) {
    std::vector<int> out;
    for (const auto& x : v.asArray()) {
        out.push_back(x.as<int>());
    }
    return out;
}

json::Value writeInts(const std::vector<int>& values) {
    json::Array out;
    for (const int x : values) {
        out.emplace_back(x);
    }
    return out;
}

std::map<std::string, GameHistory> readHistory(const json::Value& v) {
    std::map<std::string, GameHistory> out;
    for (const auto& [game, h] : v.asObject()) {
        GameHistory& g = out[game];
        if (const auto* x = h.find("gamesWon")) {
            g.gamesWon = std::max(0, x->as<int>());
        }
        if (const auto* x = h.find("planet")) {
            g.planet = std::clamp(x->as<int>(), 1, 3);
        }
        if (const auto* x = h.find("goals")) {
            for (const auto& [planet, list] : x->asObject()) {
                g.goals[std::atoi(planet.c_str())] = readInts(list);
            }
        }
        if (const auto* x = h.find("zones")) {
            g.zones = readInts(*x);
        }
    }
    return out;
}

json::Value writeHistory(const std::map<std::string, GameHistory>& history) {
    json::Object out;
    for (const auto& [game, g] : history) {
        json::Object goals;
        for (const auto& [planet, list] : g.goals) {
            goals.emplace_back(std::to_string(planet), writeInts(list));
        }
        out.emplace_back(game, json::Object{
                                   {"gamesWon", g.gamesWon},
                                   {"planet", g.planet},
                                   {"goals", std::move(goals)},
                                   {"zones", writeInts(g.zones)},
                               });
    }
    return out;
}

} // namespace

Settings loadSettings(const std::filesystem::path& file) {
    Settings s;
    std::string text;
    if (FILE* f = std::fopen(file.string().c_str(), "rb")) {
        char buffer[4096];
        std::size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) {
            text.append(buffer, n);
        }
        std::fclose(f);
    }
    if (text.empty()) {
        return s;
    }
    try {
        const json::Value v = json::parse(text);
        if (const auto* x = v.find("fullscreen")) {
            s.fullscreen = x->asBool();
        }
        if (const auto* x = v.find("pixelPerfect")) {
            s.pixelPerfect = x->asBool();
        }
        if (const auto* x = v.find("filter")) {
            const std::string& name = x->asString();
            s.filter = name == "smooth" ? Filter::Smooth : name == "scanlines" ? Filter::Scanlines : Filter::Sharp;
        }
        if (const auto* x = v.find("volume")) {
            s.volume = std::clamp(x->as<int>(), 0, 100);
        }
        if (const auto* x = v.find("music")) {
            s.music = std::clamp(x->as<int>(), 0, 100);
        }
        if (const auto* x = v.find("touchPad")) {
            const std::string& name = x->asString();
            s.touchPad = name == "on" ? TouchPad::On : name == "off" ? TouchPad::Off : TouchPad::Auto;
        }
        if (const auto* x = v.find("howToPlaySeen")) {
            s.howToPlaySeen = x->asBool();
        }
        if (const auto* x = v.find("keys")) {
            s.keys = readControls(*x);
        }
        if (const auto* x = v.find("buttons")) {
            s.buttons = readControls(*x);
        }
        if (const auto* x = v.find("mods")) {
            s.mods = readControls(*x);
        }
        if (const auto* x = v.find("history")) {
            s.history = readHistory(*x);
        }
    } catch (const std::exception& e) {
        SDL_Log("Settings not read, using the defaults: %s", e.what());
        return Settings{};
    }
    return s;
}

void saveSettings(const Settings& s, const std::filesystem::path& file) {
    const char* filter = s.filter == Filter::Smooth ? "smooth" : s.filter == Filter::Scanlines ? "scanlines" : "sharp";
    const std::string text = json::write(json::Object{
        {"fullscreen", s.fullscreen},
        {"pixelPerfect", s.pixelPerfect},
        {"filter", filter},
        {"volume", s.volume},
        {"music", s.music},
        {"touchPad", s.touchPad == TouchPad::On ? "on" : s.touchPad == TouchPad::Off ? "off" : "auto"},
        {"howToPlaySeen", s.howToPlaySeen},
        {"keys", writeControls(s.keys)},
        {"buttons", writeControls(s.buttons)},
        {"mods", writeControls(s.mods)},
        {"history", writeHistory(s.history)},
    });
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const auto temp = file.string() + ".new";
    if (FILE* f = std::fopen(temp.c_str(), "wb")) {
        const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        std::fclose(f);
        if (ok) {
            std::filesystem::rename(temp, file, ec);
        }
    }
}

} // namespace das::app
