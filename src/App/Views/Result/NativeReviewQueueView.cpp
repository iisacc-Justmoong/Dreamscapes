#include "NativeReviewQueueView.h"
#include "NativeReviewQueue.generated.h"

namespace dreamscapes::review {
ii::core::Result<void> buildReviewQueue(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
    std::shared_ptr<QueueModel> model,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images) {
    if (!model || !model->onOwnerThread() || !model->active())
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "A native review queue requires an active owner-thread model");
    auto factories = lvui::native::BackendFactories{}.with<NativeReviewQueueBackend>([model] {
        return NativeReviewQueueBackend(model);
    });
    return lvui_generated::NativeReviewQueue::build(widgets, parent, [images = std::move(images)](auto &scope) {
        (void)scope.image_services->resolver.set(images);
    }, nullptr, factories);
}
} // namespace dreamscapes::review
