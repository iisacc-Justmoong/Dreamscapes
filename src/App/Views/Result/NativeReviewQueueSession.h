#pragma once

#include "NativeReviewQueueBridge.h"
#include <memory>
#include <thread>

namespace dreamscapes {
class GenerationProjection;
class GenerationProjectionBinding;
class WorkspaceSession;
namespace review {
// Transition composition for the product's generation and review sessions.
// The LVUI view and its model remain independent of this Qt transport.
class NativeReviewQueueSession final {
  public:
    NativeReviewQueueSession(WorkspaceSession *, std::shared_ptr<Session>, NativeReviewQueueActions = {},
                             QueueStrings = {});
    ~NativeReviewQueueSession();
    NativeReviewQueueSession(const NativeReviewQueueSession &) = delete;
    NativeReviewQueueSession &operator=(const NativeReviewQueueSession &) = delete;
    std::shared_ptr<QueueModel> model() const { return model_; }
    void close();
  private:
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<QueueModel> model_;
    std::unique_ptr<GenerationProjection> projection_;
    std::unique_ptr<GenerationProjectionBinding> source_;
    std::unique_ptr<NativeReviewQueueBridge> bridge_;
    bool active_ = true;
};
} // namespace review
} // namespace dreamscapes
