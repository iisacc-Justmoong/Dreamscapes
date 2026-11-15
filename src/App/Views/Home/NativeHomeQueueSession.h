#pragma once

#include "NativeHomeQueue.h"
#include <QMetaObject>
#include <QString>
#include <functional>
#include <memory>

namespace dreamscapes {
class GenerationProjection;
class GenerationProjectionBinding;
class WorkspaceSession;
namespace home {
class NativeHomeQueueBridge;
// Connects the product's generation session to a native Home view without
// requiring a QML object or a particular native window implementation.
class NativeHomeQueueSession final {
  public:
    NativeHomeQueueSession(WorkspaceSession *, HomeQueueStrings, std::function<void()> openQueue);
    ~NativeHomeQueueSession();
    NativeHomeQueueSession(const NativeHomeQueueSession &) = delete;
    NativeHomeQueueSession &operator=(const NativeHomeQueueSession &) = delete;
    [[nodiscard]] std::shared_ptr<HomeQueueModel> model() const { return model_; }
    [[nodiscard]] GenerationProjection *projection() const { return projection_.get(); }
    void setRuntimeNames(QString model, QString device);

  private:
    std::shared_ptr<HomeQueueModel> model_;
    std::unique_ptr<GenerationProjection> projection_;
    std::unique_ptr<GenerationProjectionBinding> source_;
    std::unique_ptr<NativeHomeQueueBridge> bridge_;
    QMetaObject::Connection workspaceDestroyed_;
};
} // namespace home
} // namespace dreamscapes
