#include "GenerationProjection.h"
#include "ViewData.h"
#include <QDateTime>
#include <QHash>
namespace dreamscapes {
GenerationProjection::GenerationProjection(QObject *parent) : QObject(parent) {
    inputs_["connected"] = true;
    elapsed_.setInterval(1000);
    connect(&elapsed_, &QTimer::timeout, this, &GenerationProjection::refresh);
    refresh();
}
void GenerationProjection::setJobs(QVariantList value) {
    if (inputs_.value("jobs") == QVariant::fromValue(value))
        return;
    inputs_["jobs"] = QVariant::fromValue(value);
    refresh();
    emit inputsChanged();
}
void GenerationProjection::setResults(QVariantList value) {
    if (inputs_.value("results") == QVariant::fromValue(value))
        return;
    inputs_["results"] = QVariant::fromValue(value);
    refresh();
    emit inputsChanged();
}
void GenerationProjection::setCurrentResult(QVariantMap value) {
    if (inputs_.value("currentResult") == QVariant::fromValue(value))
        return;
    inputs_["currentResult"] = QVariant::fromValue(value);
    refresh();
    emit inputsChanged();
}
void GenerationProjection::setRequestIds(QStringList value) {
    if (inputs_.value("requestIds") == QVariant::fromValue(value))
        return;
    inputs_["requestIds"] = QVariant::fromValue(value);
    refresh();
    emit inputsChanged();
}
void GenerationProjection::setInferenceStatus(QVariantMap value) {
    if (inputs_.value("inferenceStatus") == QVariant::fromValue(value))
        return;
    inputs_["inferenceStatus"] = QVariant::fromValue(value);
    refresh();
    emit inputsChanged();
}
void GenerationProjection::setConnected(bool value) {
    if (inputs_.value("connected") == QVariant::fromValue(value))
        return;
    inputs_["connected"] = QVariant::fromValue(value);
    refresh();
    emit inputsChanged();
}
void GenerationProjection::setBusy(bool value) {
    if (inputs_.value("busy") == QVariant::fromValue(value))
        return;
    inputs_["busy"] = QVariant::fromValue(value);
    refresh();
    emit inputsChanged();
}
void GenerationProjection::setPreviewStep(int value) {
    if (inputs_.value("previewStep") == QVariant::fromValue(value))
        return;
    inputs_["previewStep"] = QVariant::fromValue(value);
    refresh();
    emit inputsChanged();
}
void GenerationProjection::setPreviewTotalSteps(int value) {
    if (inputs_.value("previewTotalSteps") == QVariant::fromValue(value))
        return;
    inputs_["previewTotalSteps"] = QVariant::fromValue(value);
    refresh();
    emit inputsChanged();
}
void GenerationProjection::setSelectedResultId(QString value) {
    if (inputs_.value("selectedResultId") == QVariant::fromValue(value))
        return;
    inputs_["selectedResultId"] = QVariant::fromValue(value);
    refresh();
    emit inputsChanged();
}
void GenerationProjection::setSelectedImageSource(QString value) {
    if (inputs_.value("selectedImageSource") == value)
        return;
    inputs_["selectedImageSource"] = value;
    refresh();
    emit inputsChanged();
}
QString GenerationProjection::stateLabel(const QString &state) const {
    return QString::fromStdString(vm::generationStateLabel(state.toStdString()));
}
void GenerationProjection::refresh() {
    vm::GenerationSnapshot input;
    input.jobs = nativeValue(jobs());
    input.results = nativeValue(results());
    input.currentResult = nativeValue(currentResult());
    input.requestIds = nativeValue(requestIds());
    input.inferenceStatus = nativeValue(inferenceStatus());
    input.connected = connected();
    input.busy = busy();
    input.previewStep = previewStep();
    input.previewTotalSteps = previewTotalSteps();
    input.selectedResultId = selectedResultId().toStdString();
    input.selectedImageSource = selectedImageSource().toStdString();
    const auto run = [&](const auto &s) {
        const auto reply = presenter_.queries().send(s);
        return reply ? viewValue(*reply).toMap() : QVariantMap{};
    };
    auto next = run(input);
    const auto active = next.value("activeGeneration").toMap();
    const auto started = QDateTime::fromString(active.value("startedAt").toString(), Qt::ISODateWithMs);
    if (started.isValid()) {
        input.elapsedSeconds = std::max<qint64>(0, started.secsTo(QDateTime::currentDateTimeUtc()));
        next = run(input);
    }
    if (active.value("id").toString().isEmpty())
        elapsed_.stop();
    else if (!elapsed_.isActive())
        elapsed_.start();
    // The native receiver chooses identities. Transport the original payloads
    // back to the GUI without re-encoding opaque metadata or URL/date wrappers.
    const auto identity = [](const QVariantMap &entry) {
        return QStringList{entry.value("id").toString(), entry.value("image").toString(),
                           entry.value("documentSource").toString(), entry.value("mediaSource").toString(),
                           entry.value("imageSource").toString()}
            .join(QChar(0x1f));
    };
    QHash<QString, QVariantMap> originals, jobOriginals;
    for (const auto &entry : results()) {
        const auto map = entry.toMap();
        if (!originals.contains(identity(map)))
            originals.insert(identity(map), map);
    }
    if (!currentResult().isEmpty() && !originals.contains(identity(currentResult())))
        originals.insert(identity(currentResult()), currentResult());
    for (const auto &entry : jobs()) {
        const auto map = entry.toMap();
        jobOriginals.insert(map.value("id").toString(), map);
    }
    const auto retain = [&](const QVariantMap &entry, bool job) {
        return job ? jobOriginals.value(entry.value("id").toString(), entry) : originals.value(identity(entry), entry);
    };
    for (const auto *key : {"submissionResults", "reviewResults", "submissionJobs"}) {
        QVariantList restored;
        for (const auto &entry : next.value(key).toList())
            restored.push_back(retain(entry.toMap(), std::string_view(key) == "submissionJobs"));
        next[key] = restored;
    }
    for (const auto *key : {"selectedResult", "activeGeneration", "reviewActiveJob"})
        next[key] = retain(next.value(key).toMap(), std::string_view(key) != "selectedResult");
    const auto old = state_;
    state_ = std::move(next);
    if (old.value("submissionJobs") != state_.value("submissionJobs"))
        emit submissionJobsChanged();
    if (old.value("submissionResults") != state_.value("submissionResults"))
        emit submissionResultsChanged();
    if (old.value("reviewResults") != state_.value("reviewResults"))
        emit reviewResultsChanged();
    if (old.value("activeGeneration") != state_.value("activeGeneration"))
        emit activeGenerationChanged();
    if (old.value("reviewActiveJob") != state_.value("reviewActiveJob"))
        emit reviewActiveJobChanged();
    if (old.value("generationPending") != state_.value("generationPending"))
        emit generationPendingChanged();
    if (old.value("reviewPending") != state_.value("reviewPending"))
        emit reviewPendingChanged();
    if (old.value("selectedResult") != state_.value("selectedResult"))
        emit selectedResultChanged();
    if (old.value("failure") != state_.value("failure"))
        emit failureChanged();
    if (old.value("generationStatusText") != state_.value("generationStatusText"))
        emit generationStatusTextChanged();
}
int GenerationProjection::countStates(const QStringList &states) const {
    int count = 0;
    for (const auto &job : jobs())
        if (states.contains(job.toMap().value("state").toString()))
            ++count;
    return count;
}
QString GenerationProjection::queueStateLabel(const QString &state) const {
    const QMap<QString, QString> labels{{"queued", tr("Queued")},
                                        {"running", tr("Running")},
                                        {"completed", tr("Completed")},
                                        {"failed", tr("Failed")},
                                        {"cancelled", tr("Canceled")},
                                        {"interrupted", tr("Interrupted")},
                                        {"connecting-host", tr("Connecting")},
                                        {"downloading", tr("Downloading")}};
    return labels.value(state, state.isEmpty() ? tr("Unavailable") : state);
}
} // namespace dreamscapes
