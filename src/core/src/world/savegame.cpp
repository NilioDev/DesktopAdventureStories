// SPDX-License-Identifier: GPL-3.0-or-later
//
// Saving and restoring what play changed in a world (docs/savegames.md).
#include "das/world/savegame.hpp"

#include "das/world/world.hpp"

#include <algorithm>
#include <map>
#include <sstream>

namespace das::world {

namespace {

json::Value position(Position p) { return json::Array{p.x, p.y}; }

Position readPosition(const json::Value& v) {
    const auto& a = v.asArray();
    if (a.size() != 2) {
        throw json::Error("a position needs two numbers");
    }
    return {a[0].as<int>(), a[1].as<int>()};
}

json::Value flagText(const std::vector<bool>& list) {
    std::string out;
    for (const bool b : list) {
        out += b ? '1' : '0';
    }
    return out;
}

void readFlagText(const json::Value& v, std::vector<bool>& list) {
    const std::string& text = v.asString();
    for (std::size_t i = 0; i < text.size() && i < list.size(); ++i) {
        list[i] = text[i] == '1';
    }
}

json::Value numbers(const std::vector<std::uint16_t>& list) {
    json::Array out;
    for (const auto n : list) {
        out.emplace_back(static_cast<int>(n));
    }
    return out;
}

} // namespace

json::Value World::saveState() const {
    json::Object zones;
    std::vector<int> order;
    for (const auto& entry : states_) {
        order.push_back(entry.first);
    }
    std::sort(order.begin(), order.end());
    for (const int index : order) {
        const ZoneState& s = states_.at(index);
        const auto& original = data_.zones.at(static_cast<std::size_t>(index)).cells;
        json::Array cells; // changed cells: index, then the three layers
        for (std::size_t i = 0; i < s.cells.size() && i < original.size(); ++i) {
            if (s.cells[i].layers != original[i].layers) {
                cells.emplace_back(json::Array{static_cast<int>(i), static_cast<int>(s.cells[i].layers[0]),
                                               static_cast<int>(s.cells[i].layers[1]),
                                               static_cast<int>(s.cells[i].layers[2])});
            }
        }
        json::Array hidden;
        for (const auto& [at, item] : s.hidden) {
            hidden.emplace_back(json::Array{at.first, at.second, static_cast<int>(item)});
        }
        json::Array monsters;
        for (const auto& m : s.monsters) {
            monsters.emplace_back(json::Array{m.at.x, m.at.y, m.health, static_cast<int>(m.facing), m.waypoint});
        }
        zones.emplace_back(std::to_string(index),
                           json::Object{{"cells", std::move(cells)},
                                        {"hidden", std::move(hidden)},
                                        {"hotspots", flagText(s.hotspotEnabled)},
                                        {"actionsOff", flagText(s.actionDisabled)},
                                        {"monstersOn", flagText(s.monsterEnabled)},
                                        {"monstersDead", flagText(s.monsterDead)},
                                        {"monsters", std::move(monsters)},
                                        {"entered", s.initialized},
                                        {"counter", s.counter},
                                        {"random", s.random},
                                        {"sharedCounter", s.sharedCounter}});
    }
    json::Array goals;
    std::map<int, int> sites(zoneSite_.begin(), zoneSite_.end());
    for (const auto& [zone, index] : sites) {
        const ZoneGoal& g = goals_[static_cast<std::size_t>(index)];
        goals.emplace_back(json::Array{zone, index, static_cast<int>(g.requiredItem),
                                       static_cast<int>(g.requiredItem2), static_cast<int>(g.findItem), g.solved});
    }
    json::Array returns;
    for (const auto& r : returns_) {
        returns.emplace_back(json::Array{r.zone, r.at.x, r.at.y});
    }
    std::ostringstream rng;
    rng << rng_;
    return json::Object{
        {"zone", zone_},
        {"player", position(player_)},
        {"facing", static_cast<int>(facing_)},
        {"returns", std::move(returns)},
        {"inventory", numbers(inventory_)},
        {"weapon", static_cast<int>(weapon_)},
        {"health", health_},
        {"heroVisible", heroVisible_},
        {"outcome", static_cast<int>(outcome_)},
        {"gamesWon", gamesWon_},
        {"ending", ending_},
        {"mission", mission_},
        {"difficulty", difficulty_},
        {"goals", std::move(goals)},
        {"zones", std::move(zones)},
        {"rng", rng.str()},
        {"drop", drop_ ? json::Value(json::Array{drop_->at.x, drop_->at.y, static_cast<int>(drop_->item)})
                       : json::Value()},
    };
}

void World::loadState(const json::Value& v) {
    states_.clear();
    passes_.clear();
    drop_.reset();
    if (const auto& d = v["drop"]; d.isArray() && d.asArray().size() == 3) {
        const auto& a = d.asArray();
        drop_ = Drop{{a[0].as<int>(), a[1].as<int>()}, a[2].as<std::uint16_t>()};
    }
    effects_.clear();
    shots_.clear();
    overlays_.clear();
    paused_ = false;

    for (const auto& [key, z] : v["zones"].asObject()) {
        const int index = std::stoi(key);
        if (index < 0 || static_cast<std::size_t>(index) >= data_.zones.size()) {
            throw json::Error("a saved zone does not exist: " + key);
        }
        ZoneState& s = stateOf(index); // the zone as it starts, then the changes
        for (const auto& c : z["cells"].asArray()) {
            const auto& a = c.asArray();
            const auto i = static_cast<std::size_t>(a.at(0).as<std::uint32_t>());
            if (i < s.cells.size()) {
                for (std::size_t layer = 0; layer < 3; ++layer) {
                    s.cells[i].layers[layer] = a.at(layer + 1).as<std::uint16_t>();
                }
            }
        }
        s.hidden.clear();
        for (const auto& h : z["hidden"].asArray()) {
            const auto& a = h.asArray();
            s.hidden[{a.at(0).as<int>(), a.at(1).as<int>()}] = a.at(2).as<std::uint16_t>();
        }
        readFlagText(z["hotspots"], s.hotspotEnabled);
        readFlagText(z["actionsOff"], s.actionDisabled);
        readFlagText(z["monstersOn"], s.monsterEnabled);
        readFlagText(z["monstersDead"], s.monsterDead);
        const auto& monsters = z["monsters"].asArray();
        for (std::size_t i = 0; i < monsters.size() && i < s.monsters.size(); ++i) {
            const auto& a = monsters[i].asArray();
            auto& m = s.monsters[i];
            m.at = {a.at(0).as<int>(), a.at(1).as<int>()};
            m.health = a.at(2).as<int>();
            m.facing = static_cast<data::Facing>(a.at(3).as<int>() & 7);
            m.waypoint = a.at(4).as<int>();
        }
        s.initialized = z["entered"].asBool();
        s.counter = z["counter"].as<int>();
        s.random = z["random"].as<int>();
        s.sharedCounter = z["sharedCounter"].as<int>();
    }

    for (const auto& g : v["goals"].asArray()) {
        const auto& a = g.asArray();
        const int zone = a.at(0).as<int>();
        const auto index = static_cast<std::size_t>(a.at(1).as<std::uint32_t>());
        if (index >= goals_.size()) {
            goals_.resize(index + 1);
            Site site;
            site.zone = zone;
            site.zones = {zone};
            sites_.resize(index + 1, site);
        }
        zoneSite_[zone] = static_cast<int>(index);
        goals_[index] = {a.at(2).as<std::uint16_t>(), a.at(3).as<std::uint16_t>(), a.at(4).as<std::uint16_t>(),
                         a.at(5).asBool()};
    }

    returns_.clear();
    for (const auto& r : v["returns"].asArray()) {
        const auto& a = r.asArray();
        returns_.push_back({a.at(0).as<int>(), {a.at(1).as<int>(), a.at(2).as<int>()}});
    }
    inventory_.clear();
    for (const auto& i : v["inventory"].asArray()) {
        inventory_.push_back(i.as<std::uint16_t>());
    }
    weapon_ = v["weapon"].as<std::uint16_t>();
    health_ = std::clamp(v["health"].as<int>(), 0, kMaxHealth);
    heroVisible_ = v["heroVisible"].asBool();
    outcome_ = static_cast<Outcome>(std::clamp(v["outcome"].as<int>(), 0, 2));
    gamesWon_ = v["gamesWon"].as<int>();
    ending_ = v["ending"].as<int>();
    mission_ = v["mission"].as<int>();
    difficulty_ = std::clamp(v["difficulty"].as<int>(), 0, 100);
    std::istringstream rng(v["rng"].asString());
    rng >> rng_;

    const int zone = v["zone"].as<int>();
    if (zone < 0 || static_cast<std::size_t>(zone) >= data_.zones.size()) {
        throw json::Error("the saved zone does not exist");
    }
    zone_ = zone;
    (void)state();
    player_ = readPosition(v["player"]);
    facing_ = static_cast<data::Facing>(v["facing"].as<int>() & 7);
}

namespace {

constexpr int kSaveVersion = 1;

} // namespace

std::string writeSaveGame(const World& world, const GeneratorOptions& options, std::string_view game,
                          const std::vector<std::string>& mods) {
    json::Array modList;
    for (const auto& m : mods) {
        modList.emplace_back(m);
    }
    json::Array recentGoals;
    for (const int g : options.recentGoals) {
        recentGoals.emplace_back(g);
    }
    json::Array recentZones;
    for (const int z : options.recentZones) {
        recentZones.emplace_back(z);
    }
    const json::Value v = json::Object{
        {"format", "das-save"},
        {"version", kSaveVersion},
        {"game", std::string(game)},
        {"mods", std::move(modList)},
        {"world",
         json::Object{{"seed", static_cast<std::int64_t>(options.seed)},
                      {"size", static_cast<int>(options.size)},
                      {"planet", options.planet},
                      {"goal", options.goal},
                      {"gamesWon", options.gamesWon},
                      {"recentGoals", std::move(recentGoals)},
                      {"recentZones", std::move(recentZones)}}},
        {"state", world.saveState()},
    };
    return json::write(v);
}

SaveGame readSaveGame(std::string_view text) {
    const json::Value v = json::parse(text);
    if (const auto* f = v.find("format"); f == nullptr || !f->isString() || f->asString() != "das-save") {
        throw json::Error("this is not a save game");
    }
    if (v["version"].as<int>() > kSaveVersion) {
        throw json::Error("this save game is from a newer version");
    }
    SaveGame out;
    out.game = v["game"].asString();
    if (const auto* mods = v.find("mods")) {
        for (const auto& m : mods->asArray()) {
            out.mods.push_back(m.asString());
        }
    }
    const auto& w = v["world"];
    out.options.seed = w["seed"].as<std::uint32_t>();
    out.options.size = static_cast<WorldSize>(std::clamp(w["size"].as<int>(), 0, 2));
    out.options.planet = w["planet"].as<int>();
    out.options.goal = w["goal"].as<int>();
    out.options.gamesWon = w["gamesWon"].as<int>();
    for (const auto& g : w["recentGoals"].asArray()) {
        out.options.recentGoals.push_back(g.as<int>());
    }
    for (const auto& z : w["recentZones"].asArray()) {
        out.options.recentZones.push_back(z.as<int>());
    }
    out.state = v["state"];
    return out;
}

void restoreGame(World& world, const data::GameData& data, const SaveGame& save) {
    world.setMap(generateWorld(data, save.options));
    world.loadState(save.state);
}

} // namespace das::world
