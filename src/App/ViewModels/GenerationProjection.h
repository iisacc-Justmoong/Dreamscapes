#pragma once
#include "Native/GenerationPresentation.h"
#include <QObject>
#include <QVariant>
#include <QTimer>
namespace dreamscapes {
class GenerationProjection : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList jobs READ jobs WRITE setJobs NOTIFY inputsChanged)
    Q_PROPERTY(QVariantList results READ results WRITE setResults NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap currentResult READ currentResult WRITE setCurrentResult NOTIFY inputsChanged)
    Q_PROPERTY(QStringList requestIds READ requestIds WRITE setRequestIds NOTIFY inputsChanged)
    Q_PROPERTY(QVariantMap inferenceStatus READ inferenceStatus WRITE setInferenceStatus NOTIFY inputsChanged)
    Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY inputsChanged)
    Q_PROPERTY(bool busy READ busy WRITE setBusy NOTIFY inputsChanged)
    Q_PROPERTY(int previewStep READ previewStep WRITE setPreviewStep NOTIFY inputsChanged)
    Q_PROPERTY(int previewTotalSteps READ previewTotalSteps WRITE setPreviewTotalSteps NOTIFY inputsChanged)
    Q_PROPERTY(QString selectedResultId READ selectedResultId WRITE setSelectedResultId NOTIFY inputsChanged)
    Q_PROPERTY(QVariantList submissionJobs READ submissionJobs NOTIFY submissionJobsChanged)
    Q_PROPERTY(QVariantList submissionResults READ submissionResults NOTIFY submissionResultsChanged)
    Q_PROPERTY(QVariantList reviewResults READ reviewResults NOTIFY reviewResultsChanged)
    Q_PROPERTY(QVariantMap activeGeneration READ activeGeneration NOTIFY activeGenerationChanged)
    Q_PROPERTY(QVariantMap reviewActiveJob READ reviewActiveJob NOTIFY reviewActiveJobChanged)
    Q_PROPERTY(bool generationPending READ generationPending NOTIFY generationPendingChanged)
    Q_PROPERTY(bool reviewPending READ reviewPending NOTIFY reviewPendingChanged)
    Q_PROPERTY(QVariantMap selectedResult READ selectedResult NOTIFY selectedResultChanged)
    Q_PROPERTY(QString failure READ failure NOTIFY failureChanged)
    Q_PROPERTY(QString generationStatusText READ generationStatusText NOTIFY generationStatusTextChanged)
public:
    explicit GenerationProjection(QObject *parent = nullptr);
    QVariantList jobs() const { return inputs_.value("jobs").toList(); }
    void setJobs(QVariantList);
    QVariantList results() const { return inputs_.value("results").toList(); }
    void setResults(QVariantList);
    QVariantMap currentResult() const { return inputs_.value("currentResult").toMap(); }
    void setCurrentResult(QVariantMap);
    QStringList requestIds() const { return inputs_.value("requestIds").toStringList(); }
    void setRequestIds(QStringList);
    QVariantMap inferenceStatus() const { return inputs_.value("inferenceStatus").toMap(); }
    void setInferenceStatus(QVariantMap);
    bool connected() const { return inputs_.value("connected").toBool(); }
    void setConnected(bool);
    bool busy() const { return inputs_.value("busy").toBool(); }
    void setBusy(bool);
    int previewStep() const { return inputs_.value("previewStep").toInt(); }
    void setPreviewStep(int);
    int previewTotalSteps() const { return inputs_.value("previewTotalSteps").toInt(); }
    void setPreviewTotalSteps(int);
    QString selectedResultId() const { return inputs_.value("selectedResultId").toString(); }
    void setSelectedResultId(QString);
    QVariantList submissionJobs() const { return state_.value("submissionJobs").toList(); }
    QVariantList submissionResults() const { return state_.value("submissionResults").toList(); }
    QVariantList reviewResults() const { return state_.value("reviewResults").toList(); }
    QVariantMap activeGeneration() const { return state_.value("activeGeneration").toMap(); }
    QVariantMap reviewActiveJob() const { return state_.value("reviewActiveJob").toMap(); }
    bool generationPending() const { return state_.value("generationPending").toBool(); }
    bool reviewPending() const { return state_.value("reviewPending").toBool(); }
    QVariantMap selectedResult() const { return state_.value("selectedResult").toMap(); }
    QString failure() const { return state_.value("failure").toString(); }
    QString generationStatusText() const { return state_.value("generationStatusText").toString(); }
    Q_INVOKABLE QString stateLabel(const QString &) const;
signals:
    void inputsChanged();
    void submissionJobsChanged();
    void submissionResultsChanged();
    void reviewResultsChanged();
    void activeGenerationChanged();
    void reviewActiveJobChanged();
    void generationPendingChanged();
    void reviewPendingChanged();
    void selectedResultChanged();
    void failureChanged();
    void generationStatusTextChanged();
private:
    void refresh();
    QVariantMap inputs_, state_;
    vm::GenerationPresenter presenter_;
    QTimer elapsed_;
};
} // namespace dreamscapes
