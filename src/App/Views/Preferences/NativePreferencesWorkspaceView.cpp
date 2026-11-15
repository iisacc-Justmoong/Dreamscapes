#include "NativePreferencesWorkspaceView.h"
#include "NativePreferencesWorkspace.generated.h"

std::function<void(double,double)> NativePreferencesWorkspaceBackend::sizeHandler(double maxPanel) {
    const std::weak_ptr<dreamscapes::preferences::WorkspaceModel> weak=model;
    return [weak,maxPanel](double w,double h){if (auto value=weak.lock()) value->sizeChanged(w,h,maxPanel);};
}
namespace dreamscapes::preferences {
ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<WorkspaceModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Preferences workspace requires a model");
    auto factories=lvui::native::BackendFactories{}.with<NativePreferencesWorkspaceBackend>([model]{return NativePreferencesWorkspaceBackend(model);});
    return lvui_generated::NativePreferencesWorkspace::build(builder,parent,[](auto &) {},nullptr,factories);
}
}
