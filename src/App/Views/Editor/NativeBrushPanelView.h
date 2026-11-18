#pragma once
#include "NativeBrushTool.h"
#include <LVUI>
#include <iiGUI>
#include <string>

namespace dreamscapes::editor {
struct BrushControlSpec final {std::string label;double minimum=0,maximum=0,step=1,initial=0;};
struct BrushPanelSpec final {BrushControlSpec size,hardness,opacity,flow;};
struct BrushPanelLayout final {
    ii::core::Observable<double> width{lvui::SliderConfiguration{}.width},height{1},contentWidth{lvui::SliderConfiguration{}.width};
};
[[nodiscard]] BrushPanelSpec brushPanelSpec();
[[nodiscard]] ii::core::Result<void> buildBrushPanel(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<BrushToolModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> brushPanelSource(std::shared_ptr<BrushToolModel>);
}
class NativeBrushPanelBackend final {
  public:
    NativeBrushPanelBackend(std::shared_ptr<dreamscapes::editor::BrushToolModel>,
                            std::shared_ptr<dreamscapes::editor::BrushPanelLayout>);
    std::function<void(double,double)> sizeHandler(double inset);
    void sizeMoved(double value) {model->sizeMoved(value);}
    void hardnessMoved(double value) {model->hardnessMoved(value);}
    void opacityMoved(double value) {model->opacityMoved(value);}
    void flowMoved(double value) {model->flowMoved(value);}
    void colorEdited(std::string value) {model->colorEdited(std::move(value));}
    void colorSubmitted() {model->colorSubmitted();}
    std::shared_ptr<dreamscapes::editor::BrushToolModel> model;
    std::shared_ptr<dreamscapes::editor::BrushPanelLayout> layout;
    ii::core::Observable<double> &panelWidth,&panelHeight,&contentWidth,&size,&hardness,&opacity,&flow;
    ii::core::Observable<std::string> &colorText,&status,&availability;
    const dreamscapes::editor::BrushPanelSpec spec;
    const std::string sizeLabel,hardnessLabel,opacityLabel,flowLabel;
    const double sizeMin,sizeMax,sizeStep,hardnessMin,hardnessMax,hardnessStep,opacityMin,opacityMax,opacityStep,flowMin,flowMax,flowStep;
};
