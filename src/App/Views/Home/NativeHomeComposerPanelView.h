#pragma once
#include "NativeHomeComposerPanel.h"
#include <iiGUI>
#include <functional>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomeComposerPanel(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
                                                           std::shared_ptr<HomeComposerPanelModel>);
}
class NativeHomeComposerPanelBackend final {
  public:
    using Model = dreamscapes::home::HomeComposerPanelModel;
    explicit NativeHomeComposerPanelBackend(std::shared_ptr<Model>);
    std::shared_ptr<Model> model;
    std::shared_ptr<ii::gui::NativeViewSource> topGrid, settingsGrid, attachments;
    ii::core::Observable<double> &viewportWidth, &contentWidth, &topHeight, &settingsHeight, &errorHeight;
    ii::core::Observable<bool> &hasAttachments, &hasError;
    ii::core::Observable<std::string> &error;
    [[nodiscard]] std::function<void(double,double)> sizeHandler(double gap, double inset, double control, double setting, double attachment) {
        return [weak = std::weak_ptr(model), metrics = dreamscapes::home::HomeComposerLayoutMetrics{gap,inset,control,setting,attachment}]
                (double width, double height) { if (auto owner = weak.lock()) (void)owner->setViewport(width,height,metrics); };
    }
    void errorSizeChanged(double, double height) { (void)model->setErrorHeight(height); }
};
