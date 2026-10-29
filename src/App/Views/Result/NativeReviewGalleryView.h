#pragma once

#include "NativeReviewGallery.h"
#include <iiGUI/NativeWidgets.h>
#include <functional>

namespace dreamscapes::review {
// Called on the UI owner thread after the session and navigation have been
// connected. The product supplies real results, previews and action handlers.
using GalleryConfigure = std::function<void(const std::shared_ptr<ReviewGalleryModel> &)>;
[[nodiscard]] ii::core::Result<void> buildReviewGallery(ii::gui::WidgetBuilder &, ii::gui::Widget parent = {},
                                                        const GalleryConfigure &configure = {});
} // namespace dreamscapes::review
