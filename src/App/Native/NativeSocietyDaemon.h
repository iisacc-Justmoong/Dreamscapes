#pragma once
#include "NativeSocietyStorage.h"

namespace dreamscapes::native {
/// Creates the desktop Society data source. Each worker operation directly
/// opens the authenticated local broker. An explicit root is a selection
/// constraint and cannot substitute for a drive selected by the daemon.
[[nodiscard]] SocietyStorageAccess societyDaemonStorage(
    std::filesystem::path broker = {}, std::filesystem::path expectedRoot = {});
}
