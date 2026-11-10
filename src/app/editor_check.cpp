// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's Check tab: finds what would go wrong in the game, such as a
// door to nowhere or a script about a tile that does not exist, and zones
// the world generator cannot use. Each problem has a button that shows it.
#include "editor.hpp"

#include "das/data/tile_flags.hpp"
#include "das/script/opcodes.hpp"

#include <algorithm>
#include <functional>

namespace das::app {

namespace {

constexpr float kPad = 6;
constexpr std::uint16_t kNoArgument = 0xffff;
namespace flags = data::tile_flags;

} // namespace

void Editor::runCheck() {
    problems_.clear();
    const auto& d = *host_.data;
    const auto layout = d.layout();
    const int tileCount = static_cast<int>(d.tiles.size());
    const int zoneCount = static_cast<int>(d.zones.size());
    const auto flagsOf = [&](int tile) {
        return tile >= 0 && tile < tileCount ? d.tiles[static_cast<std::size_t>(tile)].flags : 0u;
    };
    const auto add = [&](bool mistake, Tab tab, int zone, std::string text) -> Problem& {
        Problem p;
        p.mistake = mistake;
        p.tab = tab;
        p.zone = zone;
        p.text = std::move(text);
        problems_.push_back(std::move(p));
        return problems_.back();
    };

    // The lists of a zone together with the rooms behind its doors.
    const std::function<bool(int, const std::function<bool(const data::Zone&)>&, int)> anyRoom =
        [&](int z, const std::function<bool(const data::Zone&)>& test, int depth) {
            if (z < 0 || z >= zoneCount || depth > 8) {
                return false;
            }
            const auto& zz = d.zones[static_cast<std::size_t>(z)];
            if (test(zz)) {
                return true;
            }
            if (zz.hotspots) {
                for (const auto& h : *zz.hotspots) {
                    if (h.type == 9 && h.argument != kNoArgument && anyRoom(h.argument, test, depth + 1)) {
                        return true;
                    }
                }
            }
            return false;
        };
    const auto spotOf = [&](std::uint32_t type) {
        return [type](const data::Zone& zz) {
            return zz.hotspots && std::any_of(zz.hotspots->begin(), zz.hotspots->end(),
                                              [type](const data::Hotspot& h) { return h.type == type; });
        };
    };

    for (int zi = 0; zi < zoneCount; ++zi) {
        const auto& z = d.zones[static_cast<std::size_t>(zi)];
        const std::string where = "Zone " + std::to_string(zi) + ": ";
        // Hotspots.
        if (z.hotspots) {
            for (const auto& h : *z.hotspots) {
                const std::string spot = names::hotspot(h.type) + " at " + std::to_string(h.x) + "," +
                                         std::to_string(h.y);
                if (h.x >= z.width || h.y >= z.height) {
                    auto& p = add(true, Tab::Zones, zi, where + spot + " lies outside the zone.");
                    p.layer = 3;
                    continue;
                }
                const auto at = [&](Problem& p) {
                    p.x = h.x;
                    p.y = h.y;
                    p.layer = 3;
                };
                if ((h.type == 9 || h.type == 14 || h.type == 15) &&
                    (h.argument == kNoArgument || h.argument >= zoneCount)) {
                    at(add(h.type == 9, Tab::Zones, zi, where + spot + " leads to no zone. Set its argument."));
                }
                if ((h.type == 6 || h.type == 8) && h.argument != kNoArgument) {
                    const auto f = flagsOf(h.argument);
                    if (h.argument >= tileCount) {
                        at(add(true, Tab::Zones, zi, where + spot + " holds a tile that does not exist."));
                    } else if ((f & (flags::kItem | flags::kWeapon)) == 0) {
                        at(add(false, Tab::Zones, zi,
                               where + spot + " holds tile " + std::to_string(h.argument) +
                                   ", which is not marked as an item or weapon."));
                    }
                }
            }
        }
        // People.
        for (std::size_t m = 0; m < z.monsters.size(); ++m) {
            const auto& who = z.monsters[m];
            Problem* p = nullptr;
            if (who.character >= characters_.size()) {
                p = &add(true, Tab::Zones, zi,
                         where + "the person at " + std::to_string(who.x) + "," + std::to_string(who.y) +
                             " is a character that does not exist.");
            } else if (characters_[who.character].type == 1 || characters_[who.character].type == 4) {
                p = &add(false, Tab::Zones, zi,
                         where + characterName(who.character) + " at " + std::to_string(who.x) + "," +
                             std::to_string(who.y) + " is " + names::characterKind(characters_[who.character].type) +
                             ", not a person to place.");
            }
            if (p) {
                p->x = who.x;
                p->y = who.y;
                p->layer = 4;
            }
        }
        // What the generator needs.
        const auto needs = [&](const std::string& text) {
            auto& p = add(false, Tab::Zones, zi, where + names::zoneType(z.type) + " zones " + text);
            p.story = true;
        };
        const auto hasList = [&](auto member) {
            return anyRoom(zi, [member](const data::Zone& zz) { return !(zz.*member).empty(); }, 0);
        };
        switch (z.type) {
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
            if (!hasList(&data::Zone::requiredItems)) {
                needs("need items to ask for (Story: Asks for); the world generator cannot use this one.");
            }
            break;
        case 15:
            if (!hasList(&data::Zone::requiredItems)) {
                needs("need items to ask for (Story: Asks for).");
            }
            if (!anyRoom(zi, spotOf(12), 0)) {
                needs("need a Lock hotspot where the item is used.");
            }
            break;
        case 16:
            if (!hasList(&data::Zone::npcs)) {
                needs("need people (Story: People) to trade with.");
            }
            if (!anyRoom(zi, spotOf(1), 0)) {
                needs("need a Person spot hotspot where the trader stands.");
            }
            break;
        case 17:
        case 18:
            if (!hasList(&data::Zone::providedItems)) {
                needs("need items to give (Story: Gives).");
            }
            if (!anyRoom(zi, spotOf(2), 0) && (z.type == 18 || (!anyRoom(zi, spotOf(0), 0) && !anyRoom(zi, spotOf(5), 0)))) {
                needs(std::string("need ") + (z.type == 17 ? "an Item spot" : "a Weapon spot") +
                      " hotspot where the item lies.");
            }
            break;
        default: break;
        }
        for (const auto* list : {&z.requiredItems, &z.goalItems, &z.providedItems}) {
            for (const auto item : *list) {
                if ((flagsOf(item) & (flags::kItem | flags::kWeapon)) == 0) {
                    auto& p = add(false, Tab::Zones, zi,
                                  where + "tile " + std::to_string(item) +
                                      " is on a list of items, but it is not marked as an item.");
                    p.story = true;
                }
            }
        }

        // Scripts.
        if (!z.actions) {
            continue;
        }
        for (std::size_t si = 0; si < z.actions->size(); ++si) {
            const auto& action = (*z.actions)[si];
            int index = 0;
            for (int part = 0; part < 2; ++part) {
                const bool condition = part == 0;
                for (const auto& line : condition ? action.conditions : action.instructions) {
                    const int lineIndex = index++;
                    const auto report = [&](bool mistake, const std::string& text) {
                        auto& p = add(mistake, Tab::Scripts, zi,
                                      where + "script " + std::to_string(si) + ", \"" + lineSentence(condition, line) +
                                          "\": " + text);
                        p.script = static_cast<int>(si);
                        p.line = lineIndex;
                    };
                    const int known = condition ? static_cast<int>(script::condition(layout, line.opcode))
                                                : static_cast<int>(script::instruction(layout, line.opcode));
                    if (known == 0xffff) {
                        report(true, "the game does not know this line.");
                        continue;
                    }
                    for (const auto& [kind, value] : lineArgs(condition, line)) {
                        switch (kind) {
                        case 'x':
                            if (value >= z.width) {
                                report(true, "a place lies outside the zone.");
                            }
                            break;
                        case 'y':
                            if (value >= z.height) {
                                report(true, "a place lies outside the zone.");
                            }
                            break;
                        case 't':
                            if (value >= tileCount) {
                                report(true, "the tile does not exist.");
                            }
                            break;
                        case 'z':
                            if (value < 0 || value >= zoneCount) {
                                report(true, "the zone does not exist.");
                            }
                            break;
                        case 'p':
                            if (value < 0 || static_cast<std::size_t>(value) >= z.monsters.size()) {
                                report(false, "the zone has no such person, so the line does nothing.");
                            }
                            break;
                        case 'h':
                            if (value < 0 || !z.hotspots || static_cast<std::size_t>(value) >= z.hotspots->size()) {
                                report(false, "the zone has no such hotspot, so the line does nothing.");
                            }
                            break;
                        case 's':
                            if (value < 0 || static_cast<std::size_t>(value) >= d.sounds.size()) {
                                report(false, "the sound does not exist, so nothing plays.");
                            }
                            break;
                        case '"':
                            if (value == 0) {
                                report(false, "nothing is said.");
                            }
                            break;
                        default: break;
                        }
                    }
                }
            }
            if (action.instructions.empty()) {
                auto& p = add(false, Tab::Scripts, zi, where + "script " + std::to_string(si) + " does nothing.");
                p.script = static_cast<int>(si);
            }
        }
    }

    // Characters.
    int heroes = 0;
    for (std::size_t ci = 0; ci < characters_.size(); ++ci) {
        const auto& c = characters_[ci];
        heroes += c.type == 1 ? 1 : 0;
        for (const auto& set : c.frames) {
            for (const auto tile : set) {
                if (tile != data::kNoTile && tile >= tileCount) {
                    auto& p = add(true, Tab::Characters, -1,
                                  characterName(static_cast<int>(ci)) + ": a picture is a tile that does not exist.");
                    p.index = static_cast<int>(ci);
                    break;
                }
            }
        }
        if (c.type != 4 && c.frame(0, data::Facing::Down) == data::kNoTile) {
            auto& p = add(false, Tab::Characters, -1,
                          characterName(static_cast<int>(ci)) + " has no picture standing and facing down.");
            p.index = static_cast<int>(ci);
        }
    }
    if (heroes == 0 && !characters_.empty()) {
        add(true, Tab::Characters, -1, "No character is the hero: the game has no one to walk with.");
    }

    // Puzzles.
    if (puzzles_.size() != d.puzzles.size()) {
        readPuzzles();
    }
    for (std::size_t pi = 0; pi < puzzles_.size(); ++pi) {
        const auto& p = puzzles_[pi];
        if (p.type == 0xffffffff) {
            continue;
        }
        const std::string where = "Puzzle " + std::to_string(pi) + ": ";
        if (p.item1 == data::kNoTile) {
            add(false, Tab::Puzzles, -1, where + "it has no item.").index = static_cast<int>(pi);
        } else if (p.item1 >= tileCount) {
            add(true, Tab::Puzzles, -1, where + "its item is a tile that does not exist.").index = static_cast<int>(pi);
        } else if ((flagsOf(p.item1) & (flags::kItem | flags::kWeapon)) == 0) {
            add(false, Tab::Puzzles, -1, where + "its item is not marked as an item.").index = static_cast<int>(pi);
        }
    }
}

// The named numbers of a line, for the check: x, y, tile, zone, person,
// hotspot, sound, and '"' with 1 when there is text.
std::vector<std::pair<char, int>> Editor::lineArgs(bool condition, const data::ScriptLine& line) const {
    std::vector<std::pair<char, int>> out;
    const std::string kinds = lineArgKinds(condition, line.opcode);
    for (std::size_t i = 0; i < kinds.size() && i < line.args.size(); ++i) {
        if (kinds[i] != '-') {
            out.emplace_back(kinds[i], line.args[i]);
        }
    }
    if (lineHasText(condition, line.opcode)) {
        out.emplace_back('"', line.text.empty() ? 0 : 1);
    }
    return out;
}

void Editor::checkTab(const SDL_FRect& r) {
    if (checkedStamp_ != changes_) {
        checkedStamp_ = changes_;
        runCheck();
    }
    const float x = r.x + kPad * 2;
    const float w = std::min(r.w - 4 * kPad, 900.0f);
    float y = r.y + kPad;
    const auto mistakes = std::count_if(problems_.begin(), problems_.end(), [](const Problem& p) { return p.mistake; });
    ui::text(renderer_, x, y, "Check", ui::color::accent);
    y += 14;
    y += gui_.paragraph(x, y, w,
                        problems_.empty()
                            ? "Nothing found. The game can use everything as it is."
                            : std::to_string(mistakes) + (mistakes == 1 ? " mistake" : " mistakes") +
                                  " (these go wrong in the game) and " +
                                  std::to_string(problems_.size() - static_cast<std::size_t>(mistakes)) +
                                  " tips (these may be what you want, but are worth a look). Show takes you there.",
                        problems_.empty() ? ui::color::good : ui::color::text, 3) +
         kPad;
    const SDL_FRect list{r.x, y, r.w, r.y + r.h - y};
    const float rowH = 30;
    gui_.beginScroll(list, checkScroll_, static_cast<float>(problems_.size()) * rowH);
    float ry = list.y - checkScroll_;
    std::optional<std::size_t> show;
    for (std::size_t i = 0; i < problems_.size(); ++i) {
        const auto& p = problems_[i];
        const SDL_FRect row{x, ry, w, rowH - 4};
        ry += rowH;
        if (!gui_.visible(row)) {
            continue;
        }
        ui::fill(renderer_, row, ui::color::panel);
        ui::text(renderer_, row.x + 6, row.y + 4, p.mistake ? "Mistake" : "Tip",
                 p.mistake ? ui::color::danger : ui::color::accent);
        gui_.paragraph(row.x + 70, row.y + 4, row.w - 70 - 70, p.text, ui::color::text, 2);
        if (gui_.button("problem-" + std::to_string(i), {row.x + row.w - 64, row.y + 5, 60, gui::kRow}, "Show",
                        "Go to it: " + p.text)) {
            show = i;
        }
    }
    gui_.endScroll();
    if (!show) {
        return;
    }
    const auto p = problems_[*show];
    tab_ = p.tab;
    if (p.zone >= 0) {
        zone_ = p.zone;
    }
    switch (p.tab) {
    case Tab::Zones:
        storyView_ = p.story;
        if (p.layer >= 0) {
            layer_ = p.layer;
            shown_[static_cast<std::size_t>(p.layer)] = true;
        }
        if (p.x >= 0) {
            cursorX_ = p.x;
            cursorY_ = p.y;
            gui_.setFocus("canvas");
        }
        break;
    case Tab::Scripts:
        scriptsZone_ = zone_;
        selScript_ = p.script;
        selLine_ = p.line;
        scriptScroll_ = 0;
        break;
    case Tab::Characters: character_ = std::max(0, p.index); break;
    case Tab::Puzzles: puzzle_ = std::max(0, p.index); break;
    default: break;
    }
}

} // namespace das::app
