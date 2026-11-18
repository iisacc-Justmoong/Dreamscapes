#pragma once
#include "NativeFilePanel.h"
#include <iiGUI>

namespace dreamscapes::editor {
[[nodiscard]] ii::core::Result<void> buildFilePanel(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<FilePanelModel>);
[[nodiscard]] std::shared_ptr<ii::gui::NativeViewSource> filePanelSource(std::shared_ptr<FilePanelModel>);
}
class NativeFilePanelBackend final {
  public:
    explicit NativeFilePanelBackend(std::shared_ptr<dreamscapes::editor::FilePanelModel> value)
        : model(std::move(value)),rows(model->rows),informationMode(model->informationMode),contentsMode(model->contentsMode),
          storageMode(model->storageMode),viewTitle(model->viewTitle),width(model->width),height(model->height),
          contentWidth(model->contentWidth),valueWidth(model->valueWidth) {}
    [[nodiscard]] std::function<void(double,double)> sizeHandler(double);
    void showInformation() {(void)model->select("information");}
    void showContents() {(void)model->select("contents");}
    void showStorage() {(void)model->select("storage");}
    std::shared_ptr<dreamscapes::editor::FilePanelModel> model;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::editor::FilePanelRow>>> &rows;
    ii::core::Observable<bool> &informationMode,&contentsMode,&storageMode;
    ii::core::Observable<std::string> &viewTitle;
    ii::core::Observable<double> &width,&height,&contentWidth,&valueWidth;
};
