#include "NativeCanvasWorkspaceView.h"
#include "NativeCanvasWorkspace.generated.h"

std::function<void(double,double)> NativeCanvasWorkspaceBackend::sizeHandler(double maxPanel) {
    const std::weak_ptr<dreamscapes::canvas::WorkspaceModel> weak=model;
    return [weak,maxPanel](double w,double h){if (auto value=weak.lock()) value->sizeChanged(w,h,maxPanel);};
}
namespace dreamscapes::canvas {
ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<WorkspaceModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Canvas workspace requires a model");
    auto factories=lvui::native::BackendFactories{}.with<NativeCanvasWorkspaceBackend>([model]{return NativeCanvasWorkspaceBackend(model);});
    return lvui_generated::NativeCanvasWorkspace::build(builder,parent,[](auto &) {},nullptr,factories);
}
}
