#pragma once
#include "NativeHomeComposerChoice.h"
#include <iiGUI/NativeWidgets.h>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomeComposerChoice(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
                                                             std::shared_ptr<HomeComposerChoiceModel>);
}
class NativeHomeComposerChoiceBackend final {
  public:
    using Model = dreamscapes::home::HomeComposerChoiceModel;
    explicit NativeHomeComposerChoiceBackend(std::shared_ptr<Model>);
    std::shared_ptr<Model> model;
    std::string heading;
    bool showHeading = true;
    ii::core::Observable<std::string> &value;
    ii::core::Observable<std::vector<dreamscapes::home::HomeComposerChoiceRow>> &choices;
    ii::core::Observable<bool> &enabled;
    ii::core::Observable<double> &viewportWidth;
    void sizeChanged(double width, double height) { (void)model->setViewport(width, height); }
    void choose(std::string key) { (void)model->choose(std::move(key)); }
    void attachMenu(ii::gui::Widget menu, ii::gui::Widget anchor) { menu_ = std::move(menu); anchor_ = std::move(anchor); }
    void openMenu();
  private:
    ii::gui::Widget menu_, anchor_;
};
