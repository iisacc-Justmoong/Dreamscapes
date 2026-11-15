#pragma once
#include "NativeCanvasWorkspace.h"
#include <iiGUI/NativeWidgets.h>

namespace dreamscapes::canvas {
[[nodiscard]] ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<WorkspaceModel>);
}
class NativeCanvasWorkspaceBackend final {
  public:
    explicit NativeCanvasWorkspaceBackend(std::shared_ptr<dreamscapes::canvas::WorkspaceModel> value)
        : model(std::move(value)),categories(model->categories),presets(model->presets),widthText(model->widthText),
          heightText(model->heightText),ppiText(model->ppiText),query(model->query),unit(model->unit),
          background(model->background),selectionName(model->selectionName),status(model->status),
          canCreate(model->canCreate),width(model->width),height(model->height),listWidth(model->listWidth),detailWidth(model->detailWidth) {}
    std::function<void(double,double)> sizeHandler(double);
    void search(std::string value) {(void)model->search(std::move(value));}
    void editWidth(std::string value) {(void)model->edit("draftWidth",std::move(value));}
    void editHeight(std::string value) {(void)model->edit("draftHeight",std::move(value));}
    void editPpi(std::string value) {(void)model->edit("resolution",std::move(value));}
    void pixels() {(void)model->changeUnit("px");}
    void millimeters() {(void)model->changeUnit("mm");}
    void inches() {(void)model->changeUnit("in");}
    void white() {(void)model->edit("canvasBackground","White");}
    void transparent() {(void)model->edit("canvasBackground","Transparent");}
    void black() {(void)model->edit("canvasBackground","Black");}
    void custom() {(void)model->customSize();}
    void createCanvas() {(void)model->createCanvas();}
    std::shared_ptr<dreamscapes::canvas::WorkspaceModel> model;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::canvas::Category>>> &categories;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::canvas::Preset>>> &presets;
    ii::core::Observable<std::string> &widthText,&heightText,&ppiText,&query,&unit,&background,&selectionName,&status;
    ii::core::Observable<bool> &canCreate;
    ii::core::Observable<double> &width,&height,&listWidth,&detailWidth;
};
