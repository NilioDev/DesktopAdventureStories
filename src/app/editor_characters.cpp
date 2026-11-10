// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's Characters tab: the hero, monsters, people and weapons. Their
// names, how they move and fight, and the tiles they are drawn with.
#include "editor.hpp"

#include "das/util/text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace das::app {

namespace {

constexpr float kPad = 6;
constexpr std::uint16_t kNone = 0xffff;

float F(int v) { return static_cast<float>(v); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

constexpr std::uint16_t kKinds[] = {1, 2, 4};

const char* kFacings[] = {"Up", "Down", "Up-left", "Left", "Down-left", "Up-right", "Right", "Down-right"};
const char* kFacingsShort[] = {"Up", "Down", "UpL", "Left", "DnL", "UpR", "Right", "DnR"};
const char* kSets[] = {"Standing", "Walking 1", "Walking 2"};

} // namespace

void Editor::charactersTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right) {
    characterList(left);
    characterInspector(center);
    ui::fill(renderer_, right, ui::color::panel);
    ui::fill(renderer_, {right.x, right.y, 1, right.h}, ui::color::border);
    const float x = right.x + kPad;
    float y = right.y + kPad;
    ui::text(renderer_, x, y, "Tile in hand", ui::color::accent);
    drawTile(tile_, {right.x + right.w - kPad - 16, y - 2, 16, 16});
    y += 16;
    if (const auto picked = tileBrowser({right.x, y, right.w, right.y + right.h - y}, "character-tiles", 28)) {
        tile_ = *picked;
    }
}

void Editor::characterList(const SDL_FRect& r) {
    ui::fill(renderer_, r, ui::color::panel);
    ui::fill(renderer_, {r.x + r.w - 1, r.y, 1, r.h}, ui::color::border);
    const float x = r.x + kPad;
    const float w = r.w - 2 * kPad;
    float y = r.y + kPad;
    ui::text(renderer_, x, y, "Characters", ui::color::accent);
    y += 12;
    gui_.textField("character-search", {x, y, w, gui::kRow}, characterSearch_,
                   "Find characters by number, name, or kind (hero, monster, weapon).", 40, "Search...");
    y += gui::kRow + kPad;

    const std::string query = lower(characterSearch_);
    std::vector<int> shown;
    for (int i = 0; i < static_cast<int>(characters_.size()); ++i) {
        const auto& c = characters_[static_cast<std::size_t>(i)];
        if (!query.empty()) {
            const std::string text =
                std::to_string(i) + " " + lower(c.name) + " " + lower(names::characterKind(c.type));
            if (text.find(query) == std::string::npos) {
                continue;
            }
        }
        shown.push_back(i);
    }
    const float buttonH = gui::kRow + kPad;
    const SDL_FRect list{r.x, y, r.w - 1, r.y + r.h - y - buttonH - kPad};
    const float rowH = 20;
    gui_.beginScroll(list, characterScroll_, static_cast<float>(shown.size()) * rowH);
    float ry = list.y - characterScroll_;
    for (const int i : shown) {
        const SDL_FRect row{x, ry, w - 4, rowH - 2};
        ry += rowH;
        if (!gui_.visible(row)) {
            continue;
        }
        const auto& c = characters_[static_cast<std::size_t>(i)];
        if (gui_.button("character-" + std::to_string(i), row, "",
                        characterName(i) + ": " + names::characterKind(c.type) + ". Click to change it.",
                        i == character_)) {
            character_ = i;
        }
        drawTile(c.frame(0, data::Facing::Down), {row.x + 2, row.y + 1, 16, 16});
        ui::text(renderer_, row.x + 22, row.y + 5,
                 gui::fitText(std::to_string(i) + " " + characterName(i), row.w - 26),
                 i == character_ ? ui::color::accent : ui::color::text);
    }
    gui_.endScroll();
    if (gui_.button("character-copy", {x, r.y + r.h - kPad - gui::kRow, w, gui::kRow}, "Add a copy",
                    "Add a new character at the end, a copy of the chosen one.", false,
                    character_ >= 0 && static_cast<std::size_t>(character_) < characters_.size())) {
        rememberStory();
        auto& d = *host_.data;
        data::TaggedEntry entry = d.characters[static_cast<std::size_t>(character_)];
        const int from = character_;
        entry.id = static_cast<std::uint16_t>(d.characters.size());
        d.characters.push_back(entry);
        characters_.push_back(characters_[static_cast<std::size_t>(from)]);
        const int to = static_cast<int>(characters_.size()) - 1;
        // Its fighting values too.
        for (auto* values : {&d.characterWeapons, &d.characterAux}) {
            for (std::size_t v = 0; v < values->size(); ++v) {
                if ((*values)[v].character == from) {
                    auto copy = (*values)[v];
                    copy.character = static_cast<std::uint16_t>(to);
                    values->push_back(copy);
                    break;
                }
            }
        }
        character_ = to;
        characterScroll_ = 1e9f;
        notice_ = "Character " + std::to_string(to) + " added.";
        noticeAt_ = SDL_GetTicks();
    }
}

void Editor::storeCharacter(int i) {
    auto& entry = host_.data->characters[static_cast<std::size_t>(i)];
    entry.body = data::writeCharacter(characters_[static_cast<std::size_t>(i)], host_.data->layout(), entry.body);
}

data::CharacterValues& Editor::characterValues(std::vector<data::CharacterValues>& list, int character) {
    for (auto& v : list) {
        if (v.character == character) {
            return v;
        }
    }
    data::CharacterValues v;
    v.character = static_cast<std::uint16_t>(character);
    v.reference = kNone;
    v.value = kNone;
    list.push_back(v);
    return list.back();
}

void Editor::characterInspector(const SDL_FRect& r) {
    const float x = r.x + kPad * 2;
    const float w = std::min(r.w - 4 * kPad, 520.0f);
    float y = r.y + kPad;
    if (character_ < 0 || static_cast<std::size_t>(character_) >= characters_.size()) {
        gui_.paragraph(x, y, w, "Choose a character on the left.", ui::color::faint);
        return;
    }
    const int ci = character_;
    auto& c = characters_[static_cast<std::size_t>(ci)];
    auto& d = *host_.data;
    ui::text(renderer_, x, y, "Character " + std::to_string(ci), ui::color::accent);
    y += 14;

    // Name.
    std::string name = text::windows1252ToUtf8(c.name);
    if (gui_.textField("character-name", {x, y, w, gui::kRow}, name,
                       "The character's name. Only the editor shows it; at most 15 letters.", 15, "No name")) {
        if (mergeKey_ != "character-name") {
            rememberStory();
            mergeKey_ = "character-name";
        }
        c.name = text::utf8ToWindows1252(name);
        storeCharacter(ci);
    }
    y += gui::kRow + 4;

    // Kind and movement.
    int kindIndex = -1;
    for (std::size_t k = 0; k < std::size(kKinds); ++k) {
        if (kKinds[k] == c.type) {
            kindIndex = static_cast<int>(k);
        }
    }
    int kindPick = std::max(0, kindIndex);
    const int kindBefore = kindPick;
    gui_.stepper("character-kind", {x, y, w, gui::kRow}, "Kind", kindPick, 0, static_cast<int>(std::size(kKinds)) - 1,
                 names::characterKind(c.type),
                 "Hero: the one the player walks with (one per game). Monster or person: stands in zones, may "
                 "fight. Weapon: what the hero fights with.");
    if (kindPick != kindBefore || (kindIndex < 0 && kindPick != 0)) {
        rememberStory();
        c.type = kKinds[static_cast<std::size_t>(kindPick)];
        storeCharacter(ci);
    }
    y += gui::kRow + 2;
    if (c.type != 4) {
        int movement = c.movement;
        if (gui_.stepper("character-movement", {x, y, w, gui::kRow}, "Moves", movement, 0, 12,
                         names::movement(c.movement), "How the character moves: it " + names::movement(c.movement) + ".")) {
            rememberStory();
            c.movement = static_cast<std::uint16_t>(movement);
            storeCharacter(ci);
        }
        y += gui::kRow + 2;
    }

    // Fighting.
    const auto valueOf = [](std::uint16_t v) { return v == kNone ? 0 : static_cast<int>(v); };
    if (c.type == 2 || c.type == 1) {
        auto* values = [&]() -> data::CharacterValues* {
            for (auto& v : d.characterWeapons) {
                if (v.character == ci) {
                    return &v;
                }
            }
            return nullptr;
        }();
        int health = values ? valueOf(values->value) : 0;
        if (gui_.stepper("character-health", {x, y, w, gui::kRow}, "Health", health, 0, 999, std::to_string(health),
                         "How many hits of damage it takes before it is defeated.", 5)) {
            rememberStory();
            characterValues(d.characterWeapons, ci).value = static_cast<std::uint16_t>(health);
        }
        y += gui::kRow + 2;
        // The weapon: one of the weapon characters, or none.
        std::vector<int> weapons{-1};
        for (std::size_t k = 0; k < characters_.size(); ++k) {
            if (characters_[k].type == 4) {
                weapons.push_back(static_cast<int>(k));
            }
        }
        const int current = values && values->reference != kNone ? values->reference : -1;
        auto at = std::find(weapons.begin(), weapons.end(), current);
        int weaponIndex = at == weapons.end() ? 0 : static_cast<int>(at - weapons.begin());
        const int weaponBefore = weaponIndex;
        gui_.stepper("character-weapon", {x, y, w, gui::kRow}, "Weapon", weaponIndex, 0,
                     static_cast<int>(weapons.size()) - 1, current < 0 ? "None" : characterName(current),
                     "The weapon it fights with: how hard it hits and whether it shoots.");
        if (weaponIndex != weaponBefore) {
            rememberStory();
            const int pick = weapons[static_cast<std::size_t>(weaponIndex)];
            characterValues(d.characterWeapons, ci).reference = pick < 0 ? kNone : static_cast<std::uint16_t>(pick);
        }
        y += gui::kRow + 2;
    } else if (c.type == 4) {
        int damage = 0;
        for (const auto& v : d.characterAux) {
            if (v.character == ci) {
                damage = valueOf(v.value);
            }
        }
        if (gui_.stepper("character-damage", {x, y, w, gui::kRow}, "Damage", damage, 0, 999, std::to_string(damage),
                         "How hard the weapon hits.", 1)) {
            rememberStory();
            characterValues(d.characterAux, ci).value = static_cast<std::uint16_t>(damage);
        }
        y += gui::kRow + 2;
        int sound = -1;
        for (const auto& v : d.characterWeapons) {
            if (v.character == ci && v.reference != kNone) {
                sound = v.reference;
            }
        }
        const int maxSound = static_cast<int>(d.sounds.size()) - 1;
        if (gui_.stepper("character-sound", {x, y, w, gui::kRow}, "Sound", sound, -1, maxSound,
                         sound < 0 ? "None"
                                   : (static_cast<std::size_t>(sound) < d.sounds.size()
                                          ? d.sounds[static_cast<std::size_t>(sound)]
                                          : std::to_string(sound)),
                         "The sound the weapon makes.")) {
            rememberStory();
            characterValues(d.characterWeapons, ci).reference = sound < 0 ? kNone : static_cast<std::uint16_t>(sound);
        }
        y += gui::kRow + 2;
    }
    y += kPad;

    // The pictures.
    ui::text(renderer_, x, y, "Pictures", ui::color::accent);
    y += 12;
    y += gui_.paragraph(x, y, w,
                        c.type == 4 ? "A weapon's pictures show it in the hero's hands; the last one of the first "
                                      "row is the item the hero carries."
                                    : "One picture for each direction, standing and in two steps of walking. Click "
                                      "one to give it the tile in hand; right-click takes its tile in hand.",
                        ui::color::dim, 3) +
         4;
    const float labelW = 70;
    const float cell = std::floor(std::min(40.0f, (w - labelW) / 8 - 2));
    for (int f = 0; f < data::kFacingCount; ++f) {
        ui::textCentered(renderer_, x + labelW + F(f) * (cell + 2) + cell / 2, y, kFacingsShort[f],
                         ui::color::faint);
    }
    y += 11;
    for (int set = 0; set < data::kFrameSets; ++set) {
        ui::text(renderer_, x, y + cell / 2 - 4, kSets[set], ui::color::dim);
        for (int f = 0; f < data::kFacingCount; ++f) {
            const SDL_FRect slot{x + labelW + F(f) * (cell + 2), y, cell, cell};
            ui::fill(renderer_, slot, ui::color::background);
            auto& frame = c.frames[static_cast<std::size_t>(set)][static_cast<std::size_t>(f)];
            drawTile(frame, slot);
            const std::string id = "frame-" + std::to_string(set) + "-" + std::to_string(f);
            const std::string tileText =
                frame == data::kNoTile ? "no tile" : "tile " + std::to_string(frame);
            if (gui_.area(id, slot,
                          std::string(kSets[set]) + ", facing " + kFacings[f] + ": " + tileText +
                              ". Click to give it the tile in hand; right-click takes its tile.")) {
                if (frame != tile_) {
                    rememberStory();
                    frame = tile_;
                    storeCharacter(ci);
                }
            }
            if (gui_.hovered(slot)) {
                ui::outline(renderer_, slot, ui::color::text);
                if (gui_.input().rightPressed && frame != data::kNoTile) {
                    tile_ = frame;
                }
            }
        }
        y += cell + 2;
    }
    y += kPad;

    // Where it stands.
    int zonesWith = 0;
    for (const auto& z : d.zones) {
        if (std::any_of(z.monsters.begin(), z.monsters.end(),
                        [&](const data::ZoneMonster& m) { return m.character == ci; })) {
            ++zonesWith;
        }
    }
    ui::text(renderer_, x, y,
             zonesWith == 0 ? "Not placed in any zone"
                            : "Placed in " + std::to_string(zonesWith) + (zonesWith == 1 ? " zone" : " zones"),
             ui::color::dim);
}

} // namespace das::app
