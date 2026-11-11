// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/world/snapshot.hpp"

#include "das/util/json.hpp"
#include "das/util/text.hpp"
#include "das/util/zip.hpp"
#include "das/world/savegame.hpp"
#include "das/world/world.hpp"

#include <algorithm>

namespace das::world {

namespace {

const char* facingName(data::Facing f) {
    switch (f) {
    case data::Facing::Up: return "up";
    case data::Facing::Down: return "down";
    case data::Facing::UpLeft: return "up-left";
    case data::Facing::Left: return "left";
    case data::Facing::DownLeft: return "down-left";
    case data::Facing::UpRight: return "up-right";
    case data::Facing::Right: return "right";
    case data::Facing::DownRight: return "down-right";
    }
    return "?";
}

const char* siteKindName(SiteKind k) {
    switch (k) {
    case SiteKind::Empty: return "empty";
    case SiteKind::Town: return "town";
    case SiteKind::Puzzle: return "puzzle";
    case SiteKind::Goal: return "goal";
    case SiteKind::Dagobah: return "Dagobah";
    case SiteKind::None: break;
    }
    return "none";
}

const char* sizeName(WorldSize s) {
    switch (s) {
    case WorldSize::Small: return "small";
    case WorldSize::Large: return "large";
    default: return "medium";
    }
}

// "Name (tile 123)", or "-" for no tile.
std::string item(const data::GameData& data, std::uint16_t tile) {
    if (tile == data::kNoTile) {
        return "-";
    }
    for (const auto& n : data.tileNames) {
        if (n.tile == tile) {
            return text::windows1252ToUtf8(n.name) + " (tile " + std::to_string(tile) + ")";
        }
    }
    return "tile " + std::to_string(tile);
}

std::string position(Position p) { return std::to_string(p.x) + "," + std::to_string(p.y); }

// The map site of a zone (an outdoor zone or one of its rooms).
const Site* findSite(const WorldMap& map, int zone, const Map** on, int* index) {
    for (const Map* m : {&map.world, &map.dagobah}) {
        for (std::size_t i = 0; i < m->sites.size(); ++i) {
            const Site& s = m->sites[i];
            if (s.zone == zone || std::find(s.zones.begin(), s.zones.end(), zone) != s.zones.end()) {
                *on = m;
                *index = static_cast<int>(i);
                return &s;
            }
        }
    }
    return nullptr;
}

data::Bytes bytes(std::string_view text) { return data::Bytes(text.begin(), text.end()); }

} // namespace

std::string describeWorld(const World& w, const data::GameData& data, const GeneratorOptions* options) {
    std::string out;
    const auto line = [&](const std::string& name, const std::string& value) { out += name + ": " + value + "\n"; };
    const int zoneIndex = w.zoneIndex();
    const data::Zone& zone = w.zone();
    line("Zone", std::to_string(zoneIndex) + " (" + std::to_string(zone.width) + "x" + std::to_string(zone.height) +
                     ", type " + std::to_string(zone.type) + ", planet " + std::to_string(zone.planet) + ")");
    line("Hero", "at " + position(w.player()) + ", facing " + facingName(w.facing()) + ", health " +
                     std::to_string(w.health()) + "/" + std::to_string(kMaxHealth) +
                     (w.heroVisible() ? "" : ", hidden"));
    line("Weapon", item(data, w.weapon()));
    std::string items;
    for (const auto t : w.inventory()) {
        items += (items.empty() ? "" : "; ") + item(data, t);
    }
    line("Items", items.empty() ? "none" : items);
    line("Outcome", w.outcome() == Outcome::Won ? "won" : w.outcome() == Outcome::Lost ? "lost" : "playing");
    line("Scripts", std::string(w.paused() ? "paused (speech or wait)" : w.canSave() ? "idle" : "running") +
                        ", counter " + std::to_string(w.counter()) + ", random " + std::to_string(w.random()) +
                        ", shared counter " + std::to_string(w.sharedCounter()));
    line("Doors deep", std::to_string(w.doorDepth()));
    line("Monsters", std::to_string(w.monsters().size()) + " in this zone");
    if (!w.overlays().empty()) {
        line("Drawn by scripts", std::to_string(w.overlays().size()) + " tiles");
    }
    if (options) {
        line("World", "seed " + std::to_string(options->seed) + ", " + sizeName(options->size) + ", planet " +
                          std::to_string(w.map().planet) + ", goal puzzle " + std::to_string(w.map().goalPuzzle) +
                          ", " + std::to_string(w.map().puzzleCount) + " puzzles");
    }
    const Map* on = nullptr;
    int index = -1;
    if (const Site* site = findSite(w.map(), zoneIndex, &on, &index)) {
        const ZoneGoal goal = w.goal(zoneIndex);
        line("Map site", std::string(on == &w.map().dagobah ? "Dagobah " : "") + std::to_string(index % std::max(1, on->width)) +
                             "," + std::to_string(index / std::max(1, on->width)) + ", " + siteKindName(site->kind) +
                             ", role " + std::to_string(site->role) + (goal.solved ? ", solved" : ", open"));
        line("Puzzle", "needs " + item(data, goal.requiredItem) +
                           (goal.requiredItem2 != data::kNoTile ? " and " + item(data, goal.requiredItem2) : "") +
                           ", gives " + item(data, goal.findItem));
    } else if (options) {
        line("Map site", "none (not on the map)");
    }
    return out;
}

data::Bytes writeSnapshot(const World* world, const data::GameData* data, const SnapshotInfo& info) {
    std::string report = "Bug snapshot\n\n";
    report += "Kind: " + (info.kind.empty() ? std::string("-") : info.kind) + "\n";
    report += "Note: " + (info.note.empty() ? std::string("-") : info.note) + "\n";
    report += "Version: " + info.version + "\n";
    report += "System: " + info.system + "\n";
    report += "Time: " + info.time + "\n";
    report += "Game: " + (info.game.empty() ? std::string("-") : info.game) + "\n";
    std::string mods;
    for (const auto& m : info.mods) {
        mods += (mods.empty() ? "" : ", ") + m;
    }
    report += "Mods: " + (mods.empty() ? std::string("none") : mods) + "\n";
    if (world && data) {
        report += "Difficulty: " + std::to_string(info.difficulty) + "\n\n";
        report += describeWorld(*world, *data, info.options ? &*info.options : nullptr);
    } else {
        report += "\nNot in a game.\n";
    }

    std::vector<zip::Entry> entries;
    entries.push_back({"report.txt", bytes(report)});
    if (world && data) {
        if (info.options) {
            // A save game, as the game writes them: load it to start at the
            // same place (docs/bug-snapshots.md).
            entries.push_back({"save.json", bytes(writeSaveGame(*world, *info.options, info.game, info.mods))});
        } else {
            // A single zone tried from the data viewer or the editor: no save
            // game, but the state of what play changed.
                const json::Value state = json::Object{{"zone", world->zoneIndex()},
                                                   {"x", world->player().x},
                                                   {"y", world->player().y},
                                                   {"state", world->saveState()}};
            entries.push_back({"state.json", bytes(json::write(state))});
        }
    }
    if (!info.screenshot.empty()) {
        entries.push_back({"screenshot.png", info.screenshot});
    }
    if (!info.inputs.empty()) {
        entries.push_back({"inputs.txt", bytes(info.inputs)});
    }
    if (!info.log.empty()) {
        entries.push_back({"log.txt", bytes(info.log)});
    }
    return zip::write(entries);
}

std::optional<std::string> snapshotSave(data::ByteView snapshot) {
    for (const auto& e : zip::read(snapshot)) {
        if (e.name == "save.json") {
            return std::string(e.content.begin(), e.content.end());
        }
    }
    return std::nullopt;
}

} // namespace das::world
