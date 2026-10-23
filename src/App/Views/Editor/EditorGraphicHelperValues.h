#pragma once

#include <iiGraphicAdjust/DevelopColor.h>
#include <iiGraphicAdjust/DevelopTone.h>
#include <iiGraphicAdjust/LensFilter.h>
#include <array>
#include <map>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>

// Detached parameter calculations. No Qt, image output, UI owner or storage.
namespace graphicHelper {
struct AutoLight {};
struct AutoWhite { iiGraphicAdjust::WhiteBalanceOptions source; };
struct SampleWhite { iiGraphicAdjust::Rgba sample; iiGraphicAdjust::WhiteBalanceOptions source; };
struct WhitePreset { iiGraphicAdjust::WhiteBalancePreset preset; };
struct AutoMix {};
struct PointColor { iiGraphicAdjust::Rgba sample; };
struct LensScale { iiGraphicAdjust::LensCorrectionOptions options; };
struct ChromaticAlignment {};
using Request = std::variant<AutoLight, AutoWhite, SampleWhite, WhitePreset, AutoMix, PointColor, LensScale, ChromaticAlignment>;
using Parameter = std::variant<double, bool, std::array<double, 8>>;
struct Result {
    std::map<std::string, Parameter> parameters;
    std::string error;
    bool cancelled = false;
    bool ok() const { return !cancelled && error.empty() && !parameters.empty(); }
};
std::string_view operation(const Request &request) noexcept;
bool needsImage(const Request &request) noexcept;
Result calculate(const Request &request, const iiGraphicAdjust::ImageView *source = nullptr, std::stop_token stop = {});
} // namespace graphicHelper
