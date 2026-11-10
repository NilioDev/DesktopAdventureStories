// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zone scripts as text (docs/formats/script-text.md).
#include "test.hpp"

#include "das/data/game_files.hpp"
#include "das/pack/mod.hpp"
#include "das/script/opcodes.hpp"
#include "das/script/script_text.hpp"

#include <cstdlib>
#include <string>

using namespace das;

namespace {

data::ScriptLine line(std::uint16_t opcode, std::array<std::int16_t, 5> args = {}, std::string text = {}) {
    data::ScriptLine l;
    l.opcode = opcode;
    l.args = args;
    l.text = std::move(text);
    return l;
}

bool same(const std::vector<data::Action>& a, const std::vector<data::Action>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    const auto sameLines = [](const std::vector<data::ScriptLine>& x, const std::vector<data::ScriptLine>& y) {
        if (x.size() != y.size()) {
            return false;
        }
        for (std::size_t i = 0; i < x.size(); ++i) {
            if (x[i].opcode != y[i].opcode || x[i].args != y[i].args || x[i].text != y[i].text) {
                return false;
            }
        }
        return true;
    };
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!sameLines(a[i].conditions, b[i].conditions) || !sameLines(a[i].instructions, b[i].instructions)) {
            return false;
        }
    }
    return true;
}

int lineOfError(const std::string& text) {
    try {
        script::readScriptText(text, data::Layout::Yoda);
    } catch (const script::TextError& e) {
        return e.line();
    }
    return 0;
}

} // namespace

DAS_TEST("scripts are written as text and read back") {
    using script::Condition;
    using script::Instruction;
    const auto yoda = data::Layout::Yoda;
    std::vector<data::Action> actions(2);
    actions[0].conditions = {line(static_cast<std::uint16_t>(Condition::Bump), {4, 5, 812})};
    actions[0].instructions = {
        line(static_cast<std::uint16_t>(Instruction::SpeakNpc), {4, 5}, "Say \"hi\"\r\nto \xe9t\xe9\\"),
        line(static_cast<std::uint16_t>(Instruction::DropItem), {-1, 3, 7}),
        line(static_cast<std::uint16_t>(Instruction::Wait), {0, 0, 0, 0, 9}),
        line(999, {1}), // no such opcode: kept by number
        line(static_cast<std::uint16_t>(Instruction::SpeakHero), {}, std::string("\x01\x7f", 2)),
    };
    // An empty script stays.
    const std::string text = script::writeScriptText(actions, yoda);
    DAS_CHECK(text.find("  if Bump 4, 5, 812\n") != std::string::npos);
    DAS_CHECK(text.find("  do DropItem -1, 3, 7\n") != std::string::npos);
    DAS_CHECK(text.find("  do Wait 0, 0, 0, 0, 9\n") != std::string::npos);
    DAS_CHECK(text.find("  do op999 1\n") != std::string::npos);
    DAS_CHECK(text.find("\\\"hi\\\"\\r\\nto \xc3\xa9t\xc3\xa9\\\\\"") != std::string::npos);
    DAS_CHECK(same(script::readScriptText(text, yoda), actions));

    // Hand-written text: comments, blank lines, Windows line ends, spaces.
    const auto read = script::readScriptText("# a door that talks\r\n\nscript\n  if   Bump 1 , 2,3\n"
                                             "  do SpeakHero \"Locked.\"\nend\n",
                                             yoda);
    DAS_CHECK(read.size() == 1);
    DAS_CHECK(read.size() == 1 && read[0].conditions.size() == 1 && read[0].conditions[0].args[2] == 3);
    DAS_CHECK(read.size() == 1 && read[0].instructions.size() == 1 && read[0].instructions[0].text == "Locked.");

    // Errors name their line.
    DAS_CHECK(lineOfError("script\n  if Bumpp 1\nend\n") == 2);
    DAS_CHECK(lineOfError("script\n  do AddItem 1\n  if Enter\nend\n") == 3);
    DAS_CHECK(lineOfError("script\n  do AddItem 1, 2, 3, 4, 5, 6\nend\n") == 2);
    DAS_CHECK(lineOfError("script\n  do SpeakHero \"open\nend\n") == 2);
    DAS_CHECK(lineOfError("  do AddItem 1\n") == 1);
    DAS_CHECK(lineOfError("script\n  do AddItem 1\n") == 2);
    DAS_CHECK(lineOfError("script\n  do AddItem 70000\nend\n") == 2);
    DAS_CHECK(lineOfError("script\n  do SpeakHero \"\xe2\x82\xac\"\nend\n") == 0); // the euro sign is in Windows-1252
    DAS_CHECK(lineOfError("script\n  do SpeakHero \"\xe2\x86\x92\"\nend\n") == 2);    // an arrow is not
    // Indy has no SetRandom.
    DAS_CHECK([&] {
        try {
            script::readScriptText("script\n  do SetRandom 1\nend\n", data::Layout::Indy);
        } catch (const script::TextError&) {
            return true;
        }
        return false;
    }());
}

DAS_TEST("every script of both games survives the trip through text") {
    const char* dir = std::getenv("DAS_GAMEDATA_DIR");
    if (!dir || !*dir) {
        DAS_SKIP("set DAS_GAMEDATA_DIR to run");
    }
    for (const auto& game : data::findGameFiles(dir)) {
        const data::GameData d = data::parseGameData(data::readFile(game.dataFile));
        std::size_t failures = 0;
        for (const auto& zone : d.zones) {
            if (zone.actions && !same(script::readScriptText(script::writeScriptText(*zone.actions, d.layout()),
                                                             d.layout()),
                                      *zone.actions)) {
                ++failures;
            }
        }
        DAS_CHECK(failures == 0);
    }
}

DAS_TEST("a mod keeps changed scripts as text") {
    pack::Pack original;
    original.game = "indy";
    original.data.container.layout = data::Layout::Indy;
    original.data.tiles.resize(1);
    data::Zone zone;
    zone.width = 9;
    zone.height = 9;
    zone.cells.resize(81);
    zone.hotspots = std::vector<data::Hotspot>{};
    zone.actions = std::vector<data::Action>{};
    original.data.zones = {zone, zone};

    pack::Pack changed = original;
    data::Action a;
    a.conditions = {line(static_cast<std::uint16_t>(script::opcode(data::Layout::Indy, script::Condition::Enter)))};
    a.instructions = {line(static_cast<std::uint16_t>(script::opcode(data::Layout::Indy, script::Instruction::SpeakHero)),
                           {}, "Hello")};
    changed.data.zones[1].actions->push_back(a);

    pack::ModInfo info;
    info.id = "talk";
    info.game = "indy";
    const pack::Mod mod = pack::makeMod(original, changed, info);
    DAS_CHECK(mod.files.count("scripts/0001.txt") == 1);
    DAS_CHECK(mod.files.count("zones/0001.json") == 0);
    pack::Pack applied = original;
    pack::applyMod(applied, mod);
    DAS_CHECK(pack::encode(applied) == pack::encode(changed));

    pack::Mod bad = mod;
    bad.files["scripts/0001.txt"] = data::Bytes{'d', 'o', ' ', 'X', '\n'};
    pack::Pack p = original;
    bool refused = false;
    try {
        pack::applyMod(p, bad);
    } catch (const std::exception& e) {
        refused = std::string(e.what()).find("scripts/0001.txt: line 1") != std::string::npos;
    }
    DAS_CHECK(refused);
}
