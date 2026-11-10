// SPDX-License-Identifier: GPL-3.0-or-later
//
// Save games: the generator options that make the world again, and what
// play changed in it (docs/savegames.md).
#pragma once

#include "das/data/game_data.hpp"
#include "das/util/json.hpp"
#include "das/world/generator.hpp"

#include <string>
#include <string_view>

namespace das::world {

class World;

struct SaveGame {
    std::string game; // which game it belongs to ("yoda", "indy")
    GeneratorOptions options;
    json::Value state;
};

std::string writeSaveGame(const World& world, const GeneratorOptions& options, std::string_view game);
// Throws json::Error when the text is not a save game we can read.
SaveGame readSaveGame(std::string_view text);
// Makes the saved world again and puts the saved state on it.
void restoreGame(World& world, const data::GameData& data, const SaveGame& save);

} // namespace das::world
