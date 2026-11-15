#pragma once

#include "NativeHomeComposerSession.h"
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>
#include <string>

class GenerationComposerModel;
namespace dreamscapes {
class NativeHomeReferenceInputs;
namespace home {
// Keeps a native composer's attachment cards aligned with its input provider.
class NativeHomeReferenceBinding final : public QObject {
  public:
    using Detail = std::function<std::string(qint64)>;
    NativeHomeReferenceBinding(NativeHomeReferenceInputs *, GenerationComposerModel *, NativeHomeComposerSession *,
                               Detail, QObject *parent = nullptr);
    ~NativeHomeReferenceBinding() override;
    [[nodiscard]] ii::core::Result<void> synchronize();
    [[nodiscard]] QString lastError() const { return lastError_; }
  private:
    struct UpdateState { bool updating = false, pending = false; };
    void requestSynchronize();
    QPointer<NativeHomeReferenceInputs> inputs_;
    QPointer<GenerationComposerModel> draft_;
    QPointer<NativeHomeComposerSession> session_;
    Detail detail_;
    std::shared_ptr<UpdateState> update_ = std::make_shared<UpdateState>();
    QString lastError_;
};
} // namespace home
} // namespace dreamscapes
