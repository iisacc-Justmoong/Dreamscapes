#pragma once
#include "NativePreferencesWorkspace.h"
#include <iiGUI>

namespace dreamscapes::preferences {
[[nodiscard]] ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &,ii::gui::Widget,std::shared_ptr<WorkspaceModel>);
}
class NativePreferencesWorkspaceBackend final {
  public:
    explicit NativePreferencesWorkspaceBackend(std::shared_ptr<dreamscapes::preferences::WorkspaceModel> value)
        : model(std::move(value)),categories(model->categories),fields(model->fields),category(model->category),
          status(model->status),width(model->width),height(model->height),sidebarWidth(model->sidebarWidth),detailWidth(model->detailWidth) {}
    std::function<void(double,double)> sizeHandler(double);
    std::shared_ptr<dreamscapes::preferences::WorkspaceModel> model;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::preferences::Category>>> &categories;
    ii::core::Observable<std::vector<std::shared_ptr<dreamscapes::preferences::Field>>> &fields;
    ii::core::Observable<std::string> &category,&status;
    ii::core::Observable<double> &width,&height,&sidebarWidth,&detailWidth;
};
