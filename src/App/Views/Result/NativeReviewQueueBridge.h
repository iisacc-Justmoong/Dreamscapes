#pragma once

#include "NativeReviewQueue.h"
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace dreamscapes {
class GenerationProjection;
namespace review {
struct NativeReviewQueueActions final {
    std::function<void(const QString &)> cancel, failure, selection;
};

// Temporary transport from the existing Qt generation projection. Neither
// the LVUI view nor the queue model depends on Qt.
class NativeReviewQueueBridge final : public QObject {
  public:
    NativeReviewQueueBridge(GenerationProjection *, std::shared_ptr<QueueModel>,
                            NativeReviewQueueActions = {}, QObject *parent = nullptr);
    ~NativeReviewQueueBridge() override;
    void setPreview(QString jobId, QUrl source);
    void detach();
    QString lastError() const { return lastError_; }

  private:
    void synchronize();
    void setActionsEnabled(bool enabled);
    QPointer<GenerationProjection> projection_;
    std::weak_ptr<QueueModel> model_;
    NativeReviewQueueActions actions_;
    QString previewJobId_, lastError_;
    QUrl preview_;
    QVariantList lastJobs_;
    QVariantMap lastInference_;
    bool attached_ = false, actionsEnabled_ = false, actionsInitialized_ = false, previewOverride_ = false, snapshotAccepted_ = false;
};
} // namespace review
} // namespace dreamscapes
