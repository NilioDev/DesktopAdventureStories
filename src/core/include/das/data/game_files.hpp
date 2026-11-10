// SPDX-License-Identifier: GPL-3.0-or-later
//
// Finding the player's original game files on disk.
#pragma once

#include "das/data/binary.hpp"
#include "das/data/container.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace das::data {

struct GameFiles {
    Layout game = Layout::Yoda;
    std::filesystem::path dataFile;   // YODESK.DTA or DESKTOP.DAW
    std::filesystem::path executable; // YODESK.EXE or DESKADV.EXE; may be empty
};

// Looks for the games in a folder and its direct subfolders. File names are
// matched without regard to case. A path to a data file is accepted too.
std::vector<GameFiles> findGameFiles(const std::filesystem::path& where);

Bytes readFile(const std::filesystem::path& path);

} // namespace das::data
