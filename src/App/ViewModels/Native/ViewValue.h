#pragma once

#include <iiSDKCore/Core/Value/Value.h>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dreamscapes::vm {
using Value = ii::core::Value;
using Object = Value::Object;
using Array = Value::Array;

inline const Object &object(const Value &value) {
    static const Object empty;
    const auto *p = std::get_if<Object>(&value.storage());
    return p ? *p : empty;
}
inline const Array &array(const Value &value) {
    static const Array empty;
    const auto *p = std::get_if<Array>(&value.storage());
    return p ? *p : empty;
}
inline const Value &get(const Value &value, std::string_view key) {
    static const Value empty;
    const auto &o = object(value);
    const auto found = o.find(key);
    return found == o.end() ? empty : found->second;
}
inline bool has(const Value &value, std::string_view key) { return object(value).contains(key); }
inline std::string text(const Value &value, std::string fallback = {}) {
    if (const auto *p = std::get_if<std::string>(&value.storage())) return *p;
    return fallback;
}
inline double number(const Value &value, double fallback = 0) {
    return std::visit([fallback](const auto &v) -> double {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_arithmetic_v<T> && !std::same_as<T, bool>) return static_cast<double>(v);
        return fallback;
    }, value.storage());
}
inline bool boolean(const Value &value, bool fallback = false) {
    if (const auto *p = std::get_if<bool>(&value.storage())) return *p;
    return fallback;
}
inline Value merged(const Value &base, const Value &patch) {
    auto result = object(base);
    for (const auto &[key, value] : object(patch)) result[key] = value;
    return Value(std::move(result));
}
inline std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \n\r\t");
    if (first == std::string::npos) return {};
    return value.substr(first, value.find_last_not_of(" \n\r\t") - first + 1);
}
inline std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
inline bool contains(const Value &values, std::string_view value) {
    return std::ranges::any_of(array(values), [&](const Value &v) { return text(v) == value; });
}

// Draft member paths are values, never executable expressions. Missing members
// remain distinct from explicit null; path traversal has a fixed depth bound.
std::vector<std::string> pathParts(std::string_view path);
std::optional<Value> pathValue(const Value &source, std::string_view path);
Value withPath(const Value &source, std::string_view path, const Value &value);
Value nested(const Value &flat, const Value &overlay = {});
Value flatten(const Value &value, std::string_view prefix = {});
std::string resolvedPath(std::string_view path, const Value &indices);
struct ParsedNumber { bool valid = false; Value value; };
ParsedNumber parseNumber(const Value &field, std::string input, bool graphic);
std::string numberText(const Value &field, const Value &value, bool graphic, bool desktop = false);
Value buttonRows(double width, const Value &widths, int columns, double height, double gap, bool rightAligned);
} // namespace dreamscapes::vm
