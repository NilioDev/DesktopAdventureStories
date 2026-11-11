// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bug snapshots: one zip file with everything needed to see a problem again
// (docs/bug-snapshots.md). The player's note, a description of the game at
// that moment, a save game to start from the same place, a screenshot, the
// last controls pressed and the end of the log. Snapshots stay on the
// player's device unless they add one to a report themselves.
#pragma once

#include "das/data/binary.hpp"
#include "das/data/game_data.hpp"
#include "das/world/generator.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace das::world {

class World;

struct SnapshotInfo {
    std::string kind;    // what sort of problem the player picked ("Stuck", ...)
    std::string note;    // in the player's own words
    std::string version; // of the program
    std::string system;  // platform, renderer, screen, gamepads
    std::string time;    // when, as the player's clock says
    std::string game;    // "yoda" or "indy"; empty on the title screen
    // Set for a generated world (a game that can be saved); otherwise only
    // the zone and the state are kept.
    std::optional<GeneratorOptions> options;
    int difficulty = 50;
    std::vector<std::string> mods;
    std::string log;    // the end of the log
    std::string inputs; // the last controls pressed, one per line
    data::Bytes screenshot; // PNG; may be empty
};

// A short description of the world at this moment, for people: zone,
// position, health, items, the map site and its puzzle, scripts waiting.
std::string describeWorld(const World& world, const data::GameData& data, const GeneratorOptions* options = nullptr);

// The snapshot as a zip file. `world` may be null (a problem on the title
// screen). Holds report.txt, and when there is something to put in them
// save.json (state.json for a single zone), screenshot.png, inputs.txt and log.txt.
data::Bytes writeSnapshot(const World* world, const data::GameData* data, const SnapshotInfo& info);

// The save game in a snapshot (save.json), if it has one. Throws
// data::DataError when the file is not a zip archive.
std::optional<std::string> snapshotSave(data::ByteView snapshot);

} // namespace das::world
