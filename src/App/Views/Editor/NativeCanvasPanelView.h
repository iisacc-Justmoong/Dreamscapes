#pragma once
#include "NativeCanvasPanel.h"
#include <iiGUI/NativeView.h>
#include <iiGUI/NativeWidgets.h>

namespace dreamscapes::editor {
[[nodiscard]] ii::core::Result<void> buildCanvasPanel(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<CanvasPanelModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> canvasPanelSource(std::shared_ptr<CanvasPanelModel>);
}
class NativeCanvasPanelBackend final {
  public:
    explicit NativeCanvasPanelBackend(std::shared_ptr<dreamscapes::editor::CanvasPanelModel> value)
        : model(std::move(value)),width(model->width),height(model->height),contentWidth(model->contentWidth),
          fieldWidth(model->fieldWidth),angle(model->angle),widthText(model->widthText),heightText(model->heightText),status(model->status),angleText(model->angleText),editable(model->editable),
          sizeMode(model->sizeMode),rotateMode(model->rotateMode),mirrorMode(model->mirrorMode),horizontal(model->horizontal),vertical(model->vertical),
          left(model->left),half(model->half),right(model->right) {}
    std::function<void(double,double)> sizeHandler(double inset);
    void editWidth(std::string value) {(void)model->editWidth(std::move(value));}
    void editHeight(std::string value) {(void)model->editHeight(std::move(value));}
    void apply() {(void)model->apply();}
    void reset() {(void)model->reset();}
    void showSize() {(void)model->showSize();}
    void showRotate() {(void)model->showRotate();}
    void showMirror() {(void)model->showMirror();}
    void chooseLeft() {(void)model->chooseLeft();}
    void chooseHalf() {(void)model->chooseHalf();}
    void chooseRight() {(void)model->chooseRight();}
    void editAngle(std::string value) {(void)model->editAngle(std::move(value));}
    void commitAngle() {(void)model->commitAngle();}
    void moveAngle(double value) {(void)model->moveAngle(value);}
    void applyRotate() {(void)model->applyRotate();}
    void chooseHorizontal() {(void)model->chooseHorizontal();}
    void chooseVertical() {(void)model->chooseVertical();}
    void applyMirror() {(void)model->applyMirror();}
    std::shared_ptr<dreamscapes::editor::CanvasPanelModel> model;
    ii::core::Observable<double> &width,&height,&contentWidth,&fieldWidth,&angle;
    ii::core::Observable<std::string> &widthText,&heightText,&status,&angleText;
    ii::core::Observable<bool> &editable,&sizeMode,&rotateMode,&mirrorMode,&horizontal,&vertical,&left,&half,&right;
};
