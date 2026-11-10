#include "GenerationController.h"
#include "AdvancedImageInputs.h"
#include "SocietyGenerationStorage.h"
#include <ModelPackaging/ModelPackaging.hpp>
#include <File.h>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <chrono>
#include <limits>

using namespace iiSocietyContainer;

void GenerationController::startImageRouting(const QString &model)
{
    if (m_cancelled) { finish(m_interrupted ? "interrupted" : "cancelled"); return; }
    // Injected native operations are independent test/adaptor implementations.
    // An explicit inspector can still select another route for them.
    if (m_runtime.nativeInference && !m_runtime.inspectImageModel
        && (m_runtime.nativeGenerate || m_runtime.nativeGenerateAdvanced)) {
        startNative(model); return;
    }
    auto ticket = m_backgroundWork->acquire();
    if (!ticket) { finish("cancelled"); return; }
    m_imageRoutePending = true;
    m_imageRouteJob = m_active.value("id").toString();
    m_imageRoutePath = model;
    const auto inspect = m_runtime.inspectImageModel ? m_runtime.inspectImageModel : iiLocalDiffusion::inspectImageModel;
#ifdef Q_OS_WIN
    const std::filesystem::path path(model.toStdWString());
#else
    const std::filesystem::path path(iiFileProvider::FileDevice::encodeName(model).toStdString());
#endif
    const bool inspectComponents = !m_runtime.inspectImageModel;
    m_imageRouteWatcher.setFuture(QtConcurrent::run([path, inspect, inspectComponents, ticket = std::move(ticket)] {
        ImageRouteInspection result;
        try {
            result.model = inspect(path);
            if (inspectComponents && result.model.architecture == iiLocalDiffusion::ImageModelArchitecture::MossImage)
                result.components = iiLocalDiffusion::inspectEmbeddedImageComponents(path);
        }
        catch (const std::exception &error) { result.error = QString::fromUtf8(error.what()); }
        catch (...) { result.error = QStringLiteral("Unable to inspect the image model header."); }
        return result;
    }));
    setInferenceStatus({{"state", "checking-model"}, {"backend", "inspection"}, {"ready", false}});
}

void GenerationController::completeImageRouting()
{
    const auto result = m_imageRouteWatcher.result();
    const auto job = std::exchange(m_imageRouteJob, {});
    const auto path = std::exchange(m_imageRoutePath, {});
    m_imageRoutePending = false;
    if (m_closing || !busy() || m_active.value("id") != job) return;
    if (m_cancelled) { finish(m_interrupted ? "interrupted" : "cancelled"); return; }
    if (!result.error.isEmpty()) { finish("failed", result.error); return; }
    if (m_imageMaterializing) {
        m_imageMaterializing = false;
        m_mossTextEncoder = result.textEncoder; m_mossVae = result.vae;
        startWorkerGeneration(result.transformer); return;
    }
    if (result.model.architecture != iiLocalDiffusion::ImageModelArchitecture::MossImage) {
        if (m_runtime.nativeInference) startNative(path, result.model.architecture == iiLocalDiffusion::ImageModelArchitecture::Anima);
        else startWorkerGeneration(path);
        return;
    }
    m_active["generation"] = QJsonObject{{"backend", "iiLocalDiffusion-diffusers"},
        {"model_path", path}, {"seed", QString::number(m_active.value("seed").toInteger())},
        {"architecture", "moss-image"},
        {"weightEncoding", result.model.encoding == iiLocalDiffusion::ImageWeightEncoding::ComfyInt8
            ? "comfy-int8-unverified" : result.model.encoding == iiLocalDiffusion::ImageWeightEncoding::Floating
            ? "floating" : "other"},
        {"syntheticAdapterMappings", double(result.model.syntheticAdapterMappings)}};
    if (result.model.encoding != iiLocalDiffusion::ImageWeightEncoding::ComfyInt8
        && result.model.encoding != iiLocalDiffusion::ImageWeightEncoding::Floating) {
        finish("failed", tr("MOSS Image was recognized, but this checkpoint's weight encoding is not supported. Use floating-point or Comfy INT8 ConvRot weights.")); return;
    }
    if (m_active.value("width").toInt() % 16 || m_active.value("height").toInt() % 16) {
        finish("failed", tr("MOSS Image requires width and height to be multiples of 16. Choose compatible dimensions and try again.")); return;
    }
    QStringList unsupported;
    const auto advanced = m_active.value("advancedParameters").toObject();
    if (!advanced.isEmpty()) {
        for (const auto *key : {"seamlessTiling", "transparentBackground", "freeU", "refiner", "hiresFix", "faceRestore", "detailer"})
            if (advanced.value(key).toBool()) unsupported.append(QString::fromLatin1(key));
        for (const auto *key : {"sampler", "scheduler"})
            if (advanced.value(key).toString("auto") != "auto") unsupported.append(QString::fromLatin1(key));
        if (advanced.value("clipSkip").toInt() != 0) unsupported.append("clipSkip");
        if (advanced.value("eta").toDouble() != 0) unsupported.append("eta");
        if (!advanced.value("textualEmbeddings").toString().isEmpty()) unsupported.append("textualEmbeddings");
        if (!advanced.value("referenceImages").toArray().isEmpty()) unsupported.append("referenceImages");
        if (!advanced.value("loras").toArray().isEmpty()) unsupported.append("loras");
        for (const auto &item : advanced.value("controlNets").toArray())
            if (item.toObject().value("applied").toBool()) { unsupported.append("controlNets"); break; }
        if (advanced.value("safetyFilter").toString("off") != "off") unsupported.append("safetyFilter");
        if (advanced.value("promptWeighting").toBool()) {
            const QRegularExpression weighting(QStringLiteral("\\([^()]+\\)|\\[[^\\[\\]]+\\]"));
            if (weighting.match(m_active.value("prompt").toString()).hasMatch()
                || weighting.match(m_active.value("negativePrompt").toString()).hasMatch())
                unsupported.append("promptWeighting");
        }
    }
    if (!m_active.value("vae").toObject().isEmpty()) unsupported.append("vae");
    if (!unsupported.isEmpty()) {
        finish("failed", tr("The MOSS Image engine cannot apply these submitted settings: %1. Disable them or select a compatible model.").arg(unsupported.join(", "))); return;
    }
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    if (!QFileInfo(m_runtime.executable).isFile()) {
        finish("failed", tr("MOSS Image requires the local iiLocalDiffusion Diffusers worker, which is unavailable in this installation.")); return;
    }
#endif
    m_imageWorker = true;
    if (result.components.packaged && !result.components.transformer.empty()) {
        prepareEmbeddedImageModel(path, result.components); return;
    }
    startWorkerGeneration(path);
}

void GenerationController::prepareEmbeddedImageModel(const QString &model, const iiLocalDiffusion::EmbeddedImageComponents &components)
{
    auto ticket = m_backgroundWork->acquire();
    if (!ticket) { finish("cancelled"); return; }
    m_imageRoutePending = true; m_imageMaterializing = true;
    m_imageRouteJob = m_active.value("id").toString(); m_imageRoutePath = model;
    const auto job = m_imageRouteJob;
    const auto state = m_nativeState;
    const auto work = m_workDirectory;
    const QPointer<GenerationController> owner(this);
    if (!m_nativeCallbacks)
        m_nativeCallbacks = std::shared_ptr<QObject>(new QObject, [](QObject *target) { target->deleteLater(); });
    const auto target = m_nativeCallbacks;
    const std::filesystem::path source(model.toStdU16String());
    const std::filesystem::path cache(work->filePath("embedded-models").toStdU16String());
    m_imageRouteWatcher.setFuture(QtConcurrent::run([source, cache, components, state, work, owner, target, job, ticket = std::move(ticket)] {
        ImageRouteInspection result;
        try {
            std::stop_source stop;
            auto last = std::chrono::steady_clock::time_point{};
            const auto observer = [&](const iild::ModelPackagingProgress &progress) {
                if (state->cancelled.load()) stop.request_stop();
                const auto now = std::chrono::steady_clock::now();
                if (last != std::chrono::steady_clock::time_point{} && now - last < std::chrono::milliseconds(200)) return;
                last = now;
                QMetaObject::invokeMethod(target.get(), [owner, job, progress] {
                    if (owner && owner->m_imageRoutePending && owner->m_imageRouteJob == job && !owner->m_cancelled)
                        owner->setInferenceStatus({{"state", "preparing-model"}, {"backend", "diffusers"}, {"ready", false},
                            {"detail", QString::fromStdString(progress.detail)}, {"step", double(progress.completedBytes)}, {"total", double(progress.totalBytes)}});
                }, Qt::QueuedConnection);
            };
            const auto extract = [&](const std::string &relative) {
                if (relative.empty()) return QString{};
                const auto path = iild::materializeModelPackageAssetByPath(source, cache, std::filesystem::path(QString::fromStdString(relative).toStdU16String()), stop.get_token(), observer);
                return QString::fromStdU16String(path.u16string());
            };
            result.transformer = extract(components.transformer);
            result.textEncoder = extract(components.textEncoder);
            result.vae = extract(components.vae);
        } catch (const std::exception &error) { result.error = QString::fromUtf8(error.what()); }
        catch (...) { result.error = QStringLiteral("Unable to prepare the embedded image model components."); }
        return result;
    }));
    setInferenceStatus({{"state", "preparing-model"}, {"backend", "diffusers"}, {"ready", false}});
}

void GenerationController::startNative(const QString &model, bool preparePackage)
{
    // jobsChanged can synchronously cancel the transition to running.
    if (m_cancelled) { finish(m_interrupted ? "interrupted" : "cancelled"); return; }
    m_nativeState = std::make_shared<dreamscapes::NativeGenerationState>();
    iiLocalDiffusion::NativeGenerationRequest request;
    request.modelPath = iiFileProvider::FileDevice::encodeName(model).toStdString();
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
    const auto resources = resourceArguments(&storageError);
    if (resources.isEmpty()) { finish("failed", storageError); return; }
    iiLocalDiffusion::NativeGenerationOptions options;
    options.negativePrompt = m_active.value("negativePrompt").toString(
        m_active.value("advancedParameters").toObject().value("negativePrompt").toString()).toStdString();
    options.resourceDirectory = iiFileProvider::FileDevice::encodeName(resources[1]).toStdString();
    options.defaultModifiers = resources[2] == "--default-modifiers";
    const auto advanced = m_active.value("advancedParameters").toObject();
    iiLocalDiffusion::NativeModelComponents components;
    iiLocalDiffusion::NativeAdvancedControls sampling;
    if (!advanced.isEmpty()) {
        components.guidanceScale = float(advanced.value("cfgScale").toDouble());
        const auto vaeReference = m_active.value("vae").toObject();
        if (!vaeReference.isEmpty()) {
            const auto vae = m_fileSystem.resolveModel(vaeReference, &storageError);
            if (vae.isEmpty()) { finish("failed", storageError); return; }
            components.vae = iiFileProvider::FileDevice::encodeName(vae).toStdString();
        }
        sampling.sampler = advanced.value("sampler").toString().toStdString();
        sampling.scheduler = advanced.value("scheduler").toString().toStdString();
        sampling.clipSkip = advanced.value("clipSkip").toInt();
        sampling.eta = float(advanced.value("eta").toDouble());
        sampling.seamlessTiling = advanced.value("seamlessTiling").toBool();
        sampling.hires = advanced.value("hiresFix").toBool();
        sampling.denoiseStrength = float(advanced.value("denoiseStrength").toDouble());
        sampling.upscaler = advanced.value("upscaler").toString().toStdString();
        sampling.upscalerModel = iiFileProvider::FileDevice::encodeName(advanced.value("upscalerModel").toString()).toStdString();
        sampling.detailer = advanced.value("detailer").toBool();
        sampling.detailerModel = iiFileProvider::FileDevice::encodeName(advanced.value("detailerModel").toString()).toStdString();
        sampling.refiner = advanced.value("refiner").toBool();
        sampling.refinerSwitch = float(advanced.value("refinerSwitch").toDouble());
        sampling.refinerModel = iiFileProvider::FileDevice::encodeName(advanced.value("refinerModel").toString()).toStdString();
        sampling.imageStrength = float(advanced.value("imageStrength").toDouble());
        sampling.promptWeighting = advanced.value("promptWeighting").toBool();
        sampling.freeU = advanced.value("freeU").toBool();
        const auto embeddingPath = advanced.value("textualEmbeddings").toString();
        if (!embeddingPath.isEmpty()) {
            const auto token = "user_" + QFileInfo(embeddingPath).completeBaseName().toLower()
                .replace(QRegularExpression("[^a-z0-9_]"), "_").left(100);
            sampling.embeddings.push_back({token.toStdString(), iiFileProvider::FileDevice::encodeName(embeddingPath).toStdString()});
        }
        for (const auto &value : advanced.value("loras").toArray()) {
            const auto lora = value.toObject();
            options.loras.push_back({iiFileProvider::FileDevice::encodeName(lora.value("source").toString()).toStdString(),
                float(lora.value("weight").toDouble())});
        }
    }
    m_active["generation"] = QJsonObject{{"backend", "iiLocalDiffusion-native"},
        {"model_path", model}, {"seed", QString::number(request.seed)}};
    const auto id = m_active.value("id").toString();
    const auto control = m_runtime.nativeExecutionControl;
    const auto backend = m_runtime.backgroundActivity && m_runtime.backgroundActivity->requiresCpuExecution()
        ? iiLocalDiffusion::NativeComputeBackend::Cpu : iiLocalDiffusion::NativeComputeBackend::Automatic;
    auto generation = m_active.value("generation").toObject();
    generation["computeBackend"] = backend == iiLocalDiffusion::NativeComputeBackend::Cpu ? "cpu" : "automatic";
    m_active["generation"] = generation;
    const std::filesystem::path assetCache(m_workDirectory->filePath("embedded-models").toStdU16String());
    const bool inspectPackages = preparePackage;
    auto generate = m_runtime.nativeGenerate ? m_runtime.nativeGenerate
        : [control, backend, assetCache, inspectPackages](const auto &request, const auto &options, const auto &cancelled, const auto &progress, const auto &preview) {
            if (inspectPackages) {
                const auto package = iiLocalDiffusion::prepareNativeImagePackage(request, assetCache, {}, cancelled, progress, control);
                return iiLocalDiffusion::generateNativeImageWithResidentPackage(package, options, backend,
                    cancelled, progress, preview, control);
            }
            return iiLocalDiffusion::generateNativeImageWithResidentWeights(request, options, backend, cancelled, progress, preview, control);
        };
    if (!advanced.isEmpty()) {
        const auto advancedGenerate = m_runtime.nativeGenerateAdvanced;
        const auto referenceSources = advanced.value("referenceImages").toArray();
        const auto controlSources = advanced.value("controlNets").toArray();
        generate = [control, backend, components, sampling, referenceSources, controlSources, advancedGenerate, assetCache, inspectPackages](const auto &request, const auto &options,
            const auto &cancelled, const auto &progress, const auto &preview) {
            auto effectiveSampling = sampling;
            QString error;
            if (!dreamscapes::decodeAdvancedReferences(referenceSources, &effectiveSampling, &error, cancelled, control)
                || !dreamscapes::decodeAdvancedControlImages(controlSources, &effectiveSampling, &error, cancelled, control)) {
                iiLocalDiffusion::NativeGenerationResult result;
                result.cancelled = cancelled.load();
                result.error = error.toStdString();
                return result;
            }
            const auto package = inspectPackages
                ? iiLocalDiffusion::prepareNativeImagePackage(request, assetCache, components, cancelled, progress, control)
                : iiLocalDiffusion::NativeImagePackage{request, components, false};
            if (advancedGenerate) return advancedGenerate(package.request, options, package.components, effectiveSampling, cancelled, progress, preview);
            return iiLocalDiffusion::generateNativeAdvancedImage(package.request, options, package.components,
                effectiveSampling, backend, cancelled, progress, preview, control);
        };
    }
    const QPointer<GenerationController> owner(this);
    if (!m_nativeCallbacks)
        m_nativeCallbacks = std::shared_ptr<QObject>(new QObject, [](QObject *target) { target->deleteLater(); });
    const auto target = m_nativeCallbacks;
    dreamscapes::NativeGenerationInput input;
    input.request = std::move(request); input.options = std::move(options); input.generate = std::move(generate);
    input.state = m_nativeState;
    input.previewDirectory = std::filesystem::path(m_previewDirectory->path().toStdU16String());
    input.lifetime = std::make_shared<std::pair<decltype(m_workDirectory), decltype(m_previewDirectory)>>(
        m_workDirectory, m_previewDirectory);
    input.progress = [owner, target, id](const iiLocalDiffusion::NativeGenerationProgress &event) {
        QMetaObject::invokeMethod(target.get(), [owner, id, event] {
            if (owner) owner->acceptNativeProgress(id, event);
        }, Qt::QueuedConnection);
    };
    input.preview = [owner, target, id](dreamscapes::NativePreviewArtifact preview) {
        QMetaObject::invokeMethod(target.get(), [owner, id, preview = std::move(preview)] {
            if (owner) owner->acceptNativePreview(id, preview);
        }, Qt::QueuedConnection);
    };
    // Register background execution before starting work. Every notification can
    // retire the presenter; only the local input lease survives that boundary.
    if (m_runtime.backgroundActivity) {
        if (m_societyBackgroundActivity) {
            m_societyBackgroundActivity = m_backgroundActivityActive = false;
            m_runtime.backgroundActivity->end(true);
            if (!owner) return;
        }
        const QPointer<GenerationController> self(this);
        m_backgroundActivityActive = true;
        m_runtime.backgroundActivity->begin(id, [self, id] {
            if (!self || self->m_active.value("id") != id || !self->m_backgroundActivityActive) return;
            self->m_backgroundActivityActive = false;
            self->setNativePaused(!self->m_foreground);
            if (self) self->m_runtime.backgroundActivity->end(false);
        });
        if (!owner) return;
    }
    const QJsonObject loading{{"state", "loading"}, {"ready", false}, {"backend", "native"}};
    if (m_nativePaused) m_resumeInferenceStatus = loading;
    else setInferenceStatus(loading);
    if (!owner) return;
    if (m_cancelled || m_finishing || m_closing || m_active.value("id") != id) {
        input.lifetime.reset();
        if (m_cancelled && !m_finishing) finish(m_interrupted ? "interrupted" : "cancelled");
        return;
    }
    m_nativeDispatched = true;
    m_nativeWatcher.setFuture(QtConcurrent::run([input = std::move(input)]() mutable {
        return dreamscapes::runNativeGeneration(std::move(input));
    }));
    m_nativeTimeRemaining = std::max(1, m_runtime.nativeTimeoutMilliseconds);
    if (!m_nativePaused) m_nativeDeadline.start(m_nativeTimeRemaining);
}

void GenerationController::finishNative()
{
    m_nativeDispatched = false;
    m_nativeDeadline.stop();
    auto result = m_nativeWatcher.future().takeResult();
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
    QString terminal, error;
    if (m_nativeTimedOut) { terminal = "failed"; error = tr("Image generation stopped making progress. Try again with a smaller model."); }
    else if (m_interrupted) { terminal = "interrupted"; error = tr("The system ended background generation. Return to Dreamscapes and try again."); }
    else if (m_cancelled || result.cancelled) terminal = "cancelled";
    else if (!result.error.empty()) { terminal = "failed"; error = QString::fromStdString(result.error); }
    if (result.width != m_active.value("width").toInt() || result.height != m_active.value("height").toInt()
        || result.rgb.size() != static_cast<size_t>(result.width) * static_cast<size_t>(result.height) * 3) {
        if (terminal.isEmpty()) { terminal = "failed"; error = tr("The local engine returned an incomplete image."); }
    }
    if (!terminal.isEmpty()) {
        const QPointer<GenerationController> owner(this);
        setInferenceStatus({{"state", "idle"}, {"ready", false}, {"backend", "native"}});
        if (owner) finish(terminal, error);
        return;
    }
    beginPublication(dreamscapes::PublicationOrigin::Native, std::move(result));
}

bool GenerationController::startWorker(QString *error)
{
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    if (m_process.state() == GenerationProcess::NotRunning) {
        const auto path = connected() ? dreamscapes::generationRuntimeDirectory(m_fileSystem, error) : QString();
        if (path.isEmpty()) return false;
        // Python imports/JIT may retain temporary paths. Keep their directory valid
        // for the SDK worker's lifetime, separately from disposable per-job files.
        m_workerDirectory = std::make_shared<iiFileProvider::TemporaryDirectory>(
            iiFileProvider::Directory(path).filePath("dreamscapes-inference-XXXXXX"));
        if (!m_workerDirectory->isValid()) { *error = m_workerDirectory->errorString(); return false; }
        auto environment = m_process.processEnvironment();
        environment["IILD_NATIVE_TELEMETRY_DIR"] = iiFileProvider::Directory(path).filePath("diagnostics").toStdString();
        for (const auto &name : {"TMPDIR", "TEMP", "TMP"}) environment[name] = m_workerDirectory->path().toStdString();
        // Backend/model caches also belong to Society. The client consumes
        // already available local sources; missing models are managed in Society.
        for (const auto &name : {"HF_HOME", "HUGGINGFACE_HUB_CACHE", "TRANSFORMERS_CACHE", "TORCH_HOME",
                 "TORCH_EXTENSIONS_DIR", "TRITON_CACHE_DIR", "XDG_CACHE_HOME"})
            environment[name] = m_workerDirectory->filePath("cache/" + QString::fromLatin1(name)).toStdString();
        environment["HF_HUB_OFFLINE"] = "1";
        environment["TRANSFORMERS_OFFLINE"] = "1";
        const auto resources = resourceArguments(error);
        if (resources.isEmpty()) return false;
        environment["IILD_GENERATION_RESOURCES"] = resources[1].toStdString();
        environment["PYTHONDONTWRITEBYTECODE"] = "1";
        // Read installed bytecode caches; a fresh prefix would force source recompilation.
        environment["PYTHONPYCACHEPREFIX"] = std::nullopt;
        m_process.setProcessEnvironment(std::move(environment));
        m_process.setWorkingDirectory(m_workerDirectory->path());
        m_workerReady = false;
        m_workerForegroundSupported = false;
        m_pendingOutput.clear();
        if (busy()) m_workerDeadline.start(std::max(1, m_runtime.nativeTimeoutMilliseconds));
#ifdef Q_OS_WIN
        if (!m_runtime.executable.endsWith(".exe", Qt::CaseInsensitive))
            m_process.start(m_runtime.pythonExecutable, {m_runtime.executable, "--worker"}, m_workerDirectory);
        else
#endif
            m_process.start(m_runtime.executable, {"--worker"}, m_workerDirectory);
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
    if (!m_foreground && m_process.state() == GenerationProcess::NotRunning) return;
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
        const auto model = m_fileSystem.resolveModel(selected->reference(container()->containerId()), &error);
        if (model.isEmpty()) { setInferenceStatus({{"state", "error"}, {"ready", false}, {"error", error}}); return; }
        arguments = {"--model-path", model, "--device", m_runtime.device,
                     "--width", QString::number(m_runtime.imageExtent), "--height", QString::number(m_runtime.imageExtent),
                     "--steps", QString::number(m_runtime.steps)};
        if (!m_selectedVae.isEmpty()) {
            const auto vae = std::find_if(m_models.cbegin(), m_models.cend(), [&](const auto &value) { return value.id == m_selectedVae; });
            if (vae == m_models.cend()) { fail(tr("The selected VAE is no longer available.")); return; }
            const auto path = m_fileSystem.resolveModel(vae->reference(container()->containerId()), &error);
            if (path.isEmpty()) { fail(error); return; }
            arguments.append({"--vae", path});
        }
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
