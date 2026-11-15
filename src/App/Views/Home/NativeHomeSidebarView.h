#pragma once

#include "NativeHomeSidebar.h"
#include <iiGUI/NativeWidgets.h>
#include <memory>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomeSidebar(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<HomeSidebarModel>);
}
class NativeHomeSidebarBackend final {
  public:
    using Model = dreamscapes::home::HomeSidebarModel;
    std::shared_ptr<Model> model;
    ii::core::Observable<std::vector<dreamscapes::home::HomeSidebarRow>> &rows;
    ii::core::Observable<bool> &compact;
    ii::core::Observable<double> &viewportWidth, &viewportHeight;
    NativeHomeSidebarBackend();
    explicit NativeHomeSidebarBackend(std::shared_ptr<Model>);
    void sizeChanged(double width, double height) { model->sizeChanged(width, height); }
    void activate(std::string key) { (void)model->activate(key); }
};
