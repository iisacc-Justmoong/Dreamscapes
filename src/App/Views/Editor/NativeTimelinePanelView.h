#pragma once
#include "NativeTimelinePanel.h"
#include <iiGUI/NativeView.h>

namespace dreamscapes::editor {
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> timelinePanelSource(std::shared_ptr<TimelinePanelModel>);
}
