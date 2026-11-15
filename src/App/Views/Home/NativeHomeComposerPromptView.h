#pragma once
#include "NativeHomeComposerPrompt.h"
#include <iiGUI/NativeWidgets.h>
#include <functional>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomeComposerPrompt(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
                                                             std::shared_ptr<HomeComposerPromptModel>);
}
class NativeHomeComposerPromptBackend final {
  public:
    using Model = dreamscapes::home::HomeComposerPromptModel;
    explicit NativeHomeComposerPromptBackend(std::shared_ptr<Model>);
    std::shared_ptr<Model> model;
    std::string placeholder, accessible, clearAccessible;
    ii::core::Observable<std::string> &text;
    ii::core::Observable<bool> &clearVisible, &enabled;
    ii::core::Observable<double> &viewportWidth, &viewportHeight, &editorWidth;
    [[nodiscard]] std::function<void(double, double)> sizeHandler(double gap, double buttonWidth) {
        return [weak = std::weak_ptr(model), gap, buttonWidth](double width, double height) {
            if (auto owner = weak.lock()) (void)owner->setViewport(width, height, gap, buttonWidth);
        };
    }
    [[nodiscard]] std::function<void(double)> measureHandler(double padding, double minimum) {
        return [weak = std::weak_ptr(model), padding, minimum](double content) {
            if (auto owner = weak.lock()) (void)owner->setMeasuredHeight(content, padding, minimum);
        };
    }
    void edited(std::string value) { (void)model->edit(std::move(value)); }
    void clear() { (void)model->clear(); }
    void submitted() { (void)model->submit(); }
};
