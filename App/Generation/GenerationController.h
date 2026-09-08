#pragma once

#include <SharedStorage.h>
#include <QObject>
#include <QJsonObject>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
#include <QProcess>
#endif
#include <memory>

struct GenerationRuntime {
    QString executable;
    QString device = QStringLiteral("auto");
    int steps = 20;
    int imageExtent = 512;
    QString pythonExecutable;
    QString temporaryDirectory;
};

class GenerationController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool connected READ connected NOTIFY storageChanged)
    Q_PROPERTY(QString containerPath READ containerPath NOTIFY storageChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel WRITE setSelectedModel NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobsChanged)
    Q_PROPERTY(bool runtimeAvailable READ runtimeAvailable CONSTANT)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QUrl latestImage READ latestImage NOTIFY jobsChanged)
    Q_PROPERTY(QVariantMap latestResult READ latestResult NOTIFY jobsChanged)
    Q_PROPERTY(QUrl previewImage READ previewImage NOTIFY previewChanged)
    Q_PROPERTY(int previewStep READ previewStep NOTIFY previewChanged)
    Q_PROPERTY(int previewTotalSteps READ previewTotalSteps NOTIFY previewChanged)
    Q_PROPERTY(bool foreground READ foreground WRITE setForeground NOTIFY foregroundChanged)
    Q_PROPERTY(QVariantMap inferenceStatus READ inferenceStatus NOTIFY inferenceStatusChanged)
public:
    explicit GenerationController(QObject *parent = nullptr);
    explicit GenerationController(GenerationRuntime runtime, QObject *parent = nullptr);
    ~GenerationController() override;

    bool connected() const;
    QString containerPath() const;
    QVariantList models() const;
    QString selectedModel() const;
    void setSelectedModel(const QString &id);
    QVariantList jobs() const;
    bool busy() const;
    bool runtimeAvailable() const;
    QString errorString() const;
    QUrl latestImage() const;
    QVariantMap latestResult() const;
    QUrl previewImage() const;
    int previewStep() const;
    int previewTotalSteps() const;
    bool foreground() const;
    void setForeground(bool foreground);
    QVariantMap inferenceStatus() const;

    Q_INVOKABLE bool connectStorage(const QString &path = {});
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE QString enqueue(const QString &prompt, const QString &aspectRatio = QStringLiteral("1:1"));
    Q_INVOKABLE bool cancel(const QString &id);

signals:
    void storageChanged();
    void modelsChanged();
    void jobsChanged();
    void errorChanged();
    void previewChanged();
    void foregroundChanged();
    void inferenceStatusChanged();

private:
    bool fail(const QString &message);
    bool discardLegacyStorage();
    bool createWorkingFiles(QString *error);
    void clearWorkingFiles();
    bool publishImages(const QStringList &sources, QJsonObject &job, QString *error);
    void updateJob(QJsonObject job);
    void pump();
    void finish(const QString &state, const QString &error = {});
    bool collectResult(QString *error);
    void stopProcess(bool force);
    void readProcessOutput();
    void sendWorkerRequest();
    void acceptWorkerResult(const QByteArray &line);
    void acceptPreview(const QByteArray &line);
    void clearPreview();
    bool startWorker(QString *error);
    void prepareForeground();
    void setInferenceStatus(QJsonObject status);

    GenerationRuntime m_runtime;
    std::optional<iiSocietyContainer::SharedStorage> m_storage;
    QList<iiSocietyContainer::StoredModel> m_models;
    QString m_selected;
    QList<QJsonObject> m_jobs;
    QJsonObject m_active;
    QString m_output;
    QString m_error;
    QByteArray m_log;
    QByteArray m_pendingOutput;
    std::unique_ptr<QTemporaryDir> m_workDirectory;
    std::unique_ptr<QTemporaryDir> m_workerDirectory;
    std::unique_ptr<QTemporaryDir> m_previewDirectory;
    QUrl m_previewImage;
    int m_previewStep = 0;
    int m_previewTotalSteps = 0;
    bool m_cancelled = false;
    bool m_workerReady = false;
    bool m_workerForegroundSupported = false;
    bool m_foreground = false;
    bool m_residencyPending = false;
    QString m_controlId;
    QJsonObject m_inferenceStatus{{"state", "idle"}, {"ready", false}};
    QByteArray m_workerRequest;
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    QProcess m_process;
#endif
};
