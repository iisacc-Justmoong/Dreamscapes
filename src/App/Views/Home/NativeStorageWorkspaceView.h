#pragma once
#include "NativeStorageWorkspace.h"
#include <iiGUI>

namespace dreamscapes::storage {
[[nodiscard]] ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<WorkspaceModel>);
}
class NativeStorageWorkspaceBackend final {
  public:
    explicit NativeStorageWorkspaceBackend(std::shared_ptr<dreamscapes::storage::WorkspaceModel> value)
        : model(std::move(value)),rows(model->rows),canOpen(model->canOpen),canUp(model->canUp),
          heading(model->heading),status(model->status),query(model->query),directory(model->directory),
          width(model->width),height(model->height) {}
    void sizeChanged(double w,double h) {model->sizeChanged(w,h);}
    void search(std::string value) {(void)model->setQuery(std::move(value));}
    void up() {(void)model->up();}
    void open() {(void)model->openSelected();}
    std::shared_ptr<dreamscapes::storage::WorkspaceModel> model;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::storage::Row>>> &rows;
    ii::core::Observable<bool> &canOpen,&canUp;
    ii::core::Observable<std::string> &heading,&status,&query,&directory;
    ii::core::Observable<double> &width,&height;
};
