#pragma once
#include "NativeLaunchLicense.h"
#include <functional>
#include <memory>
#include <string>

namespace dreamscapes::native {
// Device lookup is dispatched to the supplied worker. The SDK session and its
// verified decision are owned by the supplied event loop.
[[nodiscard]] std::shared_ptr<LicenseProbe> societyLicenseProbe(ii::runtime::EventLoop &,
    std::function<bool(ii::runtime::Task)> worker,std::string applicationVersion);
}
