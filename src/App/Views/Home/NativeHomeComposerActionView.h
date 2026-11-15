#pragma once
#include "NativeHomeComposerAction.h"
#include <iiGUI/NativeWidgets.h>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomeComposerAction(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
                                                            std::shared_ptr<HomeComposerActionModel>);
}
class NativeHomeComposerActionBackend final {
  public:
    using Model = dreamscapes::home::HomeComposerActionModel;
    explicit NativeHomeComposerActionBackend(std::shared_ptr<Model>);
    std::shared_ptr<Model> model;
    bool icon, primary, showHeading;
    std::string heading, accessible;
    ii::core::Observable<std::string> &label;
    ii::core::Observable<bool> &enabled;
    ii::core::Observable<double> &viewportWidth;
    void sizeChanged(double width, double height) { (void)model->setViewport(width, height); }
    void invoke() { (void)model->invoke(); }
};
