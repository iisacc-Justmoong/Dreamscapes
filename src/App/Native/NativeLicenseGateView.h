#pragma once
#include "NativeLaunchLicense.h"
#include "NativeLicenseBridge.h"
#include <iiGUI/NativeWidgets.h>
#include <memory>

namespace dreamscapes::native {
[[nodiscard]] ii::core::Result<std::shared_ptr<NativeLicenseBridge>> buildLicenseGate(
    ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<LaunchLicense>);
}
