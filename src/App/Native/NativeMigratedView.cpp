#include "NativeMigratedView.h"
#include <cmath>

namespace dreamscapes::native {
void MigratedViewModel::sizeChanged(double width,double height) {
    if (!std::isfinite(width) || !std::isfinite(height) || width<0 || height<0 || width>8192 || height>8192) return;
    (void)viewportWidth.set(width);
    (void)viewportHeight.set(height);
}
}
