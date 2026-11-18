#pragma once
#include "NativeShapeTool.h"
#include <iiGUI>

namespace dreamscapes::editor {
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> shapeSurfaceSource(std::shared_ptr<ShapeToolModel>);
}
