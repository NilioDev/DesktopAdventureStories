// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's Scripts tab: a zone's scripts as cards of plain sentences.
// Each line is chosen from a list of what scripts can check and do, its
// numbers are named (a tile, a place, a zone), and places are pointed out on
// the map beside it.
#include "editor.hpp"

#include "das/script/opcodes.hpp"
#include "das/util/text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

namespace das::app {

namespace {

constexpr float kPad = 6;

float F(int v) { return static_cast<float>(v); }

using script::Condition;
using script::Instruction;

// What a number in a line stands for.
enum class Arg : std::uint8_t { None, X, Y, X2, Y2, Layer, Tile, Item, Zone, Number, Person, Hotspot, Sound, Tenths };

struct LineKind {
    int id; // the Condition or Instruction
    const char* title;
    // The sentence: {0} to {4} are the numbers, {t} the text.
    const char* sentence;
    std::array<Arg, 5> args;
    bool text;
    const char* help;
};

using A = Arg;

// clang-format off
const LineKind kConditions[] = {
    {0, "First visit", "the hero enters this zone for the first time", {}, false,
     "Holds once: the first time the hero comes into the zone."},
    {1, "Enter", "the hero enters this zone", {}, false, "Holds each time the hero comes into the zone."},
    {2, "Bump", "the hero walks against {2} at {0},{1}", {A::X, A::Y, A::Tile}, false,
     "Holds when the hero walks against this tile (on the objects layer) at this place."},
    {3, "Use item on", "the hero uses {4} on {3} at {0},{1} ({2})", {A::X, A::Y, A::Layer, A::Tile, A::Item}, false,
     "Holds when the hero uses this item on this tile at this place."},
    {4, "Step on", "the hero steps on {2} at {0},{1}", {A::X, A::Y, A::Tile}, false,
     "Holds when the hero walks onto this place while the floor there is this tile."},
    {5, "Counter is", "the zone's counter is {0}", {A::Number}, false,
     "Every zone has a counter that scripts can set and add to. Holds when it has this value."},
    {6, "Dice is", "the dice show {0}", {A::Number}, false, "Holds when the last roll of the dice is this number."},
    {7, "Dice more than", "the dice show more than {0}", {A::Number}, false, "Holds when the last roll is higher."},
    {8, "Dice less than", "the dice show less than {0}", {A::Number}, false, "Holds when the last roll is lower."},
    {9, "Update", "the zone is updated", {}, false,
     "Holds right after entering and after a speech bubble is closed: good for things that follow each other."},
    {10, "Tile is", "{0} is at {1},{2} ({3})", {A::Tile, A::X, A::Y, A::Layer}, false,
     "Holds when this tile is at this place on this layer."},
    {11, "Person defeated", "{0} is defeated", {A::Person}, false, "Holds when this person of the zone is defeated."},
    {12, "No monsters", "no monsters are left", {}, false, "Holds when every monster of the zone is defeated or off."},
    {13, "Hero has", "the hero carries {0}", {A::Item}, false, "Holds when the hero carries this item."},
    {14, "Asked item is", "the item this zone asks for is {0}", {A::Item}, false,
     "Holds when the world generator chose this item for the zone to ask for."},
    {15, "Ending is", "the ending is {0}", {A::Number}, false, "Holds when the game uses this ending."},
    {16, "Zone solved", "this zone is solved", {}, false, "Holds when a script marked the zone as solved."},
    {17, "Mission not done", "the mission is not done", {}, false, "Holds while the game's mission is not done."},
    {18, "Mission done", "the mission is done", {}, false, "Holds once the game's mission is done."},
    {19, "Health less than", "the hero's health is less than {0}", {A::Number}, false, "Holds when the hero is weaker."},
    {20, "Health more than", "the hero's health is more than {0}", {A::Number}, false, "Holds when the hero is stronger."},
    {21, "Never", "never", {}, false, "Never holds."},
    {22, "Item to find is", "the item to find here is {0}", {A::Item}, false,
     "Holds when the world generator chose this item for the hero to find here."},
    {23, "Use other item on", "the hero uses another item than {4} on {3} at {0},{1} ({2})",
     {A::X, A::Y, A::Layer, A::Tile, A::Item}, false,
     "Holds when the hero uses a different item on this tile: good for \"that does not work\"."},
    {24, "Hero is at", "the hero is at {0},{1}", {A::X, A::Y}, false, "Holds when the hero stands at this place."},
    {25, "Shared counter is", "the shared counter is {0}", {A::Number}, false,
     "A counter all zones share. Holds when it has this value."},
    {26, "Shared less than", "the shared counter is less than {0}", {A::Number}, false, "Holds when it is lower."},
    {27, "Shared more than", "the shared counter is more than {0}", {A::Number}, false, "Holds when it is higher."},
    {28, "Games won is", "the player has won {0} games", {A::Number}, false, "Holds when the player won this many games."},
    {29, "Item spot at", "an item spot is active at {0},{1}", {A::X, A::Y}, false,
     "Holds when an item spot at this place is still active."},
    {30, "Hero has asked item", "the hero carries an item this zone asks for", {}, false,
     "Holds when the hero carries one of the items this zone asks for."},
    {31, "Counter is not", "the zone's counter is not {0}", {A::Number}, false, "Holds when the counter has another value."},
    {32, "Dice is not", "the dice do not show {0}", {A::Number}, false, "Holds when the last roll is another number."},
    {33, "Shared is not", "the shared counter is not {0}", {A::Number}, false, "Holds when it has another value."},
    {34, "Hidden value is", "the hidden value at {1},{2} ({3}) is {0}", {A::Number, A::X, A::Y, A::Layer}, false,
     "Every cell can keep a hidden number. Holds when the one at this place has this value."},
    {35, "Games won more than", "the player has won more than {0} games", {A::Number}, false,
     "Holds when the player won more games."},
    {100, "Dice rolled", "the dice have been rolled", {}, false, "Holds once the dice were rolled in this zone."},
    {101, "Always", "always", {}, false, "Always holds."},
};

const LineKind kInstructions[] = {
    {0, "Place tile", "put {3} at {0},{1} ({2})", {A::X, A::Y, A::Layer, A::Tile}, false,
     "Puts this tile at this place on this layer."},
    {1, "Remove tile", "remove the tile at {0},{1} ({2})", {A::X, A::Y, A::Layer}, false,
     "Clears this place on this layer."},
    {2, "Move tile", "move the tile at {0},{1} ({2}) to {3},{4}", {A::X, A::Y, A::Layer, A::X2, A::Y2}, false,
     "Moves the tile at one place to another place on the same layer."},
    {3, "Draw tile", "draw {2} at {0},{1}", {A::X, A::Y, A::Tile}, false,
     "Draws this tile for a moment without changing the zone."},
    {4, "Hero says", "the hero says \"{t}\"", {}, true, "Shows a speech bubble over the hero."},
    {5, "Someone says", "the one at {0},{1} says \"{t}\"", {A::X, A::Y}, true,
     "Shows a speech bubble over this place: point at the person who talks."},
    {6, "Redraw place", "redraw {0},{1}", {A::X, A::Y}, false, "Shows the changes at this place."},
    {7, "Redraw area", "redraw from {0},{1} to {2},{3}", {A::X, A::Y, A::X2, A::Y2}, false,
     "Shows the changes in this rectangle."},
    {8, "Show changes", "show the changes", {}, false, "Shows all changes so far."},
    {9, "Wait", "wait {0}", {A::Tenths}, false, "Waits a moment."},
    {10, "Play sound", "play {0}", {A::Sound}, false, "Plays a sound."},
    {11, "Stop sound", "stop the sound", {}, false, "Stops the sound that plays."},
    {12, "Roll dice", "roll a dice from 1 to {0}", {A::Number}, false,
     "Picks a random number; conditions about the dice check it."},
    {13, "Set counter", "set the zone's counter to {0}", {A::Number}, false, "Sets the zone's counter."},
    {14, "Add to counter", "add {0} to the zone's counter", {A::Number}, false, "Adds to the zone's counter."},
    {15, "Set hidden value", "set the hidden value at {0},{1} ({2}) to {3}", {A::X, A::Y, A::Layer, A::Number},
     false, "Sets the hidden number of this place."},
    {16, "Hide hero", "hide the hero", {}, false, "Makes the hero invisible."},
    {17, "Show hero", "show the hero", {}, false, "Makes the hero visible again."},
    {18, "Put hero", "put the hero at {0},{1}", {A::X, A::Y}, false, "Moves the hero to this place at once."},
    {19, "Walk hero", "walk the hero from {0},{1} to {2},{3}", {A::X, A::Y, A::X2, A::Y2, A::Tenths}, false,
     "Walks the hero from one place to another (-1 as the start: from where the hero is)."},
    {20, "Switch script off", "switch this script off", {}, false, "This script does not run again."},
    {21, "Hotspot on", "switch {0} on", {A::Hotspot}, false, "Switches a hotspot of this zone on."},
    {22, "Hotspot off", "switch {0} off", {A::Hotspot}, false, "Switches a hotspot of this zone off."},
    {23, "Person on", "bring in {0}", {A::Person}, false, "Lets a person of this zone appear."},
    {24, "Person off", "take away {0}", {A::Person}, false, "Lets a person of this zone disappear."},
    {25, "All persons on", "bring in all persons", {}, false, "Lets every person of this zone appear."},
    {26, "All persons off", "take away all persons", {}, false, "Lets every person of this zone disappear."},
    {27, "Drop item", "drop {0} at {1},{2}", {A::Item, A::X, A::Y}, false,
     "Puts an item on the ground for the hero to pick up."},
    {28, "Give item", "give the hero {0}", {A::Item}, false, "Puts an item in the hero's bag."},
    {29, "Take item", "take {0} from the hero", {A::Item}, false, "Takes an item out of the hero's bag."},
    {30, "Mark solved", "mark this zone as solved", {}, false, "The zone counts as solved, for conditions and the map."},
    {31, "Win", "the player wins the game", {}, false, "Ends the game: won."},
    {32, "Lose", "the player loses the game", {}, false, "Ends the game: lost."},
    {33, "Go to zone", "take the hero to {0} at {1},{2}", {A::Zone, A::X, A::Y}, false,
     "Takes the hero to another zone, at this place there."},
    {34, "Set shared", "set the shared counter to {0}", {A::Number}, false, "Sets the counter all zones share."},
    {35, "Add to shared", "add {0} to the shared counter", {A::Number}, false, "Adds to the counter all zones share."},
    {36, "Set dice", "set the dice to {0}", {A::Number}, false, "Sets the dice to this number."},
    {37, "Add health", "add {0} to the hero's health", {A::Number}, false, "Heals the hero (or hurts, when negative)."},
    {100, "Redraw zone", "redraw the zone", {}, false, "Shows the whole zone again."},
    {101, "Nothing", "do nothing", {}, false, "Does nothing."},
};
// clang-format on

const char* argLabel(Arg a) {
    switch (a) {
    case Arg::X: return "x";
    case Arg::Y: return "y";
    case Arg::X2: return "to x";
    case Arg::Y2: return "to y";
    case Arg::Layer: return "layer";
    case Arg::Tile: return "tile";
    case Arg::Item: return "item";
    case Arg::Zone: return "zone";
    case Arg::Number: return "number";
    case Arg::Person: return "person";
    case Arg::Hotspot: return "hotspot";
    case Arg::Sound: return "sound";
    case Arg::Tenths: return "time";
    default: return "";
    }
}

const char* layerWord(int layer) {
    switch (layer) {
    case 0: return "floor";
    case 1: return "objects";
    case 2: return "roof";
    default: return "layer ?";
    }
}

bool isPosition(Arg a) { return a == Arg::X || a == Arg::Y || a == Arg::X2 || a == Arg::Y2; }

} // namespace

// --- what a line is ------------------------------------------------------------

namespace {

struct Found {
    const LineKind* kind = nullptr;
    bool condition = false;
};

Found findKind(data::Layout layout, bool condition, std::uint16_t opcode) {
    Found f;
    f.condition = condition;
    if (condition) {
        const int id = static_cast<int>(script::condition(layout, opcode));
        for (const auto& k : kConditions) {
            if (k.id == id) {
                f.kind = &k;
            }
        }
    } else {
        const int id = static_cast<int>(script::instruction(layout, opcode));
        for (const auto& k : kInstructions) {
            if (k.id == id) {
                f.kind = &k;
            }
        }
    }
    return f;
}

// The kinds this game has.
std::vector<const LineKind*> kindsOf(data::Layout layout, bool condition) {
    std::vector<const LineKind*> out;
    if (condition) {
        for (const auto& k : kConditions) {
            if (script::opcode(layout, static_cast<Condition>(k.id)) >= 0) {
                out.push_back(&k);
            }
        }
    } else {
        for (const auto& k : kInstructions) {
            if (script::opcode(layout, static_cast<Instruction>(k.id)) >= 0) {
                out.push_back(&k);
            }
        }
    }
    return out;
}

int opcodeOf(data::Layout layout, bool condition, const LineKind& k) {
    return condition ? script::opcode(layout, static_cast<Condition>(k.id))
                     : script::opcode(layout, static_cast<Instruction>(k.id));
}

} // namespace

std::string Editor::lineArgKinds(bool condition, std::uint16_t opcode) const {
    const auto found = findKind(host_.data->layout(), condition, opcode);
    std::string out = "-----";
    if (!found.kind) {
        return out;
    }
    for (std::size_t i = 0; i < found.kind->args.size(); ++i) {
        switch (found.kind->args[i]) {
        case Arg::X:
        case Arg::X2: out[i] = 'x'; break;
        case Arg::Y:
        case Arg::Y2: out[i] = 'y'; break;
        case Arg::Tile:
        case Arg::Item: out[i] = 't'; break;
        case Arg::Zone: out[i] = 'z'; break;
        case Arg::Person: out[i] = 'p'; break;
        case Arg::Hotspot: out[i] = 'h'; break;
        case Arg::Sound: out[i] = 's'; break;
        default: break;
        }
    }
    // Places in another zone are not checked against this one.
    if (std::find(found.kind->args.begin(), found.kind->args.end(), Arg::Zone) != found.kind->args.end()) {
        std::replace(out.begin(), out.end(), 'x', '-');
        std::replace(out.begin(), out.end(), 'y', '-');
    }
    return out;
}

bool Editor::lineHasText(bool condition, std::uint16_t opcode) const {
    const auto found = findKind(host_.data->layout(), condition, opcode);
    return found.kind && found.kind->text;
}

std::string Editor::argText(int kind, int value) const {
    const auto a = static_cast<Arg>(kind);
    const auto& d = *host_.data;
    const auto& z = d.zones[static_cast<std::size_t>(zone_)];
    switch (a) {
    case Arg::Layer: return layerWord(value);
    case Arg::Tile:
    case Arg::Item: {
        if (value < 0) {
            return a == Arg::Item ? "the zone's item" : "no tile";
        }
        const std::string name = tileName(static_cast<std::uint16_t>(value));
        return (a == Arg::Item ? "item " : "tile ") + std::to_string(value) + (name.empty() ? "" : " (" + name + ")");
    }
    case Arg::Zone: return "zone " + std::to_string(value);
    case Arg::Person:
        if (value >= 0 && static_cast<std::size_t>(value) < z.monsters.size()) {
            return "person " + std::to_string(value) + " (" +
                   characterName(z.monsters[static_cast<std::size_t>(value)].character) + ")";
        }
        return "person " + std::to_string(value);
    case Arg::Hotspot:
        if (z.hotspots && value >= 0 && static_cast<std::size_t>(value) < z.hotspots->size()) {
            return "hotspot " + std::to_string(value) + " (" +
                   names::hotspot((*z.hotspots)[static_cast<std::size_t>(value)].type) + ")";
        }
        return "hotspot " + std::to_string(value);
    case Arg::Sound:
        if (value >= 0 && static_cast<std::size_t>(value) < d.sounds.size()) {
            return "sound " + std::to_string(value) + " (" + d.sounds[static_cast<std::size_t>(value)] + ")";
        }
        return "sound " + std::to_string(value);
    case Arg::Tenths: {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.1f s", static_cast<double>(value) / 10.0);
        return buffer;
    }
    default: return std::to_string(value);
    }
}

std::string Editor::lineSentence(bool condition, const data::ScriptLine& line) const {
    const auto layout = host_.data->layout();
    const auto found = findKind(layout, condition, line.opcode);
    if (!found.kind) {
        return "an unknown line (" + std::to_string(line.opcode) + ")";
    }
    std::string out;
    for (const char* p = found.kind->sentence; *p; ++p) {
        if (p[0] == '{' && p[1] == 't' && p[2] == '}') {
            std::string said = text::windows1252ToUtf8(line.text);
            std::replace(said.begin(), said.end(), '\n', ' ');
            out += said;
            p += 2;
        } else if (p[0] == '{' && p[1] >= '0' && p[1] <= '4' && p[2] == '}') {
            const auto i = static_cast<std::size_t>(p[1] - '0');
            out += argText(static_cast<int>(found.kind->args[i]), line.args[i]);
            p += 2;
        } else {
            out += *p;
        }
    }
    return out;
}

// --- the tab ----------------------------------------------------------------------

void Editor::scriptsTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right) {
    if (scriptsZone_ != zone_) {
        scriptsZone_ = zone_;
        selScript_ = -1;
        selLine_ = -1;
        scriptScroll_ = 0;
    }
    zoneList(left);
    scriptCards(center);
    scriptMap(right);
}

void Editor::changeScripts(const std::string& mergeKey) {
    if (!mergeKey.empty() && mergeKey == mergeKey_ && !undo_.empty() && redo_.empty()) {
        dirty_ = true;
        ++changes_;
        return;
    }
    remember(zone_, -1);
    mergeKey_ = mergeKey;
}

void Editor::scriptCards(const SDL_FRect& r) {
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    if (!z.actions) {
        z.actions.emplace();
    }
    auto& scripts = *z.actions;
    const auto layout = host_.data->layout();
    const float x = r.x + kPad;
    const float w = r.w - 2 * kPad;
    float y = r.y + kPad;

    ui::text(renderer_, x, y, "Scripts of zone " + std::to_string(zone_), ui::color::accent);
    if (gui_.button("script-new", {x + w - 110, y - 3, 110, gui::kRow}, templatesOpen_ ? "Cancel" : "New script...",
                    templatesOpen_ ? "Close the list of ready-made scripts."
                                   : "Add a script: empty, or a ready-made one to change.",
                    templatesOpen_)) {
        templatesOpen_ = !templatesOpen_;
    }
    y += 14;
    y += gui_.paragraph(x, y, w,
                        "A script does its Then lines when all of its When lines hold. Click a line to change it; "
                        "places are pointed out on the map on the right.",
                        ui::color::dim, 3) +
         kPad;
    if (templatesOpen_) {
        y += scriptTemplates(x, y, w);
    }

    const SDL_FRect area{r.x, y, r.w - 1, r.y + r.h - y};
    gui_.beginScroll(area, scriptScroll_, scriptHeight_);
    float cy = area.y - scriptScroll_;
    const float top = cy;
    if (scripts.empty()) {
        gui_.paragraph(x, cy + 4, w, "This zone has no scripts. Press New script to make one.", ui::color::faint);
        cy += 30;
    }
    // A change to the list is done after drawing, so that nothing moves
    // under the loop.
    enum class Op { None, Up, Down, Copy, Delete, AddWhen, AddThen, LineUp, LineDown, LineDelete } op = Op::None;
    int opScript = -1;
    for (int si = 0; si < static_cast<int>(scripts.size()); ++si) {
        auto& action = scripts[static_cast<std::size_t>(si)];
        const std::string sid = "s" + std::to_string(si);
        const float cardTop = cy;
        const bool selectedScript = si == selScript_;
        cy += 4;
        ui::text(renderer_, x + 4, cy + 3, "Script " + std::to_string(si),
                 selectedScript ? ui::color::accent : ui::color::text);
        float bx = x + w - 4;
        const auto small = [&](const char* id, const char* label, const char* help, bool enabled = true) {
            const float bw = ui::textWidth(label) + 10;
            bx -= bw;
            const bool pressed = gui_.button(sid + id, {bx, cy, bw, gui::kRow}, label, help, false, enabled);
            bx -= 2;
            return pressed;
        };
        if (small("-delete", "Delete", "Delete this script.")) {
            op = Op::Delete;
            opScript = si;
        }
        if (small("-copy", "Copy", "Add a copy of this script below it.")) {
            op = Op::Copy;
            opScript = si;
        }
        if (small("-down", "Down", "Move this script down. Scripts are checked from the top.",
                  si + 1 < static_cast<int>(scripts.size()))) {
            op = Op::Down;
            opScript = si;
        }
        if (small("-up", "Up", "Move this script up. Scripts are checked from the top.", si > 0)) {
            op = Op::Up;
            opScript = si;
        }
        cy += gui::kRow + 4;

        // The lines: When, then Then.
        for (int part = 0; part < 2; ++part) {
            const bool condition = part == 0;
            auto& lines = condition ? action.conditions : action.instructions;
            ui::text(renderer_, x + 4, cy + 3, condition ? "When" : "Then", ui::color::dim);
            if (condition && lines.empty()) {
                ui::text(renderer_, x + 44, cy + 3, "always (no When lines)", ui::color::faint);
                cy += gui::kRow + 2;
            }
            for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
                auto& line = lines[static_cast<std::size_t>(li)];
                const int index = condition ? li : static_cast<int>(action.conditions.size()) + li;
                const bool selected = selectedScript && selLine_ == index;
                const std::string lid = sid + "-l" + std::to_string(index);
                const std::string sentence = lineSentence(condition, line);
                const SDL_FRect row{x + 40, cy, w - 44, gui::kRow};
                if (gui_.button(lid, row, "", sentence + ". Click to change this line.", selected)) {
                    selScript_ = si;
                    selLine_ = selected ? -1 : index;
                    pickPair_ = 0;
                    mergeKey_.clear();
                }
                const std::string lead = condition ? (li == 0 ? "" : "and ") : "";
                ui::text(renderer_, row.x + 4, row.y + 3, gui::fitText(lead + sentence, row.w - 8),
                         selected ? ui::color::accent : ui::color::text);
                cy += gui::kRow + 2;
                if (selected) {
                    const float used = lineEditor(lid, x + 40, cy, w - 44, condition, line);
                    cy += used;
                    // Moving and deleting the line.
                    const float third = (w - 44 - 4) / 3;
                    if (gui_.button(lid + "-up", {x + 40, cy, third, gui::kRow}, "Line up",
                                    "Move this line up.", false, li > 0)) {
                        op = Op::LineUp;
                        opScript = si;
                    }
                    if (gui_.button(lid + "-down", {x + 42 + third, cy, third, gui::kRow}, "Line down",
                                    "Move this line down.", false, li + 1 < static_cast<int>(lines.size()))) {
                        op = Op::LineDown;
                        opScript = si;
                    }
                    if (gui_.button(lid + "-delete", {x + 44 + 2 * third, cy, third, gui::kRow}, "Delete line",
                                    "Delete this line.")) {
                        op = Op::LineDelete;
                        opScript = si;
                    }
                    cy += gui::kRow + 6;
                }
            }
            if (gui_.button(sid + (condition ? "-addwhen" : "-addthen"), {x + 40, cy, 120, gui::kRow},
                            condition ? "+ When" : "+ Then",
                            condition ? "Add a condition: something that must hold for the script to run."
                                      : "Add something for the script to do.")) {
                op = condition ? Op::AddWhen : Op::AddThen;
                opScript = si;
            }
            cy += gui::kRow + 4;
        }
        cy += 2;
        ui::outline(renderer_, {x, cardTop, w, cy - cardTop}, selectedScript ? ui::color::accent : ui::color::border);
        cy += kPad;
    }
    scriptHeight_ = cy - top;
    gui_.endScroll();

    if (op == Op::None || opScript < 0) {
        return;
    }
    auto& action = scripts[static_cast<std::size_t>(opScript)];
    const auto conditions = static_cast<int>(action.conditions.size());
    const bool lineIsCondition = selLine_ < conditions;
    auto& lines = lineIsCondition ? action.conditions : action.instructions;
    const int li = lineIsCondition ? selLine_ : selLine_ - conditions;
    changeScripts();
    switch (op) {
    case Op::Up:
        std::swap(scripts[static_cast<std::size_t>(opScript)], scripts[static_cast<std::size_t>(opScript - 1)]);
        selScript_ = opScript - 1;
        break;
    case Op::Down:
        std::swap(scripts[static_cast<std::size_t>(opScript)], scripts[static_cast<std::size_t>(opScript + 1)]);
        selScript_ = opScript + 1;
        break;
    case Op::Copy:
        scripts.insert(scripts.begin() + opScript + 1, data::Action(action));
        selScript_ = opScript + 1;
        selLine_ = -1;
        break;
    case Op::Delete:
        scripts.erase(scripts.begin() + opScript);
        selScript_ = -1;
        selLine_ = -1;
        break;
    case Op::AddWhen:
    case Op::AddThen: {
        const bool condition = op == Op::AddWhen;
        data::ScriptLine line;
        const auto kinds = kindsOf(layout, condition);
        // A friendly start: entering the zone, and the hero saying something.
        const int want = condition ? 1 : 4;
        const LineKind* kind = kinds.empty() ? nullptr : kinds.front();
        for (const auto* k : kinds) {
            if (k->id == want) {
                kind = k;
            }
        }
        if (kind) {
            line.opcode = static_cast<std::uint16_t>(opcodeOf(layout, condition, *kind));
        }
        line.args = {static_cast<std::int16_t>(cursorX_), static_cast<std::int16_t>(cursorY_), 0, 0, 0};
        if (kind && kind->text) {
            line.text = "...";
        }
        auto& into = condition ? action.conditions : action.instructions;
        into.push_back(line);
        selScript_ = opScript;
        selLine_ = condition ? static_cast<int>(action.conditions.size()) - 1
                             : static_cast<int>(action.conditions.size() + action.instructions.size()) - 1;
        pickPair_ = 0;
        break;
    }
    case Op::LineUp:
        if (li > 0) {
            std::swap(lines[static_cast<std::size_t>(li)], lines[static_cast<std::size_t>(li - 1)]);
            --selLine_;
        }
        break;
    case Op::LineDown:
        if (li + 1 < static_cast<int>(lines.size())) {
            std::swap(lines[static_cast<std::size_t>(li)], lines[static_cast<std::size_t>(li + 1)]);
            ++selLine_;
        }
        break;
    case Op::LineDelete:
        if (li >= 0 && li < static_cast<int>(lines.size())) {
            lines.erase(lines.begin() + li);
            selLine_ = -1;
        }
        break;
    default: break;
    }
}

float Editor::lineEditor(const std::string& id, float x, float y, float w, bool condition, data::ScriptLine& line) {
    const float top = y;
    const auto layout = host_.data->layout();
    const auto& d = *host_.data;
    const auto& z = d.zones[static_cast<std::size_t>(zone_)];
    const auto kinds = kindsOf(layout, condition);
    const auto found = findKind(layout, condition, line.opcode);

    // What the line is.
    int index = 0;
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        if (kinds[i] == found.kind) {
            index = static_cast<int>(i);
        }
    }
    const int before = index;
    const std::string shown = found.kind ? found.kind->title : "Unknown " + std::to_string(line.opcode);
    gui_.stepper(id + "-kind", {x, y, w, gui::kRow}, condition ? "When" : "Then", index, 0,
                 static_cast<int>(kinds.size()) - 1, shown,
                 std::string(found.kind ? found.kind->help : "A line the editor has no name for.") +
                     " Left and right go through everything a script can " + (condition ? "check." : "do."));
    if (index != before && !kinds.empty()) {
        changeScripts();
        const auto& next = *kinds[static_cast<std::size_t>(index)];
        line.opcode = static_cast<std::uint16_t>(opcodeOf(layout, condition, next));
        if (!next.text) {
            line.text.clear();
        } else if (line.text.empty()) {
            line.text = "...";
        }
        return gui::kRow + 2; // drawn again next frame
    }
    y += gui::kRow + 2;
    if (!found.kind) {
        return y - top;
    }
    y += gui_.paragraph(x, y, w, found.kind->help, ui::color::dim, 3) + 2;

    // The numbers, named.
    const int pairs = static_cast<int>(std::count(found.kind->args.begin(), found.kind->args.end(), Arg::X)) +
                      static_cast<int>(std::count(found.kind->args.begin(), found.kind->args.end(), Arg::X2));
    for (std::size_t i = 0; i < found.kind->args.size(); ++i) {
        const Arg a = found.kind->args[i];
        if (a == Arg::None) {
            continue;
        }
        int value = line.args[i];
        int min = 0;
        int max = 9999;
        switch (a) {
        case Arg::X:
        case Arg::X2: min = -1; max = z.width - 1; break;
        case Arg::Y:
        case Arg::Y2: min = -1; max = z.height - 1; break;
        case Arg::Layer: max = 2; break;
        case Arg::Tile:
        case Arg::Item: min = -1; max = static_cast<int>(d.tiles.size()) - 1; break;
        case Arg::Zone: max = static_cast<int>(d.zones.size()) - 1; break;
        case Arg::Person: max = std::max(0, static_cast<int>(z.monsters.size()) - 1); break;
        case Arg::Hotspot: max = std::max(0, z.hotspots ? static_cast<int>(z.hotspots->size()) - 1 : 0); break;
        case Arg::Sound: max = std::max(0, static_cast<int>(d.sounds.size()) - 1); break;
        case Arg::Number: min = -999; max = 9999; break;
        case Arg::Tenths: max = 600; break;
        default: break;
        }
        const bool tileLike = a == Arg::Tile || a == Arg::Item;
        const bool pointable = (a == Arg::X || a == Arg::X2);
        const float side = tileLike || pointable ? 92.0f : 0.0f;
        const std::string aid = id + "-a" + std::to_string(i);
        std::string help = std::string("The ") + argLabel(a) + ". Left and right change it.";
        if (isPosition(a)) {
            help += " Or click the map on the right.";
        }
        if (gui_.stepper(aid, {x, y, w - side, gui::kRow}, argLabel(a), value, min, max,
                         argText(static_cast<int>(a), value), help)) {
            changeScripts(aid);
            line.args[i] = static_cast<std::int16_t>(value);
        }
        if (tileLike) {
            if (gui_.button(aid + "-hand", {x + w - side + 2, y, side - 2, gui::kRow}, "In hand",
                            "Use the tile in hand (" + std::to_string(tile_) +
                                "). Choose it on the right, under the map.",
                            line.args[i] == tile_)) {
                changeScripts();
                line.args[i] = static_cast<std::int16_t>(tile_);
            }
        }
        if (pointable) {
            const int pair = a == Arg::X ? 0 : 1;
            if (gui_.button(aid + "-point", {x + w - side + 2, y, side - 2, gui::kRow}, "On the map",
                            "Clicking the map sets this place.", pairs > 1 && pickPair_ == pair)) {
                pickPair_ = pair;
            }
        }
        y += gui::kRow + 2;
    }
    if (found.kind->text) {
        std::string said = text::windows1252ToUtf8(line.text);
        if (gui_.textField(id + "-text", {x, y, w, gui::kRow}, said, "What is said. Click and type.", 1000,
                           "What is said")) {
            changeScripts(id + "-text");
            line.text = text::utf8ToWindows1252(said);
        }
        y += gui::kRow + 2;
    }
    return y - top + 2;
}

// --- ready-made scripts ---------------------------------------------------------

float Editor::scriptTemplates(float x, float y, float w) {
    const float top = y;
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    const auto layout = host_.data->layout();
    const auto cx = static_cast<std::int16_t>(cursorX_);
    const auto cy = static_cast<std::int16_t>(cursorY_);
    const auto objectHere = static_cast<std::int16_t>(z.at(cursorX_, cursorY_).layers[1]);
    const auto hand = static_cast<std::int16_t>(tile_);
    using Args = std::array<std::int16_t, 5>;
    struct Line {
        bool condition;
        int id;
        Args args;
        const char* text;
    };
    struct Template {
        const char* title;
        const char* help;
        std::vector<Line> lines;
    };
    const std::string at = std::to_string(cursorX_) + "," + std::to_string(cursorY_);
    const std::vector<Template> templates = {
        {"Empty script", "A script with no lines yet.", {}},
        {"Someone talks",
         "When the hero walks against what stands at the cursor, it says something.",
         {{true, 2, Args{cx, cy, objectHere, 0, 0}, nullptr}, {false, 5, Args{cx, cy, 0, 0, 0}, "Hello there!"}}},
        {"First visit",
         "The first time the hero comes into the zone, the hero says something.",
         {{true, 0, Args{}, nullptr}, {false, 4, Args{}, "What a place!"}}},
        {"Use an item to get another",
         "When the hero uses the tile in hand on what stands at the cursor, it is taken and the zone's item "
         "appears there.",
         {{true, 3, Args{cx, cy, 1, objectHere, hand}, nullptr},
          {false, 29, Args{hand, 0, 0, 0, 0}, nullptr},
          {false, 27, Args{-1, cx, cy, 0, 0}, nullptr},
          {false, 30, Args{}, nullptr}}},
        {"A door that opens with an item",
         "When the hero uses the tile in hand on what stands at the cursor, it is taken and the thing at the "
         "cursor disappears.",
         {{true, 3, Args{cx, cy, 1, objectHere, hand}, nullptr},
          {false, 29, Args{hand, 0, 0, 0, 0}, nullptr},
          {false, 1, Args{cx, cy, 1, 0, 0}, nullptr},
          {false, 4, Args{}, "It opened!"}}},
        {"Only once",
         "Something that happens only the first time: the zone's counter remembers it.",
         {{true, 1, Args{}, nullptr},
          {true, 5, Args{0, 0, 0, 0, 0}, nullptr},
          {false, 4, Args{}, "This happens once."},
          {false, 13, Args{1, 0, 0, 0, 0}, nullptr}}},
    };
    ui::text(renderer_, x, y, "Start from:", ui::color::accent);
    y += 12;
    for (std::size_t t = 0; t < templates.size(); ++t) {
        const auto& tp = templates[t];
        if (gui_.button("template-" + std::to_string(t), {x, y, w, gui::kRow}, tp.title,
                        std::string(tp.help) + (tp.lines.empty() ? "" : " It uses the place " + at +
                                                                        " (the cursor); change it afterwards."))) {
            data::Action action;
            for (const auto& l : tp.lines) {
                const int op = l.condition ? script::opcode(layout, static_cast<Condition>(l.id))
                                           : script::opcode(layout, static_cast<Instruction>(l.id));
                if (op < 0) {
                    continue; // this game has no such line
                }
                data::ScriptLine line;
                line.opcode = static_cast<std::uint16_t>(op);
                line.args = l.args;
                if (l.text) {
                    line.text = text::utf8ToWindows1252(l.text);
                }
                (l.condition ? action.conditions : action.instructions).push_back(std::move(line));
            }
            changeScripts();
            if (!z.actions) {
                z.actions.emplace();
            }
            z.actions->push_back(std::move(action));
            selScript_ = static_cast<int>(z.actions->size()) - 1;
            selLine_ = -1;
            scriptScroll_ = 1e9f;
            templatesOpen_ = false;
        }
        y += gui::kRow + 2;
    }
    return y - top + kPad;
}

// --- the map --------------------------------------------------------------------

void Editor::scriptMap(const SDL_FRect& r) {
    ui::fill(renderer_, r, ui::color::panel);
    ui::fill(renderer_, {r.x, r.y, 1, r.h}, ui::color::border);
    auto& z = host_.data->zones[static_cast<std::size_t>(zone_)];
    const float x = r.x + kPad;
    const float w = r.w - 2 * kPad;
    float y = r.y + kPad;
    ui::text(renderer_, x, y, "Map", ui::color::accent);
    y += 12;
    const float cell = std::floor(w / static_cast<float>(std::max(z.width, z.height)));
    const SDL_FRect map{x, y, cell * F(z.width), cell * F(z.height)};
    for (int layer = 0; layer < 3; ++layer) {
        for (int cy = 0; cy < z.height; ++cy) {
            for (int cx = 0; cx < z.width; ++cx) {
                drawTile(z.at(cx, cy).layers[static_cast<std::size_t>(layer)],
                         {map.x + F(cx) * cell, map.y + F(cy) * cell, cell, cell});
            }
        }
    }
    for (const auto& m : z.monsters) {
        if (m.character < characters_.size()) {
            drawTile(characters_[m.character].frame(0, data::Facing::Down),
                     {map.x + F(m.x) * cell, map.y + F(m.y) * cell, cell, cell});
        }
    }
    ui::outline(renderer_, {map.x - 1, map.y - 1, map.w + 2, map.h + 2}, ui::color::border);

    // The selected line, and what it points at.
    data::ScriptLine* line = nullptr;
    bool condition = false;
    const LineKind* kind = nullptr;
    if (z.actions && selScript_ >= 0 && static_cast<std::size_t>(selScript_) < z.actions->size()) {
        auto& action = (*z.actions)[static_cast<std::size_t>(selScript_)];
        const auto conditions = static_cast<int>(action.conditions.size());
        if (selLine_ >= 0 && selLine_ < conditions) {
            line = &action.conditions[static_cast<std::size_t>(selLine_)];
            condition = true;
        } else if (selLine_ >= conditions &&
                   static_cast<std::size_t>(selLine_ - conditions) < action.instructions.size()) {
            line = &action.instructions[static_cast<std::size_t>(selLine_ - conditions)];
        }
        // Every place the script names, faintly.
        for (int part = 0; part < 2; ++part) {
            for (const auto& l : part == 0 ? action.conditions : action.instructions) {
                const auto f = findKind(host_.data->layout(), part == 0, l.opcode);
                if (!f.kind) {
                    continue;
                }
                for (std::size_t i = 0; i + 1 < f.kind->args.size(); ++i) {
                    if ((f.kind->args[i] == Arg::X || f.kind->args[i] == Arg::X2) && l.args[i] >= 0 &&
                        l.args[i + 1] >= 0) {
                        ui::outline(renderer_,
                                    {map.x + F(l.args[i]) * cell, map.y + F(l.args[i + 1]) * cell, cell, cell},
                                    ui::color::dim);
                    }
                }
            }
        }
    }
    if (line) {
        kind = findKind(host_.data->layout(), condition, line->opcode).kind;
    }
    const auto mark = [&](int cx, int cy, const char* label) {
        if (cx < 0 || cy < 0 || cx >= z.width || cy >= z.height) {
            return;
        }
        const SDL_FRect c{map.x + F(cx) * cell, map.y + F(cy) * cell, cell, cell};
        ui::outline(renderer_, c, ui::color::accent);
        ui::outline(renderer_, {c.x + 1, c.y + 1, c.w - 2, c.h - 2}, ui::color::accent);
        if (label) {
            ui::text(renderer_, c.x + 2, c.y + 2, label, ui::color::accent);
        }
    };
    bool takesPlace = false;
    bool takesHotspot = false;
    bool takesPerson = false;
    bool takesTile = false;
    if (line && kind) {
        for (std::size_t i = 0; i < kind->args.size(); ++i) {
            const Arg a = kind->args[i];
            if ((a == Arg::X || a == Arg::X2) && i + 1 < kind->args.size()) {
                takesPlace = true;
                mark(line->args[i], line->args[i + 1], a == Arg::X ? "1" : "2");
            } else if (a == Arg::Hotspot) {
                takesHotspot = true;
                if (z.hotspots && line->args[i] >= 0 && static_cast<std::size_t>(line->args[i]) < z.hotspots->size()) {
                    const auto& h = (*z.hotspots)[static_cast<std::size_t>(line->args[i])];
                    mark(h.x, h.y, nullptr);
                }
            } else if (a == Arg::Person) {
                takesPerson = true;
                if (line->args[i] >= 0 && static_cast<std::size_t>(line->args[i]) < z.monsters.size()) {
                    const auto& m = z.monsters[static_cast<std::size_t>(line->args[i])];
                    mark(m.x, m.y, nullptr);
                }
            } else if (a == Arg::Tile || a == Arg::Item) {
                takesTile = true;
            }
        }
    }

    // Clicking the map.
    const auto& in = gui_.input();
    std::string help = "The zone. Choose a line on the left: the places it names are marked here.";
    if (takesPlace) {
        help = "Click to set the place of the chosen line (" + std::string(pickPair_ == 0 ? "the first" : "the second") +
               " place).";
    } else if (takesHotspot) {
        help = "Click a hotspot to choose it for the chosen line.";
    } else if (takesPerson) {
        help = "Click a person to choose them for the chosen line.";
    }
    std::optional<std::pair<int, int>> hover;
    if (gui_.hovered(map)) {
        const int hx = static_cast<int>((in.mouseX - map.x) / cell);
        const int hy = static_cast<int>((in.mouseY - map.y) / cell);
        if (hx >= 0 && hy >= 0 && hx < z.width && hy < z.height) {
            hover = std::make_pair(hx, hy);
            ui::outline(renderer_, {map.x + F(hx) * cell, map.y + F(hy) * cell, cell, cell}, ui::color::text);
            help = "Cell " + std::to_string(hx) + "," + std::to_string(hy) + ". " + help;
        }
    }
    if (gui_.area("script-map", map, help) && hover && line && kind) {
        const auto [hx, hy] = *hover;
        // The place to set: the chosen pair, else the first.
        auto pairAt = std::find(kind->args.begin(), kind->args.end(), pickPair_ == 0 ? Arg::X : Arg::X2);
        if (pairAt == kind->args.end()) {
            pairAt = std::find(kind->args.begin(), kind->args.end(), Arg::X);
        }
        if (pairAt != kind->args.end() && pairAt + 1 != kind->args.end()) {
            const auto i = static_cast<std::size_t>(pairAt - kind->args.begin());
            changeScripts();
            line->args[i] = static_cast<std::int16_t>(hx);
            line->args[i + 1] = static_cast<std::int16_t>(hy);
        }
        for (std::size_t i = 0; i < kind->args.size(); ++i) {
            const Arg a = kind->args[i];
            if (a == Arg::Hotspot && z.hotspots) {
                for (std::size_t h = 0; h < z.hotspots->size(); ++h) {
                    if ((*z.hotspots)[h].x == hx && (*z.hotspots)[h].y == hy) {
                        changeScripts();
                        line->args[i] = static_cast<std::int16_t>(h);
                    }
                }
            }
            if (a == Arg::Person) {
                for (std::size_t m = 0; m < z.monsters.size(); ++m) {
                    if (z.monsters[m].x == hx && z.monsters[m].y == hy) {
                        changeScripts();
                        line->args[i] = static_cast<std::int16_t>(m);
                    }
                }
            }
        }
        // A place on the map also moves the zone's cursor there.
        cursorX_ = hx;
        cursorY_ = hy;
    }
    y += map.h + kPad;

    // Under the map: the tile in hand, for lines that name a tile.
    if (takesTile || !line) {
        ui::text(renderer_, x, y, "Tile in hand", ui::color::accent);
        drawTile(tile_, {x + w - 16, y - 2, 16, 16});
        y += 16;
        if (const auto picked = tileBrowser({r.x, y, r.w, r.y + r.h - y}, "script-tiles", 24)) {
            tile_ = *picked;
        }
    } else if (line && kind) {
        gui_.paragraph(x, y, w, std::string(kind->title) + ": " + kind->help, ui::color::dim, 6);
    }
}

} // namespace das::app
