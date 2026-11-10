// SPDX-License-Identifier: GPL-3.0-or-later
//
// The editor's Puzzles tab. A puzzle is an item with what people say about
// it: asking for it, thanking for it, offering it. The world generator
// chains puzzles into a game (docs/world-generator.md).
#include "editor.hpp"

#include "das/data/puzzle.hpp"
#include "das/util/text.hpp"

#include <algorithm>
#include <cctype>

namespace das::app {

namespace {

constexpr float kPad = 6;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

constexpr std::uint32_t kKinds[] = {0, 1, 2, 3, 0xffffffff};

std::string kindName(std::uint32_t type) {
    switch (type) {
    case 0: return "Trade";
    case 1: return "Offer";
    case 2: return "Final";
    case 3: return "Goal";
    case 0xffffffff: return "Switched off";
    default: return "Kind " + std::to_string(type);
    }
}

std::string kindHelp(std::uint32_t type) {
    switch (type) {
    case 0: return "Trade: an item that people can ask for and hand out on the way.";
    case 1: return "Offer: an item that is handed out, with other words for offering it.";
    case 2: return "Final: hands out an item that the goal zone asks for, near the end of a game.";
    case 3: return "Goal: the mission of a whole game, with its briefing.";
    case 0xffffffff: return "Switched off: the world generator never uses this puzzle.";
    default: return "A kind the games do not use.";
    }
}

struct TextSlot {
    data::PuzzleText which;
    const char* label;
    const char* help;
};

} // namespace

void Editor::readPuzzles() {
    puzzles_.clear();
    for (const auto& entry : host_.data->puzzles) {
        try {
            puzzles_.push_back(data::parsePuzzle(entry.body, host_.data->layout()));
        } catch (const std::exception&) {
            data::Puzzle broken;
            broken.type = 0xffffffff;
            puzzles_.push_back(broken);
        }
    }
    puzzle_ = std::clamp(puzzle_, 0, std::max(0, static_cast<int>(puzzles_.size()) - 1));
}

void Editor::storePuzzle(int i) {
    host_.data->puzzles[static_cast<std::size_t>(i)].body =
        data::writePuzzle(puzzles_[static_cast<std::size_t>(i)], host_.data->layout());
}

void Editor::puzzlesTab(const SDL_FRect& left, const SDL_FRect& center, const SDL_FRect& right) {
    if (puzzles_.size() != host_.data->puzzles.size()) {
        readPuzzles();
    }
    // The list.
    {
        const SDL_FRect& r = left;
        ui::fill(renderer_, r, ui::color::panel);
        ui::fill(renderer_, {r.x + r.w - 1, r.y, 1, r.h}, ui::color::border);
        const float x = r.x + kPad;
        const float w = r.w - 2 * kPad;
        float y = r.y + kPad;
        ui::text(renderer_, x, y, "Puzzles", ui::color::accent);
        y += 12;
        gui_.textField("puzzle-search", {x, y, w, gui::kRow}, puzzleSearch_,
                       "Find puzzles by number, kind or the name of their item.", 40, "Search...");
        y += gui::kRow + kPad;
        const std::string query = lower(puzzleSearch_);
        std::vector<int> shown;
        for (int i = 0; i < static_cast<int>(puzzles_.size()); ++i) {
            const auto& p = puzzles_[static_cast<std::size_t>(i)];
            if (!query.empty()) {
                const std::string text = std::to_string(i) + " " + lower(kindName(p.type)) + " " +
                                         lower(p.item1 == data::kNoTile ? "" : tileName(p.item1));
                if (text.find(query) == std::string::npos) {
                    continue;
                }
            }
            shown.push_back(i);
        }
        const float buttonH = gui::kRow + kPad;
        const SDL_FRect list{r.x, y, r.w - 1, r.y + r.h - y - buttonH - kPad};
        const float rowH = 20;
        gui_.beginScroll(list, puzzleScroll_, static_cast<float>(shown.size()) * rowH);
        float ry = list.y - puzzleScroll_;
        for (const int i : shown) {
            const SDL_FRect row{x, ry, w - 4, rowH - 2};
            ry += rowH;
            if (!gui_.visible(row)) {
                continue;
            }
            const auto& p = puzzles_[static_cast<std::size_t>(i)];
            const std::string item = p.item1 == data::kNoTile ? "no item" : tileName(p.item1);
            if (gui_.button("puzzle-" + std::to_string(i), row, "",
                            "Puzzle " + std::to_string(i) + ": " + kindHelp(p.type) + " Item: " + item + ".",
                            i == puzzle_)) {
                puzzle_ = i;
            }
            drawTile(p.item1, {row.x + 2, row.y + 1, 16, 16});
            ui::text(renderer_, row.x + 22, row.y + 5,
                     gui::fitText(std::to_string(i) + " " + kindName(p.type) + ": " + item, row.w - 26),
                     i == puzzle_ ? ui::color::accent : (p.type == 0xffffffff ? ui::color::faint : ui::color::text));
        }
        gui_.endScroll();
        if (gui_.button("puzzle-copy", {x, r.y + r.h - kPad - gui::kRow, w, gui::kRow}, "Add a copy",
                        "Add a new puzzle at the end, a copy of the chosen one.", false, !puzzles_.empty())) {
            rememberStory();
            auto& d = *host_.data;
            data::TaggedEntry entry = d.puzzles[static_cast<std::size_t>(puzzle_)];
            entry.id = static_cast<std::uint16_t>(d.puzzles.size());
            d.puzzles.push_back(entry);
            puzzles_.push_back(puzzles_[static_cast<std::size_t>(puzzle_)]);
            puzzle_ = static_cast<int>(puzzles_.size()) - 1;
            puzzleScroll_ = 1e9f;
            notice_ = "Puzzle " + std::to_string(puzzle_) + " added.";
            noticeAt_ = SDL_GetTicks();
        }
    }

    // The tile in hand.
    {
        const SDL_FRect& r = right;
        ui::fill(renderer_, r, ui::color::panel);
        ui::fill(renderer_, {r.x, r.y, 1, r.h}, ui::color::border);
        float y = r.y + kPad;
        ui::text(renderer_, r.x + kPad, y, "Tile in hand", ui::color::accent);
        drawTile(tile_, {r.x + r.w - kPad - 16, y - 2, 16, 16});
        y += 16;
        if (const auto picked = tileBrowser({r.x, y, r.w, r.y + r.h - y}, "puzzle-tiles", 28)) {
            tile_ = *picked;
        }
    }

    // The puzzle.
    const SDL_FRect& r = center;
    const float x = r.x + kPad * 2;
    const float w = std::min(r.w - 4 * kPad, 560.0f);
    float y = r.y + kPad;
    if (puzzles_.empty()) {
        gui_.paragraph(x, y, w, "This game has no puzzles.", ui::color::faint);
        return;
    }
    const int pi = puzzle_;
    auto& p = puzzles_[static_cast<std::size_t>(pi)];
    ui::text(renderer_, x, y, "Puzzle " + std::to_string(pi), ui::color::accent);
    y += 14;
    int kindIndex = -1;
    for (std::size_t k = 0; k < std::size(kKinds); ++k) {
        if (kKinds[k] == p.type) {
            kindIndex = static_cast<int>(k);
        }
    }
    int kindPick = std::max(0, kindIndex);
    const int kindBefore = kindPick;
    gui_.stepper("puzzle-kind", {x, y, w, gui::kRow}, "Kind", kindPick, 0, static_cast<int>(std::size(kKinds)) - 1,
                 kindName(p.type), kindHelp(p.type));
    if (kindPick != kindBefore) {
        rememberStory();
        p.type = kKinds[static_cast<std::size_t>(kindPick)];
        storePuzzle(pi);
    }
    y += gui::kRow + 2;
    y += gui_.paragraph(x, y, w, kindHelp(p.type), ui::color::dim, 3) + kPad;

    // The items.
    const bool yoda = host_.data->layout() == data::Layout::Yoda;
    const auto itemRow = [&](const char* id, const char* label, std::uint16_t& item, const char* help) {
        ui::fill(renderer_, {x, y, 32, 32}, ui::color::background);
        drawTile(item, {x, y, 32, 32});
        const std::string name = item == data::kNoTile ? "None" : std::to_string(item) + " " + tileName(item);
        ui::text(renderer_, x + 40, y + 2, label, ui::color::dim);
        ui::text(renderer_, x + 40, y + 14, gui::fitText(name, w - 40 - 190), ui::color::text);
        if (gui_.button(std::string(id) + "-hand", {x + w - 186, y + 9, 110, gui::kRow}, "Tile in hand",
                        std::string(help) + " This sets it to the tile in hand (" + std::to_string(tile_) + ").",
                        item == tile_)) {
            rememberStory();
            item = tile_;
            storePuzzle(pi);
        }
        if (gui_.button(std::string(id) + "-none", {x + w - 72, y + 9, 72, gui::kRow}, "None",
                        std::string(help) + " This clears it.", false, item != data::kNoTile)) {
            rememberStory();
            item = data::kNoTile;
            storePuzzle(pi);
        }
        y += 36;
    };
    itemRow("puzzle-item", "Item", p.item1, "The item of the puzzle.");
    if (yoda && p.type == 3) {
        itemRow("puzzle-item2", "Second item", p.item2, "Goals use a second item.");
    }
    y += kPad;

    // What people say.
    const bool goal = p.type == 3;
    const TextSlot slots[] = {
        {data::PuzzleText::Request, "Asking for it", "What someone says when they ask the hero for the item."},
        {data::PuzzleText::Thanks, "Thanking for it", "What someone says after the hero gave them the item."},
        {data::PuzzleText::Offer, goal ? "Mission hint" : "Offering it",
         goal ? "A hint about the mission." : "What someone says when they offer the hero the item."},
        {data::PuzzleText::Mission, "Mission briefing", "Goal only: the briefing at the start of a game."},
    };
    for (const auto& slot : slots) {
        if (slot.which == data::PuzzleText::Mission && !goal) {
            continue;
        }
        auto& raw = p.texts[static_cast<std::size_t>(slot.which)];
        ui::text(renderer_, x, y, slot.label, ui::color::accent);
        y += 11;
        std::string said = text::windows1252ToUtf8(raw);
        const std::string id = "puzzle-text-" + std::to_string(static_cast<int>(slot.which));
        if (gui_.textField(id, {x, y, w, gui::kRow}, said, std::string(slot.help) + " Click and type.", 1000,
                           "Nothing")) {
            if (mergeKey_ != id) {
                rememberStory();
                mergeKey_ = id;
            }
            raw = text::utf8ToWindows1252(said);
            storePuzzle(pi);
        }
        y += gui::kRow + 2;
        if (ui::textWidth(said) > w - 8) {
            y += gui_.paragraph(x, y, w, said, ui::color::dim, 4); // all of it
        }
        y += kPad;
    }
}

} // namespace das::app
