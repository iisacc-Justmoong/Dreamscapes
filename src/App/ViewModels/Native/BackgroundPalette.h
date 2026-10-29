#pragma once
#include "ViewValue.h"
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
enum class PaletteAction { presets, save, stops };
struct PaletteQuery final { PaletteAction action; Value defaults, saved, preset; };
class BackgroundPalette final {
  public:
    BackgroundPalette() : queries_([](const PaletteQuery &request) {
        if (request.action == PaletteAction::stops) return stops(request.preset);
        Array result = array(request.saved);
        if (request.action == PaletteAction::save) {
            result.push_back(request.preset); return Value(std::move(result));
        }
        result = array(request.defaults);
        const auto &saved = array(request.saved);
        result.insert(result.end(), saved.begin(), saved.end());
        for (auto &preset : result) {
            auto row = object(preset); row["stops"] = stops(preset); preset = Value(std::move(row));
        }
        return Value(std::move(result));
    }) {}
    const ii::runtime::QueryPort<PaletteQuery, Value> &queries() const { return queries_; }
  private:
    static Value stops(const Value &preset) {
        if (has(preset, "stops")) return get(preset, "stops");
        Array result;
        const auto &colors = array(get(preset, "colors"));
        for (std::size_t index = 0; index < colors.size(); ++index)
            result.emplace_back(Object{{"position", Value(colors.size() > 1 ? double(index) / (colors.size() - 1) : 0)},
                                       {"color", colors[index]}});
        return Value(std::move(result));
    }
    ii::runtime::QueryPort<PaletteQuery, Value> queries_;
};
} // namespace dreamscapes::vm
