#pragma once
#include "NativeHomeComposerSeed.h"
#include "NativeHomeComposerPopups.h"
#include <iiGUI>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomeComposerSeed(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<HomeComposerSeedModel>, std::shared_ptr<HomeComposerPopups> = {});
}
class NativeHomeComposerSeedBackend final {
  public:
    using Model = dreamscapes::home::HomeComposerSeedModel;
    NativeHomeComposerSeedBackend(std::shared_ptr<Model>, std::shared_ptr<dreamscapes::home::HomeComposerPopups>);
    std::shared_ptr<Model> model;
    std::string heading, randomLabel, applyLabel;
    bool showHeading;
    ii::core::Observable<std::string> &value, &draft;
    ii::core::Observable<bool> &enabled;
    ii::core::Observable<double> &viewportWidth;
    void sizeChanged(double width, double height) { (void)model->setViewport(width, height); }
    void edited(std::string text) { (void)model->edit(std::move(text)); }
    void attach(ii::gui::Widget popup, ii::gui::Widget anchor) { popup_ = std::move(popup); anchor_ = std::move(anchor); }
    void open();
    void apply();
    void randomize();
  private:
    std::shared_ptr<dreamscapes::home::HomeComposerPopups> popups_;
    ii::gui::Widget popup_, anchor_;
};
