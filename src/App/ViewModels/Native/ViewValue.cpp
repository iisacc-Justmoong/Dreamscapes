#include "ViewValue.h"
#include <charconv>
#include <iomanip>
#include <limits>
#include <locale>
#include <regex>
#include <sstream>

namespace dreamscapes::vm {
namespace {
std::optional<std::size_t> indexOf(std::string_view part) {
    std::size_t n = 0;
    const auto result = std::from_chars(part.data(), part.data() + part.size(), n);
    if (result.ec != std::errc{} || result.ptr != part.data() + part.size() || n > 65535)
        return {};
    return n;
}
Value replace(const Value &source, const std::vector<std::string> &parts, std::size_t at, const Value &value) {
    if (at == parts.size())
        return value;
    if (const auto index = indexOf(parts[at])) {
        auto result = array(source);
        if (*index >= result.size())
            result.resize(*index + 1);
        result[*index] = replace(result[*index], parts, at + 1, value);
        return Value(std::move(result));
    }
    auto result = object(source);
    result[parts[at]] = replace(get(source, parts[at]), parts, at + 1, value);
    return Value(std::move(result));
}
std::string fixed(double value, int precision) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(std::clamp(precision, 0, 12)) << value;
    return stream.str();
}
void overlay(Object &destination, const Value &flat) {
    std::vector<std::string> keys;
    for (const auto &[key, value] : object(flat))
        keys.push_back(key);
    std::stable_sort(keys.begin(), keys.end(),
                     [](const auto &a, const auto &b) { return pathParts(a).size() < pathParts(b).size(); });
    Value result(destination);
    for (const auto &key : keys)
        result = withPath(result, key, get(flat, key));
    destination = object(result);
}
void flattenInto(const Value &value, const std::string &prefix, Object &result) {
    if (value.kind() != ii::core::ValueKind::object) {
        result[prefix] = value;
        return;
    }
    for (const auto &[key, member] : object(value))
        flattenInto(member, prefix.empty() ? key : prefix + "." + key, result);
}
} // namespace

std::vector<std::string> pathParts(std::string_view path) {
    std::vector<std::string> parts;
    std::string token;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        if (c == '.' || c == '[' || c == ']') {
            if (!token.empty()) {
                parts.push_back(std::move(token));
                token.clear();
            }
            if (c == '[' && i + 1 < path.size() && path[i + 1] == ']')
                parts.emplace_back("0");
        } else
            token += c;
        if (parts.size() > 64)
            return {};
    }
    if (!token.empty())
        parts.push_back(std::move(token));
    return parts;
}
std::optional<Value> pathValue(const Value &source, std::string_view path) {
    const auto parts = pathParts(path);
    if (parts.empty())
        return {};
    const Value *cursor = &source;
    for (const auto &part : parts) {
        if (cursor->kind() == ii::core::ValueKind::array) {
            const auto index = indexOf(part);
            if (!index || *index >= array(*cursor).size())
                return {};
            cursor = &array(*cursor)[*index];
        } else {
            const auto &entries = object(*cursor);
            const auto found = entries.find(part);
            if (found == entries.end())
                return {};
            cursor = &found->second;
        }
    }
    return *cursor;
}
Value withPath(const Value &source, std::string_view path, const Value &value) {
    const auto parts = pathParts(path);
    return parts.empty() ? source : replace(source, parts, 0, value);
}
Value nested(const Value &flat, const Value &changes) {
    Object result;
    overlay(result, flat);
    overlay(result, changes);
    return Value(std::move(result));
}
Value flatten(const Value &value, std::string_view prefix) {
    Object result;
    flattenInto(value, std::string(prefix), result);
    return Value(std::move(result));
}
std::string resolvedPath(std::string_view path, const Value &indices) {
    std::string result;
    std::size_t offset = 0;
    while (offset < path.size()) {
        const auto found = path.find("[]", offset);
        result += path.substr(offset, found == std::string_view::npos ? path.size() - offset : found - offset);
        if (found == std::string_view::npos)
            break;
        const auto key = result;
        result += '[' + std::to_string(static_cast<int>(number(get(indices, key)))) + ']';
        offset = found + 2;
    }
    return result;
}
ParsedNumber parseNumber(const Value &field, std::string input, bool graphic) {
    input = trim(std::move(input));
    if (graphic &&
        (boolean(get(field, "optional")) ||
         (has(field, "initial") && get(field, "initial").kind() == ii::core::ValueKind::null)) &&
        lower(input) == "default")
        return {true, {}};
    for (std::size_t at = 0; (at = input.find("−", at)) != std::string::npos;)
        input.replace(at, 3, "-");
    if (!graphic && text(get(field, "label")) == "Pan" && input == "Center")
        return {true, Value(0)};
    const auto unit = text(get(field, "unit"));
    if (!unit.empty() && input.ends_with(unit))
        input = trim(input.substr(0, input.size() - unit.size()));
    std::vector<std::string> parts{input};
    const bool range = !graphic && get(field, "initial").kind() == ii::core::ValueKind::array;
    if (range) {
        const auto at = input.find("—");
        if (at == std::string::npos)
            return {};
        parts = {trim(input.substr(0, at)), trim(input.substr(at + 3))};
    }
    static const std::regex decimal(R"(^[+-]?(?:\d+(?:\.\d*)?|\.\d+)$)");
    static const std::regex scientific(R"(^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$)");
    Array values;
    for (const auto &part : parts) {
        if (!std::regex_match(part, graphic ? scientific : decimal))
            return {};
        double value = 0;
        try {
            value = std::stod(part);
        } catch (...) {
            return {};
        }
        if (!std::isfinite(value))
            return {};
        const auto minKey = graphic ? "min" : "minimum", maxKey = graphic ? "max" : "maximum";
        if ((has(field, minKey) && value < number(get(field, minKey))) ||
            (has(field, maxKey) && value > number(get(field, maxKey))))
            return {};
        if (graphic) {
            if ((boolean(get(field, "minExclusive")) && value <= number(get(field, "min"))) ||
                (boolean(get(field, "maxExclusive")) && value >= number(get(field, "max"))) ||
                (boolean(get(field, "integer")) && std::floor(value) != value))
                return {};
        } else {
            const auto step = number(get(field, "step"), 1);
            if (step <= 0 || std::abs(std::floor(value / step + .5) * step - value) > .00001)
                return {};
        }
        values.emplace_back(value);
    }
    if (range && number(values[0]) > number(values[1]))
        return {};
    return {true, range ? Value(values) : values.front()};
}
std::string numberText(const Value &field, const Value &value, bool graphic, bool desktop) {
    if (graphic && value.kind() == ii::core::ValueKind::null)
        return text(get(field, "displayValue"), "Default");
    const auto n = number(value, std::numeric_limits<double>::quiet_NaN());
    if (!graphic && text(get(field, "label")) == "Pan" && n == 0)
        return "Center";
    int precision = static_cast<int>(number(get(field, "decimals")));
    if (graphic && !has(field, "decimals")) {
        const auto step = number(get(field, "step"));
        if (step > 0 && step < 1)
            precision = std::min(6, static_cast<int>(std::ceil(-std::log10(step))));
    }
    if (desktop && !graphic)
        while (precision < 8 && std::abs(std::stod(fixed(n, precision)) - n) > .00000001)
            ++precision;
    auto result = std::isfinite(n) ? fixed(n, precision) : text(value);
    if (graphic && result.find('.') != std::string::npos) {
        while (result.ends_with('0'))
            result.pop_back();
        if (result.ends_with('.'))
            result.pop_back();
    }
    if (desktop && !graphic) {
        if (boolean(get(field, "desktopPositiveSign")) && n > 0)
            result = '+' + result;
        if (result.starts_with('-'))
            result.replace(0, 1, "−");
    }
    const auto unit = text(get(field, "unit"));
    return result + (unit.empty() || unit == "%" || unit == "°" ? "" : " ") + unit;
}
Value buttonRows(double width, const Value &widths, int columns, double height, double gap, bool rightAligned) {
    Array items;
    std::vector<double> row;
    double rowWidth = 0, y = 0;
    const auto finish = [&] {
        if (row.empty())
            return;
        auto x = rightAligned ? std::max(0.0, width - rowWidth) : 0;
        for (const auto w : row) {
            items.emplace_back(Object{{"x", Value(x)}, {"y", Value(y)}, {"width", Value(w)}});
            x += w + gap;
        }
        y += height + gap;
        row.clear();
        rowWidth = 0;
    };
    for (const auto &natural : array(widths)) {
        const auto w = std::min(std::max(0.0, width), number(natural));
        if (!row.empty() && (static_cast<int>(row.size()) >= columns || rowWidth + gap + w > width))
            finish();
        rowWidth += (row.empty() ? 0 : gap) + w;
        row.push_back(w);
    }
    finish();
    return Value(Object{{"items", Value(items)}, {"height", Value(items.empty() ? 0 : y - gap)}});
}
} // namespace dreamscapes::vm
