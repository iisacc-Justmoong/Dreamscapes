#pragma once
#include "NativeTimelinePanel.h"
#include <iiGUI>

namespace dreamscapes::editor {
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> timelinePanelSource(std::shared_ptr<TimelinePanelModel>);
}
