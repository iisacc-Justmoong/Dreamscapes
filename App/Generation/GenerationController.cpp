#include "GenerationController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QLockFile>
#include <QCryptographicHash>
#include <QUuid>
#include <algorithm>
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
    const auto rounded = [](int value) { return std::max(8, value / 8 * 8); };
    if (ratio == "1:1") return {unit, unit};
    if (ratio == "4:3") return {unit, rounded(unit * 3 / 4)};
    if (ratio == "3:4") return {rounded(unit * 3 / 4), unit};
    if (ratio == "16:9") return {unit, rounded(unit * 9 / 16)};
    if (ratio == "9:16") return {rounded(unit * 9 / 16), unit};
    return {};
}
GenerationRuntime defaultRuntime()
{
    GenerationRuntime runtime;
    runtime.executable = qEnvironmentVariable("IILD_GENERATOR_EXECUTABLE");
    if (runtime.executable.isEmpty())
        runtime.executable = QStringLiteral(DREAMSCAPES_DIFFUSION_EXECUTABLE);
    runtime.pythonExecutable = QStringLiteral(DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE);
    return runtime;
}
}

GenerationController::GenerationController(QObject *parent) : GenerationController(defaultRuntime(), parent) {}
GenerationController::GenerationController(GenerationRuntime runtime, QObject *parent)
    : QObject(parent), m_runtime(std::move(runtime))
{
    if (m_runtime.temporaryDirectory.isEmpty())
        m_runtime.temporaryDirectory = qEnvironmentVariable("DREAMSCAPES_TEMP_DIRECTORY", QDir::tempPath());
    if (auto *application = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(application, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            setForeground(state == Qt::ApplicationActive);
        });
        setForeground(application->applicationState() == Qt::ApplicationActive);
    }
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    auto environment = QProcessEnvironment::systemEnvironment();
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
            if (busy()) finish(m_cancelled ? "cancelled" : "failed", m_cancelled ? QString() : m_process.errorString());
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        readProcessOutput();
        m_workerReady = false;
        m_workerRequest.clear();
        m_workerDirectory.reset();
        m_controlId.clear();
        setInferenceStatus({{"state", "stopped"}, {"ready", false}});
        if (!busy()) return;
        if (m_cancelled) { finish("cancelled"); return; }
        finish("failed", tr("The inference worker exited before completing the request (exit %1). %2").arg(code)
            .arg(QString::fromUtf8(m_log).right(4000)));
    });
#endif
}

GenerationController::~GenerationController()
{
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        stopProcess(false);
        if (!m_process.waitForFinished(1000)) { stopProcess(true); m_process.waitForFinished(1000); }
    }
#endif
    clearWorkingFiles();
}

bool GenerationController::connected() const { return m_storage.has_value(); }
QString GenerationController::containerPath() const { return m_storage ? m_storage->drive().rootPath() : QString(); }
QString GenerationController::selectedModel() const { return m_selected; }
bool GenerationController::busy() const { return !m_active.isEmpty(); }
QString GenerationController::errorString() const { return m_error; }
QUrl GenerationController::previewImage() const { return m_previewImage; }
int GenerationController::previewStep() const { return m_previewStep; }
int GenerationController::previewTotalSteps() const { return m_previewTotalSteps; }
bool GenerationController::foreground() const { return m_foreground; }
QVariantMap GenerationController::inferenceStatus() const { return m_inferenceStatus.toVariantMap(); }
void GenerationController::setInferenceStatus(QJsonObject status)
{
    if (m_inferenceStatus == status) return;
    m_inferenceStatus = std::move(status);
    emit inferenceStatusChanged();
}
void GenerationController::setForeground(bool foreground)
{
    if (m_foreground == foreground) return;
    m_foreground = foreground;
    m_residencyPending = true;
    setInferenceStatus({{"state", foreground ? "preparing" : "background"},
                        {"foreground", foreground}, {"ready", false}});
    emit foregroundChanged();
    QTimer::singleShot(0, this, &GenerationController::pump);
}
bool GenerationController::runtimeAvailable() const
{
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
        result.append(QVariantMap{{"id", model.id}, {"name", model.name}, {"format", model.format}});
    return result;
}
void GenerationController::setSelectedModel(const QString &id)
{
    if (m_selected == id) return;
    if (std::none_of(m_models.cbegin(), m_models.cend(), [&](const auto &model) { return model.id == id; })) return;
    m_selected = id;
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
    if (!m_storage || !m_storage->drive().isValid()) return {};
    for (auto job = m_jobs.crbegin(); job != m_jobs.crend(); ++job) {
        if (job->value("state") != "completed") continue;
        const auto relative = job->value("image").toString();
        const auto prefix = QStringLiteral("Generation History/");
        const auto name = relative.mid(prefix.size());
        if (!relative.startsWith(prefix) || !name.startsWith(job->value("id").toString() + '-')
            || name.contains('/') || name.contains('\\')) continue;
        const auto path = QDir(containerPath()).filePath(relative);
        const QFileInfo info(path);
        if (info.isFile() && !info.isSymLink() && info.canonicalFilePath() == path) {
            auto result = job->toVariantMap();
            result.insert("imageSource", QUrl::fromLocalFile(path));
            return result;
        }
    }
    return {};
}

bool GenerationController::connectStorage(const QString &path)
{
    if (busy()) return fail(tr("Wait for the current generation or cancel it before changing storage."));
    QString error;
    auto storage = SharedStorage::open(path, &error);
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
    emit jobsChanged();
    return true;
}
void GenerationController::refreshModels()
{
    if (!m_storage) { connectStorage(); return; }
    QString error;
    m_models = m_storage->models(&error);
    if (std::none_of(m_models.cbegin(), m_models.cend(), [&](const auto &model) { return model.id == m_selected; }))
        m_selected = m_models.isEmpty() ? QString() : m_models.first().id;
    fail(error);
    emit modelsChanged();
    m_residencyPending = true;
    QTimer::singleShot(0, this, &GenerationController::pump);
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
    const QFileInfo base(m_runtime.temporaryDirectory);
    const auto path = base.canonicalFilePath();
    if (!base.isAbsolute() || !base.isDir() || path.isEmpty()
        || path == containerPath() || path.startsWith(containerPath() + '/')) {
        *error = tr("Use an available app temporary directory outside Society.");
        return false;
    }
    m_workDirectory = std::make_unique<QTemporaryDir>(QDir(path).filePath("dreamscapes-generation-XXXXXX"));
    if (!m_workDirectory->isValid()) { *error = m_workDirectory->errorString(); return false; }
    for (const auto &name : {"output", "runtime", "cache"}) {
        if (!QDir(m_workDirectory->path()).mkdir(name)) {
            *error = tr("Cannot prepare the app's temporary generation files.");
            return false;
        }
    }
    m_output = m_workDirectory->filePath("output");
    m_previewDirectory = std::make_unique<QTemporaryDir>(m_workDirectory->filePath("preview-XXXXXX"));
    if (!m_previewDirectory->isValid()) { *error = m_previewDirectory->errorString(); return false; }
    return true;
}

void GenerationController::clearWorkingFiles()
{
    clearPreview();
    m_workDirectory.reset();
    m_output.clear();
}

QString GenerationController::enqueue(const QString &prompt, const QString &aspectRatio, int count)
{
    if (count < 1 || count > 1000) { fail(tr("Choose an image count from 1 to 1000.")); return {}; }
    if (!m_storage) { fail(tr("Open Society and choose the shared container first.")); return {}; }
    const auto trimmed = prompt.trimmed();
    const auto size = imageSize(aspectRatio, m_runtime.imageExtent);
    if (trimmed.isEmpty() || trimmed.size() > 32000 || size.isEmpty() || size.width() > 4096 || size.height() > 4096
        || m_runtime.steps < 1 || m_runtime.steps > 1000) { fail(tr("Enter a prompt and a supported image size.")); return {}; }
    auto selected = std::find_if(m_models.cbegin(), m_models.cend(), [&](const auto &model) { return model.id == m_selected; });
    if (selected == m_models.cend()) { fail(tr("Add a Diffusion model to Society, then refresh the model list.")); return {}; }
    const auto reference = selected->reference(m_storage->drive().identifier());
    QString error;
    if (m_storage->resolveModel(reference, &error).isEmpty()) { fail(error); return {}; }
    // Preserve submission order even when multiple requests share a millisecond.
    auto created = QDateTime::currentDateTimeUtc();
    if (!m_jobs.isEmpty()) {
        const auto last = QDateTime::fromString(m_jobs.last().value("createdAt").toString(), Qt::ISODateWithMs);
        if (created <= last) created = last.addMSecs(1);
    }
    QString firstId;
    const auto updated = now();
    for (int index = 0; index < count; ++index) {
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (firstId.isEmpty()) firstId = id;
        const QJsonObject job{{"schemaVersion", 1}, {"id", id}, {"appId", "com.iisacc.dreamscapes"},
            {"createdAt", created.addMSecs(index).toString(Qt::ISODateWithMs)}, {"updatedAt", updated},
            {"state", "queued"}, {"prompt", trimmed},
            {"aspectRatio", aspectRatio}, {"width", size.width()}, {"height", size.height()},
            {"steps", m_runtime.steps}, {"device", m_runtime.device}, {"model", reference}, {"modelName", selected->name}};
        m_jobs.append(job);
    }
    // Validate once and publish the whole submission before starting the serial worker.
    emit jobsChanged();
    fail({});
    QTimer::singleShot(0, this, &GenerationController::pump);
    return firstId;
}

void GenerationController::pump()
{
    if (!m_storage || busy() || !m_controlId.isEmpty() || !runtimeAvailable()) return;
    QString error;
    const auto queued = std::find_if(m_jobs.cbegin(), m_jobs.cend(), [](const auto &job) { return job.value("state") == "queued"; });
    if (queued == m_jobs.cend()) { prepareForeground(); return; }
    m_active = *queued;
    m_log.clear();
    m_pendingOutput.clear();
    clearWorkingFiles();
    m_cancelled = false;
    const auto model = m_storage->resolveModel(m_active.value("model").toObject(), &error);
    if (model.isEmpty()) { finish("failed", error); return; }
    if (!createWorkingFiles(&error)) { finish("failed", error); return; }
    m_active["state"] = "running";
    m_active["startedAt"] = now();
    m_active["output"] = "Generation History";
    updateJob(m_active);
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    if (m_cancelled) { finish("cancelled"); return; }
    QStringList arguments{"--model-path", model, "--prompt", m_active.value("prompt").toString(),
        "--width", QString::number(m_active.value("width").toInt()), "--height", QString::number(m_active.value("height").toInt()),
        "--steps", QString::number(m_active.value("steps").toInt()), "--device", m_active.value("device").toString(),
        "--output-dir", m_output};
    arguments.append({"--cache-dir", m_workDirectory->filePath("cache"),
        "--preview-dir", m_previewDirectory->path()});
    if (m_active.value("model").toObject().value("format") == "safetensors")
        arguments.append({"--backend", "local", "--work-dir", m_workDirectory->filePath("runtime")});
    m_workerRequest = QJsonDocument(QJsonObject{{"schema", "iild-worker-request-v1"},
        {"id", m_active.value("id")}, {"arguments", QJsonArray::fromStringList(arguments)}}).toJson(QJsonDocument::Compact) + '\n';
    if (!startWorker(&error)) finish("failed", error);
#endif
}

bool GenerationController::startWorker(QString *error)
{
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    if (m_process.state() == QProcess::NotRunning) {
        const QFileInfo base(m_runtime.temporaryDirectory);
        const auto path = base.canonicalFilePath();
        if (!base.isAbsolute() || !base.isDir() || path.isEmpty()
            || path == containerPath() || path.startsWith(containerPath() + '/')) {
            *error = tr("Use an available app temporary directory outside Society.");
            return false;
        }
        // Python imports/JIT may retain temporary paths. Keep their directory valid
        // for the SDK worker's lifetime, separately from disposable per-job files.
        m_workerDirectory = std::make_unique<QTemporaryDir>(
            QDir(path).filePath("dreamscapes-inference-XXXXXX"));
        if (!m_workerDirectory->isValid()) { *error = m_workerDirectory->errorString(); return false; }
        auto environment = m_process.processEnvironment();
        for (const auto &name : {"TMPDIR", "TEMP", "TMP"}) environment.insert(name, m_workerDirectory->path());
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
        const auto model = m_storage->resolveModel(selected->reference(m_storage->drive().identifier()), &error);
        if (model.isEmpty()) { setInferenceStatus({{"state", "error"}, {"ready", false}, {"error", error}}); return; }
        arguments = {"--model-path", model, "--device", m_runtime.device,
                     "--width", QString::number(m_runtime.imageExtent), "--height", QString::number(m_runtime.imageExtent),
                     "--steps", QString::number(m_runtime.steps)};
        if (selected->format == "safetensors") arguments.append({"--backend", "local"});
    }
    m_controlId = "foreground-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
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
        // The app temporary directory and Society may be on different volumes.
        // Only final image bytes are committed; engine working files stay in the app.
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
    m_active["state"] = state;
    m_active["finishedAt"] = now();
    m_active["error"] = error;
    updateJob(m_active);
    fail(error);
    m_active = {};
    if (m_foreground) m_residencyPending = true;
    m_workerRequest.clear();
    clearWorkingFiles();
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
    if (event.value("schema") != "iild-preview-v1" || step <= m_previewStep || total < step || total > 10000
        || (m_previewTotalSteps && total != m_previewTotalSteps)
        || name != QString("step-%1.png").arg(step, 6, 10, QLatin1Char('0'))) return;
    const auto path = m_previewDirectory->filePath(name);
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.canonicalFilePath() != path || info.size() > 4 * 1024 * 1024) return;
    QImageReader reader(path);
    const auto size = reader.size();
    if (size.isEmpty() || size.width() > 512 || size.height() > 512 || reader.read().isNull()) return;
    m_previewImage = QUrl::fromLocalFile(path);
    m_previewStep = step;
    m_previewTotalSteps = total;
    m_active["previewSteps"] = step;
    m_active["previewTotalSteps"] = total;
    emit previewChanged();
}

void GenerationController::clearPreview()
{
    m_previewImage.clear();
    m_previewStep = m_previewTotalSteps = 0;
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
        m_cancelled = true;
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
