#include "NativeGenerationWorkspaceView.h"
#include "NativeGenerationWorkspace.generated.h"

std::function<void(double,double)> NativeGenerationWorkspaceBackend::sizeHandler(double panel,double preview) {
    const std::weak_ptr<Model> weak=model;
    return [weak,panel,preview](double width,double height) {
        if (auto owner=weak.lock()) owner->sizeChanged(width,height,panel,preview);
    };
}
namespace dreamscapes::generation {
ii::core::Result<void> buildGenerationWorkspace(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,
        std::shared_ptr<GenerationWorkspaceModel> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Generation view requires its model");
    auto factories=lvui::native::BackendFactories{}.with<NativeGenerationWorkspaceBackend>([model] {return NativeGenerationWorkspaceBackend(model);});
    return lvui_generated::NativeGenerationWorkspace::build(builder,parent,[](auto &) {},nullptr,factories);
}
}
