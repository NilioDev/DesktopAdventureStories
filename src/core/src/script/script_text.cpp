// SPDX-License-Identifier: GPL-3.0-or-later
//
// Zone scripts as readable text (docs/formats/script-text.md).
#include "das/script/script_text.hpp"

#include "das/script/opcodes.hpp"
#include "das/util/text.hpp"

#include <cctype>
#include <charconv>
#include <cstdio>

namespace das::script {

namespace {

constexpr std::uint16_t kConditionValues[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                                              14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
                                              28, 29, 30, 31, 32, 33, 34, 35, 100, 101};
constexpr std::uint16_t kInstructionValues[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                                                14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
                                                28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 100, 101};

// The opcode's name when it reads back as the same number, otherwise the
// game's own number as "opN".
std::string opcodeText(const data::ScriptLine& line, data::Layout game, bool isCondition) {
    if (isCondition) {
        const Condition c = condition(game, line.opcode);
        if (c != Condition::Unknown && opcode(game, c) == line.opcode) {
            return std::string(name(c));
        }
    } else {
        const Instruction i = instruction(game, line.opcode);
        if (i != Instruction::Unknown && opcode(game, i) == line.opcode) {
            return std::string(name(i));
        }
    }
    return "op" + std::to_string(line.opcode);
}

std::string quoted(const std::string& windows1252) {
    const std::string utf8 = text::windows1252ToUtf8(windows1252);
    std::string out = "\"";
    for (const char ch : utf8) {
        const auto c = static_cast<unsigned char>(ch);
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                char hex[8];
                std::snprintf(hex, sizeof hex, "\\x%02x", c);
                out += hex;
            } else {
                out += ch;
            }
        }
    }
    return out + "\"";
}

void writeLine(std::string& out, const char* keyword, const data::ScriptLine& line, data::Layout game,
               bool isCondition) {
    out += "  ";
    out += keyword;
    out += ' ';
    out += opcodeText(line, game, isCondition);
    // Trailing zeros are left out: missing numbers read as 0.
    std::size_t count = line.args.size();
    while (count > 0 && line.args[count - 1] == 0) {
        --count;
    }
    for (std::size_t i = 0; i < count; ++i) {
        out += i == 0 ? " " : ", ";
        out += std::to_string(line.args[i]);
    }
    if (!line.text.empty()) {
        out += count == 0 ? " " : ", ";
        out += quoted(line.text);
    }
    out += '\n';
}

std::uint16_t lookupOpcode(std::string_view word, data::Layout game, bool isCondition, int lineNumber) {
    if (word.size() > 2 && word.substr(0, 2) == "op" &&
        std::isdigit(static_cast<unsigned char>(word[2])) != 0) {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(word.data() + 2, word.data() + word.size(), value);
        if (error != std::errc{} || end != word.data() + word.size() || value > 0xffff) {
            throw TextError(lineNumber, "bad opcode number \"" + std::string(word) + "\"");
        }
        return static_cast<std::uint16_t>(value);
    }
    if (isCondition) {
        for (const auto v : kConditionValues) {
            const auto c = static_cast<Condition>(v);
            if (name(c) == word) {
                const int number = opcode(game, c);
                if (number < 0) {
                    throw TextError(lineNumber, "this game has no condition " + std::string(word));
                }
                return static_cast<std::uint16_t>(number);
            }
        }
        throw TextError(lineNumber, "unknown condition \"" + std::string(word) + "\"");
    }
    for (const auto v : kInstructionValues) {
        const auto i = static_cast<Instruction>(v);
        if (name(i) == word) {
            const int number = opcode(game, i);
            if (number < 0) {
                throw TextError(lineNumber, "this game has no instruction " + std::string(word));
            }
            return static_cast<std::uint16_t>(number);
        }
    }
    throw TextError(lineNumber, "unknown instruction \"" + std::string(word) + "\"");
}

class LineReader {
public:
    LineReader(std::string_view text, int number) : text_(text), number_(number) {}

    void skipSpace() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t')) {
            ++at_;
        }
    }
    bool done() {
        skipSpace();
        return at_ >= text_.size();
    }
    std::string_view word() {
        skipSpace();
        const std::size_t start = at_;
        while (at_ < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[at_])) != 0 || text_[at_] == '_')) {
            ++at_;
        }
        return text_.substr(start, at_ - start);
    }
    char peek() {
        skipSpace();
        return at_ < text_.size() ? text_[at_] : '\0';
    }
    void take(char c) {
        if (peek() != c) {
            fail(std::string("expected '") + c + "'");
        }
        ++at_;
    }
    std::int16_t number() {
        skipSpace();
        int value = 0;
        const auto [end, error] = std::from_chars(text_.data() + at_, text_.data() + text_.size(), value);
        if (error != std::errc{} || value < -32768 || value > 65535) {
            fail("expected a number from -32768 to 65535");
        }
        at_ = static_cast<std::size_t>(end - text_.data());
        return static_cast<std::int16_t>(static_cast<std::uint16_t>(value));
    }
    std::string string() {
        take('"');
        std::string out;
        for (;;) {
            if (at_ >= text_.size()) {
                fail("the text has no closing quote");
            }
            const char c = text_[at_++];
            if (c == '"') {
                break;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) {
                fail("the text has no closing quote");
            }
            const char e = text_[at_++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case 'x': {
                unsigned value = 0;
                if (at_ + 2 > text_.size() ||
                    std::from_chars(text_.data() + at_, text_.data() + at_ + 2, value, 16).ptr != text_.data() + at_ + 2) {
                    fail("\\x needs two hex digits");
                }
                at_ += 2;
                out += static_cast<char>(value);
                break;
            }
            default: fail(std::string("unknown escape \\") + e);
            }
        }
        try {
            return text::utf8ToWindows1252(out);
        } catch (const std::exception&) {
            fail("the text has a character the game cannot show");
        }
    }
    [[noreturn]] void fail(const std::string& message) const { throw TextError(number_, message); }

private:
    std::string_view text_;
    std::size_t at_ = 0;
    int number_;
};

data::ScriptLine readLine(LineReader& in, data::Layout game, bool isCondition) {
    data::ScriptLine line;
    const std::string_view op = in.word();
    if (op.empty()) {
        in.fail(isCondition ? "expected a condition" : "expected an instruction");
    }
    line.opcode = lookupOpcode(op, game, isCondition, 0);
    std::size_t count = 0;
    bool first = true;
    while (!in.done()) {
        if (!first) {
            in.take(',');
        }
        first = false;
        if (in.peek() == '"') {
            line.text = in.string();
            if (!in.done()) {
                in.fail("the text must come last");
            }
            break;
        }
        if (count == line.args.size()) {
            in.fail("at most five numbers");
        }
        line.args[count++] = in.number();
    }
    return line;
}

} // namespace

std::string writeScriptText(const std::vector<data::Action>& actions, data::Layout game) {
    std::string out;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (i > 0) {
            out += '\n';
        }
        out += "script\n";
        for (const auto& c : actions[i].conditions) {
            writeLine(out, "if", c, game, true);
        }
        for (const auto& d : actions[i].instructions) {
            writeLine(out, "do", d, game, false);
        }
        out += "end\n";
    }
    return out;
}

std::vector<data::Action> readScriptText(std::string_view text, data::Layout game) {
    std::vector<data::Action> actions;
    bool open = false;
    int number = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view raw = text.substr(start, end - start);
        start = end + 1;
        ++number;
        if (!raw.empty() && raw.back() == '\r') {
            raw.remove_suffix(1);
        }
        LineReader in(raw, number);
        if (in.done() || in.peek() == '#') {
            continue;
        }
        const std::string_view keyword = in.word();
        try {
            if (keyword == "script") {
                if (open) {
                    in.fail("the script before has no \"end\"");
                }
                open = true;
                actions.emplace_back();
            } else if (keyword == "end") {
                if (!open) {
                    in.fail("\"end\" without \"script\"");
                }
                open = false;
            } else if (keyword == "if" || keyword == "do") {
                if (!open) {
                    in.fail("\"" + std::string(keyword) + "\" outside a script");
                }
                auto& action = actions.back();
                if (keyword == "if") {
                    if (!action.instructions.empty()) {
                        in.fail("conditions (if) come before instructions (do)");
                    }
                    action.conditions.push_back(readLine(in, game, true));
                } else {
                    action.instructions.push_back(readLine(in, game, false));
                }
                continue;
            } else {
                in.fail("expected script, if, do or end");
            }
            if (!in.done()) {
                in.fail("unexpected text after \"" + std::string(keyword) + "\"");
            }
        } catch (const TextError& e) {
            if (e.line() == 0) {
                throw TextError(number, std::string(e.what()).substr(std::string("line 0: ").size()));
            }
            throw;
        }
    }
    if (open) {
        throw TextError(number, "the last script has no \"end\"");
    }
    return actions;
}

} // namespace das::script
