#include "NativeStorageWorkspaceView.h"
#include "NativeStorageWorkspace.generated.h"

namespace dreamscapes::storage {
ii::core::Result<void> buildWorkspace(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::shared_ptr<WorkspaceModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Storage workspace requires a model");
    auto factories=lvui::native::BackendFactories{}.with<NativeStorageWorkspaceBackend>([model]{return NativeStorageWorkspaceBackend(model);});
    return lvui_generated::NativeStorageWorkspace::build(builder,parent,[](auto &) {},nullptr,factories);
}
}
