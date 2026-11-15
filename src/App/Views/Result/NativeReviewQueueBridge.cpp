#include "NativeReviewQueueBridge.h"
#include "../../ViewModels/GenerationProjection.h"
#include "../../ViewModels/ViewData.h"
#include <QThread>
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
NativeReviewQueueBridge::NativeReviewQueueBridge(GenerationProjection *projection, std::shared_ptr<QueueModel> model,
                                                 NativeReviewQueueActions actions, QObject *parent)
    : QObject(parent), projection_(projection), model_(model), actions_(std::move(actions)) {
    if (!projection || !model) throw std::invalid_argument("A review queue bridge requires a source and model");
    if (projection->thread() != QThread::currentThread() || thread() != QThread::currentThread() || !model->onOwnerThread())
        throw std::logic_error("A review queue bridge requires its owner thread");
    if (!model->active()) throw std::invalid_argument("A review queue bridge requires an active model");
    attached_ = true;
    try {
        synchronize();
        connect(projection, &GenerationProjection::inputsChanged, this, [this] { synchronize(); });
        connect(projection, &QObject::destroyed, this, [this] { detach(); });
    } catch (...) { try { detach(); } catch (...) {} throw; }
}
NativeReviewQueueBridge::~NativeReviewQueueBridge() { try { detach(); } catch (...) {} }

void NativeReviewQueueBridge::setPreview(QString jobId, QUrl source) {
    if (thread() != QThread::currentThread()) throw std::logic_error("A review queue bridge requires its owner thread");
    if (!attached_) return;
    previewOverride_ = true;
    previewJobId_ = std::move(jobId); preview_ = std::move(source);
    synchronize();
}
void NativeReviewQueueBridge::synchronize() {
    const auto model = model_.lock();
    if (!attached_ || !projection_ || !model) return;
    if (!model->active()) { detach(); return; }
    const auto jobs = projection_->jobs();
    const auto inference = projection_->inferenceStatus();
    const auto jobId = (previewOverride_ ? previewJobId_ : projection_->previewJobId()).toStdString();
    auto preview = (previewOverride_ ? preview_ : projection_->previewImage()).toString(QUrl::FullyEncoded).toStdString();
    ii::core::Result<void> result;
    if (snapshotAccepted_ && jobs == lastJobs_ && inference == lastInference_) {
        result = model->setPreview(jobId, std::move(preview));
    } else {
        result = model->setSnapshot({.jobs = nativeValue(jobs), .inference = nativeValue(inference),
                                     .previewJobId = jobId, .previewSource = std::move(preview)});
        if (result) { lastJobs_ = jobs; lastInference_ = inference; }
    }
    snapshotAccepted_ = bool(result);
    lastError_ = result ? QString{} : QString::fromStdString(result.error().message);
    setActionsEnabled(bool(result));
}
void NativeReviewQueueBridge::setActionsEnabled(bool enabled) {
    if (actionsInitialized_ && actionsEnabled_ == enabled) return;
    auto model = model_.lock();
    if (!model || !model->active()) return;
    actionsInitialized_ = true;
    actionsEnabled_ = enabled;
    const QPointer<NativeReviewQueueBridge> weak(this);
    QueueModel::Action cancel, failure, selection;
    if (enabled && actions_.cancel) cancel = [weak](const std::string &id) {
        if (weak && weak->attached_ && weak->actionsEnabled_ && weak->lastError_.isEmpty() && weak->actions_.cancel)
            weak->actions_.cancel(QString::fromStdString(id));
    };
    if (enabled && actions_.failure) failure = [weak](const std::string &id) {
        if (weak && weak->attached_ && weak->actionsEnabled_ && weak->lastError_.isEmpty() && weak->actions_.failure)
            weak->actions_.failure(QString::fromStdString(id));
    };
    if (enabled && actions_.selection) selection = [weak](const std::string &id) {
        if (weak && weak->attached_ && weak->actionsEnabled_ && weak->lastError_.isEmpty() && weak->actions_.selection)
            weak->actions_.selection(QString::fromStdString(id));
    };
    try {
        if (!enabled) model->setSelectionEnabled(false);
        model->setCancelHandler(std::move(cancel));
        model->setFailureHandler(std::move(failure));
        model->setSelectionHandler(std::move(selection));
        if (enabled) model->setSelectionEnabled(true);
    } catch (...) {
        actionsEnabled_ = false;
        lastError_ = QStringLiteral("Review queue command binding failed");
        try { model->setCancelHandler({}); } catch (...) {}
        try { model->setFailureHandler({}); } catch (...) {}
        try { model->setSelectionHandler({}); } catch (...) {}
        try { model->setSelectionEnabled(false); } catch (...) {}
    }
}
void NativeReviewQueueBridge::detach() {
    if (!attached_) return;
    attached_ = false; actionsEnabled_ = false;
    if (projection_) disconnect(projection_, nullptr, this, nullptr);
    projection_.clear(); actions_ = {}; previewJobId_.clear(); preview_.clear(); previewOverride_ = false;
    lastJobs_.clear(); lastInference_.clear(); snapshotAccepted_ = false;
    if (auto model = model_.lock(); model && model->active()) {
        // Disable both commands even when an observer of either field throws.
        try { model->setCancelHandler({}); } catch (...) {}
        try { model->setFailureHandler({}); } catch (...) {}
        try { model->setSelectionHandler({}); } catch (...) {}
        try { model->setSelectionEnabled(false); } catch (...) {}
    }
    model_.reset();
}
} // namespace dreamscapes::review
