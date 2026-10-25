#include "GenerationDraft.h"
#include <set>
#include <limits>
#include <sstream>
#include <charconv>

namespace dreamscapes::vm {
Value videoDefaults(const Value &prompts) {
    return Value(Object{{"prompt", Value(text(get(prompts, "prompt")))}, {"negativePrompt", Value(text(get(prompts, "negativePrompt")))}, {"width", Value(1024)}, {"height", Value(576)}, {"duration", Value(5)}, {"fps", Value(24)}, {"outputCount", Value(1)}, {"steps", Value(30)}, {"cfgScale", Value(3)}, {"seed", Value(-1)}, {"randomSeed", Value(true)}, {"interpolationFactor", Value(2)}, {"decodeTimestep", Value(.05)}, {"decodeNoiseScale", Value(.025)}, {"imageConditionNoise", Value(0)}, {"device", Value("auto")}, {"precision", Value("auto")}, {"offload", Value("auto")}, {"cpuTextEncoding", Value(true)}, {"vaeTiling", Value(true)}, {"crf", Value(18)}, {"encodingPreset", Value("medium")}, {"camera1", Value("none")}, {"camera2", Value("none")}, {"camera3", Value("none")}});
}
std::string aspectRatio(double width, double height) {
    for (const auto &[x, y] : {std::pair{1,1}, {4,3}, {3,4}, {16,9}, {9,16}}) if (width * y == height * x) return std::to_string(x) + ':' + std::to_string(y);
    return "Custom";
}
bool videoCameraValid(const Value &values) {
    std::set<std::string> motions;
    for (auto field : {"camera1", "camera2", "camera3"}) { const auto value = text(get(values, field), "none"); if (value != "none" && !motions.insert(value).second) return false; }
    if (motions.size() > 1 && motions.contains("static")) return false;
    for (const auto &[a, b] : {std::pair{"pan-left", "pan-right"}, {"tilt-up", "tilt-down"}, {"dolly-in", "dolly-out"}, {"orbit-left", "orbit-right"}, {"crane-up", "crane-down"}, {"zoom-in", "zoom-out"}}) if (motions.contains(a) && motions.contains(b)) return false;
    return true;
}
VideoEditPlan planVideoEdit(const Value &values, const Value &invalid, const VideoEdit &edit) {
    auto errors = object(invalid); auto next = object(values);
    if (edit.key == "aspectRatio") {
        static const std::map<std::string, std::pair<int,int>, std::less<>> sizes{{"1:1", {512,512}}, {"4:3", {640,480}}, {"3:4", {480,640}}, {"16:9", {1024,576}}, {"9:16", {576,1024}}};
        const auto found = sizes.find(text(edit.value)); if (found == sizes.end()) return {false, values, invalid};
        errors.erase("width"); errors.erase("height"); next["width"] = Value(found->second.first); next["height"] = Value(found->second.second); return {true, Value(next), Value(errors)};
    }
    static const std::map<std::string, std::pair<double,double>, std::less<>> ranges{{"width",{32,4096}}, {"height",{32,4096}}, {"duration",{1,30}}, {"fps",{12,120}}, {"outputCount",{1,1000}}, {"steps",{1,1000}}, {"cfgScale",{0,100}}, {"seed",{0,4294967295.0}}, {"interpolationFactor",{2,8}}, {"decodeTimestep",{0,1}}, {"decodeNoiseScale",{0,1}}, {"imageConditionNoise",{0,1}}, {"crf",{0,51}}};
    static const std::set<std::string, std::less<>> integers{"width", "height", "duration", "fps", "outputCount", "steps", "seed", "interpolationFactor", "crf"};
    const auto range = ranges.find(edit.key);
    if (range != ranges.end()) {
        const auto n = number(edit.value, std::numeric_limits<double>::quiet_NaN());
        const bool valid = std::isfinite(n) && n >= range->second.first && n <= range->second.second && (!integers.contains(edit.key) || std::floor(n) == n)
            && ((edit.key != "width" && edit.key != "height") || std::fmod(n, 32) == 0) && (edit.key != "fps" || n == 12 || n == 24 || n == 30 || n == 60 || n == 120);
        if (!valid) { errors[edit.key] = Value(true); return {false, values, Value(errors)}; }
    }
    errors.erase(edit.key);
    if (edit.key == "randomSeed") { if (boolean(edit.value) || number(get(values, "seed")) >= 0) errors.erase("seed"); else errors["seed"] = Value(true); }
    next[edit.key] = edit.value;
    return {true, Value(next), Value(errors), edit.key == "duration" || edit.key == "fps"};
}
void VideoDraft::restoreComposition(int frames, double fps) {
    if (!std::isfinite(fps) || fps <= 0 || frames <= 0) return;
    auto values = object(values_), invalid = object(invalid_); values["duration"] = Value(frames / fps); values["fps"] = Value(fps); invalid.erase("duration"); invalid.erase("fps"); values_ = Value(values); invalid_ = Value(invalid);
}
Value imageSize(std::string_view ratio, int shorter) {
    const auto at = ratio.find(':'); double x = 0, y = 0;
    if (at != std::string_view::npos) {
        auto a = std::from_chars(ratio.data(), ratio.data() + at, x), b = std::from_chars(ratio.data() + at + 1, ratio.data() + ratio.size(), y);
        if (a.ec != std::errc{} || a.ptr != ratio.data() + at || b.ec != std::errc{} || b.ptr != ratio.data() + ratio.size()) x = y = 0;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || x <= 0 || y <= 0 || shorter <= 0) return Value(Object{{"width", Value(1024)}, {"height", Value(1024)}});
    const auto scale = shorter / std::min(x, y);
    return Value(Object{{"width", Value(std::floor(x * scale / 8 + .5) * 8)}, {"height", Value(std::floor(y * scale / 8 + .5) * 8)}});
}
ComposerState::ComposerState() : edits_([this](const ComposerEdit &edit) {
    if (values_.contains(edit.key)) values_[edit.key] = edit.value;
    if (edit.key == "mediaType" || edit.key == "defaultPrompts" || edit.key == "promptDefaultsReady") applyDefaults();
    return presentation();
}) {
    values_ = {{"prompt", Value("")}, {"mediaType", Value("Image")}, {"aspectRatio", Value("1:1")}, {"generationCount", Value(1)}, {"videoDuration", Value(5)}, {"videoFps", Value(24)},
        {"shortSide", Value(1024)}, {"seed", Value(-1)}, {"negativePrompt", Value("")}, {"appliedNegativePrompt", Value("")}, {"appliedDefaultPrompt", Value("")}, {"promptDefaultsReady", Value(false)},
        {"loadedPreset", Value(false)}, {"selectedPresetName", Value("")}, {"errorText", Value("")}, {"defaultPrompts", Value(Object{})}, {"submitting", Value(false)}, {"generationPending", Value(false)}};
}
void ComposerState::applyDefaults() {
    const auto &defaults = get(values_.at("defaultPrompts"), text(values_.at("mediaType")));
    if (boolean(values_.at("promptDefaultsReady")) && values_.at("prompt") == values_.at("appliedDefaultPrompt")) {
        values_["appliedDefaultPrompt"] = Value(text(get(defaults, "prompt"))); values_["prompt"] = values_.at("appliedDefaultPrompt");
    }
    const auto next = Value(text(get(defaults, "negativePrompt")));
    if (values_.at("negativePrompt") == values_.at("appliedNegativePrompt")) values_["negativePrompt"] = next;
    values_["appliedNegativePrompt"] = next;
}
Value ComposerState::presentation() const {
    auto result = values_; const auto video = text(values_.at("mediaType")) == "Video";
    result["video"] = Value(video);
    auto size = imageSize(text(values_.at("aspectRatio")), static_cast<int>(number(values_.at("shortSide"))));
    if (video) {
        static const std::map<std::string, std::pair<int,int>, std::less<>> dimensions{{"1:1",{512,512}}, {"4:3",{640,480}}, {"3:4",{480,640}}, {"16:9",{1024,576}}, {"9:16",{576,1024}}};
        const auto found = dimensions.find(text(values_.at("aspectRatio"))); const auto pair = found == dimensions.end() ? std::pair{512,512} : found->second;
        size = Value(Object{{"width", Value(pair.first)}, {"height", Value(pair.second)}});
    }
    result["outputSize"] = size; result["usesExplicitParameters"] = Value(boolean(values_.at("loadedPreset")) || number(values_.at("shortSide")) != 1024 || values_.at("negativePrompt") != values_.at("appliedNegativePrompt"));
    return Value(result);
}
} // namespace dreamscapes::vm
