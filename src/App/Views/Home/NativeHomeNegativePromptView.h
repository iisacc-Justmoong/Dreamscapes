#pragma once
#include "NativeHomeNegativePrompt.h"
#include "NativeHomeComposerPopups.h"
#include <iiGUI>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomeNegativePrompt(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<HomeNegativePromptModel>, std::shared_ptr<HomeComposerPopups> = {});
}
class NativeHomeNegativePromptBackend final {
  public:
    using Model = dreamscapes::home::HomeNegativePromptModel;
    NativeHomeNegativePromptBackend(std::shared_ptr<Model>, std::shared_ptr<dreamscapes::home::HomeComposerPopups>);
    std::shared_ptr<Model> model;
    std::shared_ptr<ii::gui::NativeViewSource> promptSource;
    std::string heading, doneLabel;
    ii::core::Observable<bool> &enabled;
    ii::core::Observable<double> &viewportWidth;
    void sizeChanged(double width, double height) { (void)model->setViewport(width, height); }
    void attach(ii::gui::Widget popup, ii::gui::Widget anchor) { popup_ = std::move(popup); anchor_ = std::move(anchor); }
    void open();
    void done();
  private:
    std::shared_ptr<dreamscapes::home::HomeComposerPopups> popups_;
    ii::gui::Widget popup_, anchor_;
};
