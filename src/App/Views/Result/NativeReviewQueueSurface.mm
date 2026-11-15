#include "NativeReviewQueueSurface.h"
#include "NativeReviewImages.h"
#include "NativeReviewQueueView.h"
#include "App/Native/NativeEmbeddedSurface.h"
#include <iiSDKCore/Core/Result/Result.h>
#include <utility>

namespace dreamscapes::review {
struct NativeReviewQueueSurface::Impl final {
    std::shared_ptr<QueueModel> model;
    std::shared_ptr<native::NativeEmbeddedSurface> surface;
};
NativeReviewQueueSurface::NativeReviewQueueSurface(void *parent, std::shared_ptr<QueueModel> model, Callbacks callbacks)
    : impl_(std::make_unique<Impl>()) {
    impl_->model = std::move(model);
    auto source = impl_->model;
    impl_->surface = native::NativeEmbeddedSurface::create(parent,
        [source](ii::runtime::EventLoop &loop, ii::runtime::Runtime *workers)
            -> ii::core::Result<native::NativeEmbeddedSurface::Content> {
            if (!source || !source->active() || !source->onOwnerThread() || !workers)
                return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Native review queue requires an active model and worker");
            auto images = std::make_shared<NativeReviewImages>(loop, *workers);
            return native::NativeEmbeddedSurface::Content{
                .build = [source, images](ii::gui::WidgetBuilder &builder) {
                    return buildReviewQueue(builder, {}, source, images);
                },
                .resources = images};
        }, {.workers = 1, .capacity = 16}, {.ready = std::move(callbacks.ready),
            .failed = std::move(callbacks.failed), .closed = std::move(callbacks.closed)});
}
std::shared_ptr<NativeReviewQueueSurface> NativeReviewQueueSurface::create(void *parent,
    std::shared_ptr<QueueModel> model, Callbacks callbacks) {
    return std::shared_ptr<NativeReviewQueueSurface>(new NativeReviewQueueSurface(parent, std::move(model), std::move(callbacks)));
}
NativeReviewQueueSurface::~NativeReviewQueueSurface() = default;
ii::core::Result<void> NativeReviewQueueSurface::start(ii::gui::Rect frame) {
    if (!impl_->model) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Native review queue requires a model");
    if (!impl_->model->onOwnerThread()) return ii::core::fail(ii::core::ErrorCode::wrong_executor, "Native review queue requires its owner thread");
    if (!impl_->model->active()) return ii::core::fail(ii::core::ErrorCode::closed, "Native review queue is closed");
    return impl_->surface->start(frame);
}
ii::core::Result<void> NativeReviewQueueSurface::setFrame(ii::gui::Rect frame) { return impl_->surface->setFrame(frame); }
ii::core::Result<void> NativeReviewQueueSurface::setVisible(bool visible) { return impl_->surface->setVisible(visible); }
ii::core::Result<void> NativeReviewQueueSurface::close() { return impl_->surface->close(); }
std::optional<ii::gui::WidgetMount> NativeReviewQueueSurface::widgets() const { return impl_->surface->widgets(); }
} // namespace dreamscapes::review
