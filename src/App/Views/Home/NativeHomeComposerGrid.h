#pragma once
#include "NativeHomeComposerPanel.h"
#include <memory>

namespace dreamscapes::home {
enum class HomeComposerGrid { top, settings };
// Mounts LVUI control sources once and moves their native hosts on layout changes.
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> homeComposerGridSource(
    std::weak_ptr<HomeComposerPanelModel>, HomeComposerGrid);
} // namespace dreamscapes::home
