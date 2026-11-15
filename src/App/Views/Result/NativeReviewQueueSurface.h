#pragma once

#include "NativeReviewQueue.h"
#include <iiGUI/NativeWidgets.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace dreamscapes::review {
// A transitional native surface for an already-owned platform view. The
// caller retains the parent until closed is delivered on the owner thread.
class NativeReviewQueueSurface final : public std::enable_shared_from_this<NativeReviewQueueSurface> {
  public:
    struct Callbacks final {
        std::function<void()> ready;
        std::function<void(const std::string &)> failed;
        std::function<void()> closed;
    };
    static std::shared_ptr<NativeReviewQueueSurface> create(void *nativeParent,
        std::shared_ptr<QueueModel>, Callbacks = {});
    ~NativeReviewQueueSurface();
    NativeReviewQueueSurface(const NativeReviewQueueSurface &) = delete;
    NativeReviewQueueSurface &operator=(const NativeReviewQueueSurface &) = delete;

    [[nodiscard]] ii::core::Result<void> start(ii::gui::Rect frame);
    [[nodiscard]] ii::core::Result<void> setFrame(ii::gui::Rect frame);
    [[nodiscard]] ii::core::Result<void> setVisible(bool visible);
    [[nodiscard]] ii::core::Result<void> close();
    std::optional<ii::gui::WidgetMount> widgets() const;

  private:
    struct Impl;
    NativeReviewQueueSurface(void *, std::shared_ptr<QueueModel>, Callbacks);
    std::unique_ptr<Impl> impl_;
};
} // namespace dreamscapes::review
