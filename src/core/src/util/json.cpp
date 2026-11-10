// SPDX-License-Identifier: GPL-3.0-or-later
#include "das/util/json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace das::json {

namespace {

[[noreturn]] void typeError(const char* wanted) { throw Error(std::string("JSON value is not ") + wanted); }

} // namespace

bool Value::asBool() const {
    if (const auto* b = std::get_if<bool>(&v_)) {
        return *b;
    }
    typeError("a boolean");
}

std::int64_t Value::asInt() const {
    if (const auto* i = std::get_if<std::int64_t>(&v_)) {
        return *i;
    }
    if (const auto* d = std::get_if<double>(&v_); d && std::floor(*d) == *d) {
        return static_cast<std::int64_t>(*d);
    }
    typeError("an integer");
}

double Value::asDouble() const {
    if (const auto* d = std::get_if<double>(&v_)) {
        return *d;
    }
    if (const auto* i = std::get_if<std::int64_t>(&v_)) {
        return static_cast<double>(*i);
    }
    typeError("a number");
}

const std::string& Value::asString() const {
    if (const auto* s = std::get_if<std::string>(&v_)) {
        return *s;
    }
    typeError("a string");
}

const Array& Value::asArray() const {
    if (const auto* a = std::get_if<std::shared_ptr<Array>>(&v_)) {
        return **a;
    }
    typeError("an array");
}

const Object& Value::asObject() const {
    if (const auto* o = std::get_if<std::shared_ptr<Object>>(&v_)) {
        return **o;
    }
    typeError("an object");
}

const Value* Value::find(std::string_view key) const {
    for (const auto& [k, v] : asObject()) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

const Value& Value::operator[](std::string_view key) const {
    if (const Value* v = find(key)) {
        return *v;
    }
    throw Error("JSON object has no member '" + std::string(key) + "'");
}

// ---------------------------------------------------------------- writer

namespace {

void writeString(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

bool isFlat(const Array& a) {
    return std::all_of(a.begin(), a.end(), [](const Value& v) { return !v.isArray() && !v.isObject(); });
}

void writeValue(std::string& out, const Value& v, bool pretty, int indent);

// Small objects of plain values (a hotspot, a script line) read best on one
// line.
constexpr std::size_t kInlineObjectWidth = 120;

bool isSimple(const Object& o) {
    return std::all_of(o.begin(), o.end(), [](const auto& member) {
        return !member.second.isObject() && (!member.second.isArray() || isFlat(member.second.asArray()));
    });
}

void writeInline(std::string& out, const Value& v) {
    if (v.isArray()) {
        const auto& a = v.asArray();
        out += '[';
        for (std::size_t i = 0; i < a.size(); ++i) {
            out += i > 0 ? ", " : "";
            writeInline(out, a[i]);
        }
        out += ']';
    } else if (v.isObject()) {
        const auto& o = v.asObject();
        out += '{';
        for (std::size_t i = 0; i < o.size(); ++i) {
            out += i > 0 ? ", " : "";
            writeString(out, o[i].first);
            out += ": ";
            writeInline(out, o[i].second);
        }
        out += '}';
    } else {
        writeValue(out, v, false, 0);
    }
}

void writeValue(std::string& out, const Value& v, bool pretty, int indent) {
    const auto newline = [&](int level) {
        if (pretty) {
            out += '\n';
            out.append(static_cast<std::size_t>(level) * 2, ' ');
        }
    };
    if (v.isNull()) {
        out += "null";
    } else if (v.isBool()) {
        out += v.asBool() ? "true" : "false";
    } else if (v.isInt()) {
        out += std::to_string(v.asInt());
    } else if (v.isNumber()) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.17g", v.asDouble());
        out += buf;
    } else if (v.isString()) {
        writeString(out, v.asString());
    } else if (v.isArray()) {
        const auto& a = v.asArray();
        const bool flat = isFlat(a);
        out += '[';
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (i > 0) {
                out += flat && pretty ? ", " : ",";
            }
            if (!flat) {
                newline(indent + 1);
            }
            writeValue(out, a[i], pretty, indent + 1);
        }
        if (!flat && !a.empty()) {
            newline(indent);
        }
        out += ']';
    } else {
        const auto& o = v.asObject();
        if (pretty && !o.empty() && isSimple(o)) {
            std::string line;
            writeInline(line, v);
            if (line.size() <= kInlineObjectWidth) {
                out += line;
                return;
            }
        }
        out += '{';
        for (std::size_t i = 0; i < o.size(); ++i) {
            if (i > 0) {
                out += ',';
            }
            newline(indent + 1);
            writeString(out, o[i].first);
            out += pretty ? ": " : ":";
            writeValue(out, o[i].second, pretty, indent + 1);
        }
        if (!o.empty()) {
            newline(indent);
        }
        out += '}';
    }
}

// ---------------------------------------------------------------- parser

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    Value document() {
        Value v = value(0);
        skipSpace();
        if (pos_ != s_.size()) {
            fail("unexpected text after the value");
        }
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& what) const {
        throw Error("JSON parse error at offset " + std::to_string(pos_) + ": " + what);
    }

    void skipSpace() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) {
            ++pos_;
        }
    }

    char peek() {
        skipSpace();
        if (pos_ >= s_.size()) {
            fail("unexpected end");
        }
        return s_[pos_];
    }

    void expect(char c) {
        if (peek() != c) {
            fail(std::string("expected '") + c + "'");
        }
        ++pos_;
    }

    bool literal(std::string_view word) {
        if (s_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    Value value(int depth) {
        if (depth > 200) {
            fail("nested too deeply");
        }
        const char c = peek();
        if (c == '{') {
            ++pos_;
            Object o;
            if (peek() == '}') {
                ++pos_;
                return o;
            }
            for (;;) {
                if (peek() != '"') {
                    fail("expected a key");
                }
                std::string key = string();
                expect(':');
                o.emplace_back(std::move(key), value(depth + 1));
                if (peek() == ',') {
                    ++pos_;
                    continue;
                }
                expect('}');
                return o;
            }
        }
        if (c == '[') {
            ++pos_;
            Array a;
            if (peek() == ']') {
                ++pos_;
                return a;
            }
            for (;;) {
                a.push_back(value(depth + 1));
                if (peek() == ',') {
                    ++pos_;
                    continue;
                }
                expect(']');
                return a;
            }
        }
        if (c == '"') {
            return string();
        }
        if (literal("true")) {
            return true;
        }
        if (literal("false")) {
            return false;
        }
        if (literal("null")) {
            return nullptr;
        }
        return number();
    }

    Value number() {
        const std::size_t start = pos_;
        bool isDouble = false;
        if (pos_ < s_.size() && s_[pos_] == '-') {
            ++pos_;
        }
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c >= '0' && c <= '9') {
                ++pos_;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                isDouble = true;
                ++pos_;
            } else {
                break;
            }
        }
        const std::string_view digits = s_.substr(start, pos_ - start);
        if (digits.empty() || digits == "-") {
            fail("expected a value");
        }
        if (!isDouble) {
            std::int64_t i = 0;
            const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), i);
            if (ec == std::errc() && end == digits.data() + digits.size()) {
                return i;
            }
        }
        try {
            return std::stod(std::string(digits));
        } catch (const std::exception&) {
            fail("bad number");
        }
    }

    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xc0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xe0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }

    std::uint32_t hex4() {
        if (pos_ + 4 > s_.size()) {
            fail("short \\u escape");
        }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') {
                v |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                v |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                v |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                fail("bad \\u escape");
            }
        }
        return v;
    }

    std::string string() {
        ++pos_; // opening quote
        std::string out;
        for (;;) {
            if (pos_ >= s_.size()) {
                fail("unterminated string");
            }
            const char c = s_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) {
                fail("unterminated escape");
            }
            const char e = s_[pos_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                std::uint32_t cp = hex4();
                if (cp >= 0xd800 && cp < 0xdc00 && literal("\\u")) {
                    const std::uint32_t low = hex4();
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                }
                appendUtf8(out, cp);
                break;
            }
            default: fail("bad escape");
            }
        }
    }

    std::string_view s_;
    std::size_t pos_ = 0;
};

} // namespace

std::string write(const Value& value, bool pretty) {
    std::string out;
    writeValue(out, value, pretty, 0);
    if (pretty) {
        out += '\n';
    }
    return out;
}

Value parse(std::string_view text) { return Parser(text).document(); }

} // namespace das::json
