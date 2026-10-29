#include "ViewProjection.h"
#include "GenerationPresentation.h"
#include "GenerationDraft.h"
#include <charconv>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace dreamscapes::vm {
namespace {
std::string scalar(const Value &value) {
    if (value.kind() == ii::core::ValueKind::text) return text(value);
    if (value.kind() == ii::core::ValueKind::boolean) return boolean(value) ? "true" : "false";
    if (value.kind() == ii::core::ValueKind::null) return {};
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), number(value));
    return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string{};
}
const Value &field(const Value &entry, std::string_view key) {
    return key.empty() || entry.kind() != ii::core::ValueKind::object ? entry : get(entry, key);
}
int integer(const Value &value, int fallback = 0) {
    const double n = number(value, fallback);
    return static_cast<int>(std::clamp(n, double(std::numeric_limits<int>::min()),
                                     double(std::numeric_limits<int>::max())));
}
std::string label(const Value &entry, std::string_view key, std::string_view fallback) {
    auto result = scalar(field(entry, key));
    return result.empty() && !fallback.empty() ? scalar(field(entry, fallback)) : result;
}
bool equivalent(const Value &left, const Value &right) {
    const auto numeric = [](const Value &value) {
        return value.kind() == ii::core::ValueKind::signed_integer ||
               value.kind() == ii::core::ValueKind::unsigned_integer || value.kind() == ii::core::ValueKind::real;
    };
    return numeric(left) && numeric(right) ? number(left) == number(right) : left == right;
}
}
ViewProjection::ViewProjection() : queries_([](const ProjectionQuery &query) { return read(query); }) {}
Value ViewProjection::read(const ProjectionQuery &query) {
    static const Value empty;
    const auto arg = [&](std::size_t index) -> const Value & {
        return index < query.arguments.size() ? query.arguments[index] : empty;
    };
    const auto &rows = array(arg(0));
    constexpr int maximumRows = 1 << 20;
    if (rows.size() > maximumRows) throw std::length_error("View projection exceeds its row bound");
    Array result;
    switch (query.operation) {
    case ProjectionRead::labels:
    case ProjectionRead::values:
    case ProjectionRead::choices: {
        const auto key = text(arg(1)), fallback = text(arg(2));
        const int selected = integer(arg(3), -1);
        for (std::size_t index = 0; index < rows.size(); ++index) {
            if (query.operation == ProjectionRead::values) result.push_back(field(rows[index], key));
            else if (query.operation == ProjectionRead::labels)
                result.emplace_back((selected == static_cast<int>(index) ? "✓ " : "") + label(rows[index], key, fallback));
            else result.emplace_back(Object{{"label", Value(label(rows[index], key, fallback))},
                                            {text(arg(4), "value"), field(rows[index], text(arg(3)))}});
        }
        return Value(std::move(result));
    }
    case ProjectionRead::index:
    case ProjectionRead::matchingIndices: {
        const auto key = text(arg(2));
        for (std::size_t index = 0; index < rows.size(); ++index)
            if (equivalent(field(rows[index], key), arg(1))) {
                if (query.operation == ProjectionRead::index) return Value(static_cast<int>(index));
                result.emplace_back(static_cast<int>(index));
            }
        return query.operation == ProjectionRead::index ? Value(-1) : Value(std::move(result));
    }
    case ProjectionRead::indices: {
        const int size = std::max(0, integer(arg(0)));
        if (size > maximumRows) throw std::length_error("View range exceeds its row bound");
        const auto bound = [size](int index) { return index < 0 ? std::max(0, size + index) : std::min(size, index); };
        const int first = bound(integer(arg(1))), last = bound(integer(arg(2), size));
        for (int index = first; index < last; ++index) result.emplace_back(index);
        return Value(std::move(result));
    }
    case ProjectionRead::concatenateIndices: {
        const auto size = std::int64_t(std::max(0, integer(arg(0)))) + std::max(0, integer(arg(1)));
        if (size > maximumRows) throw std::length_error("View concatenation exceeds its row bound");
        for (std::int64_t index = 0; index < size; ++index) result.emplace_back(index);
        return Value(std::move(result));
    }
    case ProjectionRead::join: {
        std::string joined;
        for (const auto &value : rows) {
            const auto part = scalar(value);
            if (part.empty()) continue;
            if (!joined.empty()) joined += text(arg(1));
            joined += part;
        }
        return Value(std::move(joined));
    }
    case ProjectionRead::counts: {
        std::vector<int> counts{1,2,3,4,5,6,7,8,12,16,24,32,64,128,256,512,1000};
        const int current = integer(arg(0), 1);
        if (current >= 1 && current <= 1000 && !std::ranges::binary_search(counts, current))
            counts.insert(std::ranges::lower_bound(counts, current), current);
        for (int count : counts) result.emplace_back(count);
        return Value(std::move(result));
    }
    case ProjectionRead::notifications:
        for (std::size_t index = 0; index < std::min<std::size_t>(rows.size(), 8); ++index) {
            const auto state = text(get(rows[index], "state"));
            const auto labels = object(arg(1));
            const auto found = labels.find(state);
            const auto name = found == labels.end() ? state : text(found->second, state);
            auto prompt = text(get(rows[index], "prompt"));
            result.emplace_back(name + " · " + (prompt.empty() ? text(arg(2), "Generation") : prompt));
        }
        if (result.empty()) result.push_back(arg(3));
        return Value(std::move(result));
    case ProjectionRead::timelineLayout: {
        int top = 32, curveHeight = 0;
        const bool curves = text(arg(1)) == "Curves";
        for (const auto &entry : rows) {
            const auto type = text(get(entry, "type"));
            const int height = type == "Shots" ? 56 : type == "Contents" ? 40 + (curves ? 80 : 0) : type == "Audio" ? 48 : 32;
            auto row = object(entry);
            row["top"] = Value(top); row["height"] = Value(height);
            result.emplace_back(std::move(row)); top += height;
            if (type == "Contents" && curves) curveHeight += 80;
        }
        return Value(Object{{"tracks", Value(std::move(result))}, {"height", Value(rows.empty() ? 196 : top)},
                            {"curveHeight", Value(curveHeight)}});
    }
    case ProjectionRead::timecode: {
        const auto fps = number(arg(1));
        if (!std::isfinite(fps) || fps < 1 || fps > 1000) return Value("--:--:--:--");
        const int nominal = static_cast<int>(std::round(fps)), frame = std::max(0, integer(arg(0)));
        const int seconds = frame / nominal;
        std::ostringstream output;
        output << std::setfill('0') << std::setw(2) << seconds / 3600 << ':'
               << std::setw(2) << seconds / 60 % 60 << ':' << std::setw(2) << seconds % 60 << ':'
               << std::setw(2) << frame % nominal;
        return Value(output.str());
    }
    case ProjectionRead::trackLabels:
        for (std::size_t index = 0; index < rows.size(); ++index)
            result.emplace_back(std::to_string(index + 1) + " · " + text(get(rows[index], "name")));
        return Value(std::move(result));
    case ProjectionRead::videoResults:
        for (std::size_t index = 0; index < rows.size(); ++index)
            result.emplace_back(Object{{"ordinal", Value(static_cast<int>(index + 1))},
                {"frames", get(get(get(rows[index], "generation"), "video"), "frame_count")}});
        return Value(std::move(result));
    case ProjectionRead::sizeChoices: {
        for (const auto &value : rows) {
            const double extent = number(value);
            if (!(extent > 0) || extent > 65536) continue;
            const auto size = imageSize(text(arg(1)), static_cast<int>(extent));
            const int width = integer(get(size, "width")), height = integer(get(size, "height"));
            result.emplace_back(std::to_string(width) + " × " + std::to_string(height));
        }
        return Value(std::move(result));
    }
    case ProjectionRead::nextChoice: {
        if (rows.empty()) return {};
        std::size_t at = rows.size() - 1;
        for (std::size_t index = 0; index < rows.size(); ++index)
            if (equivalent(field(rows[index], "value"), arg(1))) { at = index; break; }
        const auto &next = rows[(at + 1) % rows.size()];
        return next.kind() == ii::core::ValueKind::object ? next : Value(Object{{"value", next}});
    }
    case ProjectionRead::filenames:
        for (const auto &entry : rows) {
            const auto path = scalar(entry);
            const auto slash = path.find_last_of('/');
            result.emplace_back(slash == std::string::npos ? path : path.substr(slash + 1));
        }
        return Value(std::move(result));
    case ProjectionRead::rulerStep: {
        const double pitch = number(arg(0));
        if (!(pitch > 0) || !std::isfinite(pitch)) return Value(10);
        const double needed = std::clamp(64 / pitch, 10., 100000000.);
        const double magnitude = std::pow(10, std::floor(std::log10(needed)));
        for (int multiplier : {1,2,5,10}) if (multiplier * magnitude >= needed)
            return Value(static_cast<int>(multiplier * magnitude));
        return Value(10);
    }
    case ProjectionRead::fileField: {
        const auto key = text(arg(2));
        if (has(arg(0), key)) {
            const auto &value = get(arg(0), key);
            return Value(value.kind() == ii::core::ValueKind::null ? "null" : scalar(value));
        }
        const auto &value = get(arg(1), key);
        return value.kind() == ii::core::ValueKind::null ? arg(3) : Value(scalar(value));
    }
    }
    return {};
}
} // namespace dreamscapes::vm
