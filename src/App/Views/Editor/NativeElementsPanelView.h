#pragma once
#include "NativeElementsPanel.h"
#include <iiGUI>
#include <functional>
#include <utility>

namespace dreamscapes::editor {
[[nodiscard]] ii::core::Result<void> buildElementsPanel(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<ElementsPanelModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> elementsPanelSource(std::shared_ptr<ElementsPanelModel>);
}
class NativeElementsPanelBackend final {
  public:
    explicit NativeElementsPanelBackend(std::shared_ptr<dreamscapes::editor::ElementsPanelModel> value)
        : model(std::move(value)),shape(model->shapeTool()),editable(model->editable),constrained(model->constrained),fillEditable(model->fillEditable),
          rectangle(shape->rectangle),ellipse(shape->ellipse),polygon(shape->polygon),line(shape->line),shapeStatus(shape->status),width(model->width),height(model->height),
          contentWidth(model->contentWidth),fieldWidth(model->fieldWidth),widthText(model->widthText),heightText(model->heightText),
          fillText(model->fillText),selectedName(model->selectedName),status(model->status) {}
    [[nodiscard]] std::function<void(double,double)> sizeHandler(double inset,double gap);
    void editWidth(std::string value) {(void)model->editWidth(std::move(value));}
    void editHeight(std::string value) {(void)model->editHeight(std::move(value));}
    void setConstrained(bool value) {(void)model->setConstrained(value);}
    void apply() {(void)model->apply();}
    void editFill(std::string value) {(void)model->editFill(std::move(value));}
    void applyFill() {(void)model->applyFill();}
    void chooseRectangle() {(void)shape->choose(dreamscapes::editor::ShapeKind::rectangle);}
    void chooseEllipse() {(void)shape->choose(dreamscapes::editor::ShapeKind::ellipse);}
    void choosePolygon() {(void)shape->choose(dreamscapes::editor::ShapeKind::polygon);}
    void chooseLine() {(void)shape->choose(dreamscapes::editor::ShapeKind::line);}
    std::shared_ptr<dreamscapes::editor::ElementsPanelModel> model;
    std::shared_ptr<dreamscapes::editor::ShapeToolModel> shape;
    ii::core::Observable<bool> &editable,&constrained,&fillEditable;
    ii::core::Observable<bool> &rectangle,&ellipse,&polygon,&line;
    ii::core::Observable<std::string> &shapeStatus;
    ii::core::Observable<double> &width,&height,&contentWidth,&fieldWidth;
    ii::core::Observable<std::string> &widthText,&heightText,&fillText,&selectedName,&status;
};
