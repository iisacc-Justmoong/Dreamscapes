#include "GenerationController.h"
#include "SocietyGenerationStorage.h"
#include <StorageMap.h>
#include <QtConcurrent/QtConcurrentRun>
#include <QRandomGenerator>
#include <QPointer>
#include <QImage>
#if defined(Q_OS_IOS)
#include "GenerationScreenActivity.h"
#endif

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QLockFile>
#include <QCryptographicHash>
#include <QUuid>
#include <algorithm>
#include <limits>
#if defined(Q_OS_UNIX) && !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
#include <signal.h>
#endif

using namespace iiSocietyContainer;
namespace {
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
bool jobId(const QString &value)
{
    return !QUuid(value).isNull() && QUuid(value).toString(QUuid::WithoutBraces) == value;
}
QSize imageSize(const QString &ratio, int extent)
{
    const auto unit = std::max(8, extent / 8 * 8);
    if (unit > 4096) return {};
    // Keep the shorter side fixed; round the expanded side to the nearest latent-grid unit.
    const auto expanded = [unit](int numerator, int denominator) {
        return ((unit * numerator + 4 * denominator) / (8 * denominator)) * 8;
    };
    if (ratio == "1:1") return {unit, unit};
    if (ratio == "4:3") return {expanded(4, 3), unit};
    if (ratio == "3:4") return {unit, expanded(4, 3)};
    if (ratio == "16:9") return {expanded(16, 9), unit};
    if (ratio == "9:16") return {unit, expanded(16, 9)};
    return {};
}
GenerationRuntime defaultRuntime()
{
    GenerationRuntime runtime;
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    runtime.nativeInference = true;
#endif
#if defined(Q_OS_IOS)
    runtime.screenActivity = nativeGenerationScreenActivity();
    runtime.backgroundActivity = nativeGenerationBackgroundActivity();
    const QFileInfo previousCache(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    // Normalize OS aliases such as /var before checking for redirected cache entries.
    runtime.legacyQ8CacheDirectory = QDir(previousCache.exists()
        ? previousCache.canonicalFilePath() : previousCache.absoluteFilePath()).filePath("iiLocalDiffusion/q8");
#endif
    runtime.executable = qEnvironmentVariable("IILD_GENERATOR_EXECUTABLE");
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
}

GenerationController::GenerationController(QObject *parent) : GenerationController(defaultRuntime(), parent) {}
GenerationController::GenerationController(GenerationRuntime runtime, QObject *parent)
    : QObject(parent), m_runtime(std::move(runtime))
{
    if (!m_runtime.nativeExecutionControl)
        m_runtime.nativeExecutionControl = std::make_shared<iiLocalDiffusion::NativeExecutionControl>();
    m_storagePoll.setInterval(2000);
    connect(&m_storagePoll, &QTimer::timeout, this, &GenerationController::pollStorage);
    connect(&m_storagePoll, &QTimer::timeout, this, &GenerationController::pollDownload);
    connect(&m_societyClient, &iiSocietyClient::Client::synchronized, this, [this] { pollDownload(); if (!busy()) refreshModels(); });
    m_remoteGeneration = m_runtime.remoteGeneration ? m_runtime.remoteGeneration
        : std::make_shared<iiSocietyGeneration::Remote>([this](const QJsonObject &request) { return m_societyClient.requestHost(request); });
    connect(&m_societyClient, &iiSocietyClient::Client::hostResponse, m_remoteGeneration.get(), &iiSocietyGeneration::Remote::receive);
    connect(m_remoteGeneration.get(), &iiSocietyGeneration::Remote::accepted, this, [this] {
        if (!m_remoteActive || !busy()) return;
        m_active["state"] = "running"; m_active["execution"] = "host"; m_active["startedAt"] = now();
        updateJob(m_active);
        beginSocietyBackgroundActivity();
        downloadModelInBackground();
    });
    connect(m_remoteGeneration.get(), &iiSocietyGeneration::Remote::progress, this, [this](const QJsonObject &status) {
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
    connect(&m_societyClient, &iiSocietyClient::Client::progress, this, [this](const QString &, qint64 done, qint64 total) {
        if (m_societyBackgroundActivity && m_runtime.backgroundActivity && total > 0)
            m_runtime.backgroundActivity->update({iiLocalDiffusion::NativeGenerationStage::Preparing,
                int(done * 1000 / total), 1000});
    });
    connect(m_remoteGeneration.get(), &iiSocietyGeneration::Remote::finished, this,
        [this](const QString &state, const QByteArray &png, const QJsonObject &generation, const QString &remoteError) {
        if (!m_remoteActive || !busy()) return;
        if (state != "completed") { finish(state, remoteError); return; }
        QString error;
        if (!createWorkingFiles(&error)) { finish("failed", error); return; }
        const auto path = m_workDirectory->filePath("remote.png");
        QFile image(path);
        if (!image.open(QIODevice::WriteOnly) || image.write(png) != png.size()) {
            finish("failed", tr("Cannot save the image received from Society.")); return;
        }
        image.close(); m_active["generation"] = generation;
        if (!publishImages({path}, m_active, &error)) { finish("failed", error); return; }
        finish("completed");
    });
    connect(&m_nativeWatcher, &QFutureWatcher<iiLocalDiffusion::NativeGenerationResult>::finished,
            this, &GenerationController::finishNative);
    connect(&m_cacheMigrationWatcher, &QFutureWatcher<QString>::finished, this, [this] {
        m_cacheMigrationActive = false;
        const auto error = m_cacheMigrationWatcher.result();
        if (!error.isEmpty()) fail(error);
        QTimer::singleShot(0, this, &GenerationController::pump);
    });
    m_nativeDeadline.setSingleShot(true);
    connect(&m_nativeDeadline, &QTimer::timeout, this, [this] {
        if (!busy() || m_nativeWatcher.isFinished()) return;
        m_nativeTimedOut = true;
        m_nativeCancelled = true;
        setInferenceStatus({{"state", "cancelling"}, {"ready", false}, {"backend", "native"}});
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
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("IILD_WORKER_PROGRESS", "1");
    environment.insert("IILD_MODEL_VALIDATION", "metadata");
    if (!environment.contains("IILD_PYTHON_EXECUTABLE") && !m_runtime.pythonExecutable.isEmpty())
        environment.insert("IILD_PYTHON_EXECUTABLE", m_runtime.pythonExecutable);
    m_process.setProcessEnvironment(environment);
#ifdef Q_OS_UNIX
    // Cancel this job's inference process and any optional codec subprocesses.
    m_process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#endif
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &GenerationController::readProcessOutput);
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_cancelled) stopProcess(false);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_workerReady = false;
            m_workerRequest.clear();
            m_workerDirectory.reset();
            m_controlId.clear();
            setInferenceStatus({{"state", "error"}, {"ready", false}, {"error", m_process.errorString()}});
            if (busy() && !m_remoteActive) finish(m_cancelled ? "cancelled" : "failed", m_cancelled ? QString() : m_process.errorString());
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        readProcessOutput();
        m_workerReady = false;
        m_workerRequest.clear();
        m_workerDirectory.reset();
        m_controlId.clear();
        setInferenceStatus({{"state", "stopped"}, {"ready", false}});
        if (!busy() || m_remoteActive) { QTimer::singleShot(0, this, &GenerationController::pump); return; }
        if (m_cancelled) { finish("cancelled"); return; }
        finish("failed", tr("The inference worker exited before completing the request (exit %1). %2").arg(code)
            .arg(QString::fromUtf8(m_log).right(4000)));
    });
#endif
}

GenerationController::~GenerationController()
{
    if (m_screenActive && m_runtime.screenActivity) m_runtime.screenActivity(false);
    m_nativeCancelled = true;
    m_cacheMigrationCancelled = true;
    disconnect(&m_cacheMigrationWatcher, nullptr, this, nullptr);
    m_cacheMigrationWatcher.waitForFinished();
    disconnect(&m_nativeWatcher, nullptr, this, nullptr);
    m_nativeWatcher.waitForFinished();
    if (m_backgroundActivityActive) m_runtime.backgroundActivity->end(false);
    if (m_runtime.nativeInference) iiLocalDiffusion::releaseNativeDiffusionCache();
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        stopProcess(false);
        if (!m_process.waitForFinished(1000)) { stopProcess(true); m_process.waitForFinished(1000); }
    }
#endif
    clearWorkingFiles();
}

bool GenerationController::connected() const { return m_storage && m_storage->drive().isReady(); }
QString GenerationController::containerPath() const { return m_storage ? m_storage->drive().rootPath() : QString(); }
QString GenerationController::selectedModel() const { return m_selected; }
bool GenerationController::busy() const { return !m_active.isEmpty(); }
QString GenerationController::errorString() const { return m_error; }
QUrl GenerationController::previewImage() const { return m_previewImage; }
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
    const bool active = m_runtime.nativeInference && !m_remoteActive && busy() && m_foreground;
    if (m_screenActive == active) return;
    m_screenActive = active;
    if (m_runtime.screenActivity) m_runtime.screenActivity(active);
    emit screenActivityChanged();
}
void GenerationController::setInferenceStatus(QJsonObject status)
{
    if (m_inferenceStatus == status) return;
    m_inferenceStatus = std::move(status);
    emit inferenceStatusChanged();
}
void GenerationController::setForeground(bool foreground)
{
    if (m_foreground == foreground) {
        if (!foreground && m_societyBackgroundActivity && m_storage && m_runtime.backgroundActivity->allowsBackgroundExecution()) {
            m_societyClient.start(m_storage->drive().rootPath());
            m_remoteGeneration->setPaused(false);
            QTimer::singleShot(0, this, &GenerationController::pump);
        }
        if (busy() && !m_remoteActive && m_runtime.backgroundActivity && !foreground)
            setNativePaused(!m_runtime.backgroundActivity->allowsBackgroundExecution());
        return;
    }
    m_foreground = foreground;
    if (foreground && m_storage) m_societyClient.start(m_storage->drive().rootPath());
    const bool canTransfer = foreground || (m_runtime.backgroundActivity && m_runtime.backgroundActivity->allowsBackgroundExecution());
    if (!canTransfer) m_societyClient.stop();
    if (m_remoteGeneration) m_remoteGeneration->setPaused(!canTransfer);
    if (!foreground && m_runtime.nativeInference && !m_remoteActive) {
        if (!busy()) iiLocalDiffusion::releaseNativeDiffusionCache();
        else if (m_runtime.backgroundActivity)
            setNativePaused(!m_runtime.backgroundActivity->allowsBackgroundExecution());
        else interruptNative();
    }
    if (foreground) setNativePaused(false);
    updateScreenActivity();
    m_residencyPending = true;
    if (!busy())
        setInferenceStatus({{"state", foreground ? "preparing" : "background"},
                            {"foreground", foreground}, {"ready", false}});
    emit foregroundChanged();
    QTimer::singleShot(0, this, &GenerationController::pump);
}
void GenerationController::setNativePaused(bool paused)
{
    if (m_nativePaused == paused || m_cancelled) return;
    m_nativePaused = paused;
    if (m_runtime.backgroundActivity) m_runtime.backgroundActivity->setPresentationPaused(paused);
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
    if (!busy() || m_cancelled) return;
    m_interrupted = true;
    m_cancelled = true;
    m_nativeCancelled = true;
    setInferenceStatus({{"state", "cancelling"}, {"ready", false}, {"backend", "native"}});
}
bool GenerationController::runtimeAvailable() const
{
    if (m_runtime.nativeInference) return m_runtime.nativeGenerate || iiLocalDiffusion::nativeDiffusionAvailable();
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    return false;
#else
    return QFileInfo(m_runtime.executable).isExecutable() && QFileInfo(m_runtime.executable).isFile();
#endif
}
bool GenerationController::fail(const QString &message) { m_error = message; emit errorChanged(); return false; }
QVariantList GenerationController::models() const
{
    QVariantList result;
    for (const auto &model : m_models)
        result.append(QVariantMap{{"id", model.id}, {"name", model.name}, {"format", model.format},
            {"available", model.available}, {"bytes", model.bytes}});
    return result;
}
void GenerationController::setSelectedModel(const QString &id)
{
    if (m_selected == id) return;
    if (std::none_of(m_models.cbegin(), m_models.cend(), [&](const auto &model) { return model.id == id; })) return;
    m_selected = id;
    if (!busy() && !m_controlId.isEmpty() && m_preparingModel != id) {
        // A newly selected model must not wait for obsolete eager preparation.
        const auto controlId = m_controlId;
        stopProcess(false);
        QTimer::singleShot(2000, this, [this, controlId] {
            if (!busy() && m_controlId == controlId) stopProcess(true);
        });
    }
    m_residencyPending = true;
    setInferenceStatus({{"state", "waiting-model"}, {"ready", false}});
    emit modelsChanged();
    QTimer::singleShot(0, this, &GenerationController::pump);
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
    for (auto job = m_jobs.crbegin(); job != m_jobs.crend(); ++job) {
        const auto result = resultForImage(*job, job->value("image").toString());
        if (!result.isEmpty()) return result;
    }
    return {};
}

QVariantList GenerationController::completedResults() const
{
    QVariantList results;
    // Keep submission/output order so newly completed images append to the gallery.
    for (const auto &job : m_jobs) {
        for (const auto &image : job.value("images").toArray()) {
            const auto result = resultForImage(job, image.toString());
            if (!result.isEmpty()) results.append(result);
        }
    }
    return results;
}

QVariantMap GenerationController::resultForImage(const QJsonObject &job, const QString &relative) const
{
    if (!m_storage || !m_storage->drive().isValid() || job.value("state") != "completed") return {};
    const auto prefix = QStringLiteral("Generation History/");
    const auto name = relative.mid(prefix.size());
    if (!relative.startsWith(prefix) || !name.startsWith(job.value("id").toString() + '-')
        || name.contains('/') || name.contains('\\')) return {};
    const auto path = QDir(containerPath()).filePath(relative);
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.canonicalFilePath() != path) return {};
    auto result = job.toVariantMap();
    result.insert("image", relative);
    result.insert("imageSource", QUrl::fromLocalFile(path));
    return result;
}

bool GenerationController::connectStorage(const QString &path)
{
    if (busy() || m_cacheMigrationActive) return fail(tr("Wait for the current storage operation before changing storage."));
    QString error;
    m_storageSelection = path;
    if (!m_fileSystem.open(path)) return fail(m_fileSystem.errorString());
    auto storage = SharedStorage::open(m_fileSystem.rootPath(), &error);
    if (!storage) return fail(error);
    m_storage = std::move(storage);
    m_selected.clear();
    m_jobs.clear();
    if (!discardLegacyStorage()) {
        m_storage.reset();
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
    if (m_foreground && !busy() && m_controlId.isEmpty()) {
        const auto previousError = m_error;
        const bool previouslyReady = connected();
        refreshModels();
        // A background inventory poll must not erase a generation failure.
        if (previouslyReady && m_error.isEmpty() && !previousError.isEmpty()) fail(previousError);
    }
}
void GenerationController::refreshModels()
{
    if (busy() || m_cacheMigrationActive) return;
    QString error;
    auto storage = m_fileSystem.open(m_storageSelection)
        ? SharedStorage::open(m_fileSystem.rootPath(), &error) : std::optional<SharedStorage>();
    if (!storage) {
        if (error.isEmpty()) error = m_fileSystem.errorString();
        m_storage.reset(); m_models.clear(); m_selected.clear();
        emit storageChanged(); emit modelsChanged();
        fail(error);
        return;
    }
    const bool storageChangedNow = !m_storage || m_storage->drive().identifier() != storage->drive().identifier()
        || m_storage->drive().rootPath() != storage->drive().rootPath();
    auto models = storage->models(&error);
    const bool changed = storageChangedNow || models.size() != m_models.size()
        || !std::equal(models.cbegin(), models.cend(), m_models.cbegin(), m_models.cend(),
            [](const StoredModel &a, const StoredModel &b) { return a.id == b.id && a.fingerprint == b.fingerprint && a.available == b.available; });
    m_storage = std::move(storage);
    if (m_foreground) m_societyClient.start(m_storage->drive().rootPath());
    m_models = std::move(models);
    if (std::none_of(m_models.cbegin(), m_models.cend(), [&](const auto &model) { return model.id == m_selected; }))
        m_selected = m_models.isEmpty() ? QString() : m_models.first().id;
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
        startLegacyCacheMigration();
    }
    if (changed) {
        emit modelsChanged();
        m_residencyPending = true;
        QTimer::singleShot(0, this, &GenerationController::pump);
    }
}
void GenerationController::startLegacyCacheMigration()
{
    const auto source = m_runtime.legacyQ8CacheDirectory;
    if (!m_storage || m_cacheMigrationActive || source.isEmpty()
        || (!QFileInfo::exists(source) && !QFileInfo(source).isSymLink())) return;
    QString error;
    const auto target = m_storage->ensureDirectory(StoreSection::Models,
        ".society-runtime/iiLocalDiffusion/q8", &error);
    if (target.isEmpty()) { fail(error); return; }
    m_cacheMigrationActive = true;
    m_cacheMigrationCancelled = false;
    m_cacheMigrationWatcher.setFuture(QtConcurrent::run([this, source, target] {
        QString error;
        dreamscapes::migrateLegacyQ8Cache(source, target, m_cacheMigrationCancelled, &error);
        return error;
    }));
}
bool GenerationController::discardLegacyStorage()
{
    // Upgrade cleanup only. New sessions never serialize or restore their queue.
    for (const auto &relative : {QStringLiteral(".dreamscapes/generation"), QStringLiteral("Generation History/Dreamscapes")}) {
        const auto directory = QDir(containerPath()).filePath(relative);
        const QFileInfo info(directory);
        if (!info.exists() && !info.isSymLink() && !info.isJunction()) continue;
        if (!info.isDir() || info.isSymLink() || info.isJunction() || info.canonicalFilePath() != directory)
            return fail(tr("The obsolete generation directory was redirected; it was preserved."));
        QLockFile legacyWorker(directory + "/.worker.lock");
        legacyWorker.setStaleLockTime(0);
        if (!legacyWorker.tryLock())
            return fail(tr("The previous Dreamscapes version is still using its generation files."));
        for (const auto &entry : QDir(directory).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
            if (!jobId(entry.fileName()) || entry.canonicalFilePath() != entry.absoluteFilePath()) continue;
            QFile request(entry.filePath() + "/request.json");
            const QFileInfo record(request);
            if (!record.isFile() || record.isSymLink() || record.size() > 1024 * 1024 || !request.open(QIODevice::ReadOnly)) continue;
            auto job = QJsonDocument::fromJson(request.readAll()).object();
            if (job.value("id") != entry.fileName() || job.value("appId") != "com.iisacc.dreamscapes"
                || job.value("schemaVersion") != 1 || job.value("state") != "completed") continue;
            const auto prefix = "Asset Library/Dreamscapes/" + entry.fileName() + '/';
            const auto image = job.value("image").toString();
            if (!image.startsWith(prefix)) continue; // Already published in flat History.
            const auto name = image.mid(prefix.size());
            if (name.contains('/') || name.contains('\\') || name.startsWith('.')
                || !QStringList{"png", "jpg", "jpeg", "webp"}.contains(QFileInfo(name).suffix().toLower()))
                return fail(tr("The previous generation record contains an invalid image path."));
            const auto original = QDir(containerPath()).filePath(image);
            if (!QFileInfo::exists(original) && !QFileInfo(original).isSymLink()) continue; // User-deleted result.
            QString error;
            const auto source = m_storage->filePath(StoreSection::AssetLibrary, "Dreamscapes/" + entry.fileName() + '/' + name, &error);
            if (source.isEmpty()) return fail(error);
            const auto destination = m_storage->filePath(StoreSection::GenerationHistory,
                entry.fileName() + "-0001." + QFileInfo(name).suffix().toLower(), &error);
            if (destination.isEmpty()) return fail(error);
            if (QFileInfo::exists(destination)) {
                // Retry a cleanup interrupted after the final image was committed.
                QFile left(source), right(destination);
                QCryptographicHash a(QCryptographicHash::Sha256), b(QCryptographicHash::Sha256);
                if (!left.open(QIODevice::ReadOnly) || !right.open(QIODevice::ReadOnly)
                    || !a.addData(&left) || !b.addData(&right) || a.result() != b.result())
                    return fail(tr("A generated image with this name already exists. Existing files were preserved."));
            } else if (!publishImages({source}, job, &error)) return fail(error);
            if (!QFile::remove(source)) return fail(tr("Cannot remove the old generated image after publishing it."));
            const auto oldOutput = QFileInfo(source).absolutePath();
            for (const auto &name : QStringList{"generation.json", QFileInfo(source).fileName() + ".json", QFileInfo(source).completeBaseName() + ".json"}) {
                const auto path = oldOutput + '/' + name;
                const QFileInfo metadata(path);
                if (metadata.isFile() && !metadata.isSymLink() && metadata.canonicalFilePath() == path)
                    QFile::remove(path);
            }
            QDir().rmdir(oldOutput); // Preserve unrelated Assets in nonempty directories.
            QDir().rmdir(QFileInfo(oldOutput).absolutePath());
        }
        for (const auto &entry : QDir(directory).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
            if (entry.fileName() == ".worker.lock") continue;
            const bool removed = entry.isDir() && !entry.isSymLink() && !entry.isJunction()
                ? QDir(entry.filePath()).removeRecursively() : QFile::remove(entry.filePath());
            if (!removed) return fail(tr("Cannot remove the obsolete generation working files."));
        }
        legacyWorker.unlock();
        if (!QDir().rmdir(directory)) return fail(tr("Cannot remove the obsolete generation directory."));
    }
    const QFileInfo parent(QDir(containerPath()).filePath(".dreamscapes"));
    if (parent.isDir() && !parent.isSymLink() && !parent.isJunction()
        && parent.canonicalFilePath() == parent.absoluteFilePath())
        QDir().rmdir(parent.filePath()); // Empty app-owned parent only.
    return true;
}

void GenerationController::updateJob(QJsonObject job)
{
    updateScreenActivity();
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
    const auto path = m_storage ? dreamscapes::generationRuntimeDirectory(*m_storage, error) : QString();
    if (path.isEmpty()) return false;
    m_workDirectory = std::make_unique<QTemporaryDir>(QDir(path).filePath("dreamscapes-generation-XXXXXX"));
    if (!m_workDirectory->isValid()) { *error = m_workDirectory->errorString(); return false; }
    for (const auto &name : {"output", "runtime", "cache"}) {
        if (!QDir(m_workDirectory->path()).mkdir(name)) {
            *error = tr("Cannot prepare generation files in Society.");
            return false;
        }
    }
    m_output = m_workDirectory->filePath("output");
    m_previewDirectory = std::make_unique<QTemporaryDir>(m_workDirectory->filePath("preview-XXXXXX"));
    if (!m_previewDirectory->isValid()) { *error = m_previewDirectory->errorString(); return false; }
    return true;
}

QStringList GenerationController::resourceArguments(QString *error)
{
    if (!m_storage) { *error = tr("Open Society to access generation resources."); return {}; }
    const auto resources = dreamscapes::generationResourceDirectory(*m_storage, error);
    if (resources.isEmpty()) return {};
    const auto manifest = m_storage->filePath(StoreSection::Models,
        ".generation-resources/iiLocalDiffusion/generation-defaults.json", error);
    if (manifest.isEmpty()) return {};
    return {"--generation-resources", resources,
        QFileInfo::exists(manifest) ? "--default-modifiers" : "--no-default-modifiers"};
}

void GenerationController::clearWorkingFiles()
{
    clearPreview();
    m_workDirectory.reset();
    m_output.clear();
}

QString GenerationController::enqueue(const QString &prompt, const QString &aspectRatio, int count)
{
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    if (!runtimeAvailable()) { fail(tr("Local image generation is unavailable in this build.")); return {}; }
#endif
    if (count < 1 || count > 1000) { fail(tr("Choose an image count from 1 to 1000.")); return {}; }
    if (!connected()) { fail(tr("Open Society on this device to load its storage map.")); return {}; }
    const auto trimmed = prompt.trimmed();
    const auto size = imageSize(aspectRatio, m_runtime.imageExtent);
    if (trimmed.isEmpty() || trimmed.size() > 32000 || size.isEmpty() || size.width() > 4096 || size.height() > 4096
        || m_runtime.steps < 1 || m_runtime.steps > 1000) { fail(tr("Enter a prompt and a supported image size.")); return {}; }
    auto selected = std::find_if(m_models.cbegin(), m_models.cend(), [&](const auto &model) { return model.id == m_selected; });
    if (selected == m_models.cend()) { fail(tr("Add a Diffusion model to Society, then refresh the model list.")); return {}; }
    const StorageMap map(m_storage->drive());
    auto required = map.files("models/" + selected->id);
    required.append(map.files("models/.generation-resources/iiLocalDiffusion")); required.removeDuplicates();
    const bool localReady = required.isEmpty() ? selected->available : map.available(required);
    if (localReady && m_runtime.nativeInference && selected->format != "safetensors" && selected->format != "unified") {
        fail(tr("Choose a checkpoint or unified model for on-device generation.")); return {};
    }
    const auto reference = selected->reference(m_storage->drive().identifier());
    QString error;
    if (localReady && m_storage->resolveModel(reference, &error).isEmpty()) { fail(error); return {}; }
    // Preserve submission order even when multiple requests share a millisecond.
    auto created = QDateTime::currentDateTimeUtc();
    if (!m_jobs.isEmpty()) {
        const auto last = QDateTime::fromString(m_jobs.last().value("createdAt").toString(), Qt::ISODateWithMs);
        if (created <= last) created = last.addMSecs(1);
    }
    QStringList jobIds;
    jobIds.reserve(count);
    const auto updated = now();
    for (int index = 0; index < count; ++index) {
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        jobIds.append(id);
        const QJsonObject job{{"schemaVersion", 1}, {"id", id}, {"appId", "com.iisacc.dreamscapes"},
            {"createdAt", created.addMSecs(index).toString(Qt::ISODateWithMs)}, {"updatedAt", updated},
            {"state", "queued"}, {"execution", localReady ? "local" : "host"}, {"prompt", trimmed},
            {"aspectRatio", aspectRatio}, {"width", size.width()}, {"height", size.height()},
            {"steps", m_runtime.steps}, {"seed", double(QRandomGenerator::global()->generate())},
            {"device", m_runtime.device}, {"model", reference}, {"modelName", selected->name}};
        m_jobs.append(job);
    }
    // Validate once and publish the whole submission before starting the serial worker.
    emit jobsChanged();
    fail({});
    emit submissionQueued(jobIds);
    QTimer::singleShot(0, this, &GenerationController::pump);
    return jobIds.first();
}

void GenerationController::pump()
{
    if (m_runtime.nativeInference && !m_foreground
        && !(m_runtime.backgroundActivity && m_runtime.backgroundActivity->allowsBackgroundExecution())) return;
    if (!m_storage || busy() || !m_controlId.isEmpty() || !runtimeAvailable()) return;
    QString error;
    const auto queued = std::find_if(m_jobs.cbegin(), m_jobs.cend(), [](const auto &job) { return job.value("state") == "queued"; });
    if (queued == m_jobs.cend()) { prepareForeground(); return; }
    m_active = *queued;
    m_log.clear();
    m_pendingOutput.clear();
    clearWorkingFiles();
    m_cancelled = false;
    m_interrupted = false;
    m_nativeTimedOut = false;
    iiSocietyContainer::StorageMap map(m_storage->drive());
    auto required = map.files("models/" + m_active.value("model").toObject().value("path").toString());
    const auto resources = map.files("models/.generation-resources/iiLocalDiffusion");
    required.append(resources); required.removeDuplicates();
    if (m_active.value("execution") == "host" || (!required.isEmpty() && !map.available(required))) {
        m_remoteActive = true; m_requiredModelFiles = required;
        m_active["state"] = "connecting-host"; m_active["execution"] = "host";
        m_societyClient.start(m_storage->drive().rootPath());
        m_remoteGeneration->setPaused(!m_foreground
            && !(m_runtime.backgroundActivity && m_runtime.backgroundActivity->allowsBackgroundExecution()));
        if (!m_remoteGeneration->start(m_active)) { finish("failed", tr("The Society host queue is already busy.")); return; }
        setInferenceStatus({{"state", "waiting-host"}, {"backend", "remote"}, {"ready", false}});
        updateJob(m_active);
        return;
    }
    if (m_cacheMigrationActive || (m_runtime.nativeInference && !m_foreground)) { m_active = {}; return; }
    const auto model = m_storage->resolveModel(m_active.value("model").toObject(), &error);
    if (model.isEmpty()) { finish("failed", error); return; }
    if (!createWorkingFiles(&error)) { finish("failed", error); return; }
    m_active["state"] = "running";
    m_active["startedAt"] = now();
    m_active["output"] = "Generation History";
    updateJob(m_active);
    if (m_runtime.nativeInference) { startNative(model); return; }
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    setInferenceStatus({{"state", "loading"}, {"ready", false}, {"model", model}});
    if (m_cancelled) { finish("cancelled"); return; }
    QStringList arguments{"--model-path", model, "--prompt", m_active.value("prompt").toString(),
        "--width", QString::number(m_active.value("width").toInt()), "--height", QString::number(m_active.value("height").toInt()),
        "--steps", QString::number(m_active.value("steps").toInt()), "--device", m_active.value("device").toString(),
        "--output-dir", m_output};
    arguments.append({"--cache-dir", m_workDirectory->filePath("cache"),
        "--preview-dir", m_previewDirectory->path()});
    arguments.append(resourceArguments(&error));
    if (!error.isEmpty()) { finish("failed", error); return; }
    if (m_active.value("model").toObject().value("format") == "safetensors")
        arguments.append({"--backend", "local", "--work-dir", m_workDirectory->filePath("runtime")});
    m_workerRequest = QJsonDocument(QJsonObject{{"schema", "iild-worker-request-v1"},
        {"id", m_active.value("id")}, {"arguments", QJsonArray::fromStringList(arguments)}}).toJson(QJsonDocument::Compact) + '\n';
    if (!startWorker(&error)) finish("failed", error);
#endif
}

void GenerationController::pollDownload()
{
    if (m_modelDownloads.isEmpty() || !m_storage) return;
    iiSocietyContainer::StorageMap map(m_storage->drive());
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
    if (!m_storage || m_requiredModelFiles.isEmpty()) return;
    const auto model = m_active.value("model").toObject().value("path").toString();
    if (iiSocietyContainer::StorageMap(m_storage->drive()).available(m_requiredModelFiles)) {
        m_active["modelDownload"] = QJsonObject{{"state", "ready"}};
        updateJob(m_active); return;
    }
    if (m_modelDownloads.contains(model)) {
        m_active["modelDownload"] = QJsonObject{{"state", "background"}, {"requestId", m_modelDownloads.value(model)}};
        updateJob(m_active); return;
    }
    QString error;
    const auto request = iiSocietyContainer::StorageMap(m_storage->drive()).request(m_requiredModelFiles, &error);
    m_active["modelDownload"] = request.isEmpty() ? QJsonObject{{"state", "failed"}, {"error", error}}
        : QJsonObject{{"state", "background"}, {"requestId", request}};
    if (!request.isEmpty()) { m_modelDownloads.insert(model, request); m_societyClient.synchronizeNow(); }
    updateJob(m_active);
}

void GenerationController::startNative(const QString &model)
{
    // jobsChanged can synchronously cancel the transition to running.
    if (m_cancelled) { finish(m_interrupted ? "interrupted" : "cancelled"); return; }
    m_nativeCancelled = false;
    iiLocalDiffusion::NativeGenerationRequest request;
    request.modelPath = QFile::encodeName(model).toStdString();
    request.prompt = m_active.value("prompt").toString().toStdString();
    request.width = m_active.value("width").toInt();
    request.height = m_active.value("height").toInt();
    request.steps = m_active.value("steps").toInt();
    request.seed = m_active.value("seed").toInteger();
#ifdef DREAMSCAPES_LOCAL_RUNTIME_PROBE
    bool validSeed = false;
    const auto probeSeed = qEnvironmentVariable("DREAMSCAPES_PROBE_SEED").toLongLong(&validSeed);
    if (validSeed) request.seed = probeSeed;
#endif
    // This controller owns the inactivity deadline. The SDK's independent
    // total-duration limit must allow a long generation that keeps progressing.
    request.timeoutMilliseconds = std::numeric_limits<int>::max();
    QString storageError;
    const auto cache = m_storage->ensureDirectory(StoreSection::Models,
        ".society-runtime/iiLocalDiffusion/q8", &storageError);
    if (cache.isEmpty()) { finish("failed", storageError); return; }
    const auto resources = resourceArguments(&storageError);
    if (resources.isEmpty()) { finish("failed", storageError); return; }
    request.q8CacheDirectory = QFile::encodeName(cache).toStdString();
    iiLocalDiffusion::NativeGenerationOptions options;
    options.resourceDirectory = QFile::encodeName(resources[1]).toStdString();
    options.defaultModifiers = resources[2] == "--default-modifiers";
    m_active["generation"] = QJsonObject{{"backend", "iiLocalDiffusion-native"},
        {"model_path", model}, {"seed", QString::number(request.seed)}};
    const auto id = m_active.value("id").toString();
    if (m_runtime.backgroundActivity) {
        if (m_societyBackgroundActivity) {
            m_societyBackgroundActivity = m_backgroundActivityActive = false;
            m_runtime.backgroundActivity->end(true);
        }
        const QPointer<GenerationController> self(this);
        m_backgroundActivityActive = true;
        m_runtime.backgroundActivity->begin(id, [self, id] {
            if (!self || self->m_active.value("id") != id || !self->m_backgroundActivityActive) return;
            // Losing a system execution grant does not discard the image.
            // Foreground work can continue; hidden work parks at an engine
            // boundary and resumes with the same request, seed and tensors.
            self->m_backgroundActivityActive = false;
            self->setNativePaused(!self->m_foreground);
            self->m_runtime.backgroundActivity->end(false);
        });
    }
    const QJsonObject loading{{"state", "loading"}, {"ready", false}, {"backend", "native"}};
    if (m_nativePaused) m_resumeInferenceStatus = loading;
    else setInferenceStatus(loading);
    const auto control = m_runtime.nativeExecutionControl;
    const auto backend = m_runtime.backgroundActivity && m_runtime.backgroundActivity->requiresCpuExecution()
        ? iiLocalDiffusion::NativeComputeBackend::Cpu : iiLocalDiffusion::NativeComputeBackend::Automatic;
    auto generation = m_active.value("generation").toObject();
    generation["computeBackend"] = backend == iiLocalDiffusion::NativeComputeBackend::Cpu ? "cpu" : "automatic";
    m_active["generation"] = generation;
    const auto generate = m_runtime.nativeGenerate ? m_runtime.nativeGenerate
        : [control, backend](const auto &request, const auto &options, const auto &cancelled, const auto &progress, const auto &preview) {
            return iiLocalDiffusion::generateNativeImageWithPreview(request, options, backend, cancelled, progress, preview, control);
        };
    const auto legacyCache = m_runtime.legacyQ8CacheDirectory;
    m_nativeWatcher.setFuture(QtConcurrent::run([this, request, options, id, generate, legacyCache, cache] {
        try {
            QString migrationError;
            if (!dreamscapes::migrateLegacyQ8Cache(legacyCache, cache, m_nativeCancelled, &migrationError))
                throw std::runtime_error(migrationError.toStdString());
            return generate(request, options, m_nativeCancelled, [this, id](const iiLocalDiffusion::NativeGenerationProgress &event) {
                QMetaObject::invokeMethod(this, [this, id, event] {
                    if (m_active.value("id") != id || m_cancelled || m_nativeTimedOut) return;
                    if (m_runtime.backgroundActivity) m_runtime.backgroundActivity->update(event);
                    using Stage = iiLocalDiffusion::NativeGenerationStage;
                    if (!m_nativePaused && event.stage != Stage::Waiting
                        && event.step > 0 && (event.total >= event.step || event.stage == Stage::Computing)) {
                        m_nativeTimeRemaining = std::max(1, m_runtime.nativeTimeoutMilliseconds);
                        m_nativeDeadline.start(m_nativeTimeRemaining);
                    }
                    if (event.stage == Stage::Computing) return;
                    const char *state = event.stage == Stage::Waiting ? "waiting-engine"
                        : event.stage == Stage::Preparing ? "preparing-model"
                        : event.stage == Stage::Loading ? "loading" : event.stage == Stage::Encoding ? "encoding"
                        : event.stage == Stage::Decoding ? "decoding" : "denoising";
                    const QJsonObject status{{"state", state}, {"ready", false}, {"backend", "native"},
                        {"step", event.step}, {"total", event.total}};
                    if (m_nativePaused) m_resumeInferenceStatus = status;
                    else setInferenceStatus(status);
                    const auto steps = m_active.value("steps").toInt();
                    if (event.stage == Stage::Denoising
                        && (event.total == steps || event.total == std::max(1, int(steps * 0.35f)))
                        && event.step >= 0 && event.step <= event.total
                        && (event.total != m_previewTotalSteps || event.step >= m_previewStep)) {
                        m_previewStep = event.step;
                        m_previewTotalSteps = event.total;
                        emit previewChanged();
                    }
                }, Qt::QueuedConnection);
            }, [this, id](const iiLocalDiffusion::NativeGenerationPreview &preview) {
                QMetaObject::invokeMethod(this, [this, id, preview] {
                    if (m_active.value("id") == id && !m_cancelled && !m_nativeTimedOut) acceptNativePreview(preview);
                }, Qt::QueuedConnection);
            });
        } catch (const std::exception &error) {
            iiLocalDiffusion::NativeGenerationResult result;
            result.error = error.what();
            return result;
        } catch (...) {
            iiLocalDiffusion::NativeGenerationResult result;
            result.error = "The native image engine stopped unexpectedly.";
            return result;
        }
    }));
    m_nativeTimeRemaining = std::max(1, m_runtime.nativeTimeoutMilliseconds);
    if (!m_nativePaused) m_nativeDeadline.start(m_nativeTimeRemaining);
}

void GenerationController::finishNative()
{
    m_nativeDeadline.stop();
    const auto result = m_nativeWatcher.result();
    auto generation = m_active.value("generation").toObject();
    generation["performance"] = QJsonObject{{"modelCacheHit", result.modelCacheHit},
        {"memoryBudgetBytes", static_cast<double>(result.memoryBudgetBytes)}, {"threads", result.threads},
        {"modelLoadMilliseconds", result.modelLoadMilliseconds}, {"generationMilliseconds", result.generationMilliseconds}};
    auto performance = generation["performance"].toObject();
    performance["q8CacheUsed"] = result.q8CacheUsed;
    performance["diskCacheHit"] = result.diskCacheHit;
    performance["modelBytes"] = static_cast<double>(result.modelBytes);
    performance["preparationMilliseconds"] = result.preparationMilliseconds;
    generation["performance"] = performance;
    m_active["generation"] = generation;
    setInferenceStatus({{"state", "idle"}, {"ready", false}, {"backend", "native"}});
    if (m_nativeTimedOut) { finish("failed", tr("Image generation stopped making progress. Try again with a smaller model.")); return; }
    if (m_interrupted) { finish("interrupted", tr("The system ended background generation. Return to Dreamscapes and try again.")); return; }
    if (m_cancelled || result.cancelled) { finish("cancelled"); return; }
    if (!result.error.empty()) { finish("failed", QString::fromStdString(result.error)); return; }
    QString error;
    if (m_storage->resolveModel(m_active.value("model").toObject(), &error).isEmpty()) {
        finish("failed", error); return;
    }
    if (result.width != m_active.value("width").toInt() || result.height != m_active.value("height").toInt()
        || result.rgb.size() != static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 3) {
        finish("failed", tr("The local engine returned an incomplete image.")); return;
    }
    const QImage image(result.rgb.data(), result.width, result.height, result.width * 3, QImage::Format_RGB888);
    const auto path = QDir(m_output).filePath("image.png");
    if (!image.save(path, "PNG")) { finish("failed", tr("Cannot save the generated image.")); return; }
    if (!publishImages({path}, m_active, &error)) finish("failed", error);
    else finish("completed");
}

bool GenerationController::startWorker(QString *error)
{
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    if (m_process.state() == QProcess::NotRunning) {
        const auto path = m_storage ? dreamscapes::generationRuntimeDirectory(*m_storage, error) : QString();
        if (path.isEmpty()) return false;
        // Python imports/JIT may retain temporary paths. Keep their directory valid
        // for the SDK worker's lifetime, separately from disposable per-job files.
        m_workerDirectory = std::make_unique<QTemporaryDir>(
            QDir(path).filePath("dreamscapes-inference-XXXXXX"));
        if (!m_workerDirectory->isValid()) { *error = m_workerDirectory->errorString(); return false; }
        auto environment = m_process.processEnvironment();
        for (const auto &name : {"TMPDIR", "TEMP", "TMP"}) environment.insert(name, m_workerDirectory->path());
        // Backend/model caches also belong to Society. The client consumes
        // already available local sources; missing models are managed in Society.
        for (const auto &name : {"HF_HOME", "HUGGINGFACE_HUB_CACHE", "TRANSFORMERS_CACHE", "TORCH_HOME",
                 "TORCH_EXTENSIONS_DIR", "TRITON_CACHE_DIR", "XDG_CACHE_HOME"})
            environment.insert(name, m_workerDirectory->filePath("cache/" + QString::fromLatin1(name)));
        environment.insert("HF_HUB_OFFLINE", "1");
        environment.insert("TRANSFORMERS_OFFLINE", "1");
        const auto resources = resourceArguments(error);
        if (resources.isEmpty()) return false;
        environment.insert("IILD_GENERATION_RESOURCES", resources[1]);
        environment.insert("PYTHONDONTWRITEBYTECODE", "1");
        // Read installed bytecode caches; a fresh prefix would force source recompilation.
        environment.remove("PYTHONPYCACHEPREFIX");
        m_process.setProcessEnvironment(environment);
        m_process.setWorkingDirectory(m_workerDirectory->path());
        m_workerReady = false;
        m_workerForegroundSupported = false;
        m_pendingOutput.clear();
        m_process.start(m_runtime.executable, {"--worker"});
    } else {
        sendWorkerRequest();
    }
    return true;
#else
    *error = tr("This platform does not provide the local inference worker.");
    return false;
#endif
}

void GenerationController::prepareForeground()
{
    if (m_runtime.nativeInference) {
        m_residencyPending = false;
        setInferenceStatus({{"state", m_foreground ? "waiting-generation" : "background"}, {"ready", false}});
        return;
    }
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    if (!m_residencyPending) return;
    m_residencyPending = false;
    if (!m_foreground && m_process.state() == QProcess::NotRunning) return;
    QString error;
    QStringList arguments;
    if (m_foreground && !m_selected.isEmpty()) {
        const auto selected = std::find_if(m_models.cbegin(), m_models.cend(), [&](const auto &model) {
            return model.id == m_selected;
        });
        if (selected == m_models.cend()) return;
        if (!selected->available) {
            setInferenceStatus({{"state", "model-on-host"}, {"ready", false}});
            return;
        }
        const auto model = m_storage->resolveModel(selected->reference(m_storage->drive().identifier()), &error);
        if (model.isEmpty()) { setInferenceStatus({{"state", "error"}, {"ready", false}, {"error", error}}); return; }
        arguments = {"--model-path", model, "--device", m_runtime.device,
                     "--width", QString::number(m_runtime.imageExtent), "--height", QString::number(m_runtime.imageExtent),
                     "--steps", QString::number(m_runtime.steps)};
        arguments.append(resourceArguments(&error));
        if (!error.isEmpty()) { setInferenceStatus({{"state", "error"}, {"ready", false}, {"error", error}}); return; }
        if (selected->format == "safetensors") arguments.append({"--backend", "local"});
    }
    m_controlId = "foreground-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_preparingModel = m_selected;
    m_cancelled = false;
    setInferenceStatus({{"state", arguments.isEmpty() ? "waiting-model" : "preparing"},
                        {"foreground", m_foreground}, {"ready", false}, {"requestId", m_controlId}});
    // Start the worker first so its session-owned cache path is stable. Nothing
    // is sent until the capability-bearing IILD_READY arrives.
    if (!startWorker(&error)) {
        m_controlId.clear();
        setInferenceStatus({{"state", "error"}, {"ready", false}, {"error", error}});
        return;
    }
    if (!arguments.isEmpty()) arguments.append({"--cache-dir", m_workerDirectory->filePath("cache")});
    m_workerRequest = QJsonDocument(QJsonObject{{"schema", "iild-worker-request-v1"}, {"id", m_controlId},
        {"action", "foreground"}, {"foreground", m_foreground},
        {"arguments", QJsonArray::fromStringList(arguments)}}).toJson(QJsonDocument::Compact) + '\n';
    sendWorkerRequest();
#endif
}

bool GenerationController::collectResult(QString *error)
{
    if (m_storage->resolveModel(m_active.value("model").toObject(), error).isEmpty()) return false;
    const QFileInfo output(m_output);
    if (!m_workDirectory || m_output != m_workDirectory->filePath("output") || !output.isDir()
        || output.isSymLink() || output.canonicalFilePath() != m_output) {
        *error = tr("The temporary generation output was redirected.");
        return false;
    }
    const QFileInfo provenance(m_output + "/generation.json");
    QFile record(provenance.filePath());
    if (!provenance.isFile() || provenance.isSymLink() || provenance.size() > 16 * 1024 * 1024
        || !record.open(QIODevice::ReadOnly)) {
        *error = tr("The engine exited without a readable image and generation record.");
        return false;
    }
    const auto generation = QJsonDocument::fromJson(record.readAll()).object();
    if (generation.isEmpty()) { *error = tr("The engine returned an invalid generation record."); return false; }
    m_active["generation"] = generation; // Session-only provenance; no JSON is stored in Society.
    QStringList sources;
    for (const auto &file : QDir(m_output).entryInfoList(QDir::Files, QDir::Name))
        if (QStringList{"png", "jpg", "jpeg", "webp"}.contains(file.suffix().toLower()))
            sources.append(file.filePath());
    return publishImages(sources, m_active, error);
}

bool GenerationController::publishImages(const QStringList &sources, QJsonObject &job, QString *error)
{
    if (sources.isEmpty()) {
        *error = tr("The engine exited without a readable generated image.");
        return false;
    }
    QStringList destinations;
    QJsonArray images;
    // Validate every image before publishing any part of a batch.
    for (const auto &source : sources) {
        const QFileInfo file(source);
        QImageReader reader(source);
        if (!file.isFile() || file.isSymLink() || file.canonicalFilePath() != source
            || reader.size() != QSize(job.value("width").toInt(), job.value("height").toInt())
            || reader.read().isNull()) {
            *error = tr("The engine returned an unreadable or incorrectly sized image.");
            return false;
        }
        const auto name = job.value("id").toString()
            + QString("-%1.").arg(destinations.size() + 1, 4, 10, QLatin1Char('0')) + file.suffix().toLower();
        const auto destination = m_storage->filePath(StoreSection::GenerationHistory, name, error);
        if (destination.isEmpty()) return false;
        if (QFileInfo::exists(destination)) {
            *error = tr("A generated image with this name already exists. Existing files were preserved.");
            return false;
        }
        destinations.append(destination);
        images.append("Generation History/" + name);
    }
    for (qsizetype i = 0; i < sources.size(); ++i) {
        // Only validated final images are published into Generation History.
        // Engine intermediates remain in Society's private runtime area.
        QFile source(sources[i]);
        QSaveFile destination(destinations[i]);
        bool copied = source.open(QIODevice::ReadOnly) && destination.open(QIODevice::WriteOnly);
        while (copied && !source.atEnd()) {
            const auto bytes = source.read(1024 * 1024);
            copied = source.error() == QFile::NoError && destination.write(bytes) == bytes.size();
        }
        if (!copied || !destination.commit()) {
            for (qsizetype previous = 0; previous < i; ++previous) QFile::remove(destinations[previous]);
            *error = tr("Cannot save the generated image: %1").arg(source.error() == QFile::NoError
                ? destination.errorString() : source.errorString());
            return false;
        }
    }
    job["output"] = "Generation History";
    job["images"] = images;
    job["image"] = images.first();
    return true;
}

void GenerationController::finish(const QString &state, const QString &error)
{
    // Replication outlives this generation job, including failure/cancellation.
    m_remoteActive = false; m_requiredModelFiles.clear();
    m_active["state"] = state;
    m_active["finishedAt"] = now();
    m_active["error"] = error;
    updateJob(m_active);
    if (state == "completed") m_societyClient.synchronizeNow();
    if (m_runtime.nativeInference && m_runtime.backgroundActivity)
        m_runtime.backgroundActivity->finishPresentation(m_active.value("id").toString(), state);
    fail(error);
    m_active = {};
    m_runtime.nativeExecutionControl->setPaused(false);
    m_nativePaused = false;
    updateScreenActivity();
    if (m_foreground) m_residencyPending = true;
    m_workerRequest.clear();
    clearWorkingFiles();
    const bool queuedOnHost = std::any_of(m_jobs.cbegin(), m_jobs.cend(), [](const auto &job) {
        return job.value("state") == "queued" && job.value("execution") == "host";
    });
    if (m_backgroundActivityActive && (!m_societyBackgroundActivity || (m_modelDownloads.isEmpty() && !queuedOnHost))) {
        m_societyBackgroundActivity = false;
        m_backgroundActivityActive = false;
        m_runtime.backgroundActivity->end(state == "completed");
    }
    if (!m_foreground && m_runtime.nativeInference) iiLocalDiffusion::releaseNativeDiffusionCache();
    emit jobsChanged();
    QTimer::singleShot(0, this, &GenerationController::pump);
}

void GenerationController::readProcessOutput()
{
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    const auto data = m_process.readAllStandardOutput();
    m_log = (m_log + data).right(16384);
    m_pendingOutput += data;
    qsizetype newline;
    while ((newline = m_pendingOutput.indexOf('\n')) >= 0) {
        const auto line = m_pendingOutput.left(newline);
        m_pendingOutput.remove(0, newline + 1);
        // Progress bars can leave a carriage-return prefix on the merged stream.
        const auto start = line.indexOf("IILD_PREVIEW ");
        if (start >= 0 && line.size() - start <= 4096) acceptPreview(line.mid(start + 13));
        const auto nativeProgress = line.indexOf("IILD_NATIVE_PROGRESS ");
        if (nativeProgress >= 0 && line.size() - nativeProgress <= 4096)
            acceptNativeWorkerProgress(line.mid(nativeProgress + 21));
        const auto modelProgress = line.indexOf("IILD_MODEL_PROGRESS ");
        if (modelProgress >= 0 && line.size() - modelProgress <= 4096 && (busy() || !m_controlId.isEmpty()) && !m_cancelled) {
            const auto event = QJsonDocument::fromJson(line.mid(modelProgress + 20)).object();
            const auto completed = event.value("completed_bytes").toDouble(-1), total = event.value("total_bytes").toDouble(-1);
            if (event.value("schema") == "iild-model-progress-v1" && completed >= 0 && total >= completed)
                setInferenceStatus({{"state", completed == total ? "preparing" : "checking-model"}, {"ready", false},
                    {"completedBytes", completed}, {"totalBytes", total}});
        }
        const auto ready = line.indexOf("IILD_READY ");
        if (ready >= 0 && line.size() - ready <= 4096
            && QJsonDocument::fromJson(line.mid(ready + 11)).object().value("schema") == "iild-worker-v1") {
            m_workerReady = true;
            m_workerForegroundSupported = QJsonDocument::fromJson(line.mid(ready + 11)).object()
                .value("capabilities").toArray().contains(QStringLiteral("foreground-residency"));
            sendWorkerRequest();
        }
        const auto result = line.indexOf("IILD_RESULT ");
        if (result >= 0 && line.size() - result <= 65536) acceptWorkerResult(line.mid(result + 12));
    }
    // A 4,000-character SDK error can expand to 48 KB when JSON escapes Unicode.
    m_pendingOutput = m_pendingOutput.right(65536);
#endif
}

void GenerationController::acceptNativeWorkerProgress(const QByteArray &line)
{
    if (!busy() || m_cancelled || !m_controlId.isEmpty()) return;
    const auto event = QJsonDocument::fromJson(line).object();
    if (event.value("schema") != "iild-native-progress-v1") return;
    const auto stage = event.value("stage").toString();
    if (!QStringList{"waiting", "loading", "encoding", "denoising", "decoding", "preparing-model"}.contains(stage)) return;
    const int step = event.value("step").toInt(-1), total = event.value("total").toInt(-1);
    if (step < 0 || total < 0 || step > total || total > 1000000) return;
    if (stage == "denoising") {
        const auto steps = m_active.value("steps").toInt();
        if ((total != steps && total != std::max(1, int(steps * 0.35f)))
            || (total == m_previewTotalSteps && step <= m_previewStep)) return;
        m_previewStep = step;
        m_previewTotalSteps = total;
        emit previewChanged();
    }
    setInferenceStatus({{"state", stage}, {"ready", false}, {"backend", "native"},
                        {"step", step}, {"total", total}});
}

void GenerationController::sendWorkerRequest()
{
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    if ((!busy() && m_controlId.isEmpty()) || !m_workerReady || m_workerRequest.isEmpty()) return;
    if (!m_controlId.isEmpty() && !m_workerForegroundSupported) {
        m_controlId.clear();
        m_workerRequest.clear();
        setInferenceStatus({{"state", "error"}, {"ready", false},
                            {"error", tr("Update iiLocalDiffusion to enable foreground model preparation.")}});
        QTimer::singleShot(0, this, &GenerationController::pump);
        return;
    }
    if (busy() && m_cancelled) { stopProcess(false); return; }
    if (m_process.write(m_workerRequest) != m_workerRequest.size()) {
        m_log += "\nCannot send the inference request to iiLocalDiffusion.";
        stopProcess(true);
    }
    m_workerRequest.clear();
#endif
}

void GenerationController::acceptWorkerResult(const QByteArray &line)
{
    const auto event = QJsonDocument::fromJson(line).object();
    if (event.value("schema") != "iild-worker-result-v1" || !event.value("ok").isBool()) return;
    if (!m_controlId.isEmpty() && event.value("id") == m_controlId) {
        m_controlId.clear();
        auto status = event.value("residency").toObject();
        status["pid"] = event.value("pid");
        status["cache"] = event.value("cache");
        if (!event.value("ok").toBool()) {
            status["state"] = "error";
            status["ready"] = false;
            status["error"] = event.value("error");
        } else if (m_residencyPending) {
            status["state"] = "preparing";
            status["ready"] = false;
        }
        setInferenceStatus(status);
        QTimer::singleShot(0, this, &GenerationController::pump);
        return;
    }
    if (!busy() || m_cancelled) return;
    if (event.value("schema") != "iild-worker-result-v1" || event.value("id") != m_active.value("id")
        || !event.value("ok").isBool()) return;
    m_active["worker"] = event; // Timing/cache diagnostics remain in the app's memory.
    auto status = event.value("residency").toObject();
    status["pid"] = event.value("pid");
    setInferenceStatus(status);
    if (!event.value("ok").toBool()) {
        auto error = event.value("error").toString();
        if (error.isEmpty()) error = QString::fromUtf8(m_log).right(4000);
        finish("failed", error);
        return;
    }
    QString error;
    if (!collectResult(&error)) finish("failed", error);
    else finish("completed");
}

void GenerationController::acceptPreview(const QByteArray &line)
{
    if (!busy() || m_cancelled || !m_previewDirectory) return;
    const auto event = QJsonDocument::fromJson(line).object();
    const auto step = event.value("step").toInt();
    const auto total = event.value("total_steps").toInt();
    const auto name = event.value("image").toString();
    const auto sequence = event.value("sequence").toInt(step);
    if (event.value("schema") != "iild-preview-v1" || step < 1 || sequence <= m_previewSequence || total < step || total > 10000
        || (m_previewTotalSteps == total && step < m_previewStep)
        || name != QString("step-%1.png").arg(sequence, 6, 10, QLatin1Char('0'))) return;
    const auto path = m_previewDirectory->filePath(name);
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.canonicalFilePath() != path || info.size() > 4 * 1024 * 1024) return;
    QImageReader reader(path);
    const auto size = reader.size();
    if (size.isEmpty() || size.width() > 512 || size.height() > 512 || reader.read().isNull()) return;
    m_previewImage = QUrl::fromLocalFile(path);
    m_previewSequence = sequence;
    m_previewStep = step;
    m_previewTotalSteps = total;
    m_active["previewSteps"] = step;
    m_active["previewTotalSteps"] = total;
    emit previewChanged();
}

void GenerationController::acceptNativePreview(const iiLocalDiffusion::NativeGenerationPreview &preview)
{
    if (!busy() || !m_previewDirectory || preview.sequence <= m_previewSequence
        || preview.width < 1 || preview.height < 1 || preview.width > 512 || preview.height > 512
        || preview.step < 1 || preview.total < preview.step
        || preview.rgb.size() != static_cast<std::size_t>(preview.width) * preview.height * 3) return;
    const QImage image(preview.rgb.data(), preview.width, preview.height, preview.width * 3, QImage::Format_RGB888);
    const auto name = QString("step-%1.png").arg(preview.sequence, 6, 10, QLatin1Char('0'));
    QSaveFile file(m_previewDirectory->filePath(name));
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) return;
    acceptPreview(QJsonDocument(QJsonObject{{"schema", "iild-preview-v1"}, {"step", preview.step},
        {"total_steps", preview.total}, {"sequence", preview.sequence}, {"image", name}}).toJson(QJsonDocument::Compact));
}

void GenerationController::clearPreview()
{
    m_previewImage.clear();
    m_previewStep = m_previewTotalSteps = 0;
    m_previewSequence = 0;
    emit previewChanged();
    m_previewDirectory.reset();
}
void GenerationController::stopProcess(bool force)
{
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    if (m_process.state() == QProcess::NotRunning) return;
#ifdef Q_OS_UNIX
    const auto pid = m_process.processId();
    if (pid > 0) ::kill(-static_cast<pid_t>(pid), force ? SIGKILL : SIGTERM);
#else
    if (force) m_process.kill(); else m_process.terminate();
#endif
#else
    Q_UNUSED(force);
#endif
}
bool GenerationController::cancel(const QString &id)
{
    if (busy() && m_active.value("id") == id) {
        if (m_remoteActive) { m_remoteGeneration->cancel(); return true; }
        m_cancelled = true;
        m_nativeCancelled = true;
        if (m_runtime.nativeInference)
            setInferenceStatus({{"state", "cancelling"}, {"ready", false}, {"backend", "native"}});
        stopProcess(false);
        QTimer::singleShot(2000, this, [this, id] { if (m_active.value("id") == id) stopProcess(true); });
        return true;
    }
    if (!m_storage || !jobId(id)) return false;
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
