// SPDX-License-Identifier: GPL-3.0-or-later
//
// The zone script interpreter. See docs/formats/scripts.md for what each
// condition and instruction does.
#include "das/world/world.hpp"

#include "das/util/text.hpp"

#include <algorithm>

namespace das::world {

using script::Condition;
using script::Event;
using script::Instruction;

namespace {

constexpr int kLayers = 3;
// Update passes started by other passes; a guard against scripts that keep
// starting each other.
constexpr int kMaxChainedUpdates = 32;
// In the original text, these bytes stand for the names of the zone's
// required item and find item.
constexpr char kRequiredItemMark = '\xa2';
constexpr char kFindItemMark = '\xa5';

int arg(const data::ScriptLine& line, int index) { return line.args[static_cast<std::size_t>(index)]; }

bool validLayer(int layer) { return layer >= 0 && layer < kLayers; }

} // namespace

std::string World::tileName(std::uint16_t tile) const {
    for (const auto& n : data_.tileNames) {
        if (n.tile == tile) {
            return n.name;
        }
    }
    return {};
}

// Fills in item names, fixes "a" before a vowel, and converts to UTF-8.
std::string World::speechText(const std::string& raw) const {
    const ZoneGoal g = goal(zone_);
    std::string out;
    for (const char c : raw) {
        if (c == kRequiredItemMark || c == kFindItemMark) {
            const std::uint16_t item = c == kRequiredItemMark ? g.requiredItem : g.findItem;
            const std::string name = item == data::kNoTile ? std::string() : tileName(item);
            if (!name.empty() && std::string("AEIOUaeiou").find(name[0]) != std::string::npos &&
                out.size() >= 2 && (out[out.size() - 2] == 'a' || out[out.size() - 2] == 'A') &&
                out.back() == ' ' && (out.size() == 2 || out[out.size() - 3] == ' ')) {
                out.insert(out.size() - 1, "n");
            }
            out += name;
            continue;
        }
        if (c == '\r') {
            continue;
        }
        out.push_back(c);
    }
    return text::windows1252ToUtf8(out);
}

void World::setTile(Position at, int layer, std::uint16_t tile) {
    if (inside(at) && validLayer(layer)) {
        cells()[static_cast<std::size_t>(at.y * zone().width + at.x)].layers[static_cast<std::size_t>(layer)] = tile;
    }
}

void World::trigger(Event event, EventArgs args) {
    if (!zone().actions || zone().actions->empty()) {
        return;
    }
    Pass pass;
    pass.event = event;
    pass.args = args;
    passes_.push_back(pass);
}

std::vector<Effect> World::takeEffects() {
    std::vector<Effect> out;
    out.swap(effects_);
    return out;
}

void World::resume() {
    if (!paused_) {
        return;
    }
    paused_ = false;
    runScripts();
}

void World::changeZone(int zoneIndex, Position at) {
    if (zoneIndex < 0 || static_cast<std::size_t>(zoneIndex) >= data_.zones.size()) {
        return;
    }
    enter(zoneIndex, at);
}

void World::runScripts() {
    while (!paused_ && !passes_.empty() && outcome_ == Outcome::Playing) {
        Pass& pass = passes_.front();
        const auto& actions = *zone().actions;
        bool stopped = false;
        while (pass.action < actions.size()) {
            const data::Action& action = actions[pass.action];
            if (!pass.inAction) {
                bool holds = !actionDisabled(pass.action);
                for (std::size_t i = 0; holds && i < action.conditions.size(); ++i) {
                    holds = conditionHolds(action.conditions[i], pass);
                }
                if (!holds) {
                    ++pass.action;
                    continue;
                }
                pass.inAction = true;
                pass.instruction = 0;
                pass.disableAction = false;
                if (pass.event == Event::PlaceItem) {
                    reacted_ = true;
                }
            }
            while (pass.instruction < action.instructions.size()) {
                const int zoneBefore = zone_;
                const auto entered = zoneChanged_;
                zoneChanged_ = false;
                const bool go = execute(action.instructions[pass.instruction], pass);
                if (zoneChanged_ || zone_ != zoneBefore) {
                    // enter() replaced the passes; `pass` is gone.
                    return;
                }
                zoneChanged_ = entered;
                if (!go) {
                    paused_ = true;
                    return;
                }
                ++pass.instruction;
                if (outcome_ != Outcome::Playing) {
                    stopped = true;
                    break;
                }
            }
            if (pass.disableAction) {
                state().actionDisabled[pass.action] = true;
            }
            pass.inAction = false;
            ++pass.action;
            if (stopped) {
                break;
            }
        }
        const bool spoke = pass.spoke;
        const bool wasUpdate = pass.event == Event::Update;
        // A script that put the hero on another cell counts as a step onto
        // where he ended up. Yoda Stories needs this: after the landing on
        // Dagobah a script fences the hero in, and the one that takes the
        // fence away again waits for him to stand next to the ship. Not
        // after a step itself, or two teleporters would send him to and fro.
        const bool placed = pass.placedHero && pass.event != Event::Walk;
        passes_.pop_front();
        if (stopped) {
            passes_.clear();
            break;
        }
        if (placed) {
            trigger(Event::Walk, {player_.x, player_.y, 0, 0, -1});
        }
        // After speech, the original runs the zone's scripts once more so
        // that conversations can go on.
        if (spoke && chainedUpdates_ < kMaxChainedUpdates) {
            ++chainedUpdates_;
            trigger(Event::Update);
        } else if (wasUpdate && passes_.empty()) {
            chainedUpdates_ = 0;
        }
    }
    if (passes_.empty()) {
        state().initialized = true;
    }
}

bool World::conditionHolds(const data::ScriptLine& line, const Pass& pass) {
    const ZoneState& s = state();
    const ZoneGoal g = goal(zone_);
    const EventArgs& e = pass.args;
    const auto tileAt = [&](int x, int y, int layer) -> int {
        if (!inside({x, y}) || !validLayer(layer)) {
            return -2; // never equal to a tile or -1
        }
        const std::uint16_t t = tile({x, y}, layer);
        return t == data::kNoTile ? -1 : t;
    };
    const auto itemTarget = [&] {
        // The tile the item is used on must match, or the cell must be empty.
        if (arg(line, 0) != e.x || arg(line, 1) != e.y) {
            return false;
        }
        const int t = tileAt(e.x, e.y, arg(line, 2));
        return t == arg(line, 3) || t == -1;
    };

    switch (script::condition(data_.layout(), line.opcode)) {
    case Condition::FirstEnter: return pass.event == Event::Enter && !s.initialized;
    case Condition::Enter: return pass.event == Event::Enter;
    case Condition::Update: return pass.event == Event::Update;
    case Condition::Bump:
        return pass.event == Event::Bump && e.x + e.dx == arg(line, 0) && e.y + e.dy == arg(line, 1) &&
               tileAt(arg(line, 0), arg(line, 1), 1) == arg(line, 2);
    case Condition::StandingOn:
        return pass.event == Event::Walk && e.x == arg(line, 0) && e.y == arg(line, 1) &&
               tileAt(arg(line, 0), arg(line, 1), 0) == arg(line, 2);
    case Condition::PlaceItem: {
        if (pass.event != Event::PlaceItem || !itemTarget()) {
            return false;
        }
        const int wanted = arg(line, 4) == -1 ? (g.requiredItem == data::kNoTile ? -1 : g.requiredItem) : arg(line, 4);
        return wanted >= 0 && e.item == wanted;
    }
    case Condition::PlaceItemIsNot: {
        if (pass.event != Event::PlaceItem || !itemTarget()) {
            return false;
        }
        const int wanted = arg(line, 4) == -1 ? (g.requiredItem == data::kNoTile ? -1 : g.requiredItem) : arg(line, 4);
        return wanted >= 0 && e.item != wanted;
    }
    case Condition::CounterIs: return s.counter == arg(line, 0);
    case Condition::CounterIsNot: return s.counter != arg(line, 0);
    case Condition::RandomIs: return s.random == arg(line, 0);
    case Condition::RandomIsNot: return s.random != arg(line, 0);
    case Condition::RandomIsGreaterThan: return s.random > arg(line, 0);
    case Condition::RandomIsLessThan: return s.random < arg(line, 0);
    case Condition::RandomIsSet: return s.random >= 0;
    case Condition::SharedCounterIs: return s.sharedCounter == arg(line, 0);
    case Condition::SharedCounterIsNot: return s.sharedCounter != arg(line, 0);
    case Condition::SharedCounterIsLessThan: return s.sharedCounter < arg(line, 0);
    case Condition::SharedCounterIsGreaterThan: return s.sharedCounter > arg(line, 0);
    case Condition::TileIs:
    case Condition::VariableIs: return tileAt(arg(line, 1), arg(line, 2), arg(line, 3)) == arg(line, 0);
    case Condition::MonsterIsDead: {
        const auto i = static_cast<std::size_t>(arg(line, 0));
        return arg(line, 0) >= 0 && i < s.monsterDead.size() && s.monsterDead[i];
    }
    case Condition::NoMonstersActive:
        for (std::size_t i = 0; i < s.monsterDead.size(); ++i) {
            if (!s.monsterDead[i]) {
                return false;
            }
        }
        return true;
    case Condition::HasItem: {
        const int item = arg(line, 0) == -1 ? (g.findItem == data::kNoTile ? -1 : g.findItem) : arg(line, 0);
        return item >= 0 && hasItem(static_cast<std::uint16_t>(item));
    }
    case Condition::RequiredItemIs:
        return arg(line, 0) == g.requiredItem || arg(line, 0) == g.requiredItem2;
    case Condition::FindItemIs: return arg(line, 0) == g.findItem;
    case Condition::HasRequiredItem:
        return (g.requiredItem != data::kNoTile && hasItem(g.requiredItem)) ||
               (g.requiredItem2 != data::kNoTile && hasItem(g.requiredItem2));
    case Condition::EndingIs: return ending_ == arg(line, 0);
    case Condition::ZoneIsSolved: return g.solved;
    case Condition::MissionNotDone: return mission_ == -1;
    case Condition::MissionDone: return mission_ == 1;
    case Condition::HealthIsLessThan: return health_ < arg(line, 0);
    case Condition::HealthIsGreaterThan: return health_ > arg(line, 0);
    case Condition::HeroIsAt: return player_.x == arg(line, 0) && player_.y == arg(line, 1);
    case Condition::GamesWonIs: return gamesWon_ == arg(line, 0);
    case Condition::GamesWonIsGreaterThan: return gamesWon_ > arg(line, 0);
    case Condition::HasQuestHotspotAt:
        if (zone().hotspots) {
            const auto& list = *zone().hotspots;
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (list[i].type == 0 && list[i].x == arg(line, 0) && list[i].y == arg(line, 1) && hotspotEnabled(i)) {
                    return true;
                }
            }
        }
        return false;
    case Condition::Always: return true;
    case Condition::Unused21:
    case Condition::Unknown: return false;
    }
    return false;
}

bool World::execute(const data::ScriptLine& line, Pass& pass) {
    ZoneState& s = state();
    const ZoneGoal g = goal(zone_);
    const auto at = [&](int xi, int yi) { return Position{arg(line, xi), arg(line, yi)}; };

    switch (script::instruction(data_.layout(), line.opcode)) {
    case Instruction::PlaceTile:
    case Instruction::SetVariable:
        setTile(at(0, 1), arg(line, 2), static_cast<std::uint16_t>(arg(line, 3)));
        return true;
    case Instruction::RemoveTile: setTile(at(0, 1), arg(line, 2), data::kNoTile); return true;
    case Instruction::MoveTile: {
        const Position from = at(0, 1);
        const int layer = arg(line, 2);
        if (!inside(from) || !validLayer(layer)) {
            return true;
        }
        const std::uint16_t t = tile(from, layer);
        setTile(from, layer, data::kNoTile);
        setTile(at(3, 4), layer, t);
        return true;
    }
    case Instruction::DrawTile: overlays_.emplace_back(at(0, 1), static_cast<std::uint16_t>(arg(line, 2))); return true;
    case Instruction::SpeakHero:
    case Instruction::SpeakNpc: {
        Effect effect;
        effect.kind = Effect::Kind::Speech;
        effect.text = speechText(line.text);
        if (script::instruction(data_.layout(), line.opcode) == Instruction::SpeakNpc) {
            effect.at = at(0, 1);
        }
        effects_.push_back(std::move(effect));
        pass.spoke = true;
        // Pausing does not advance the pass, so step past this line here.
        ++pass.instruction;
        return false;
    }
    case Instruction::Wait: {
        Effect effect;
        effect.kind = Effect::Kind::Wait;
        effect.value = std::max(0, arg(line, 0)) * 100;
        effects_.push_back(effect);
        ++pass.instruction;
        return false;
    }
    case Instruction::PlaySound: {
        Effect effect;
        effect.kind = Effect::Kind::Sound;
        effect.value = arg(line, 0);
        effects_.push_back(effect);
        return true;
    }
    case Instruction::StopSound: effects_.push_back({Effect::Kind::StopSound, {}, {}, 0}); return true;
    case Instruction::RollDice:
        if (arg(line, 0) > 0) {
            s.random = static_cast<int>(rng_() % static_cast<std::uint32_t>(arg(line, 0))) + 1;
        }
        return true;
    case Instruction::SetRandom: s.random = arg(line, 0); return true;
    case Instruction::SetCounter: s.counter = arg(line, 0); return true;
    case Instruction::AddToCounter: s.counter += arg(line, 0); return true;
    case Instruction::SetSharedCounter: s.sharedCounter = arg(line, 0); return true;
    case Instruction::AddToSharedCounter: s.sharedCounter += arg(line, 0); return true;
    case Instruction::HideHero: heroVisible_ = false; return true;
    case Instruction::ShowHero: heroVisible_ = true; return true;
    case Instruction::SetHeroPosition:
        if (inside(at(0, 1))) {
            pass.placedHero = pass.placedHero || !(player_ == at(0, 1));
            player_ = at(0, 1);
        }
        return true;
    case Instruction::MoveHeroTo: {
        // One step per call, with a pause in between, until the hero is there.
        if (!pass.moving && arg(line, 0) != -1 && arg(line, 1) != -1 && inside(at(0, 1))) {
            player_ = at(0, 1);
        }
        pass.moving = true;
        const Position from = player_;
        const Position to = at(2, 3);
        if (from == to || !inside(to)) {
            pass.moving = false;
            return true;
        }
        Position next = from;
        next.x += to.x > from.x ? 1 : to.x < from.x ? -1 : 0;
        next.y += to.y > from.y ? 1 : to.y < from.y ? -1 : 0;
        player_ = next;
        pass.placedHero = true;
        Effect effect;
        effect.kind = Effect::Kind::Wait;
        effect.value = std::max(0, arg(line, 4)) * 100;
        effects_.push_back(effect);
        if (next == to) {
            pass.moving = false;
            ++pass.instruction;
        }
        return false;
    }
    case Instruction::DisableAction: pass.disableAction = true; return true;
    case Instruction::EnableHotspot:
    case Instruction::DisableHotspot: {
        const auto i = static_cast<std::size_t>(arg(line, 0));
        if (arg(line, 0) >= 0 && i < s.hotspotEnabled.size()) {
            s.hotspotEnabled[i] = script::instruction(data_.layout(), line.opcode) == Instruction::EnableHotspot;
        }
        return true;
    }
    case Instruction::EnableMonster:
    case Instruction::DisableMonster: {
        const auto i = static_cast<std::size_t>(arg(line, 0));
        if (arg(line, 0) >= 0 && i < s.monsterEnabled.size()) {
            s.monsterEnabled[i] = script::instruction(data_.layout(), line.opcode) == Instruction::EnableMonster;
        }
        return true;
    }
    case Instruction::EnableAllMonsters: std::fill(s.monsterEnabled.begin(), s.monsterEnabled.end(), true); return true;
    case Instruction::DisableAllMonsters: std::fill(s.monsterEnabled.begin(), s.monsterEnabled.end(), false); return true;
    case Instruction::DropItem: {
        // The item appears on the cell; the hero picks it up there.
        const int item = arg(line, 0) == -1 ? (g.findItem == data::kNoTile ? -1 : g.findItem) : arg(line, 0);
        if (item >= 0) {
            const Position cell = at(1, 2);
            if (inside(cell) && tile(cell, 1) != data::kNoTile) {
                // Something stands there already (a cabinet in a wall): the
                // item goes inside it, and bumping it finds the item. Putting
                // the item on top would wipe out what was there.
                s.hidden[{cell.x, cell.y}] = static_cast<std::uint16_t>(item);
            } else {
                setTile(cell, 1, static_cast<std::uint16_t>(item));
            }
        }
        return true;
    }
    case Instruction::AddItem:
        if (arg(line, 0) >= 0) {
            addItem(static_cast<std::uint16_t>(arg(line, 0)));
        }
        return true;
    case Instruction::RemoveItem: {
        const int item = arg(line, 0) == -1 ? (g.requiredItem == data::kNoTile ? -1 : g.requiredItem) : arg(line, 0);
        if (item >= 0) {
            const auto it = std::find(inventory_.begin(), inventory_.end(), static_cast<std::uint16_t>(item));
            if (it != inventory_.end()) {
                inventory_.erase(it);
            }
        }
        return true;
    }
    case Instruction::MarkAsSolved: setSolved(zone_); return true;
    case Instruction::WinGame:
        outcome_ = Outcome::Won;
        effects_.push_back({Effect::Kind::Won, {}, {}, 0});
        return true;
    case Instruction::LoseGame:
        health_ = 0;
        outcome_ = Outcome::Lost;
        effects_.push_back({Effect::Kind::Lost, {}, {}, 0});
        return true;
    case Instruction::AddHealth: health_ = std::clamp(health_ + arg(line, 0), 0, kMaxHealth); return true;
    case Instruction::ChangeZone: changeZone(arg(line, 0), at(1, 2)); return true;
    case Instruction::RedrawTile:
    case Instruction::RedrawTiles:
    case Instruction::RedrawZone:
    case Instruction::Render:
    case Instruction::Nothing:
    case Instruction::Unknown: return true;
    }
    return true;
}

} // namespace das::world
