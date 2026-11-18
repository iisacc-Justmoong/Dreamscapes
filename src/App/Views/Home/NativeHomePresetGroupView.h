#pragma once
#include "NativeHomePresetGroup.h"
#include <iiGUI>
#include <functional>

namespace dreamscapes::home {
[[nodiscard]] ii::core::Result<void> buildHomePresetGroup(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
                                                         std::shared_ptr<HomePresetGroupModel>);
}
class NativeHomePresetGroupBackend final {
  public:
    using Model = dreamscapes::home::HomePresetGroupModel;
    explicit NativeHomePresetGroupBackend(std::shared_ptr<Model>);
    std::shared_ptr<Model> model;
    std::string heading;
    std::shared_ptr<ii::gui::NativeViewSource> choice, save;
    ii::core::Observable<double> &viewportWidth, &choiceWidth, &saveWidth;
    [[nodiscard]] std::function<void(double,double)> sizeHandler(double gap, double save) {
        return [weak = std::weak_ptr(model), gap, save](double width, double height) {
            if (auto owner = weak.lock()) (void)owner->setViewport(width,height,gap,save);
        };
    }
};
