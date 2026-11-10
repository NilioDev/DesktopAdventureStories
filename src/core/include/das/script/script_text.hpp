// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zone scripts as readable text, for editing them in a text editor:
//
//   script
//     if Bump 4, 5, 812
//     do SpeakNpc 4, 5, "Hello there."
//     do AddItem 412
//   end
//
// Reading the text written for some scripts gives the same scripts back.
// See docs/formats/script-text.md.
#pragma once

#include "das/data/game_data.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace das::script {

class TextError : public std::runtime_error {
public:
    TextError(int line, const std::string& message)
        : std::runtime_error("line " + std::to_string(line) + ": " + message), line_(line) {}
    int line() const { return line_; }

private:
    int line_;
};

std::string writeScriptText(const std::vector<data::Action>& actions, data::Layout game);
// Throws TextError for text it cannot read.
std::vector<data::Action> readScriptText(std::string_view text, data::Layout game);

} // namespace das::script
