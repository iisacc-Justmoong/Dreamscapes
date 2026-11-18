#pragma once
#include "NativeSelectionTool.h"
#include <iiGUI>
namespace dreamscapes::editor {
std::shared_ptr<ii::gui::NativeViewSource> selectionSurfaceSource(std::shared_ptr<SelectionToolModel>);
}
