#include "NativeHomeAttachmentsView.h"
#include "NativeHomeAttachments.generated.h"

namespace dreamscapes::home {
ii::core::Result<void> buildHomeAttachments(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
        std::shared_ptr<HomeAttachmentsModel> model,
        std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home attachments require an active model");
    auto host = widgets.content_host(parent, "homeAttachmentsHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([model = std::move(model), images = std::move(images)](ii::gui::WidgetBuilder &builder) {
        using View = lvui_generated::NativeHomeAttachments;
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeAttachmentsBackend>([model] {
            return NativeHomeAttachmentsBackend(model);
        });
        return View::build(builder, {}, [&](auto &owner) { (void)owner.image_services->resolver.set(images); }, nullptr, factories);
    });
}
}
NativeHomeAttachmentsBackend::NativeHomeAttachmentsBackend(std::shared_ptr<Model> source)
    : model(std::move(source)), rows(model->rows), visible(model->visible), enabled(model->enabled), viewportWidth(model->viewportWidth) {}
