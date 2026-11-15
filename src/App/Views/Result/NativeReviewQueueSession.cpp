#include "NativeReviewQueueSession.h"
#include "App/ViewModels/GenerationProjection.h"
#include "App/ViewModels/GenerationProjectionBinding.h"
#include "App/ViewModels/WorkspaceSession.h"
#include <QThread>
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
NativeReviewQueueSession::NativeReviewQueueSession(WorkspaceSession *workspace, std::shared_ptr<Session> review,
                                                   NativeReviewQueueActions actions, QueueStrings strings) {
    if (!workspace || !review) throw std::invalid_argument("A native review queue requires both product sessions");
    if (workspace->thread() != QThread::currentThread())
        throw std::logic_error("The native review queue requires the product session's owner thread");
    model_ = QueueModel::create(std::move(review), std::move(strings));
    projection_ = std::make_unique<GenerationProjection>();
    source_ = std::make_unique<GenerationProjectionBinding>(workspace, projection_.get());
    bridge_ = std::make_unique<NativeReviewQueueBridge>(projection_.get(), model_, std::move(actions));
}
NativeReviewQueueSession::~NativeReviewQueueSession() {
    if (active_) try { close(); } catch (...) {}
}
void NativeReviewQueueSession::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("The native review queue requires its owner thread");
    if (!active_) return;
    active_ = false;
    // Detachment can notify a view. Keep every dependency alive locally and
    // do not read the owner again after the first such notification.
    auto bridge = std::move(bridge_);
    auto source = std::move(source_);
    auto projection = std::move(projection_);
    auto model = model_;
    bridge.reset();
    source.reset();
    projection.reset();
    if (model && model->active()) model->close();
}
} // namespace dreamscapes::review
