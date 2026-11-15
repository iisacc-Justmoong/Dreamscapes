#include "GenerationProjectionBinding.h"
#include "GenerationProjection.h"
#include "WorkspaceSession.h"
#include "../Generation/GenerationController.h"
#include <QThread>
#include <stdexcept>

namespace dreamscapes {
GenerationProjectionBinding::GenerationProjectionBinding(WorkspaceSession *session, GenerationProjection *projection,
                                                         QObject *parent)
    : QObject(parent), session_(session), projection_(projection) {
    if (!session || !projection)
        throw std::invalid_argument("Generation projection binding requires a session and projection");
    if (thread() != QThread::currentThread() || session->thread() != thread() || projection->thread() != thread())
        throw std::logic_error("Generation projection binding requires one owner thread");
    attached_ = true;
    connect(session, &WorkspaceSession::generationChanged, this, [this] { rebindController(true); });
    connect(session, &WorkspaceSession::currentResultChanged, this, [this] { synchronize(CurrentResult); });
    connect(session, &WorkspaceSession::resultJobIdsChanged, this, [this] { synchronize(RequestIds); });
    connect(session, &QObject::destroyed, this, [this] {
        session_.clear();
        clearController();
        synchronize(All);
    });
    connect(projection, &QObject::destroyed, this, [this] { detach(); });
    rebindController(false);
    synchronize(All);
}

GenerationProjectionBinding::~GenerationProjectionBinding() { detach(); }

void GenerationProjectionBinding::clearController() {
    for (const auto &connection : controllerConnections_)
        disconnect(connection);
    controllerConnections_.clear();
    controller_.clear();
}

void GenerationProjectionBinding::detach() {
    if (!attached_)
        return;
    attached_ = false;
    clearController();
    if (session_)
        disconnect(session_, nullptr, this, nullptr);
    if (projection_)
        disconnect(projection_, nullptr, this, nullptr);
    session_.clear();
    projection_.clear();
}

void GenerationProjectionBinding::rebindController(bool sync) {
    if (!attached_)
        return;
    auto *next = session_ ? qobject_cast<GenerationController *>(session_->generation()) : nullptr;
    if (next && next->thread() != thread())
        throw std::logic_error("Generation controller must share the binding's owner thread");
    clearController();
    controller_ = next;
    if (next) {
        const auto watch = [this, next](auto signal, unsigned fields) {
            controllerConnections_.push_back(connect(next, signal, this, [this, fields] { synchronize(fields); }));
        };
        watch(&GenerationController::jobsChanged, Jobs);
        watch(&GenerationController::resultsChanged, Results);
        watch(&GenerationController::storageChanged, Storage);
        watch(&GenerationController::inferenceStatusChanged, Inference);
        watch(&GenerationController::previewChanged, Preview);
        controllerConnections_.push_back(connect(next, &QObject::destroyed, this, [this] { rebindController(true); }));
    }
    if (sync)
        synchronize(Jobs | Results | Storage | Inference | Preview);
}

void GenerationProjectionBinding::synchronize(unsigned fields) {
    if (!attached_ || !projection_)
        return;
    const auto source = controller_;
    const auto session = session_;
    QVariantMap patch;
    if (fields & Jobs) {
        patch.insert("jobs", source ? source->jobs() : QVariantList{});
        patch.insert("busy", source ? source->busy() : false);
    }
    if (fields & Results)
        patch.insert("results", source ? source->completedResults() : QVariantList{});
    if (fields & Storage)
        patch.insert("connected", source ? source->connected() : false);
    if (fields & Inference)
        patch.insert("inferenceStatus", source ? source->inferenceStatus() : QVariantMap{});
    if (fields & Preview) {
        patch.insert("previewStep", source ? source->previewStep() : 0);
        patch.insert("previewTotalSteps", source ? source->previewTotalSteps() : 0);
        patch.insert("previewImage", source ? source->previewImage() : QUrl{});
        patch.insert("previewJobId", source ? source->previewJobId() : QString{});
    }
    if (fields & CurrentResult)
        patch.insert("currentResult", session ? session->currentResult() : QVariantMap{});
    if (fields & RequestIds)
        patch.insert("requestIds", session ? session->resultJobIds() : QStringList{});
    projection_->applySourceInputs(patch);
}
} // namespace dreamscapes
