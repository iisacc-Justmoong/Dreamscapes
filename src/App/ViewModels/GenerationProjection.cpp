#include "GenerationProjection.h"
#include "ViewData.h"
#include <QDateTime>
namespace dreamscapes {
GenerationProjection::GenerationProjection(QObject *parent) : QObject(parent) {
    inputs_["connected"] = true; elapsed_.setInterval(1000); connect(&elapsed_, &QTimer::timeout, this, &GenerationProjection::refresh); refresh();
}
void GenerationProjection::setJobs(QVariantList value) { if (inputs_.value("jobs") == QVariant::fromValue(value)) return; inputs_["jobs"] = QVariant::fromValue(value); refresh(); emit inputsChanged(); }
void GenerationProjection::setResults(QVariantList value) { if (inputs_.value("results") == QVariant::fromValue(value)) return; inputs_["results"] = QVariant::fromValue(value); refresh(); emit inputsChanged(); }
void GenerationProjection::setCurrentResult(QVariantMap value) { if (inputs_.value("currentResult") == QVariant::fromValue(value)) return; inputs_["currentResult"] = QVariant::fromValue(value); refresh(); emit inputsChanged(); }
void GenerationProjection::setRequestIds(QStringList value) { if (inputs_.value("requestIds") == QVariant::fromValue(value)) return; inputs_["requestIds"] = QVariant::fromValue(value); refresh(); emit inputsChanged(); }
void GenerationProjection::setInferenceStatus(QVariantMap value) { if (inputs_.value("inferenceStatus") == QVariant::fromValue(value)) return; inputs_["inferenceStatus"] = QVariant::fromValue(value); refresh(); emit inputsChanged(); }
void GenerationProjection::setConnected(bool value) { if (inputs_.value("connected") == QVariant::fromValue(value)) return; inputs_["connected"] = QVariant::fromValue(value); refresh(); emit inputsChanged(); }
void GenerationProjection::setBusy(bool value) { if (inputs_.value("busy") == QVariant::fromValue(value)) return; inputs_["busy"] = QVariant::fromValue(value); refresh(); emit inputsChanged(); }
void GenerationProjection::setPreviewStep(int value) { if (inputs_.value("previewStep") == QVariant::fromValue(value)) return; inputs_["previewStep"] = QVariant::fromValue(value); refresh(); emit inputsChanged(); }
void GenerationProjection::setPreviewTotalSteps(int value) { if (inputs_.value("previewTotalSteps") == QVariant::fromValue(value)) return; inputs_["previewTotalSteps"] = QVariant::fromValue(value); refresh(); emit inputsChanged(); }
void GenerationProjection::setSelectedResultId(QString value) { if (inputs_.value("selectedResultId") == QVariant::fromValue(value)) return; inputs_["selectedResultId"] = QVariant::fromValue(value); refresh(); emit inputsChanged(); }
QString GenerationProjection::stateLabel(const QString &state) const { return QString::fromStdString(vm::generationStateLabel(state.toStdString())); }
void GenerationProjection::refresh() {
    vm::GenerationSnapshot input; input.jobs = nativeValue(jobs()); input.results = nativeValue(results()); input.currentResult = nativeValue(currentResult()); input.requestIds = nativeValue(requestIds()); input.inferenceStatus = nativeValue(inferenceStatus());
    input.connected = connected(); input.busy = busy(); input.previewStep = previewStep(); input.previewTotalSteps = previewTotalSteps(); input.selectedResultId = selectedResultId().toStdString();
    const auto run = [&](const auto &s) { const auto reply = presenter_.queries().send(s); return reply ? viewValue(*reply).toMap() : QVariantMap{}; };
    auto next = run(input); const auto active = next.value("activeGeneration").toMap();
    const auto started = QDateTime::fromString(active.value("startedAt").toString(), Qt::ISODateWithMs);
    if (started.isValid()) { input.elapsedSeconds = std::max<qint64>(0, started.secsTo(QDateTime::currentDateTimeUtc())); next = run(input); }
    if (active.value("id").toString().isEmpty()) elapsed_.stop(); else if (!elapsed_.isActive()) elapsed_.start();
    const auto old = state_; state_ = std::move(next);
    if (old.value("submissionJobs") != state_.value("submissionJobs")) emit submissionJobsChanged();
    if (old.value("submissionResults") != state_.value("submissionResults")) emit submissionResultsChanged();
    if (old.value("reviewResults") != state_.value("reviewResults")) emit reviewResultsChanged();
    if (old.value("activeGeneration") != state_.value("activeGeneration")) emit activeGenerationChanged();
    if (old.value("reviewActiveJob") != state_.value("reviewActiveJob")) emit reviewActiveJobChanged();
    if (old.value("generationPending") != state_.value("generationPending")) emit generationPendingChanged();
    if (old.value("reviewPending") != state_.value("reviewPending")) emit reviewPendingChanged();
    if (old.value("selectedResult") != state_.value("selectedResult")) emit selectedResultChanged();
    if (old.value("failure") != state_.value("failure")) emit failureChanged();
    if (old.value("generationStatusText") != state_.value("generationStatusText")) emit generationStatusTextChanged();
}
} // namespace dreamscapes
