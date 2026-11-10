// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small JSON value type with a writer and a parser, for our own open data
// format. Objects keep their key order so files stay stable and diffable.
#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace das::json {

class Value;
using Array = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Value {
public:
    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : v_(b) {}
    Value(int i) : v_(static_cast<std::int64_t>(i)) {}
    Value(unsigned i) : v_(static_cast<std::int64_t>(i)) {}
    Value(std::int64_t i) : v_(i) {}
    Value(std::uint64_t i) : v_(static_cast<std::int64_t>(i)) {}
    Value(double d) : v_(d) {}
    Value(const char* s) : v_(std::string(s)) {}
    Value(std::string s) : v_(std::move(s)) {}
    Value(std::string_view s) : v_(std::string(s)) {}
    Value(Array a) : v_(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : v_(std::make_shared<Object>(std::move(o))) {}

    bool isNull() const { return std::holds_alternative<std::monostate>(v_); }
    bool isBool() const { return std::holds_alternative<bool>(v_); }
    bool isInt() const { return std::holds_alternative<std::int64_t>(v_); }
    bool isNumber() const { return isInt() || std::holds_alternative<double>(v_); }
    bool isString() const { return std::holds_alternative<std::string>(v_); }
    bool isArray() const { return std::holds_alternative<std::shared_ptr<Array>>(v_); }
    bool isObject() const { return std::holds_alternative<std::shared_ptr<Object>>(v_); }

    bool asBool() const;
    std::int64_t asInt() const;
    double asDouble() const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;

    // Object member access; throws when missing.
    const Value& operator[](std::string_view key) const;
    const Value* find(std::string_view key) const;

    // Checked integer conversion.
    template <typename T>
        requires(sizeof(T) < sizeof(std::int64_t) || std::is_same_v<T, std::int64_t>)
    T as() const {
        const std::int64_t i = asInt();
        if (i < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
            i > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
            throw Error("number " + std::to_string(i) + " is out of range");
        }
        return static_cast<T>(i);
    }

private:
    std::variant<std::monostate, bool, std::int64_t, double, std::string, std::shared_ptr<Array>,
                 std::shared_ptr<Object>>
        v_;
};

// Writes JSON. Arrays of plain numbers are kept on one line.
std::string write(const Value& value, bool pretty = true);
Value parse(std::string_view text);

} // namespace das::json
