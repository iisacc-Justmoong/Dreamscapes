#include "NativeHomeSelectedFileView.h"
#include "NativeHomeSelectedFile.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeSelectedFile(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
    std::shared_ptr<HomeSelectedFileModel> model,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images) {
    if (!model || !model->active())
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Selected file requires an active model");
    auto factories = lvui::native::BackendFactories{}.with<NativeHomeSelectedFileBackend>([model] {
        return NativeHomeSelectedFileBackend(model);
    });
    return lvui_generated::NativeHomeSelectedFile::build(widgets, parent, [images = std::move(images)](auto &scope) {
        (void)scope.image_services->resolver.set(images);
    }, nullptr, factories);
}
} // namespace dreamscapes::home
