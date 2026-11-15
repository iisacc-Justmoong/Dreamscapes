#pragma once

#include <QRectF>
#include <functional>

namespace dreamscapes::native {
// Window-system transport used only while native panels share a Qt host.
class NativePresentationSurface {
  public:
    struct Callbacks final {
        std::function<void()> ready;
        std::function<void()> failed;
        std::function<void()> closed;
    };
    virtual ~NativePresentationSurface() = default;
    virtual bool start(QRectF) = 0;
    virtual bool show(QRectF) = 0;
    virtual void hide() = 0;
    virtual void close() = 0;
    virtual void restoreHostFocus() = 0;
};
} // namespace dreamscapes::native
