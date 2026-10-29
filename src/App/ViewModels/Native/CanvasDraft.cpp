#include "CanvasDraft.h"
#include <charconv>
#include <limits>
#include <numeric>
#include <sstream>
namespace dreamscapes::vm {
namespace {
double inputNumber(const Value &input) {
    const auto s = trim(text(input));
    double n = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), n);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size() ? n : std::numeric_limits<double>::quiet_NaN();
}
std::string decimal(double n) {
    if (!std::isfinite(n))
        return "NaN";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(15);
    out << n;
    return out.str();
}
} // namespace
CanvasDraft::CanvasDraft(Sections sections, Specification specification, Convert convert)
    : sections_(std::move(sections)), specification_(std::move(specification)), convert_(std::move(convert)),
      commands_([this](const auto &command) { return execute(command); }) {
    values_ = {{"categoryIndex", Value(0)},
               {"query", Value("")},
               {"custom", Value(false)},
               {"selectedPreset", Value(Object{})},
               {"draftWidth", Value("1080")},
               {"draftHeight", Value("1080")},
               {"unit", Value("px")},
               {"resolution", Value("300")},
               {"canvasBackground", Value("White")},
               {"creationError", Value("")}};
}
Value CanvasDraft::sections() const {
    const auto category = static_cast<int>(number(values_.at("categoryIndex")));
    const auto query = text(values_.at("query"));
    if (category != cachedCategory_ || query != cachedQuery_) {
        cachedCategory_ = category;
        cachedQuery_ = query;
        cachedSections_ = sections_(category, query);
    }
    return cachedSections_;
}
void CanvasDraft::choose(const Value &preset) {
    values_["selectedPreset"] = preset;
    values_["custom"] = Value(false);
    values_["draftWidth"] = Value(decimal(number(get(preset, "width"))));
    values_["draftHeight"] = Value(decimal(number(get(preset, "height"))));
    values_["unit"] = Value(text(get(preset, "unit")));
    values_["resolution"] = Value(decimal(number(get(preset, "ppi"), 300)));
    values_["creationError"] = Value("");
}
Value CanvasDraft::presentation() const {
    auto result = values_;
    const auto groups = sections();
    int count = 0;
    for (const auto &section : array(groups))
        count += array(get(section, "presets")).size();
    result["sections"] = groups;
    result["resultCount"] = Value(count);
    const auto custom = boolean(values_.at("custom"));
    const auto &preset = values_.at("selectedPreset");
    const auto spec = custom || !text(get(preset, "id")).empty()
                          ? specification_(inputNumber(values_.at("draftWidth")),
                                           inputNumber(values_.at("draftHeight")), text(values_.at("unit")),
                                           inputNumber(values_.at("resolution")), text(values_.at("canvasBackground")))
                          : Value(Object{{"valid", Value(false)}, {"error", Value("")}});
    result["specification"] = spec;
    result["selectionName"] = Value(custom ? "Custom canvas" : text(get(preset, "name"), "Choose a canvas"));
    result["dimensionText"] = Value(text(values_.at("draftWidth")) + " × " + text(values_.at("draftHeight")) + " " +
                                    text(values_.at("unit")));
    std::string ratio = "—";
    if (boolean(get(spec, "valid"))) {
        const auto width = static_cast<std::int64_t>(number(get(spec, "pixelWidth"))),
                   height = static_cast<std::int64_t>(number(get(spec, "pixelHeight")));
        const auto gcd = std::gcd(width, height);
        if (gcd > 0)
            ratio = std::to_string(width / gcd) + ':' + std::to_string(height / gcd);
    }
    result["aspectRatio"] = Value(ratio);
    return Value(result);
}
CanvasDraftReply CanvasDraft::execute(const CanvasDraftCommand &command) {
    switch (command.action) {
    case CanvasDraftAction::write:
        if (values_.contains(command.key))
            values_[command.key] = command.value;
        break;
    case CanvasDraftAction::choose:
        choose(command.value);
        break;
    case CanvasDraftAction::begin:
        values_["canvasBackground"] = Value("White");
        values_["creationError"] = Value("");
        [[fallthrough]];
    case CanvasDraftAction::chooseCategory: {
        values_["categoryIndex"] = command.action == CanvasDraftAction::begin ? Value(0) : command.value;
        values_["query"] = Value("");
        values_["custom"] = Value(false);
        const auto groups = sections();
        if (!array(groups).empty() && !array(get(array(groups).front(), "presets")).empty())
            choose(array(get(array(groups).front(), "presets")).front());
        break;
    }
    case CanvasDraftAction::search: {
        values_["query"] = command.value;
        values_["custom"] = Value(false);
        const auto groups = sections();
        if (!array(groups).empty() && !array(get(array(groups).front(), "presets")).empty())
            choose(array(get(array(groups).front(), "presets")).front());
        else
            values_["selectedPreset"] = Value(Object{});
        break;
    }
    case CanvasDraftAction::customSize:
        values_["query"] = Value("");
        values_["custom"] = Value(true);
        values_["selectedPreset"] = Value(Object{});
        values_["draftWidth"] = Value("1800");
        values_["draftHeight"] = Value("1200");
        values_["unit"] = Value("px");
        values_["resolution"] = Value("300");
        values_["creationError"] = Value("");
        break;
    case CanvasDraftAction::changeUnit: {
        const auto next = text(command.value), old = text(values_.at("unit"));
        const auto ppi = inputNumber(values_.at("resolution"));
        for (auto field : {"draftWidth", "draftHeight"}) {
            const auto value = convert_(inputNumber(values_.at(field)), old, next, ppi);
            values_[field] =
                Value(decimal(next == "px" ? std::floor(value + .5) : std::floor(value * 1000 + .5) / 1000));
        }
        values_["unit"] = command.value;
        values_["custom"] = Value(true);
        break;
    }
    case CanvasDraftAction::create: {
        const auto state = presentation();
        if (!boolean(get(get(state, "specification"), "valid")))
            return {state, {}};
        const auto custom = boolean(values_.at("custom"));
        const auto &preset = values_.at("selectedPreset");
        auto specification = object(get(state, "specification"));
        specification["name"] = get(state, "selectionName");
        specification["presetId"] = custom ? Value("") : get(preset, "id");
        specification["category"] = custom ? Value("Custom size") : get(preset, "category");
        specification["section"] = custom ? Value("Your dimensions") : get(preset, "section");
        return {state, Value(specification)};
    }
    }
    return {presentation(), {}};
}
} // namespace dreamscapes::vm
