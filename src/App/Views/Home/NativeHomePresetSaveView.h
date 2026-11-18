#pragma once
#include "NativeHomePresetSave.h"
#include "NativeHomeComposerPopups.h"
#include <iiGUI>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomePresetSave(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<HomePresetSaveModel>, std::shared_ptr<HomeComposerPopups> = {});
}
class NativeHomePresetSaveBackend final {
  public:
    using Model = dreamscapes::home::HomePresetSaveModel;
    NativeHomePresetSaveBackend(std::shared_ptr<Model>, std::shared_ptr<dreamscapes::home::HomeComposerPopups>);
    std::shared_ptr<Model> model;
    std::string heading, placeholder, saveLabel, cancelLabel;
    ii::core::Observable<std::string> &draft;
    ii::core::Observable<bool> &enabled, &canSave;
    void edited(std::string text) { (void)model->edit(std::move(text)); }
    void attach(ii::gui::Widget popup, ii::gui::Widget anchor) { popup_ = std::move(popup); anchor_ = std::move(anchor); }
    void open();
    void cancel();
    void save();
  private:
    std::shared_ptr<dreamscapes::home::HomeComposerPopups> popups_;
    ii::gui::Widget popup_, anchor_;
};
