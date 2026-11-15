#pragma once

#include "NativeHomeSelectedFile.h"
#include <LVUI/NativeImageResolver.h>
#include <iiGUI/Image.h>
#include <iiGUI/NativeWidgets.h>
#include <memory>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomeSelectedFile(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<HomeSelectedFileModel>,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images = {});
}
