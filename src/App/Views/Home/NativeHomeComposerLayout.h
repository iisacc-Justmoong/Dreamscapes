#pragma once
#include <iiSDKCore>
#include <array>
#include <cstddef>

namespace dreamscapes::home {
struct HomeComposerRect final {
    double x = 0, y = 0, width = 0, height = 0;
    bool visible = true;
    bool operator==(const HomeComposerRect &) const = default;
};
struct HomeComposerLayoutSpec final {
    double wideBreakpoint, mediumBreakpoint, compactBreakpoint;
    std::array<double, 6> topPreferred{};
    std::array<double, 7> settingsPreferred{};
    bool compactControls = false;
    std::array<double, 2> videoPreferred{settingsPreferred[0], settingsPreferred[1]};
};
struct HomeComposerLayoutMetrics final {
    double gap, inset, controlHeight, settingHeight, attachmentHeight;
    bool operator==(const HomeComposerLayoutMetrics &) const = default;
};
struct HomeComposerLayout final {
    double contentWidth = 0, topHeight = 0, settingsHeight = 0, requestedHeight = 0;
    std::array<HomeComposerRect, 6> top;
    // In compact Home, top slot 2 is the inline negative prompt.
    // Settings slots 9 and 10 hold the attachment and Generate actions.
    std::array<HomeComposerRect, 11> settings;
    bool operator==(const HomeComposerLayout &) const = default;
};
// Geometry only: sources and widgets remain mounted and receive the returned frames.
[[nodiscard]] ii::core::Result<HomeComposerLayout> calculateHomeComposerLayout(
    double width, double promptHeight, bool video, bool attachments, double errorHeight,
    const HomeComposerLayoutSpec &, const HomeComposerLayoutMetrics &, double negativePromptHeight = 0);
} // namespace dreamscapes::home
