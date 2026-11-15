#pragma once

#include "NativeReviewQueue.h"
#include <LVUI/NativeImageResolver.h>
#include <iiGUI/Image.h>
#include <iiGUI/NativeWidgets.h>
#include <memory>

namespace dreamscapes::review {
// The host supplies live-preview image I/O, keeping decoding and files out of
// the view. A null resolver leaves unavailable previews empty.
ii::core::Result<void> buildReviewQueue(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
    std::shared_ptr<QueueModel> model,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images = {});
}
