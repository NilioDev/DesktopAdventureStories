// SPDX-License-Identifier: GPL-3.0-or-later
//
// Our own open data pack: the content of an original game data file as JSON
// and palette PNG files, plus the game's sound files. After a one-time
// import the original files are no longer needed. Writing a pack back to the
// original format gives the identical bytes. See docs/formats/pack.md.
#pragma once

#include "das/data/binary.hpp"
#include "das/data/game_data.hpp"
#include "das/data/palette.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace das::pack {

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kManifestName = "pack.json";

// A pack as a set of files: relative path (with '/') -> content.
using Files = std::map<std::string, data::Bytes>;

struct Pack {
    std::string game; // "yoda" or "indy"
    data::GameData data;
    data::Palette palette;
    Files sounds; // "sounds/<name>" -> file content, as far as found
};

std::string gameId(data::Layout layout);

Files encode(const Pack& pack);
Pack decode(const Files& files);

void writeFolder(const Files& files, const std::filesystem::path& dir);
Files readFolder(const std::filesystem::path& dir);

} // namespace das::pack
