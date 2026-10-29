#include <iiFileProviderDevice.h>
#pragma once

#include <SharedStorage.h>
#include <iiSocietyHelper.h>
#include <iiSocietyHelper/Client.h>
#include <iiSocietyHelper/FileDownload.h>
#include <iiSocietyHelper/AccountSession.h>
#include <iiSocietyHelper/Generation/Remote.h>
#include <Generation/NativeDiffusion.hpp>
#include <QFutureWatcher>
#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QSize>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
#include "GenerationProcess.h"
#endif
#include <memory>
#include "GenerationBackgroundActivity.h"
#include "ModelPreferences.h"
#include "GenerationPublication.h"
#include "NativeGenerationWork.h"
class FailureAgent;

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
    dreamscapes::NativeGenerationOperation nativeGenerate;
    std::function<void(bool)> screenActivity;
    std::shared_ptr<GenerationBackgroundActivity> backgroundActivity;
    std::shared_ptr<iiLocalDiffusion::NativeExecutionControl> nativeExecutionControl;
    // Optional transport injection for integration tests; production uses the
    // authenticated Society client and never accepts a model URL.
    std::shared_ptr<iiSocietyHelper::Generation::Remote> remoteGeneration;
    std::function<iiLocalDiffusion::NativeGenerationResult(const iiLocalDiffusion::NativeGenerationRequest &,
        const iiLocalDiffusion::NativeGenerationOptions &, const iiLocalDiffusion::NativeModelComponents &,
        const iiLocalDiffusion::NativeAdvancedControls &, const std::atomic_bool &,
        const iiLocalDiffusion::NativeProgressCallback &, const iiLocalDiffusion::NativePreviewCallback &)> nativeGenerateAdvanced;
    dreamscapes::GenerationPublicationOperation publishResult;
    std::function<bool(const QString &, const QString &, const std::atomic_bool &, QString *)> migrateLegacyCache;
    std::function<void()> releaseNativeCache;
    QString failureAgentExecutable;
    QString failureReportsDirectory;
    int failureAgentTimeoutMilliseconds = 90000;
};

class GenerationController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *account READ account CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY storageChanged)
    Q_PROPERTY(QString containerPath READ containerPath NOTIFY storageChanged)
    Q_PROPERTY(QUrl filesDirectory READ filesDirectory NOTIFY storageChanged)
    Q_PROPERTY(iiSocietyContainer::Container *container READ container CONSTANT)
    Q_PROPERTY(QObject *fileDownload READ fileDownload CONSTANT)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList videoModels READ videoModels NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedVideoModel READ selectedVideoModel WRITE setSelectedVideoModel NOTIFY modelsChanged)
    Q_PROPERTY(bool videoRuntimeAvailable READ videoRuntimeAvailable NOTIFY storageChanged)
    Q_PROPERTY(QVariantList vaes READ vaes NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedVae READ selectedVae WRITE setSelectedVae NOTIFY modelsChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel WRITE setSelectedModel NOTIFY modelsChanged)
    Q_PROPERTY(QString defaultImageModel READ defaultImageModel NOTIFY modelPreferencesChanged)
    Q_PROPERTY(QString defaultVideoModel READ defaultVideoModel NOTIFY modelPreferencesChanged)
    Q_PROPERTY(QVariantMap defaultGenerationPrompts READ defaultGenerationPrompts NOTIFY modelPreferencesChanged)
    Q_PROPERTY(QString modelPreferencesError READ modelPreferencesError NOTIFY modelPreferencesChanged)
    Q_PROPERTY(QStringList videoPresetNames READ videoPresetNames NOTIFY videoPresetsChanged)
    Q_PROPERTY(QString videoPresetError READ videoPresetError NOTIFY videoPresetsChanged)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobsChanged)
    Q_PROPERTY(bool runtimeAvailable READ runtimeAvailable NOTIFY storageChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap latestFailure READ latestFailure NOTIFY failureAnalysisChanged)
    Q_PROPERTY(QUrl latestImage READ latestImage NOTIFY resultsChanged)
    Q_PROPERTY(QVariantMap latestResult READ latestResult NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList completedResults READ completedResults NOTIFY resultsChanged)
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
    QUrl filesDirectory() const;
    iiSocietyContainer::Container *container() const { return m_fileSystem.container(); }
    iiSocietyHelper::FileDownload *fileDownload() { return &m_fileDownload; }
    QVariantList models() const;
    QVariantList videoModels() const;
    Q_INVOKABLE QStringList modelModalities(const QString &id) const;
    Q_INVOKABLE bool setModelModalities(const QString &id, const QStringList &modalities);
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
    QVariantMap defaultGenerationPrompts() const;
    Q_INVOKABLE bool setDefaultGenerationPrompt(const QString &category, const QString &text, bool negative);
    QString modelPreferencesError() const;
    QStringList videoPresetNames() const;
    QString videoPresetError() const { return m_videoPresetError; }
    Q_INVOKABLE QVariantMap videoPreset(const QString &name);
    Q_INVOKABLE bool saveVideoPreset(const QString &name, const QVariantMap &parameters);
    Q_INVOKABLE bool setDefaultImageModel(const QString &id);
    Q_INVOKABLE bool setDefaultVideoModel(const QString &id);
    QVariantList jobs() const;
    bool busy() const;
    bool runtimeAvailable() const;
    QString errorString() const;
    QVariantMap latestFailure() const;
    Q_INVOKABLE QVariantMap failureAnalysis(const QString& jobId) const;
    Q_INVOKABLE QString explainOperationFailure(const QString& operation, const QString& error, const QVariantMap& parameters = {});
signals:
    void failureAnalysisChanged();
    void failureExplained(const QString& jobId, const QVariantMap& report);
public:
    QUrl latestImage() const;
    QVariantMap latestResult() const;
    Q_INVOKABLE QVariantList historyEntries(const QVariantList &files) const;
    Q_INVOKABLE QVariantList searchEntries(const QVariantList &files, const QVariantList &published, const QVariantList &history) const;
    Q_INVOKABLE QVariantMap storedImage(const QVariantMap &file) const;
    // Materializes a local PNG when required by file/input consumers. Views use CanvasPreview.
    Q_INVOKABLE QUrl imagePreview(const QUrl &source) const;
    Q_INVOKABLE QUrl imageDocument(const QUrl &source);
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
    Q_INVOKABLE QString enqueueHomeCanvas(const QVariantMap &parameters, const QString &aspectRatio, bool explicitControls = false);
    Q_INVOKABLE bool cancel(const QString &id);
    Q_INVOKABLE void cancelBatch(const QStringList &ids);

signals:
    void videoPresetsChanged();
    void storageChanged();
    void modelsChanged();
    void modelPreferencesChanged();
    void jobsChanged();
    void resultsChanged();
    void submissionQueued(const QStringList &jobIds);
    void errorChanged();
    void previewChanged();
    void foregroundChanged();
    void inferenceStatusChanged();
    void screenActivityChanged();

private:
    struct ModelInventory {
        QString selection, root, id, error;
        QList<iiSocietyContainer::StoredModel> models;
        QStringList videoModels;
        QMap<QString, QStringList> modalities;
        quint64 epoch = 0;
    };
    void applyModelInventory(QList<iiSocietyContainer::StoredModel> models,
        QStringList videoModels, QMap<QString, QStringList> modalities, const QString &error, bool storageChanged);
    QString enqueueAdvancedRequest(const QVariantMap &parameters, const QString &aspectRatio = {});
    bool saveModelPreferences(const dreamscapes::ModelPreferences &value);
    QString enqueueRequest(const QString &prompt, const QSize &size, const QString &aspectRatio,
        int count, qint64 seed, int steps, QJsonObject advanced = {}, QJsonObject video = {});
    QVariantMap resultForImage(const QJsonObject &job, const QString &relative) const;
    void notifyResultsChanged();
    QVariantMap projectLatestResult() const;
    QVariantList projectCompletedResults() const;
    bool fail(const QString &message);
    void pollStorage();
    void startNative(const QString &model);
    void finishNative();
    void beginPublication(dreamscapes::PublicationOrigin origin,
        std::optional<iiLocalDiffusion::NativeGenerationResult> native = {}, QByteArray png = {});
    void completePublication();
    QFuture<QString> releaseNativeCache();
    void completeCacheRelease();
    bool discardLegacyStorage();
    bool createWorkingFiles(QString *error);
    QStringList resourceArguments(QString *error);
    QFuture<void> clearWorkingFiles();
    QString preserveImageInput(const QString &source, const QString &destination, QString *error);
    QString imageInputPath(const QString &source, QString *error);
    void updateJob(QJsonObject job);
    void pump();
    void pollDownload();
    void downloadModelInBackground();
    void beginSocietyBackgroundActivity();
    void finish(const QString &state, const QString &error = {});
    void completeFinish();
    void ensureFailureAgent();
    void stopProcess(bool force);
    void readProcessOutput(const QByteArray &data);
    void checkWorkerDeadline(quint64 token);
    void sendWorkerRequest();
    void acceptWorkerResult(const QByteArray &line);
    void acceptNativeWorkerProgress(const QByteArray &line);
    void acceptPreview(const QByteArray &line);
    void acceptNativeProgress(const QString &id, const iiLocalDiffusion::NativeGenerationProgress &event);
    void acceptNativePreview(const QString &id, const dreamscapes::NativePreviewArtifact &preview);
    void clearPreview();
    bool startWorker(QString *error);
    void prepareForeground();
    void setInferenceStatus(QJsonObject status, bool recordFailureProgress = true);
    void updateScreenActivity();
    void interruptNative();
    void setNativePaused(bool paused);
    void startLegacyCacheMigration();

    GenerationRuntime m_runtime;
    iiSocietyHelper::Helper m_storageHelper;
    iiSocietyHelper::FileSystem &m_fileSystem;
    // The UI borrows the same SDK account snapshot used by the Society client.
    AccountSession m_accountSession;
    iiSocietyHelper::Client m_societyClient;
    // Explicit user-file hydration has its own in-process authenticated session.
    // It never waits for a Society app's replica-session lease or cancels jobs.
    iiSocietyHelper::Client m_fileClient;
    iiSocietyHelper::FileDownload m_fileDownload;
    std::shared_ptr<iiSocietyHelper::Generation::Remote> m_remoteGeneration;
    QMap<QString, QString> m_modelDownloads;
    QStringList m_requiredModelFiles;
    bool m_remoteActive = false;
    bool m_societyBackgroundActivity = false;
    QString m_storageSelection;
    QTimer m_storagePoll;
    QFutureWatcher<ModelInventory> m_inventoryWatcher;
    quint64 m_inventoryEpoch = 0;
    QFutureWatcher<iiLocalDiffusion::NativeGenerationResult> m_nativeWatcher;
    QFutureWatcher<dreamscapes::GenerationPublicationResult> m_publicationWatcher;
    QFutureWatcher<void> m_cleanupWatcher;
    QString m_finishState, m_finishError;
    std::shared_ptr<dreamscapes::PublicationControl> m_publicationControl;
    QString m_publicationJob, m_publicationRoot, m_publicationContainer;
    QJsonObject m_publicationResumeStatus;
    bool m_publicationPending = false, m_finishing = false, m_closing = false;
    bool m_nativeDispatched = false;
    QFutureWatcher<QString> m_cacheMigrationWatcher;
    QFutureWatcher<QString> m_cacheReleaseWatcher;
    bool m_cacheReleasePending = false;
    QString m_cacheReleaseJob;
    std::shared_ptr<std::atomic_bool> m_cacheMigrationCancelled = std::make_shared<std::atomic_bool>(false);
    bool m_cacheMigrationActive = false;
    std::shared_ptr<dreamscapes::NativeGenerationState> m_nativeState = std::make_shared<dreamscapes::NativeGenerationState>();
    std::shared_ptr<QObject> m_nativeCallbacks;
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
    bool m_storageReady = false;
    QList<iiSocietyContainer::StoredModel> m_models;
    QString m_selected;
    QString m_selectedVae;
    QString m_selectedVideo;
    dreamscapes::ModelPreferences m_modelPreferences;
    QString m_modelPreferencesError;
    QVariantMap m_memoryVideoPresets;
    QString m_videoPresetError;
    bool m_imageModelOverridden = false;
    bool m_videoModelOverridden = false;
    QStringList m_videoModelIds;
    QMap<QString, QStringList> m_modelModalities;
    QList<QJsonObject> m_jobs;
    QVariantList m_notifiedResults;
    QVariantMap m_notifiedLatestResult;
    QJsonObject m_active;
    QString m_output;
    QString m_error;
    FailureAgent* m_failureAgent = nullptr;
    QJsonObject m_latestFailure;
    QJsonObject m_failureProgress;
    QList<QJsonObject> m_operationFailureReports;
    QByteArray m_log;
    QByteArray m_pendingOutput;
    std::shared_ptr<iiFileProvider::TemporaryDirectory> m_workDirectory;
    std::shared_ptr<iiFileProvider::TemporaryDirectory> m_workerDirectory;
    std::shared_ptr<iiFileProvider::TemporaryDirectory> m_previewDirectory;
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
    GenerationProcess m_process;
    quint64 m_workerProbe = 0;
    QJsonValue m_workerProbeJob;
#endif
};
