#pragma once
#include "NativeHomeWorkspace.h"

namespace dreamscapes::home {
enum class HomeWorkspacePair { files, session };
// iiGUI currently lacks an LVUI responsive grid. This retained layout only
// positions two LVUI sources; it does not draw controls or own product data.
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> homeWorkspacePairSource(
    std::weak_ptr<HomeWorkspaceModel>, HomeWorkspacePair);
}
