#pragma once

#include <SharedStorage.h>
#include <iiSocietyHelper.h>
#include <Generation/NativeDiffusion.hpp>
#include <QFutureWatcher>
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
    int steps = 10;
    int imageExtent = 1024; // Shorter output side, aligned to the 8px latent grid.
    QString pythonExecutable;
    QString temporaryDirectory;
    bool nativeInference = false;
    int nativeTimeoutMilliseconds = 900000;
    QString nativeQ8CacheDirectory;
    std::function<iiLocalDiffusion::NativeGenerationResult(const iiLocalDiffusion::NativeGenerationRequest &,
        const std::atomic_bool &, const iiLocalDiffusion::NativeProgressCallback &)> nativeGenerate;
    std::function<void(bool)> screenActivity;
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
    Q_PROPERTY(bool runtimeAvailable READ runtimeAvailable NOTIFY storageChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QUrl latestImage READ latestImage NOTIFY jobsChanged)
    Q_PROPERTY(QVariantMap latestResult READ latestResult NOTIFY jobsChanged)
    Q_PROPERTY(QVariantList completedResults READ completedResults NOTIFY jobsChanged)
    Q_PROPERTY(QUrl previewImage READ previewImage NOTIFY previewChanged)
    Q_PROPERTY(int previewStep READ previewStep NOTIFY previewChanged)
    Q_PROPERTY(int previewTotalSteps READ previewTotalSteps NOTIFY previewChanged)
    Q_PROPERTY(bool foreground READ foreground WRITE setForeground NOTIFY foregroundChanged)
    Q_PROPERTY(QVariantMap inferenceStatus READ inferenceStatus NOTIFY inferenceStatusChanged)
    Q_PROPERTY(bool keepsScreenAwake READ keepsScreenAwake NOTIFY screenActivityChanged)
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
    QVariantList completedResults() const;
    QUrl previewImage() const;
    int previewStep() const;
    int previewTotalSteps() const;
    bool foreground() const;
    void setForeground(bool foreground);
    QVariantMap inferenceStatus() const;
    bool keepsScreenAwake() const;

    Q_INVOKABLE bool connectStorage(const QString &path = {});
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE QString enqueue(const QString &prompt, const QString &aspectRatio = QStringLiteral("1:1"), int count = 1);
    Q_INVOKABLE bool cancel(const QString &id);

signals:
    void storageChanged();
    void modelsChanged();
    void jobsChanged();
    void errorChanged();
    void previewChanged();
    void foregroundChanged();
    void inferenceStatusChanged();
    void screenActivityChanged();

private:
    QVariantMap resultForImage(const QJsonObject &job, const QString &relative) const;
    bool fail(const QString &message);
    void pollStorage();
    void startNative(const QString &model);
    void finishNative();
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
    void updateScreenActivity();

    GenerationRuntime m_runtime;
    iiSocietyHelper::FileSystem m_fileSystem;
    QString m_storageSelection;
    QTimer m_storagePoll;
    QFutureWatcher<iiLocalDiffusion::NativeGenerationResult> m_nativeWatcher;
    std::atomic_bool m_nativeCancelled{false};
    QTimer m_nativeDeadline;
    bool m_nativeTimedOut = false;
    bool m_interrupted = false;
    bool m_screenActive = false;
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
