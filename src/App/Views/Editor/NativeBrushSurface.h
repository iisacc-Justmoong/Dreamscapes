#pragma once
#include "NativeBrushTool.h"
#include <iiGUI>
#include <memory>
namespace dreamscapes::editor {
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> brushSurfaceSource(std::shared_ptr<BrushToolModel>);
}
