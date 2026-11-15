#pragma once

#include "NativeHomeQueue.h"
#include <iiGUI/NativeWidgets.h>
#include <memory>
#include <string>

namespace dreamscapes::home {
// The caller provides the exact Figma SVG from the product resource store.
// The view only decodes the supplied bytes and never reads or writes files.
[[nodiscard]] ii::core::Result<void> buildHomeQueue(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<HomeQueueModel> model, std::string readyIndicatorSvg);
} // namespace dreamscapes::home
