#include <File.h>
#include <iiFileProviderDevice.h>
#include "GenerationController.h"
#include "ImageParameterCodec.h"
#include "VideoGeneration.h"
#include "AdvancedImageOutput.h"
#include "SocietyGenerationStorage.h"
#include "ImageCanvas.h"
#include "FailureAgent.h"
#include <ModelPackaging/ModelInspection.hpp>
#include <QtConcurrent/QtConcurrentRun>
#include <QPointer>
#if defined(Q_OS_IOS)
#include "GenerationScreenActivity.h"
#endif

#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <limits>
#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace iiSocietyContainer;
namespace {
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QMap<QString, QStringList> inspectModels(const QString &root, const QList<StoredModel> &models)
{
    QMap<QString, QStringList> result;
    for (const auto &model : models) {
        const auto info = iild::inspectModel(iiFileProvider::Directory(root).filePath("Models/" + model.id).toStdU16String());
        QStringList modalities;
        for (const auto &modality : info.modalities) modalities.append(QString::fromStdString(modality));
        // Older inventories contain checkpoints without architecture metadata.
        // Retain their legacy image eligibility until the user classifies them.
        if (modalities.isEmpty()) modalities.append("image");
        result.insert(model.id, modalities);
    }
    return result;
}
bool jobId(const QString &value)
{
    return !QUuid(value).isNull() && QUuid(value).toString(QUuid::WithoutBraces) == value;
}
GenerationRuntime defaultRuntime()
{
    GenerationRuntime runtime;
    runtime.modelPreferencesFile = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + "/generation-models.conf";
    runtime.failureReportsDirectory = qEnvironmentVariable("DREAMSCAPES_FAILURE_REPORTS_DIR",
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/FailureReports");
    runtime.executable = qEnvironmentVariable("IILD_GENERATOR_EXECUTABLE");
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    runtime.nativeInference = true;
#elif defined(Q_OS_MACOS)
    // Native inference is the desktop default; respect an explicit process runtime.
    runtime.nativeInference = runtime.executable.isEmpty();
#elif defined(Q_OS_WIN)
    runtime.nativeInference = runtime.executable.isEmpty() && iiLocalDiffusion::nativeDiffusionAvailable();
#endif
#if defined(Q_OS_IOS)
    runtime.screenActivity = nativeGenerationScreenActivity();
    runtime.backgroundActivity = nativeGenerationBackgroundActivity();
    const QFileInfo previousCache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    // Normalize OS aliases such as /var before checking for redirected cache entries.
    runtime.legacyQ8CacheDirectory = iiFileProvider::Directory(previousCache.exists()
        ? previousCache.canonicalFilePath() : previousCache.absoluteFilePath()).filePath("iiLocalDiffusion/q8");
#endif
    if (runtime.executable.isEmpty())
        runtime.executable = QStringLiteral(DREAMSCAPES_DIFFUSION_EXECUTABLE);
    runtime.pythonExecutable = QStringLiteral(DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE);
#ifdef DREAMSCAPES_LOCAL_RUNTIME_PROBE
    // Device benchmarks can reproduce historical sizes without changing the
    // production QuickGenerate resolution contract.
    bool validExtent = false;
    const auto extent = qEnvironmentVariableIntValue("DREAMSCAPES_PROBE_EXTENT", &validExtent);
    if (validExtent && extent >= 64 && extent <= 2048 && extent % 8 == 0) runtime.imageExtent = extent;
#endif
    return runtime;
}
std::filesystem::path preferencePath(const QString &path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toStdString());
#endif
}
}

GenerationController::GenerationController(QObject *parent) : GenerationController(defaultRuntime(), parent) {}
GenerationController::GenerationController(GenerationRuntime runtime, QObject *parent)
    : QObject(parent), m_runtime(std::move(runtime)), m_fileSystem(*m_storageHelper.fileSystem()), m_societyClient(&m_accountSession, nullptr),
      m_fileClient(&m_accountSession, nullptr), m_fileDownload(&m_fileSystem, &m_fileClient)
{
    std::string preferenceError;
    connect(this, &GenerationController::jobsChanged, this, &GenerationController::notifyResultsChanged);
    connect(container(), &iiSocietyContainer::Container::stateChanged, this,
        [this, revision = container()->revision()]() mutable {
            if (revision == container()->revision()) return; // Search/filter presentation only.
            revision = container()->revision(); notifyResultsChanged();
        });
    connect(container(), &iiSocietyContainer::Container::stateChanged, this,
        [this, previousId = container()->containerId(), previousRoot = container()->rootPath(),
         previousAvailable = container()->available()]() mutable {
            const auto current = container();
            if (previousId == current->containerId() && previousRoot == current->rootPath()
                && previousAvailable == current->available()) return;
            previousId = current->containerId(); previousRoot = current->rootPath();
            previousAvailable = current->available(); emit storageChanged();
        });
    dreamscapes::readModelPreferences(preferencePath(m_runtime.modelPreferencesFile), m_modelPreferences, preferenceError);
    m_modelPreferencesError = QString::fromStdString(preferenceError);
#ifdef Q_OS_WIN
    if (m_runtime.pythonExecutable.isEmpty())
        m_runtime.pythonExecutable = QStringLiteral(DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE);
#endif
    if (!m_runtime.nativeExecutionControl)
        m_runtime.nativeExecutionControl = std::make_shared<iiLocalDiffusion::NativeExecutionControl>();
    m_storagePoll.setInterval(2000);
    connect(&m_storagePoll, &QTimer::timeout, this, &GenerationController::pollStorage);
    connect(&m_storagePoll, &QTimer::timeout, this, &GenerationController::pollDownload);
    connect(&m_societyClient, &iiSocietyHelper::Client::synchronized, this, [this] { pollDownload(); pollStorage(); });
    connect(&m_inventoryWatcher, &QFutureWatcher<ModelInventory>::finished, this, [this] {
        auto inventory = m_inventoryWatcher.result();
        if (inventory.epoch != m_inventoryEpoch || inventory.selection != m_storageSelection
            || !m_foreground || busy() || m_cacheMigrationActive) return;
        // Opening a storage selection also reconstructs its complete file mirror.
        // Do that only for an actual selection/availability transition.
        if (!m_storageReady || inventory.root != container()->rootPath()
            || inventory.id != container()->containerId() || !inventory.error.isEmpty()) {
            const auto previousError = m_error;
            const bool previouslyReady = connected();
            refreshModels();
            if (previouslyReady && m_error.isEmpty() && !previousError.isEmpty()) fail(previousError);
            return;
        }
        const bool unchanged = inventory.models.size() == m_models.size()
            && std::equal(inventory.models.cbegin(), inventory.models.cend(), m_models.cbegin(),
                [](const StoredModel &a, const StoredModel &b) {
                    return a.id == b.id && a.name == b.name && a.format == b.format
                        && a.fingerprint == b.fingerprint && a.available == b.available && a.bytes == b.bytes;
                }) && inventory.modalities == m_modelModalities;
        if (unchanged) return;
        const auto previousError = m_error;
        applyModelInventory(std::move(inventory.models), std::move(inventory.videoModels), std::move(inventory.modalities), {}, false);
        if (m_error.isEmpty() && !previousError.isEmpty()) fail(previousError);
    });
    m_remoteGeneration = m_runtime.remoteGeneration ? m_runtime.remoteGeneration
        : std::make_shared<iiSocietyHelper::Generation::Remote>([this](const QJsonObject &request) { return m_societyClient.requestHost(request); });
    connect(&m_societyClient, &iiSocietyHelper::Client::hostResponse, m_remoteGeneration.get(), &iiSocietyHelper::Generation::Remote::receive);
    connect(m_remoteGeneration.get(), &iiSocietyHelper::Generation::Remote::accepted, this, [this] {
        if (!m_remoteActive || !busy()) return;
        m_active["state"] = "running"; m_active["execution"] = "host"; m_active["startedAt"] = now();
        updateJob(m_active);
        beginSocietyBackgroundActivity();
        downloadModelInBackground();
    });
    connect(m_remoteGeneration.get(), &iiSocietyHelper::Generation::Remote::progress, this, [this](const QJsonObject &status) {
        if (m_remoteActive && busy()) {
            setInferenceStatus(status);
            if (m_societyBackgroundActivity && m_runtime.backgroundActivity && !status.value("phase").toString().isEmpty()) {
                using Stage = iiLocalDiffusion::NativeGenerationStage;
                const auto phase = status.value("phase").toString();
                const auto stage = phase == "loading" ? Stage::Loading : phase == "encoding" ? Stage::Encoding
                    : phase == "denoising" ? Stage::Denoising : phase == "decoding" ? Stage::Decoding
                    : phase == "waiting" ? Stage::Waiting : phase == "computing" ? Stage::Computing : Stage::Preparing;
                m_runtime.backgroundActivity->update({stage, status.value("step").toInt(), status.value("total").toInt()});
            }
        }
    });
    connect(&m_societyClient, &iiSocietyHelper::Client::progress, this, [this](const QString &, qint64 done, qint64 total) {
        if (m_societyBackgroundActivity && m_runtime.backgroundActivity && total > 0)
            m_runtime.backgroundActivity->update({iiLocalDiffusion::NativeGenerationStage::Preparing,
                int(done * 1000 / total), 1000});
    });
    connect(m_remoteGeneration.get(), &iiSocietyHelper::Generation::Remote::finished, this,
        [this](const QString &state, const QByteArray &png, const QJsonObject &generation, const QString &remoteError) {
        if (!m_remoteActive || !busy() || m_publicationPending || m_finishing) return;
        if (state != "completed") { finish(state, remoteError); return; }
        QString error;
        if (!createWorkingFiles(&error)) { finish("failed", error); return; }
        m_active["generation"] = generation;
        beginPublication(dreamscapes::PublicationOrigin::Remote, {}, png);
    });
    connect(&m_nativeWatcher, &QFutureWatcher<iiLocalDiffusion::NativeGenerationResult>::finished,
            this, &GenerationController::finishNative);
    connect(&m_publicationWatcher, &QFutureWatcher<dreamscapes::GenerationPublicationResult>::finished,
            this, &GenerationController::completePublication);
    connect(&m_cleanupWatcher, &QFutureWatcher<void>::finished,
            this, &GenerationController::completeFinish);
    connect(&m_cacheReleaseWatcher, &QFutureWatcher<QString>::finished,
            this, &GenerationController::completeCacheRelease);
    connect(&m_cacheMigrationWatcher, &QFutureWatcher<QString>::finished, this, [this] {
        const QPointer<GenerationController> owner(this);
        m_cacheMigrationActive = false;
        const auto error = m_cacheMigrationWatcher.result();
        if (!error.isEmpty()) fail(error);
        if (owner) QTimer::singleShot(0, this, &GenerationController::pump);
    });
    m_nativeDeadline.setSingleShot(true);
    connect(&m_nativeDeadline, &QTimer::timeout, this, [this] {
        if (!busy() || m_nativeWatcher.isFinished() || m_publicationPending || m_finishing) return;
        m_nativeTimedOut = true;
        m_nativeState->cancelled = true;
        setInferenceStatus({{"state", "cancelling"}, {"ready", false}, {"backend", "native"}});
    });
    m_workerDeadline.setSingleShot(true);
    m_workerDeadline.setParent(this);
    m_workerDeadline.setObjectName(QStringLiteral("generationWorkerProgressWatchdog"));
    connect(&m_workerDeadline, &QTimer::timeout, this, [this] {
        if (!busy() || m_cacheReleasePending || m_publicationPending || m_finishing || (m_runtime.nativeInference && m_active.value("mediaType") != "Video") || m_remoteActive || m_cancelled) return;
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
        // An expired GUI timer requests a native pipe barrier. Already-arrived
        // output reaches the controller before this liveness decision.
        m_workerProbeJob = m_active.value("id");
        m_process.probeOutput(++m_workerProbe);
#endif
    });
    m_storagePoll.start();
    if (m_runtime.backgroundActivity) {
        const QPointer<GenerationController> self(this);
        m_runtime.backgroundActivity->observeForeground([self](bool foreground) {
            if (self) self->setForeground(foreground);
        });
    } else if (auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(application, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            // A temporary inactive state (an alert or Control Center) is not
            // background execution. Cancel only after the app is hidden.
            setForeground(m_runtime.nativeInference
                ? state == Qt::ApplicationActive || state == Qt::ApplicationInactive
                : state == Qt::ApplicationActive);
        });
        setForeground(application->applicationState() == Qt::ApplicationActive
            || (m_runtime.nativeInference && application->applicationState() == Qt::ApplicationInactive));
    }
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    ii::process::EnvironmentPatch environment{{"IILD_WORKER_PROGRESS", "1"}, {"IILD_MODEL_VALIDATION", "metadata"}};
    if (!qEnvironmentVariableIsSet("IILD_PYTHON_EXECUTABLE") && !m_runtime.pythonExecutable.isEmpty())
        environment["IILD_PYTHON_EXECUTABLE"] = m_runtime.pythonExecutable.toStdString();
    m_process.setProcessEnvironment(std::move(environment));
    connect(&m_process, &GenerationProcess::outputAvailable, this, &GenerationController::readProcessOutput);
    connect(&m_process, &GenerationProcess::outputProbed, this, &GenerationController::checkWorkerDeadline);
    connect(&m_process, &GenerationProcess::started, this, [this] {
        if (m_cancelled) stopProcess(false);
    });
    connect(&m_process, &GenerationProcess::errorOccurred, this, [this](GenerationProcess::ProcessError error) {
        if (error == GenerationProcess::FailedToStart) {
            const QPointer<GenerationController> owner(this);
            m_workerReady = false;
            m_workerRequest.clear();
            auto retired = std::move(m_workerDirectory);
            if (retired) (void)QtConcurrent::run([retired = std::move(retired)] {});
            m_controlId.clear();
            setInferenceStatus({{"state", "error"}, {"ready", false}, {"error", m_process.errorString()}});
            if (!owner) return;
            if (busy() && !m_remoteActive && !m_publicationPending && !m_finishing)
                finish(m_cancelled ? "cancelled" : "failed", m_cancelled ? QString() : m_process.errorString());
        }
    });
    connect(&m_process, &GenerationProcess::finished, this, [this](qint64 code, GenerationProcess::ExitStatus exitStatus) {
        const QPointer<GenerationController> owner(this);
        m_workerReady = false;
        m_workerRequest.clear();
        auto retired = std::move(m_workerDirectory);
        if (retired) (void)QtConcurrent::run([retired = std::move(retired)] {});
        m_controlId.clear();
        if (m_publicationPending || m_finishing || m_cacheReleasePending) return;
        if (busy()) {
            m_active["workerExitCode"] = code;
            m_active["workerExitStatus"] = exitStatus == GenerationProcess::CrashExit ? "crash" : "normal";
        }
        setInferenceStatus({{"state", "stopped"}, {"ready", false}});
        if (!owner) return;
        if (!busy() || m_remoteActive) { QTimer::singleShot(0, this, &GenerationController::pump); return; }
        if (m_nativeTimedOut) { finish("failed", m_active.value("timeoutError").toString()); return; }
        if (m_cancelled) { finish("cancelled"); return; }
        finish("failed", tr("The inference worker exited before completing the request (exit %1). %2").arg(code)
            .arg(QString::fromUtf8(m_log).right(4000)));
    });
#endif
}

GenerationController::~GenerationController()
{
    m_closing = true;
    if (m_failureAgent) m_failureAgent->close();
    if (m_publicationControl) m_publicationControl->cancel();
    disconnect(&m_publicationWatcher, nullptr, this, nullptr);
    disconnect(&m_cleanupWatcher, nullptr, this, nullptr);
    disconnect(&m_cacheReleaseWatcher, nullptr, this, nullptr);
    if (m_screenActive && m_runtime.screenActivity) m_runtime.screenActivity(false);
    m_nativeState->cancelled = true;
    m_nativeState->accepting = false;
    m_runtime.nativeExecutionControl->setPaused(false);
    *m_cacheMigrationCancelled = true;
    disconnect(&m_cacheMigrationWatcher, nullptr, this, nullptr);
    disconnect(&m_nativeWatcher, nullptr, this, nullptr);
    if (m_backgroundActivityActive) m_runtime.backgroundActivity->end(false);
    if (m_runtime.nativeInference && !m_cacheReleasePending) (void)releaseNativeCache();
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    disconnect(&m_process, nullptr, this, nullptr);
    // The native owner retires its process tree before releasing these paths.
    // Presentation removal never joins a pipe, process or temporary directory.
    m_process.retire(std::make_shared<std::pair<decltype(m_workDirectory), decltype(m_previewDirectory)>>(
        m_workDirectory, m_previewDirectory));
    auto workerDirectory = std::move(m_workerDirectory);
    if (workerDirectory) (void)QtConcurrent::run([workerDirectory = std::move(workerDirectory)] {});
#endif
    clearWorkingFiles();
}

bool GenerationController::connected() const { return m_storageReady && container()->available(); }
QString GenerationController::containerPath() const { return container()->rootPath(); }
QString GenerationController::selectedModel() const { return m_selected; }
bool GenerationController::busy() const { return !m_active.isEmpty(); }
QString GenerationController::errorString() const { return m_error; }
QVariantMap GenerationController::latestFailure() const { return m_latestFailure.toVariantMap(); }
QVariantMap GenerationController::failureAnalysis(const QString& id) const {
    for (const auto& job : m_jobs) if (job["id"] == id) return job["failureAnalysis"].toObject().toVariantMap();
    for (const auto& report : m_operationFailureReports) if (report["jobId"] == id) return report.toVariantMap();
    return {};
}
QString GenerationController::explainOperationFailure(const QString& operation, const QString& error, const QVariantMap& parameters) {
    if (m_closing || error.trimmed().isEmpty()) return {};
    ensureFailureAgent();
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto reports = m_runtime.failureReportsDirectory;
    QJsonObject evidence{{"job", QJsonObject{{"id", id}, {"mediaType", operation}, {"parameters", QJsonObject::fromVariantMap(parameters)}, {"finishedAt", now()}}},
        {"inference", QJsonObject{{"state", "operation-failed"}, {"backend", "application"}}}, {"rawError", error.left(16384)},
        {"modelsDirectory", containerPath() + "/Models/.society-runtime/iiLocalLLM/Models"},
        {"sessionsDirectory", reports.isEmpty() ? QString() : reports + "/.sessions"},
        {"reportPath", reports.isEmpty() ? QString() : reports + "/." + id + ".json"}};
    m_latestFailure = m_failureAgent->submit(evidence);
    m_operationFailureReports.append(m_latestFailure);
    if (m_operationFailureReports.size() > 16) m_operationFailureReports.removeFirst();
    emit failureAnalysisChanged();
    return id;
}
QUrl GenerationController::previewImage() const { return m_previewImage; }
QString GenerationController::previewJobId() const { return m_previewJobId; }
int GenerationController::previewStep() const { return m_previewStep; }
int GenerationController::previewTotalSteps() const { return m_previewTotalSteps; }
bool GenerationController::foreground() const { return m_foreground; }
QVariantMap GenerationController::inferenceStatus() const { return m_inferenceStatus.toVariantMap(); }
bool GenerationController::keepsScreenAwake() const { return m_screenActive; }
QVariantMap GenerationController::backgroundExecutionStatus() const
{
    auto status = m_runtime.backgroundActivity ? m_runtime.backgroundActivity->status() : QVariantMap();
    status.insert("paused", m_nativePaused);
    status.insert("waitingAtEngineBoundary", m_runtime.nativeExecutionControl->isWaiting());
    status.insert("pausedMilliseconds", std::chrono::duration<double, std::milli>(
        m_runtime.nativeExecutionControl->pausedDuration()).count());
    return status;
}
void GenerationController::updateScreenActivity()
{
    const bool active = m_runtime.nativeInference && !m_remoteActive && !m_publicationPending && !m_finishing && busy() && m_foreground;
    if (m_screenActive == active) return;
    m_screenActive = active;
    const QPointer<GenerationController> owner(this);
    if (m_runtime.screenActivity) m_runtime.screenActivity(active);
    if (owner) emit screenActivityChanged();
}
void GenerationController::setInferenceStatus(QJsonObject status)
{
    const auto stage = status["state"].toString();
    auto phase = status["phase"].toString(); if (phase.isEmpty()) phase = stage;
    static const QStringList workStages{"waiting", "waiting-model", "waiting-generation", "preparing", "preparing-model", "checking-model",
        "loading", "encoding", "computing", "denoising", "decoding", "interpolating", "encoding-video", "publishing",
        "connecting-host", "downloading", "running", "generating", "complete"};
    if (busy() && !m_finishing && workStages.contains(phase)) m_failureProgress = status;
    if (m_inferenceStatus == status) return;
    m_inferenceStatus = std::move(status);
    emit inferenceStatusChanged();
}
void GenerationController::setForeground(bool foreground)
{
    if (m_closing) return;
    const QPointer<GenerationController> owner(this);
    const auto current = [owner, foreground] { return owner && !owner->m_closing && owner->m_foreground == foreground; };
    if (m_foreground == foreground) {
        if (!foreground && m_societyBackgroundActivity && connected() && m_runtime.backgroundActivity->allowsBackgroundExecution()) {
            m_societyClient.start(containerPath());
            if (!current()) return;
            m_remoteGeneration->setPaused(false);
            if (!current()) return;
            QTimer::singleShot(0, this, &GenerationController::pump);
        }
        if (busy() && !m_remoteActive && m_runtime.backgroundActivity && !foreground)
            setNativePaused(!m_runtime.backgroundActivity->allowsBackgroundExecution());
        return;
    }
    m_foreground = foreground;
    if (foreground && connected()) m_societyClient.start(containerPath());
    if (!current()) return;
    const bool canTransfer = foreground || (m_runtime.backgroundActivity && m_runtime.backgroundActivity->allowsBackgroundExecution());
    if (!canTransfer) m_societyClient.stop();
    if (!current()) return;
    if (m_remoteGeneration) m_remoteGeneration->setPaused(!canTransfer);
    if (!current()) return;
    if (!foreground && m_runtime.nativeInference && !m_remoteActive && busy() && m_active.value("mediaType") != "Video") {
        if (m_runtime.backgroundActivity)
            setNativePaused(!m_runtime.backgroundActivity->allowsBackgroundExecution());
        else interruptNative();
        if (!current()) return;
    }
    if (foreground) setNativePaused(false);
    if (!current()) return;
    updateScreenActivity();
    if (!current()) return;
    m_residencyPending = true;
    if (!busy())
        setInferenceStatus({{"state", foreground ? "preparing" : "background"},
                            {"foreground", foreground}, {"ready", false}});
    if (!current()) return;
    QTimer::singleShot(0, this, &GenerationController::pump);
    emit foregroundChanged();
}
void GenerationController::setNativePaused(bool paused)
{
    if (m_publicationPending || m_finishing || (m_nativeDispatched && m_nativeWatcher.isFinished())) return;
    if (m_nativePaused == paused || m_cancelled) return;
    const QPointer<GenerationController> owner(this);
    m_nativePaused = paused;
    if (m_runtime.backgroundActivity) m_runtime.backgroundActivity->setPresentationPaused(paused);
    if (!owner) return;
    m_runtime.nativeExecutionControl->setPaused(paused);
    if (paused) {
        m_resumeInferenceStatus = m_inferenceStatus;
        if (m_nativeDeadline.isActive()) m_nativeTimeRemaining = m_nativeDeadline.remainingTime();
        m_nativeDeadline.stop();
        setInferenceStatus({{"state", "paused"}, {"ready", false}, {"backend", "native"}});
    } else if (busy()) {
        if (!m_nativeWatcher.isFinished()) m_nativeDeadline.start(std::max(1, m_nativeTimeRemaining));
        setInferenceStatus(m_resumeInferenceStatus);
    }
}
void GenerationController::interruptNative()
{
    if (!busy() || m_cancelled || m_publicationPending || m_finishing || (m_nativeDispatched && m_nativeWatcher.isFinished())) return;
    m_interrupted = true;
    m_cancelled = true;
    m_nativeState->cancelled = true;
    setInferenceStatus({{"state", "cancelling"}, {"ready", false}, {"backend", "native"}});
}
bool GenerationController::runtimeAvailable() const
{
    if (m_runtime.nativeInference) return m_runtime.nativeGenerate || m_runtime.nativeGenerateAdvanced || iiLocalDiffusion::nativeDiffusionAvailable();
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return false;
#else
#ifdef Q_OS_WIN
    if (!m_runtime.executable.endsWith(".exe", Qt::CaseInsensitive))
        return QFileInfo(m_runtime.executable).isFile() && QFileInfo(m_runtime.pythonExecutable).isFile()
            && QFileInfo(m_runtime.pythonExecutable).isExecutable();
#endif
    return QFileInfo(m_runtime.executable).isExecutable() && QFileInfo(m_runtime.executable).isFile();
#endif
}
bool GenerationController::fail(const QString &message) { m_error = message; emit errorChanged(); return false; }
QVariantList GenerationController::models() const
{
    QVariantList result;
    for (const auto &model : m_models)
        if (!model.id.startsWith("VAE/", Qt::CaseInsensitive) && m_modelModalities.value(model.id).contains("image"))
            result.append(QVariantMap{{"id", model.id}, {"name", model.name}, {"format", model.format},
                {"available", model.available}, {"bytes", model.bytes}});
    return result;
}
bool GenerationController::videoRuntimeAvailable() const
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return false;
#else
#ifdef Q_OS_WIN
    if (!m_runtime.executable.endsWith(".exe", Qt::CaseInsensitive))
        return QFileInfo(m_runtime.executable).isFile() && QFileInfo(m_runtime.pythonExecutable).isFile()
            && QFileInfo(m_runtime.pythonExecutable).isExecutable();
#endif
    return QFileInfo(m_runtime.executable).isExecutable() && QFileInfo(m_runtime.executable).isFile();
#endif
}
QVariantList GenerationController::videoModels() const
{
    QVariantList result;
    for (const auto &model : m_models)
        if (m_videoModelIds.contains(model.id))
            result.append(QVariantMap{{"id",model.id},{"name",model.name},{"available",model.available}});
    return result;
}
QStringList GenerationController::modelModalities(const QString &id) const { return m_modelModalities.value(id); }
bool GenerationController::setModelModalities(const QString &id, const QStringList &modalities)
{
    if (busy() || m_cacheMigrationActive) return fail(tr("Wait for the current generation before changing model classification."));
    const auto found = std::find_if(m_models.cbegin(), m_models.cend(), [&](const auto &model) { return model.id == id; });
    if (found == m_models.cend() || !found->available) return fail(tr("Select a locally available model to classify."));
    QString error;
    const auto path = m_fileSystem.path(StoreSection::Models, id, &error);
    if (path.isEmpty()) return fail(error);
    try {
        std::vector<std::string> values;
        for (const auto &value : modalities) values.push_back(value.toStdString());
        iild::setModelModalities(path.toStdU16String(), values);
        refreshModels();
        return m_error.isEmpty();
    } catch (const std::exception &failure) { return fail(QString::fromUtf8(failure.what())); }
}
QString GenerationController::selectedVideoModel() const { return m_selectedVideo; }
void GenerationController::setSelectedVideoModel(const QString &id)
{
    if (!m_videoModelIds.contains(id)) return;
    m_videoModelOverridden = true;
    if (m_selectedVideo == id) return;
    m_selectedVideo = id;
    fail({}); emit modelsChanged();
}
QVariantList GenerationController::vaes() const
{
    QVariantList result;
    for (const auto &model : m_models)
        if (model.id.startsWith("VAE/", Qt::CaseInsensitive) && model.format == "safetensors")
            result.append(QVariantMap{{"id", model.id}, {"name", model.name}, {"available", model.available}});
    return result;
}
QString GenerationController::selectedVae() const { return m_selectedVae; }
void GenerationController::setSelectedVae(const QString &id)
{
    if (!id.isEmpty() && std::none_of(m_models.cbegin(), m_models.cend(), [&](const auto &model) {
            return model.id == id && model.id.startsWith("VAE/", Qt::CaseInsensitive) && model.format == "safetensors";
        })) { fail(tr("Choose a VAE from Society's VAE folder.")); return; }
    if (m_selectedVae == id) return;
    m_selectedVae = id;
    fail({});
    m_residencyPending = true;
    emit modelsChanged();
}
void GenerationController::setSelectedModel(const QString &id)
{
    if (id.startsWith("VAE/", Qt::CaseInsensitive) || !m_modelModalities.value(id).contains("image")) return;
    if (std::none_of(m_models.cbegin(), m_models.cend(), [&](const auto &model) { return model.id == id; })) return;
    m_imageModelOverridden = true;
    if (m_selected == id) return;
    m_selected = id;
    // Keep the runtime and its anonymous model sources alive. The pending
    // selection is prepared when the current preparation reaches its boundary.
    m_residencyPending = true;
    setInferenceStatus({{"state", "waiting-model"}, {"ready", false}});
    emit modelsChanged();
    QTimer::singleShot(0, this, &GenerationController::pump);
}
QString GenerationController::defaultImageModel() const { return QString::fromStdString(m_modelPreferences.imageModel); }
QString GenerationController::defaultVideoModel() const { return QString::fromStdString(m_modelPreferences.videoModel); }
QVariantMap GenerationController::defaultGenerationPrompts() const
{
    QVariantMap result;
    for (std::size_t i = 0; i < dreamscapes::generationPromptCategories.size(); ++i) {
        const auto category = dreamscapes::generationPromptCategories[i];
        const auto &prompts = m_modelPreferences.prompts[i];
        result.insert(QString::fromUtf8(category.data(), qsizetype(category.size())),
            QVariantMap{{"prompt", QString::fromStdString(prompts.prompt)},
                {"negativePrompt", QString::fromStdString(prompts.negativePrompt)}});
    }
    return result;
}
bool GenerationController::setDefaultGenerationPrompt(const QString &category, const QString &text, bool negative)
{
    const auto &categories = dreamscapes::generationPromptCategories;
    const auto found = std::find_if(categories.begin(), categories.end(), [&](const auto name) {
        return QString::fromUtf8(name.data(), qsizetype(name.size())) == category;
    });
    if (found == categories.end() || text.size() > 32000) {
        m_modelPreferencesError = tr("Choose a generation category and a prompt of at most 32,000 characters.");
        emit modelPreferencesChanged(); return false;
    }
    auto value = m_modelPreferences;
    auto &prompts = value.prompts[std::size_t(found - categories.begin())];
    (negative ? prompts.negativePrompt : prompts.prompt) = text.toStdString();
    return saveModelPreferences(value);
}
QString GenerationController::modelPreferencesError() const { return m_modelPreferencesError; }
bool GenerationController::saveModelPreferences(const dreamscapes::ModelPreferences &value)
{
    if (value == m_modelPreferences && m_modelPreferencesError.isEmpty()) return true;
    std::string error;
    if (!dreamscapes::writeModelPreferences(preferencePath(m_runtime.modelPreferencesFile), value, error)) {
        m_modelPreferencesError = QString::fromStdString(error);
        emit modelPreferencesChanged();
        return false;
    }
    m_modelPreferences = value;
    m_modelPreferencesError.clear();
    emit modelPreferencesChanged();
    return true;
}
bool GenerationController::setDefaultImageModel(const QString &id)
{
    const auto images = models();
    if (!id.isEmpty() && std::none_of(images.cbegin(), images.cend(), [&](const auto &model) {
        return model.toMap().value("id").toString() == id;
    })) {
        m_modelPreferencesError = tr("Choose an image generation model from Society.");
        emit modelPreferencesChanged(); return false;
    }
    auto value = m_modelPreferences;
    value.imageModel = id.toStdString();
    if (!saveModelPreferences(value)) return false;
    setSelectedModel(id.isEmpty() && !images.isEmpty() ? images.first().toMap().value("id").toString() : id);
    m_imageModelOverridden = false;
    return true;
}
bool GenerationController::setDefaultVideoModel(const QString &id)
{
    if (!id.isEmpty() && !m_videoModelIds.contains(id)) {
        m_modelPreferencesError = tr("Choose a video generation model from Society.");
        emit modelPreferencesChanged(); return false;
    }
    auto value = m_modelPreferences;
    value.videoModel = id.toStdString();
    if (!saveModelPreferences(value)) return false;
    setSelectedVideoModel(id.isEmpty() && !m_videoModelIds.isEmpty() ? m_videoModelIds.first() : id);
    m_videoModelOverridden = false;
    return true;
}
QVariantList GenerationController::jobs() const
{
    QVariantList result;
    for (auto job = m_jobs.crbegin(); job != m_jobs.crend(); ++job) result.append(job->toVariantMap());
    return result;
}
QUrl GenerationController::latestImage() const
{
    return latestResult().value("imageSource").toUrl();
}
QVariantMap GenerationController::latestResult() const
{
    return m_notifiedLatestResult;
}
QVariantList GenerationController::completedResults() const
{
    return m_notifiedResults;
}
QVariantMap GenerationController::projectLatestResult() const
{
    for (auto job = m_jobs.crbegin(); job != m_jobs.crend(); ++job) {
        const auto result = resultForImage(*job, job->value(job->value("mediaType") == "Video" ? "video" : "image").toString());
        if (!result.isEmpty()) return result;
    }
    return {};
}

QVariantList GenerationController::projectCompletedResults() const
{
    QVariantList results;
    // Keep submission/output order so newly completed images append to the gallery.
    for (const auto &job : m_jobs) {
        for (const auto &image : job.value(job.value("mediaType") == "Video" ? "videos" : "images").toArray()) {
            const auto result = resultForImage(job, image.toString());
            if (!result.isEmpty()) results.append(result);
        }
    }
    return results;
}

void GenerationController::notifyResultsChanged()
{
    auto results = projectCompletedResults(); auto latest = projectLatestResult();
    if (results == m_notifiedResults && latest == m_notifiedLatestResult) return;
    m_notifiedResults = std::move(results); m_notifiedLatestResult = std::move(latest);
    emit resultsChanged();
}

QVariantMap GenerationController::resultForImage(const QJsonObject &job, const QString &relative) const
{
    if (!container()->available() || job.value("state") != "completed") return {};
    const auto prefix = QStringLiteral("Generation History/");
    const auto name = relative.mid(prefix.size());
    if (!relative.startsWith(prefix) || !name.startsWith(job.value("id").toString() + '-')
        || name.contains('/') || name.contains('\\')) return {};
    const auto alias = container()->alias("generation-history/" + name);
    if (!alias.value("available").toBool()) return {};
    auto result = job.toVariantMap();
    result.insert("alias", alias);
    result.insert("image", relative);
    result.insert("mediaType", job.value("mediaType").toString("Image"));
    result.insert("mediaSource", alias.value("source"));
    if (job.value("mediaType") == "Video") {
        const auto poster = job.value("poster").toString();
        if (poster != prefix + job.value("id").toString() + "-poster.png") return {};
        const auto image = container()->alias("generation-history/" + poster.mid(prefix.size()));
        if (!image.value("available").toBool()) return {};
        result.insert("imageSource", image.value("source"));
    } else if (name.endsWith(".iisc", Qt::CaseInsensitive)) {
        result.insert("path", alias.value("path")); return storedImage(result);
    } else result.insert("imageSource", alias.value("source"));
    return result;
}
QVariantMap GenerationController::storedImage(const QVariantMap &file) const
{
    const auto path = file.value("path").toString();
    if (!path.endsWith(".iisc", Qt::CaseInsensitive)) return file;
    const auto nested = file.value("alias").toMap();
    const auto key = file.value("key", nested.value("key")).toString();
    const auto alias = container()->alias(key);
    if (!alias.value("available").toBool() || alias.value("containerId").toString() != container()->containerId()
        || alias.value("path").toString() != path) return {};
    // Presentation consumes the owner's Alias snapshot. Actual decoding is
    // requested lazily by the asynchronous image provider, never by this getter.
    const auto preview = dreamscapes::imageCanvasPreviewSource(path, alias.value("version").toString());
    auto result = file;
    result.insert("alias", alias); result.insert("mediaType", "Image");
    result.insert("documentSource", alias.value("source"));
    result.insert("imageSource", preview); result.insert("previewSource", preview); result.insert("thumbnailSource", preview);
    return result;
}
QUrl GenerationController::imagePreview(const QUrl &source) const
{
    return source.isLocalFile() ? m_fileSystem.preview(source.toLocalFile()) : QUrl{};
}
QUrl GenerationController::imageDocument(const QUrl &source)
{
    if (!source.isLocalFile()) { fail(tr("Choose a local image or canvas.")); return {}; }
    const auto alias = m_fileSystem.importCanvas(source.toLocalFile());
    if (alias.isEmpty()) { fail(m_fileSystem.errorString()); return {}; }
    fail({}); return alias.value("source").toUrl();
}
QUrl GenerationController::filesDirectory() const
{
    return m_fileSystem.url("files");
}

bool GenerationController::selectStorageLocation(const QString &path)
{
    if (busy() || m_cacheMigrationActive)
        return fail(tr("Wait for the current storage operation before changing storage."));
    const QUrl url(path);
    const auto local = url.isLocalFile() ? url.toLocalFile() : path;
    if (local.trimmed().isEmpty() || !iiFileProvider::Directory::isAbsolutePath(local))
        return fail(tr("Choose an existing Society drive folder using an absolute path."));
    if (!m_fileSystem.selectDefaultContainer(local)) return fail(m_fileSystem.errorString());
    return connectStorage(m_fileSystem.rootPath());
}

bool GenerationController::connectStorage(const QString &path)
{
    if (busy() || m_cacheMigrationActive) return fail(tr("Wait for the current storage operation before changing storage."));
    ++m_inventoryEpoch;
    QString error;
    m_storageSelection = path; m_storageReady = false;
    if (!m_fileSystem.open(path)) return fail(m_fileSystem.errorString());
    m_storageReady = true;
    m_selected.clear();
    m_selectedVideo.clear();
    m_imageModelOverridden = m_videoModelOverridden = false;
    m_selectedVae.clear();
    m_jobs.clear();
    // Publish the cleared projection before cleanup can report an error to
    // observers. The successful and rejected reconnects both replace this session.
    notifyResultsChanged();
    if (!discardLegacyStorage()) {
        m_storageReady = false;
        emit jobsChanged();
        emit storageChanged();
        return false;
    }
    emit storageChanged();
    refreshModels();
    startLegacyCacheMigration();
    emit jobsChanged();
    return true;
}
void GenerationController::pollStorage()
{
    if (!m_foreground || busy() || !m_controlId.isEmpty() || m_cacheMigrationActive
        || m_inventoryWatcher.isRunning()) return;
    const auto selection = m_storageSelection;
    const auto epoch = m_inventoryEpoch;
    m_inventoryWatcher.setFuture(QtConcurrent::run([selection, epoch] {
        ModelInventory inventory;
        inventory.selection = selection;
        inventory.epoch = epoch;
        try {
            const auto storage = SharedStorage::open(selection, &inventory.error);
            if (!storage) return inventory;
            inventory.root = storage->drive().rootPath();
            inventory.id = storage->drive().identifier();
            inventory.models = storage->models(&inventory.error);
            inventory.modalities = inspectModels(inventory.root, inventory.models);
            for (const auto &model : inventory.models)
                if (inventory.modalities.value(model.id).contains("video")) inventory.videoModels.append(model.id);
        } catch (const std::exception &error) {
            inventory.error = QString::fromUtf8(error.what());
        }
        return inventory;
    }));
}
void GenerationController::refreshModels()
{
    if (busy() || m_cacheMigrationActive) return;
    ++m_inventoryEpoch;
    QString error;
    const auto previousId = container()->containerId(), previousRoot = container()->rootPath();
    const bool wasReady = m_storageReady;
    if (!m_fileSystem.open(m_storageSelection)) {
        error = m_fileSystem.errorString(); m_storageReady = false;
        m_models.clear(); m_selected.clear(); m_selectedVae.clear(); m_selectedVideo.clear(); m_videoModelIds.clear(); m_modelModalities.clear();
        emit storageChanged(); emit modelsChanged(); fail(error); return;
    }
    const bool storageChangedNow = !wasReady || previousId != container()->containerId() || previousRoot != container()->rootPath();
    m_storageReady = true;
    auto models = m_fileSystem.models();
    error = m_fileSystem.errorString();
    QStringList videoModels;
    const auto root = containerPath();
    auto modalities = inspectModels(root, models);
    for (const auto &model : models)
        if (modalities.value(model.id).contains("video")) videoModels.append(model.id);
    applyModelInventory(std::move(models), std::move(videoModels), std::move(modalities), error, storageChangedNow);
}
void GenerationController::applyModelInventory(QList<StoredModel> models, QStringList videoModels,
                                              QMap<QString, QStringList> modalities, const QString &error, bool storageChangedNow)
{
    const bool changed = storageChangedNow || modalities != m_modelModalities || models.size() != m_models.size()
        || !std::equal(models.cbegin(), models.cend(), m_models.cbegin(), m_models.cend(),
            [](const StoredModel &a, const StoredModel &b) { return a.id == b.id && a.name == b.name && a.format == b.format && a.fingerprint == b.fingerprint
                && a.available == b.available && a.bytes == b.bytes; });
    if (m_foreground) m_societyClient.start(containerPath());
    m_models = std::move(models);
    m_modelModalities = std::move(modalities);
    const auto previousVideoModels = m_videoModelIds;
    m_videoModelIds = std::move(videoModels);
    const auto previousImage = m_selected;
    const auto previousVideo = m_selectedVideo;
    if (!m_videoModelOverridden || !m_videoModelIds.contains(m_selectedVideo)) {
        m_videoModelOverridden = false;
        m_selectedVideo = m_videoModelIds.contains(defaultVideoModel()) ? defaultVideoModel()
            : m_videoModelIds.isEmpty() ? QString() : m_videoModelIds.first();
    }
    const auto images = this->models();
    const auto hasImage = [&](const QString &id) {
        return std::any_of(images.cbegin(), images.cend(), [&](const auto &model) { return model.toMap().value("id").toString() == id; });
    };
    if (!m_imageModelOverridden || !hasImage(m_selected)) {
        m_imageModelOverridden = false;
        m_selected = hasImage(defaultImageModel()) ? defaultImageModel()
            : images.isEmpty() ? QString() : images.first().toMap().value("id").toString();
    }
    if (std::none_of(m_models.cbegin(), m_models.cend(), [&](const auto &model) { return model.id == m_selectedVae; }))
        m_selectedVae.clear();
    fail(error);
    if (storageChangedNow) {
        // Requests remain durable in the old Society replica; tracking is local
        // to the currently opened container.
        m_modelDownloads.clear();
        if (m_societyBackgroundActivity) {
            m_societyBackgroundActivity = m_backgroundActivityActive = false;
            m_runtime.backgroundActivity->end(true);
        }
        emit storageChanged();
        if (!m_runtime.nativeInference) startLegacyCacheMigration();
    }
    if (changed || previousVideoModels != m_videoModelIds || previousImage != m_selected || previousVideo != m_selectedVideo) {
        emit modelsChanged();
        m_residencyPending = true;
        QTimer::singleShot(0, this, &GenerationController::pump);
    }
}
QFuture<QString> GenerationController::releaseNativeCache()
{
    const auto release = m_runtime.releaseNativeCache ? m_runtime.releaseNativeCache : iiLocalDiffusion::releaseNativeDiffusionCache;
    return QtConcurrent::run([release] {
        try { release(); return QString{}; }
        catch (const std::exception &error) { return QString::fromUtf8(error.what()); }
        catch (...) { return QStringLiteral("The native model cache could not be released."); }
    });
}

void GenerationController::completeCacheRelease()
{
    const auto id = std::exchange(m_cacheReleaseJob, {});
    const auto error = m_cacheReleaseWatcher.result();
    m_cacheReleasePending = false;
    if (m_closing) return;
    if (!busy() || m_active.value("id") != id) { QTimer::singleShot(0, this, &GenerationController::pump); return; }
    if (m_cancelled) { finish("cancelled"); return; }
    if (!error.isEmpty()) { finish("failed", error); return; }
    QString failure;
    if (!startWorker(&failure)) finish("failed", failure);
}

void GenerationController::startLegacyCacheMigration()
{
    const auto source = m_runtime.legacyQ8CacheDirectory;
    if (!connected() || m_cacheMigrationActive || source.isEmpty()
        || (!QFileInfo::exists(source) && !QFileInfo(source).isSymLink())) return;
    QString error;
    const auto target = m_fileSystem.ensureDirectory("models",
        ".society-runtime/iiLocalDiffusion/q8");
    error = m_fileSystem.errorString();
    if (target.isEmpty()) { fail(error); return; }
    m_cacheMigrationActive = true;
    m_cacheMigrationCancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_cacheMigrationCancelled;
    const auto migrate = m_runtime.migrateLegacyCache ? m_runtime.migrateLegacyCache : dreamscapes::migrateLegacyQ8Cache;
    m_cacheMigrationWatcher.setFuture(QtConcurrent::run([source, target, cancelled, migrate] {
        QString error;
        migrate(source, target, *cancelled, &error);
        return error;
    }));
}
bool GenerationController::discardLegacyStorage() {
    return m_fileSystem.discardLegacyGenerationStorage() || fail(m_fileSystem.errorString());
}

void GenerationController::updateJob(QJsonObject job)
{
    const QPointer<GenerationController> owner(this);
    updateScreenActivity();
    if (!owner) return;
    job["updatedAt"] = now();
    const auto found = std::find_if(m_jobs.begin(), m_jobs.end(), [&](const auto &existing) {
        return existing.value("id") == job.value("id");
    });
    if (found == m_jobs.end()) m_jobs.append(job);
    else *found = job;
    emit jobsChanged();
}

bool GenerationController::createWorkingFiles(QString *error)
{
    const auto path = connected() ? dreamscapes::generationRuntimeDirectory(m_fileSystem, error) : QString();
    if (path.isEmpty()) return false;
    m_workDirectory = std::make_shared<iiFileProvider::TemporaryDirectory>(iiFileProvider::Directory(path).filePath("dreamscapes-generation-XXXXXX"));
    if (!m_workDirectory->isValid()) { *error = m_workDirectory->errorString(); return false; }
    for (const auto &name : {"output", "runtime", "cache"}) {
        if (!iiFileProvider::Directory(m_workDirectory->path()).mkdir(name)) {
            *error = tr("Cannot prepare generation files in Society.");
            return false;
        }
    }
    m_output = m_workDirectory->filePath("output");
    m_previewDirectory = std::make_shared<iiFileProvider::TemporaryDirectory>(m_workDirectory->filePath("preview-XXXXXX"));
    if (!m_previewDirectory->isValid()) { *error = m_previewDirectory->errorString(); return false; }
    return true;
}

QStringList GenerationController::resourceArguments(QString *error)
{
    if (!connected()) { *error = tr("Open Society to access generation resources."); return {}; }
    const auto resources = dreamscapes::generationResourceDirectory(m_fileSystem, error);
    if (resources.isEmpty()) return {};
    const auto manifest = m_fileSystem.path(StoreSection::Models,
        ".generation-resources/iiLocalDiffusion/generation-defaults.json", error);
    if (manifest.isEmpty()) return {};
    return {"--generation-resources", resources,
        m_fileSystem.aliasForPath(manifest).value("available").toBool() ? "--default-modifiers" : "--no-default-modifiers"};
}

QFuture<void> GenerationController::clearWorkingFiles()
{
    auto work = std::move(m_workDirectory);
    auto preview = std::move(m_previewDirectory);
    m_output.clear();
    clearPreview();
    // No owner access follows the notification. Retired directories may contain
    // large results; release child then parent off the GUI thread.
    if (!work && !preview) return {};
    return QtConcurrent::run([work = std::move(work), preview = std::move(preview)]() mutable {
        preview.reset(); work.reset();
    });
}

void GenerationController::pump()
{
    if (m_runtime.nativeInference && !m_foreground
        && !(m_runtime.backgroundActivity && m_runtime.backgroundActivity->allowsBackgroundExecution())) return;
    if (!connected() || busy() || m_finishing || m_closing || m_cacheReleasePending || !m_controlId.isEmpty() || !runtimeAvailable()) return;
    QString error;
    const auto queued = std::find_if(m_jobs.cbegin(), m_jobs.cend(), [](const auto &job) { return job.value("state") == "queued"; });
    if (queued == m_jobs.cend()) { prepareForeground(); return; }
    m_active = *queued;
    m_failureProgress = {};
    m_log.clear();
    m_pendingOutput.clear();
    clearWorkingFiles();
    m_cancelled = false;
    m_interrupted = false;
    m_nativeTimedOut = false;
    m_nativeDispatched = false;
    auto &map = m_fileSystem;
    auto required = map.files("models/" + m_active.value("model").toObject().value("path").toString());
    const auto resources = map.files("models/.generation-resources/iiLocalDiffusion");
    if (m_active.value("mediaType") != "Video") required.append(resources); required.removeDuplicates();
    if (m_active.value("execution") == "host" || (!required.isEmpty() && !map.available(required))) {
        if (m_active.contains("advancedParameters")) {
            finish("failed", tr("The local model became unavailable. Advanced parameters cannot be silently forwarded to an unsupported host.")); return;
        }
        m_remoteActive = true; m_requiredModelFiles = required;
        m_active["state"] = "connecting-host"; m_active["execution"] = "host";
        m_societyClient.start(containerPath());
        m_remoteGeneration->setPaused(!m_foreground
            && !(m_runtime.backgroundActivity && m_runtime.backgroundActivity->allowsBackgroundExecution()));
        if (!m_remoteGeneration->start(m_active)) { finish("failed", tr("The Society host queue is already busy.")); return; }
        setInferenceStatus({{"state", "waiting-host"}, {"backend", "remote"}, {"ready", false}});
        updateJob(m_active);
        return;
    }
    if (m_cacheMigrationActive || (m_runtime.nativeInference && !m_foreground)) { m_active = {}; return; }
    const auto model = m_fileSystem.resolveModel(m_active.value("model").toObject(), &error);
    if (model.isEmpty()) { finish("failed", error); return; }
    if (!createWorkingFiles(&error)) { finish("failed", error); return; }
    m_active["state"] = "running";
    m_active["startedAt"] = now();
    m_active["output"] = "Generation History";
    updateJob(m_active);
    if (m_runtime.nativeInference && m_active.value("mediaType") != "Video") { startNative(model); return; }
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    setInferenceStatus({{"state", "loading"}, {"ready", false}, {"model", model}});
    if (m_cancelled) { finish("cancelled"); return; }
    QStringList arguments{"--model-path", model, "--prompt", m_active.value("prompt").toString(),
        "--width", QString::number(m_active.value("width").toInt()), "--height", QString::number(m_active.value("height").toInt()),
        "--steps", QString::number(m_active.value("steps").toInt()),
        "--seed", QString::number(m_active.value("seed").toInteger()), "--device", m_active.value("device").toString(),
        "--output-dir", m_output, "--negative-prompt", m_active.value("negativePrompt").toString(
            m_active.value(m_active.value("mediaType") == "Video" ? "videoParameters" : "advancedParameters")
                .toObject().value("negativePrompt").toString())};
    if (m_active.value("mediaType") == "Video") {
        // Embedded encoders are model assets, not disposable job outputs.
        const auto cache = m_fileSystem.ensureDirectory("models", ".society-runtime/iiLocalDiffusion/cache/video");
        if (cache.isEmpty()) { finish("failed", m_fileSystem.errorString()); return; }
        const auto outputIndex = arguments.indexOf("--output-dir");
        arguments.removeAt(outputIndex + 1); arguments.removeAt(outputIndex);
        const auto video = m_active.value("videoParameters").toObject();
        arguments.append({"--backend","video","--output",iiFileProvider::Directory(m_output).filePath("video.mp4"),
            "--duration",QString::number(video.value("duration").toInt()),"--fps",QString::number(video.value("fps").toInt()),
            "--cache-dir",cache,"--cpu-text-encoding",
            "--ffmpeg",QStringLiteral(DREAMSCAPES_FFMPEG_EXECUTABLE),"--ffprobe",QStringLiteral(DREAMSCAPES_FFPROBE_EXECUTABLE)});
        if (video.value("recipe").toBool()) {
            // The SDK stitches kept shots in order; frames are measured in output
            // FPS and per-shot image keys are relative to that shot's first frame.
            arguments.removeAt(arguments.indexOf("--duration") + 1);
            arguments.removeOne("--duration");
            arguments.removeOne("--cpu-text-encoding");
            const auto storyPath = m_workDirectory->filePath("storyboard.json");
            iiFileProvider::FileDevice story(storyPath);
            auto shots = video.value("shots").toArray();
            for (int index = 0; index < shots.size(); ++index) {
                auto shot = shots[index].toObject();
                shot["seed"] = double((m_active.value("seed").toInteger() + index) % 4294967296LL);
                shots[index] = shot;
            }
            const auto bytes = QJsonDocument(QJsonObject{{"shots", shots}}).toJson(QJsonDocument::Compact);
            if (!story.open(QIODevice::WriteOnly) || story.write(bytes) != bytes.size()) {
                finish("failed", tr("Cannot write the submitted video storyboard.")); return;
            }
            story.close();
            arguments[arguments.indexOf("--device") + 1] = video.value("device").toString();
            arguments.append({"--storyboard", storyPath,
                "--guidance-scale", QString::number(video.value("cfgScale").toDouble()),
                "--interpolation-factor", QString::number(video.value("interpolationFactor").toInt()),
                "--dtype", video.value("precision").toString(), "--offload", video.value("offload").toString(),
                video.value("cpuTextEncoding").toBool() ? "--cpu-text-encoding" : "--no-cpu-text-encoding",
                video.value("vaeTiling").toBool() ? "--vae-tiling" : "--no-vae-tiling",
                "--decode-timestep", QString::number(video.value("decodeTimestep").toDouble()),
                "--decode-noise-scale", QString::number(video.value("decodeNoiseScale").toDouble()),
                "--image-cond-noise-scale", QString::number(video.value("imageConditionNoise").toDouble()),
                "--video-crf", QString::number(video.value("crf").toInt()),
                "--video-preset", video.value("encodingPreset").toString()});
        }
        const auto firstFrame = video.value("firstFrame").toString();
        if (!firstFrame.isEmpty()) arguments.append({"--first-frame",firstFrame});
        m_workerRequest = QJsonDocument(QJsonObject{{"schema","iild-worker-request-v1"},{"id",m_active.value("id")},
            {"arguments",QJsonArray::fromStringList(arguments)}}).toJson(QJsonDocument::Compact) + '\n';
        if (m_runtime.nativeInference) {
            m_cacheReleasePending = true; m_cacheReleaseJob = m_active.value("id").toString();
            m_cacheReleaseWatcher.setFuture(releaseNativeCache());
            setInferenceStatus({{"state", "releasing-cache"}, {"backend", "native"}, {"ready", false}});
            return;
        }
        if (!startWorker(&error)) finish("failed",error);
        return;
    }
    arguments.append({"--cache-dir", m_workDirectory->filePath("cache"),
        "--preview-dir", m_previewDirectory->path()});
    const auto vaeReference = m_active.value("vae").toObject();
    if (!vaeReference.isEmpty()) {
        const auto vae = m_fileSystem.resolveModel(vaeReference, &error);
        if (vae.isEmpty()) { finish("failed", error); return; }
        arguments.append({"--vae", vae});
    }
    arguments.append(resourceArguments(&error));
    if (!error.isEmpty()) { finish("failed", error); return; }
    const auto advanced = m_active.value("advancedParameters").toObject();
    if (!advanced.isEmpty()) {
        // Force the capability-checked native route rather than silently selecting
        // a pipeline with a different sampling/weighting contract.
        arguments.append({"--engine", "native",
            "--guidance-scale", QString::number(advanced.value("cfgScale").toDouble(), 'g', 17),
            "--native-sampler", advanced.value("sampler").toString()});
        for (const auto &value : advanced.value("loras").toArray()) {
            const auto lora = value.toObject();
            arguments.append({"--lora", lora.value("source").toString(),
                "--lora-scale", QString::number(lora.value("weight").toDouble(), 'g', 17)});
        }
    }
    if (m_active.value("model").toObject().value("format") == "safetensors")
        arguments.append({"--backend", "local", "--work-dir", m_workDirectory->filePath("runtime")});
    m_workerRequest = QJsonDocument(QJsonObject{{"schema", "iild-worker-request-v1"},
        {"id", m_active.value("id")}, {"arguments", QJsonArray::fromStringList(arguments)}}).toJson(QJsonDocument::Compact) + '\n';
    if (!startWorker(&error)) finish("failed", error);
#endif
}

void GenerationController::pollDownload()
{
    if (m_modelDownloads.isEmpty() || !connected()) return;
    auto &map = m_fileSystem;
    for (auto it = m_modelDownloads.begin(); it != m_modelDownloads.end();) {
        const auto download = map.requestState(it.value());
        const auto state = download.value("state").toString();
        if (state == "ready" || state == "failed" || state == "cancelled") {
            const auto jobs = m_jobs;
            for (auto job : jobs) {
                if (job.value("modelDownload").toObject().value("requestId").toString() != it.value()) continue;
                job["modelDownload"] = QJsonObject{{"state", state}, {"requestId", it.value()}, {"error", download.value("error")}};
                if (m_active.value("id") == job.value("id")) m_active["modelDownload"] = job.value("modelDownload");
                updateJob(job);
            }
            it = m_modelDownloads.erase(it);
        }
        else ++it;
    }
    const bool queuedOnHost = std::any_of(m_jobs.cbegin(), m_jobs.cend(), [](const auto &job) {
        return job.value("state") == "queued" && job.value("execution") == "host";
    });
    if (m_modelDownloads.isEmpty() && m_societyBackgroundActivity && !m_remoteActive && !queuedOnHost) {
        m_societyBackgroundActivity = m_backgroundActivityActive = false;
        m_runtime.backgroundActivity->end(true);
        if (!m_foreground) m_societyClient.stop();
    }
}

void GenerationController::beginSocietyBackgroundActivity()
{
    if (!m_runtime.backgroundActivity || m_backgroundActivityActive) return;
    m_societyBackgroundActivity = m_backgroundActivityActive = true;
    const QPointer<GenerationController> self(this);
    m_runtime.backgroundActivity->beginNetwork(m_active.value("id").toString(), [self] {
        if (!self || !self->m_societyBackgroundActivity) return;
        self->m_societyBackgroundActivity = self->m_backgroundActivityActive = false;
        self->m_runtime.backgroundActivity->end(false);
        if (!self->m_foreground) {
            self->m_societyClient.stop(); self->m_remoteGeneration->setPaused(true);
        }
    });
}

void GenerationController::downloadModelInBackground()
{
    if (!connected() || m_requiredModelFiles.isEmpty()) return;
    const auto model = m_active.value("model").toObject().value("path").toString();
    if (m_fileSystem.available(m_requiredModelFiles)) {
        m_active["modelDownload"] = QJsonObject{{"state", "ready"}};
        updateJob(m_active); return;
    }
    if (m_modelDownloads.contains(model)) {
        m_active["modelDownload"] = QJsonObject{{"state", "background"}, {"requestId", m_modelDownloads.value(model)}};
        updateJob(m_active); return;
    }
    QString error;
    const auto request = m_fileSystem.requestFiles(m_requiredModelFiles, &error);
    m_active["modelDownload"] = request.isEmpty() ? QJsonObject{{"state", "failed"}, {"error", error}}
        : QJsonObject{{"state", "background"}, {"requestId", request}};
    if (!request.isEmpty()) { m_modelDownloads.insert(model, request); m_societyClient.synchronizeNow(); }
    updateJob(m_active);
}

void GenerationController::beginPublication(dreamscapes::PublicationOrigin origin,
    std::optional<iiLocalDiffusion::NativeGenerationResult> native, QByteArray png)
{
    if (!busy() || m_publicationPending || m_finishing || m_closing) return;
    dreamscapes::GenerationPublicationInput input;
    input.origin = origin; input.root = containerPath(); input.containerId = container()->containerId();
    input.directory = m_workDirectory ? m_workDirectory->path() : QString{}; input.output = m_output;
    input.job = m_active; input.native = std::move(native); input.remotePng = std::move(png);
    input.lifetime = m_workDirectory; input.control = std::make_shared<dreamscapes::PublicationControl>();
    m_publicationControl = input.control; m_publicationJob = m_active.value("id").toString();
    m_publicationRoot = input.root; m_publicationContainer = input.containerId;
    m_publicationResumeStatus = origin == dreamscapes::PublicationOrigin::Native
        ? QJsonObject{{"state", "idle"}, {"ready", false}, {"backend", "native"}} : m_inferenceStatus;
    m_publicationPending = true; m_workerDeadline.stop(); m_nativeDeadline.stop();
    const auto operation = m_runtime.publishResult ? m_runtime.publishResult : dreamscapes::publishGenerationResult;
    try {
        m_publicationWatcher.setFuture(QtConcurrent::run([operation, input = std::move(input)]() mutable {
            const auto control = input.control;
            dreamscapes::GenerationPublicationResult result;
            try { result = operation(std::move(input)); }
            catch (const std::exception &error) { result.error = QString::fromUtf8(error.what()); }
            catch (...) { result.error = QStringLiteral("The generated result could not be saved."); }
            control->finish(); return result;
        }));
    } catch (const std::exception &error) {
        finish("failed", QString::fromUtf8(error.what())); return;
    }
    const QPointer<GenerationController> owner(this);
    updateScreenActivity();
    if (owner) setInferenceStatus({{"state", "publishing"}, {"ready", false}, {"backend", "publication"}});
}

void GenerationController::completePublication()
{
    if (!m_publicationPending || m_closing) return;
    auto result = m_publicationWatcher.future().takeResult();
    const auto id = m_publicationJob; const QPointer<GenerationController> owner(this);
    if (!busy() || m_active.value("id").toString() != id) return;
    if (m_cancelled || result.cancelled) { finish(m_interrupted ? "interrupted" : "cancelled"); return; }
    if (!result.ok()) {
        finish("failed", result.error.isEmpty() ? tr("The generated result could not be saved.") : result.error); return;
    }
    if (containerPath() != m_publicationRoot || container()->containerId() != m_publicationContainer) {
        finish("failed", tr("The selected drive changed while saving the generated result.")); return;
    }
    const bool adopted = m_fileSystem.adoptStorage(result.storage);
    if (!owner) return;
    if (!adopted) {
        finish("failed", m_fileSystem.errorString()); return;
    }
    if (!owner || !m_publicationPending || m_active.value("id").toString() != id) return;
    if (containerPath() != m_publicationRoot || container()->containerId() != m_publicationContainer) {
        finish("failed", tr("The selected drive changed while saving the generated result.")); return;
    }
    const bool video = m_active.value("mediaType") == "Video";
    const auto primary = result.values.value(video ? "video" : "image").toString();
    const auto relative = primary.startsWith("Generation History/") ? primary.mid(19) : QString{};
    const auto alias = relative.isEmpty() ? QVariantMap{} : container()->alias("generation-history/" + relative);
    if (result.values.value("output") != "Generation History" || !alias.value("available").toBool()
        || !alias.value("exists").toBool()) {
        finish("failed", tr("The saved generation result is unavailable.")); return;
    }
    // Transfer publication fields only. Background model-download status and
    // other owner-side updates made during preparation remain current.
    for (const auto &key : {"generation", "output", "image", "images", "video", "videos", "poster"})
        if (result.values.contains(key)) m_active[key] = result.values.value(key);
    finish("completed");
}

void GenerationController::ensureFailureAgent()
{
    if (m_failureAgent) return;
    auto executable = m_runtime.failureAgentExecutable;
    if (executable.isEmpty()) executable = QCoreApplication::applicationDirPath() + "/DreamscapesFailureAgent"
#ifdef Q_OS_WIN
        + ".exe"
#endif
        ;
    m_failureAgent = new FailureAgent(executable, m_runtime.failureAgentTimeoutMilliseconds, this);
    connect(m_failureAgent, &FailureAgent::explained, this, [this](const QString& id, const QJsonObject& report) {
        if (m_closing) return;
        // Analysis completion never changes the active request or starts work.
        if (m_active.value("id") == id) m_active["failureAnalysis"] = report;
        for (auto& job : m_jobs) if (job["id"] == id) { job["failureAnalysis"] = report; break; }
        for (auto& existing : m_operationFailureReports) if (existing["jobId"] == id) { existing = report; break; }
        const QPointer<GenerationController> guard(this);
        if (m_latestFailure.value("jobId") == id) { m_latestFailure = report; emit failureAnalysisChanged(); if (!guard) return; }
        emit failureExplained(id, report.toVariantMap());
    });
}
void GenerationController::finish(const QString &state, const QString &error)
{
    if (!busy() || m_finishing || m_closing) return;
    m_finishing = true;
    const QPointer<GenerationController> owner(this);
    m_finishState = state; m_finishError = error;
    if (state == "failed") {
        ensureFailureAgent();
        auto job = m_active; job["state"] = "failed"; job["finishedAt"] = now();
        const auto trace = job["telemetry"].toObject()["trace_path"].toString();
        const auto reports = m_runtime.failureReportsDirectory;
        QJsonArray roots{containerPath() + "/Models/.society-runtime/iiLocalDiffusion"};
        if (m_workDirectory) roots.append(m_workDirectory->path());
        auto status = m_failureProgress;
        if (status.isEmpty()) status["state"] = "not-reported";
        QJsonObject evidence{{"job", job}, {"inference", status}, {"rawError", error},
            {"workerLog", QString::fromUtf8(m_log)}, {"timedOut", m_nativeTimedOut}, {"tracePath", trace},
            {"allowedTraceRoots", roots}, {"modelsDirectory", containerPath() + "/Models/.society-runtime/iiLocalLLM/Models"},
            {"sessionsDirectory", reports.isEmpty() ? QString() : reports + "/.sessions"},
            {"reportPath", reports.isEmpty() ? QString() : reports + "/." + job["id"].toString() + ".json"}};
        if (job.contains("workerExitCode")) evidence["exitCode"] = job["workerExitCode"];
        m_latestFailure = m_failureAgent->submit(evidence, m_workDirectory);
        m_active["failureAnalysis"] = m_latestFailure;
        emit failureAnalysisChanged(); if (!owner) return;
    }
    m_workerDeadline.stop(); m_nativeDeadline.stop(); m_workerProgressKey.clear();
    m_remoteActive = false; m_requiredModelFiles.clear(); m_workerRequest.clear();
    updateScreenActivity(); if (!owner) return;
    const auto cleanup = clearWorkingFiles(); if (!owner) return;
    if (cleanup.isValid()) m_cleanupWatcher.setFuture(cleanup);
    else completeFinish();
}

void GenerationController::completeFinish()
{
    if (!busy() || !m_finishing || m_closing) return;
    const QPointer<GenerationController> owner(this);
    const auto state = std::exchange(m_finishState, {});
    const auto error = std::exchange(m_finishError, {});
    const auto id = m_active.value("id").toString();
    const bool publication = m_publicationPending;
    const auto resume = m_publicationResumeStatus;
    m_workerDeadline.stop(); m_workerProgressKey.clear();
    // Replication outlives this generation job, including failure/cancellation.
    m_remoteActive = false; m_requiredModelFiles.clear();
    m_active["state"] = state; m_active["finishedAt"] = now(); m_active["error"] = error;
    updateJob(m_active); if (!owner) return;
    if (state == "completed") { m_societyClient.synchronizeNow(); if (!owner) return; }
    if (m_runtime.nativeInference && m_runtime.backgroundActivity) {
        m_runtime.backgroundActivity->finishPresentation(id, state); if (!owner) return;
    }
    fail(error); if (!owner) return;
    m_runtime.nativeExecutionControl->setPaused(false); m_nativePaused = false;
    updateScreenActivity(); if (!owner) return;
    if (m_foreground) m_residencyPending = true;
    const bool queuedOnHost = std::any_of(m_jobs.cbegin(), m_jobs.cend(), [](const auto &job) {
        return job.value("state") == "queued" && job.value("execution") == "host";
    });
    if (m_backgroundActivityActive && (!m_societyBackgroundActivity || (m_modelDownloads.isEmpty() && !queuedOnHost))) {
        m_societyBackgroundActivity = false; m_backgroundActivityActive = false;
        m_runtime.backgroundActivity->end(state == "completed"); if (!owner) return;
    }
    if (publication) { setInferenceStatus(resume); if (!owner) return; }
    m_publicationPending = false; m_publicationControl.reset();
    m_publicationJob.clear(); m_publicationRoot.clear(); m_publicationContainer.clear(); m_publicationResumeStatus = {};
    m_active = {}; m_finishing = false;
    QTimer::singleShot(0, this, &GenerationController::pump);
    emit jobsChanged();
}

bool GenerationController::cancel(const QString &id)
{
    if (busy() && m_active.value("id") == id) {
        if (m_finishing) return false;
        if (m_cacheReleasePending) {
            m_cancelled = true; m_nativeState->cancelled = true;
            setInferenceStatus({{"state", "cancelling"}, {"backend", "native"}, {"ready", false}});
            return true;
        }
        if (m_publicationPending) {
            if (!m_publicationControl || !m_publicationControl->cancel()) return false;
            m_cancelled = true;
            setInferenceStatus({{"state", "cancelling"}, {"ready", false}, {"backend", "publication"}});
            return true;
        }
        if (m_remoteActive) { m_remoteGeneration->cancel(); return true; }
        m_cancelled = true;
        m_nativeState->cancelled = true;
        stopProcess(false);
        QTimer::singleShot(2000, this, [this, id] { if (m_active.value("id") == id) stopProcess(true); });
        // Complete internal cancellation before observers can remove the owner.
        if (m_runtime.nativeInference)
            setInferenceStatus({{"state", "cancelling"}, {"ready", false}, {"backend", m_active.value("mediaType") == "Video" ? "video" : "native"}});
        return true;
    }
    if (!connected() || !jobId(id)) return false;
    for (auto job : m_jobs) {
        if (job.value("id") == id && job.value("state") == "queued") {
            job["state"] = "cancelled";
            job["finishedAt"] = now();
            updateJob(job);
            return true;
        }
    }
    return false;
}
