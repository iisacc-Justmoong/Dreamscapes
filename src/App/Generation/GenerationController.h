#pragma once

#include <SharedStorage.h>
#include <iiSocietyHelper.h>
#include <iiSocietyClient/Client.h>
#include <iiSocietyClient/AccountSession.h>
#include <iiSocietyGeneration/Remote.h>
#include <Generation/NativeDiffusion.hpp>
#include <QFutureWatcher>
#include <QObject>
#include <QJsonObject>
#include <QStringList>
#include <QSize>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
#include <QProcess>
#endif
#include <memory>
#include "GenerationBackgroundActivity.h"
#include "ModelPreferences.h"

struct GenerationRuntime {
    QString executable;
    QString device = QStringLiteral("auto");
    int steps = 10;
    int imageExtent = 1024; // Shorter output side, aligned to the 8px latent grid.
    QString pythonExecutable;
    bool nativeInference = false;
    int nativeTimeoutMilliseconds = 900000; // Maximum active time without measurable progress.
    QString legacyQ8CacheDirectory; // Read/move-only upgrade source; never used by inference.
    QString modelPreferencesFile; // Empty for injected runtimes; production uses app configuration.
    std::function<iiLocalDiffusion::NativeGenerationResult(const iiLocalDiffusion::NativeGenerationRequest &,
        const iiLocalDiffusion::NativeGenerationOptions &, const std::atomic_bool &,
        const iiLocalDiffusion::NativeProgressCallback &, const iiLocalDiffusion::NativePreviewCallback &)> nativeGenerate;
    std::function<void(bool)> screenActivity;
    std::shared_ptr<GenerationBackgroundActivity> backgroundActivity;
    std::shared_ptr<iiLocalDiffusion::NativeExecutionControl> nativeExecutionControl;
    // Optional transport injection for integration tests; production uses the
    // authenticated Society client and never accepts a model URL.
    std::shared_ptr<iiSocietyGeneration::Remote> remoteGeneration;
    std::function<iiLocalDiffusion::NativeGenerationResult(const iiLocalDiffusion::NativeGenerationRequest &,
        const iiLocalDiffusion::NativeGenerationOptions &, const iiLocalDiffusion::NativeModelComponents &,
        const iiLocalDiffusion::NativeAdvancedControls &, const std::atomic_bool &,
        const iiLocalDiffusion::NativeProgressCallback &, const iiLocalDiffusion::NativePreviewCallback &)> nativeGenerateAdvanced;
};

class GenerationController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *account READ account CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY storageChanged)
    Q_PROPERTY(QString containerPath READ containerPath NOTIFY storageChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList videoModels READ videoModels NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedVideoModel READ selectedVideoModel WRITE setSelectedVideoModel NOTIFY modelsChanged)
    Q_PROPERTY(bool videoRuntimeAvailable READ videoRuntimeAvailable NOTIFY storageChanged)
    Q_PROPERTY(QVariantList vaes READ vaes NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedVae READ selectedVae WRITE setSelectedVae NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel WRITE setSelectedModel NOTIFY modelsChanged)
    Q_PROPERTY(QString defaultImageModel READ defaultImageModel NOTIFY modelPreferencesChanged)
    Q_PROPERTY(QString defaultVideoModel READ defaultVideoModel NOTIFY modelPreferencesChanged)
    Q_PROPERTY(QString modelPreferencesError READ modelPreferencesError NOTIFY modelPreferencesChanged)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobsChanged)
    Q_PROPERTY(bool runtimeAvailable READ runtimeAvailable NOTIFY storageChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QUrl latestImage READ latestImage NOTIFY jobsChanged)
    Q_PROPERTY(QVariantMap latestResult READ latestResult NOTIFY jobsChanged)
    Q_PROPERTY(QVariantList completedResults READ completedResults NOTIFY jobsChanged)
    Q_PROPERTY(QUrl previewImage READ previewImage NOTIFY previewChanged)
    Q_PROPERTY(QString previewJobId READ previewJobId NOTIFY previewChanged)
    Q_PROPERTY(int previewStep READ previewStep NOTIFY previewChanged)
    Q_PROPERTY(int previewTotalSteps READ previewTotalSteps NOTIFY previewChanged)
    Q_PROPERTY(bool foreground READ foreground WRITE setForeground NOTIFY foregroundChanged)
    Q_PROPERTY(QVariantMap inferenceStatus READ inferenceStatus NOTIFY inferenceStatusChanged)
    Q_PROPERTY(bool keepsScreenAwake READ keepsScreenAwake NOTIFY screenActivityChanged)
public:
    explicit GenerationController(QObject *parent = nullptr);
    explicit GenerationController(GenerationRuntime runtime, QObject *parent = nullptr);
    ~GenerationController() override;

    QObject *account() const { return m_accountSession.account(); }
    bool connected() const;
    QString containerPath() const;
    QVariantList models() const;
    QVariantList videoModels() const;
    QString selectedVideoModel() const;
    void setSelectedVideoModel(const QString &id);
    bool videoRuntimeAvailable() const;
    QVariantList vaes() const;
    QString selectedVae() const;
    void setSelectedVae(const QString &id);
    QString selectedModel() const;
    void setSelectedModel(const QString &id);
    QString defaultImageModel() const;
    QString defaultVideoModel() const;
    QString modelPreferencesError() const;
    Q_INVOKABLE bool setDefaultImageModel(const QString &id);
    Q_INVOKABLE bool setDefaultVideoModel(const QString &id);
    QVariantList jobs() const;
    bool busy() const;
    bool runtimeAvailable() const;
    QString errorString() const;
    QUrl latestImage() const;
    QVariantMap latestResult() const;
    QVariantList completedResults() const;
    QUrl previewImage() const;
    QString previewJobId() const;
    int previewStep() const;
    int previewTotalSteps() const;
    bool foreground() const;
    void setForeground(bool foreground);
    QVariantMap inferenceStatus() const;
    bool keepsScreenAwake() const;
    QVariantMap backgroundExecutionStatus() const;

    Q_INVOKABLE bool connectStorage(const QString &path = {});
    Q_INVOKABLE bool selectStorageLocation(const QString &path);
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE QString enqueue(const QString &prompt, const QString &aspectRatio = QStringLiteral("1:1"), int count = 1, qint64 seed = -1);
    Q_INVOKABLE QString enqueueVideo(const QString &prompt, const QString &aspectRatio = QStringLiteral("1:1"),
        int count = 1, const QUrl &firstFrame = {}, int duration = 5, int fps = 24, qint64 seed = -1);
    Q_INVOKABLE QString enqueueVideoRecipe(const QVariantMap &parameters);
    Q_INVOKABLE QString enqueueAdvanced(const QVariantMap &parameters);
    Q_INVOKABLE QString enqueueHomeCanvas(const QVariantMap &parameters, const QString &aspectRatio);
    Q_INVOKABLE bool cancel(const QString &id);

signals:
    void storageChanged();
    void modelsChanged();
    void modelPreferencesChanged();
    void jobsChanged();
    void submissionQueued(const QStringList &jobIds);
    void errorChanged();
    void previewChanged();
    void foregroundChanged();
    void inferenceStatusChanged();
    void screenActivityChanged();

private:
    QString enqueueAdvancedRequest(const QVariantMap &parameters, const QString &aspectRatio = {});
    bool saveModelPreferences(const dreamscapes::ModelPreferences &value);
    QString enqueueRequest(const QString &prompt, const QSize &size, const QString &aspectRatio,
        int count, qint64 seed, int steps, QJsonObject advanced = {}, QJsonObject video = {});
    QVariantMap resultForImage(const QJsonObject &job, const QString &relative) const;
    bool fail(const QString &message);
    void pollStorage();
    void startNative(const QString &model);
    void finishNative();
    bool discardLegacyStorage();
    bool createWorkingFiles(QString *error);
    QStringList resourceArguments(QString *error);
    void clearWorkingFiles();
    bool publishVideo(QString *error);
    bool publishImages(const QStringList &sources, QJsonObject &job, QString *error);
    void updateJob(QJsonObject job);
    void pump();
    void pollDownload();
    void downloadModelInBackground();
    void beginSocietyBackgroundActivity();
    void finish(const QString &state, const QString &error = {});
    bool collectResult(QString *error);
    void stopProcess(bool force);
    void readProcessOutput();
    void sendWorkerRequest();
    void acceptWorkerResult(const QByteArray &line);
    void acceptNativeWorkerProgress(const QByteArray &line);
    void acceptPreview(const QByteArray &line);
    void acceptNativePreview(const iiLocalDiffusion::NativeGenerationPreview &preview);
    void clearPreview();
    bool startWorker(QString *error);
    void prepareForeground();
    void setInferenceStatus(QJsonObject status);
    void updateScreenActivity();
    void interruptNative();
    void setNativePaused(bool paused);
    void startLegacyCacheMigration();

    GenerationRuntime m_runtime;
    iiSocietyHelper::FileSystem m_fileSystem;
    // The UI borrows the same SDK account snapshot used by the Society client.
    AccountSession m_accountSession;
    iiSocietyClient::Client m_societyClient;
    std::shared_ptr<iiSocietyGeneration::Remote> m_remoteGeneration;
    QMap<QString, QString> m_modelDownloads;
    QStringList m_requiredModelFiles;
    bool m_remoteActive = false;
    bool m_societyBackgroundActivity = false;
    QString m_storageSelection;
    QTimer m_storagePoll;
    QFutureWatcher<iiLocalDiffusion::NativeGenerationResult> m_nativeWatcher;
    QFutureWatcher<QString> m_cacheMigrationWatcher;
    std::atomic_bool m_cacheMigrationCancelled{false};
    bool m_cacheMigrationActive = false;
    std::atomic_bool m_nativeCancelled{false};
    QTimer m_nativeDeadline;
    QTimer m_workerDeadline;
    QString m_workerProgressKey;
    bool m_nativeTimedOut = false;
    bool m_interrupted = false;
    bool m_screenActive = false;
    bool m_backgroundActivityActive = false;
    bool m_nativePaused = false;
    int m_nativeTimeRemaining = 0;
    QJsonObject m_resumeInferenceStatus;
    std::optional<iiSocietyContainer::SharedStorage> m_storage;
    QList<iiSocietyContainer::StoredModel> m_models;
    QString m_selected;
    QString m_selectedVae;
    QString m_selectedVideo;
    dreamscapes::ModelPreferences m_modelPreferences;
    QString m_modelPreferencesError;
    bool m_imageModelOverridden = false;
    bool m_videoModelOverridden = false;
    QStringList m_videoModelIds;
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
    QString m_previewJobId;
    int m_previewStep = 0;
    int m_previewTotalSteps = 0;
    int m_previewSequence = 0;
    bool m_cancelled = false;
    bool m_workerReady = false;
    bool m_workerForegroundSupported = false;
    bool m_foreground = false;
    bool m_residencyPending = false;
    QString m_controlId;
    QString m_preparingModel;
    QJsonObject m_inferenceStatus{{"state", "idle"}, {"ready", false}};
    QByteArray m_workerRequest;
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    QProcess m_process;
#endif
};
