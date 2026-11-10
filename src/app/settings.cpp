// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.hpp"

#include "das/util/json.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace das::app {

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
