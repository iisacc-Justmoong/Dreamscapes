#pragma once
#include "NativeTitleBar.h"

namespace dreamscapes::native {
[[nodiscard]] ii::core::Result<void> buildNativeTitleBar(ii::gui::WidgetBuilder &,ii::gui::Widget,
                                                         std::shared_ptr<NativeTitleBarModel>);
}
class NativeTitleBarBackend final {
  public:
    explicit NativeTitleBarBackend(std::shared_ptr<dreamscapes::native::NativeTitleBarModel> value)
        : model(std::move(value)), nativeControls(model->nativeControls),width(model->width),
          leadingWidth(model->leadingWidth),agentWidth(model->agentWidth),agentText(model->agentText) {}
    [[nodiscard]] std::function<void(double,double)> sizeHandler(double preferred,double inset,double trailing) {
        return [weak=std::weak_ptr(model),preferred,inset,trailing](double width,double) {
            if (auto value=weak.lock()) value->sizeChanged(width,preferred,inset,trailing);
        };
    }
    ii::core::Result<bool> drag(const ii::gui::WidgetInput &input) {return model->drag(input);}
    void editAgent(const std::string &text) {model->editAgent(text);}
    void account() {model->account();}
    std::shared_ptr<dreamscapes::native::NativeTitleBarModel> model;
    bool nativeControls;
    ii::core::Observable<double> &width,&leadingWidth,&agentWidth;
    ii::core::Observable<std::string> &agentText;
};
