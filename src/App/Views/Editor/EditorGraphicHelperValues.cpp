#include "EditorGraphicHelperValues.h"
#include <iiGraphicAdjust/Optics.h>
#include <type_traits>

namespace graphicHelper {
namespace GA = iiGraphicAdjust;
std::string_view operation(const Request &request) noexcept {
    constexpr std::array<std::string_view, 8> names{"autoDevelopLight", "autoWhiteBalance", "sampledWhiteBalance", "whiteBalancePreset", "autoBlackAndWhiteMix", "samplePointColor", "autoLensScale", "estimateChromaticAberration"};
    return names[request.index()];
}
bool needsImage(const Request &request) noexcept {
    return !std::holds_alternative<SampleWhite>(request) && !std::holds_alternative<WhitePreset>(request) && !std::holds_alternative<PointColor>(request);
}
Result calculate(const Request &request, const GA::ImageView *source, std::stop_token stop) {
    Result result;
    if (stop.stop_requested()) { result.cancelled = true; return result; }
    if (needsImage(request) && !source) { result.error = "Supply the captured source raster for this analysis."; return result; }
    const auto error = [&](const GA::Error &value) { result.error = value.message; };
    const auto white = [&](const GA::Result<GA::WhiteBalanceOptions> &value) {
        if (!value) { error(value.error()); return; }
        result.parameters = {{"temperature", value->temperature}, {"tint", value->tint},
            {"asShotTemperature", value->asShotTemperature}, {"asShotTint", value->asShotTint}};
    };
    std::visit([&](const auto &input) {
        using T = std::decay_t<decltype(input)>;
        if constexpr (std::is_same_v<T, AutoLight>) {
            const auto value = GA::autoDevelopLight(*source);
            if (!value) { error(value.error()); return; }
            result.parameters = {{"exposure", value->exposure}, {"contrast", value->contrast}, {"highlights", value->highlights},
                {"shadows", value->shadows}, {"whites", value->whites}, {"blacks", value->blacks}};
        } else if constexpr (std::is_same_v<T, AutoWhite>) white(GA::autoWhiteBalance(*source, input.source));
        else if constexpr (std::is_same_v<T, SampleWhite>) white(GA::sampledWhiteBalance(input.sample, input.source));
        else if constexpr (std::is_same_v<T, WhitePreset>) white(GA::whiteBalancePreset(input.preset));
        else if constexpr (std::is_same_v<T, AutoMix>) {
            const auto value = GA::autoBlackAndWhiteMix(*source);
            if (!value) { error(value.error()); return; }
            result.parameters = {{"blackAndWhite", true}, {"blackAndWhiteMix", *value}};
        } else if constexpr (std::is_same_v<T, PointColor>) {
            const auto value = GA::samplePointColor(input.sample);
            if (!value) { error(value.error()); return; }
            result.parameters = {{"lightness", value->lightness}, {"chroma", value->chroma}, {"hue", value->hue},
                {"hueShift", value->hueShift}, {"saturationShift", value->saturationShift}, {"luminanceShift", value->luminanceShift},
                {"variance", value->variance}, {"range", value->range}, {"hueRange", value->hueRange},
                {"saturationRange", value->saturationRange}, {"luminanceRange", value->luminanceRange}};
        } else if constexpr (std::is_same_v<T, LensScale>) {
            const auto value = GA::autoLensScale(*source, input.options);
            if (!value) { error(value.error()); return; }
            result.parameters = {{"scale", *value}};
        } else if constexpr (std::is_same_v<T, ChromaticAlignment>) {
            const auto value = GA::estimateChromaticAberration(*source);
            if (!value) { error(value.error()); return; }
            result.parameters = {{"redChromaticAberration", (*value)[0]}, {"blueChromaticAberration", (*value)[1]}, {"autoChromaticAberration", false}};
        }
    }, request);
    if (stop.stop_requested()) { result.cancelled = true; result.parameters.clear(); result.error.clear(); }
    return result;
}
} // namespace graphicHelper
