#pragma once
#include "NativeFailureDetails.h"
#include <iiGUI/NativeWidgets.h>

namespace dreamscapes::failure {
[[nodiscard]] ii::core::Result<void> buildFailureDetails(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<FailureDetailsModel> model);
}
