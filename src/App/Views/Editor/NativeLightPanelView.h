#pragma once
#include "NativeLightPanel.h"
#include <iiGUI>

namespace dreamscapes::editor {
[[nodiscard]] ii::core::Result<void> buildLightPanel(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<LightPanelModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> lightPanelSource(std::shared_ptr<LightPanelModel>);
}
class NativeLightPanelBackend final {
  public:
    explicit NativeLightPanelBackend(std::shared_ptr<dreamscapes::editor::LightPanelModel> value)
        :model(std::move(value)),fields(model->fields),available(model->available),busy(model->busy),
         canPreview(model->canPreview),canApply(model->canApply),status(model->status),availability(model->availability),
         layerName(model->layerName),width(model->width),height(model->height),contentWidth(model->contentWidth),
         sliderWidth(model->sliderWidth),listHeight(model->listHeight) {}
    std::function<void(double,double)> sizeHandler(double inset,double valueWidth,double rowHeight,double rowPadding);
    void preview() {(void)model->requestPreview();}
    void autoLight() {(void)model->requestAuto();}
    void apply() {(void)model->apply();}
    void reset() {model->reset();}
    std::shared_ptr<dreamscapes::editor::LightPanelModel> model;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::editor::LightField>>> &fields;
    ii::core::Observable<bool> &available,&busy,&canPreview,&canApply;
    ii::core::Observable<std::string> &status,&availability,&layerName;
    ii::core::Observable<double> &width,&height,&contentWidth,&sliderWidth,&listHeight;
};
