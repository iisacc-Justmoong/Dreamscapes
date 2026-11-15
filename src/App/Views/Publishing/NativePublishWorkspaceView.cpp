#include "NativePublishWorkspaceView.h"
#include "NativePublishWorkspace.generated.h"

std::function<void(double,double)> NativePublishWorkspaceBackend::sizeHandler(double maximumPanel) {
    const std::weak_ptr<dreamscapes::publishing::WorkspaceModel> weak=model;
    return [weak,maximumPanel](double width,double height) {if (auto value=weak.lock()) value->sizeChanged(width,height,maximumPanel);};
}
namespace dreamscapes::publishing {
ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<WorkspaceModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Publish workspace requires a model");
    auto factories=lvui::native::BackendFactories{}.with<NativePublishWorkspaceBackend>([model]{return NativePublishWorkspaceBackend(model);});
    return lvui_generated::NativePublishWorkspace::build(builder,parent,[](auto &) {},nullptr,factories);
}
}
