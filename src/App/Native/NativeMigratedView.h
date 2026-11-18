#pragma once
#include <iiGUI>
#include <iiSDKCore>
#include <memory>
#include <string_view>
#include <vector>

namespace dreamscapes::native {
// Presentation-only state for the simplified LVUI replacements.
class MigratedViewModel final {
  public:
    void sizeChanged(double width,double height);
    ii::core::Observable<double> viewportWidth{1},viewportHeight{1};
};
[[nodiscard]] ii::core::Result<void> buildMigratedView(ii::gui::WidgetBuilder &,ii::gui::Widget,
    std::string_view,std::shared_ptr<MigratedViewModel>);
struct MigratedViewDescriptor final {std::string_view source,replacement;};
[[nodiscard]] const std::vector<MigratedViewDescriptor> &migratedViews();
}
class NativeMigratedViewBackend final {
  public:
    explicit NativeMigratedViewBackend(std::shared_ptr<dreamscapes::native::MigratedViewModel> value)
        : model(std::move(value)),viewportWidth(model->viewportWidth),viewportHeight(model->viewportHeight) {}
    void sizeChanged(double width,double height) {model->sizeChanged(width,height);}
    std::shared_ptr<dreamscapes::native::MigratedViewModel> model;
    ii::core::Observable<double> &viewportWidth,&viewportHeight;
};
