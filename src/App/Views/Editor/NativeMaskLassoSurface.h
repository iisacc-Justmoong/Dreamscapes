#pragma once
#include "NativeMaskLasso.h"
#include <iiGUI>
namespace dreamscapes::editor {
std::shared_ptr<ii::gui::NativeViewSource> maskLassoSurfaceSource(std::shared_ptr<MaskLassoModel>);
}
