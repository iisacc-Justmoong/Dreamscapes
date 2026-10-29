#include "GenerationController.h"
#include "ImageParameterCodec.h"
#include "VideoGeneration.h"
#include "AdvancedImageOutput.h"
#include "AdvancedImageInputs.h"
#include "SocietyGenerationStorage.h"
#include "ImageCanvas.h"
#include <File.h>
#include <Generation/NativePose.hpp>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <limits>

using namespace iiSocietyContainer;
namespace {
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
}

QString GenerationController::enqueue(const QString &prompt, const QString &aspectRatio, int count, qint64 seed)
{
    return enqueueRequest(prompt, imageSize(aspectRatio, m_runtime.imageExtent), aspectRatio, count, seed, m_runtime.steps);
}

QString GenerationController::preserveImageInput(const QString &source, const QString &destination, QString *error)
{
    if (!dreamscapes::storeImageCanvas(source, destination, error)) return {};
    const auto runtime = dreamscapes::generationRuntimeDirectory(m_fileSystem, error);
    return runtime.isEmpty() ? QString{} : dreamscapes::imageCanvasPreview(destination, iiFileProvider::Directory(runtime).filePath("image-previews"), error);
}

QString GenerationController::imageInputPath(const QString &source, QString *error)
{
    const auto native = dreamscapes::persistImageCanvas(source, error, containerPath());
    if (native.isEmpty()) return {};
    if (QFileInfo(source).suffix().compare("iisc", Qt::CaseInsensitive) != 0) return QFileInfo(source).canonicalFilePath();
    const auto runtime = dreamscapes::generationRuntimeDirectory(m_fileSystem, error);
    return runtime.isEmpty() ? QString{} : dreamscapes::imageCanvasPreview(native, iiFileProvider::Directory(runtime).filePath("image-previews"), error);
}

QString GenerationController::enqueueVideo(const QString &prompt, const QString &aspectRatio,
    int count, const QUrl &firstFrame, int duration, int fps, qint64 seed)
{
    if (!videoRuntimeAvailable()) { fail(tr("Video generation requires the desktop iiLocalDiffusion runtime.")); return {}; }
    if (duration < 1 || duration > 30 || !QList<int>{12,24,30,60,120}.contains(fps)) {
        fail(tr("Choose a video duration from 1 to 30 seconds and 12, 24, 30, 60 or 120 FPS.")); return {};
    }
    if (!connected()) { fail(tr("Open Society on this device to load its storage map.")); return {}; }
    QJsonObject video{{"duration",duration},{"fps",fps},{"frames",duration*fps}};
    QString directory;
    if (!firstFrame.isEmpty()) {
        QString error;
        const auto input = firstFrame.isLocalFile() ? imageInputPath(firstFrame.toLocalFile(), &error) : QString{};
        QImageReader reader(input); reader.setAutoTransform(true);
        const auto size = reader.size();
        if (!firstFrame.isLocalFile() || !reader.canRead() || size.isEmpty()
            || qint64(size.width())*size.height() > 64LL*1024*1024
            || QFileInfo(firstFrame.toLocalFile()).size() > 256LL*1024*1024 || reader.read().isNull()) {
            fail(tr("Choose a readable local image for the video's first frame.")); return {};
        }
        directory = m_fileSystem.ensureDirectory("files",
            "Dreamscapes/GenerationInputs/" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        error = m_fileSystem.errorString();
        const auto target = directory.isEmpty() ? QString{} : preserveImageInput(firstFrame.toLocalFile(), iiFileProvider::Directory(directory).filePath("first-frame.iisc"), &error);
        if (target.isEmpty()) {
            if (!directory.isEmpty()) m_fileSystem.removeGenerationInputs(directory);
            fail(error.isEmpty() ? tr("Cannot preserve the video's first frame.") : error); return {};
        }
        video["firstFrame"] = target;
    }
    const auto id = enqueueRequest(prompt,dreamscapes::videoSize(aspectRatio),aspectRatio,count,seed,30,{},video);
    if (id.isEmpty() && !directory.isEmpty()) m_fileSystem.removeGenerationInputs(directory);
    return id;
}

QString GenerationController::enqueueVideoRecipe(const QVariantMap &parameters)
{
    if (!videoRuntimeAvailable()) { fail(tr("Video generation requires the desktop iiLocalDiffusion runtime.")); return {}; }
    if (!connected()) { fail(tr("Open Society on this device to load its storage map.")); return {}; }
    auto video = QJsonObject::fromVariantMap(parameters);
    QString error;
    if (!dreamscapes::normalizeVideoRecipe(&video, &error)) { fail(error); return {}; }
    // Own every submitted image before publishing any job. Cancelling the file
    // picker or changing the draft cannot mutate accepted image conditions.
    QString directory;
    int imageIndex = 0;
    auto preserve = [&](QJsonArray *conditions) {
        QJsonArray owned;
        for (const auto &entry : *conditions) {
            auto condition = entry.toObject();
            const QUrl source(condition.value("image").toString());
            const auto input = source.isLocalFile() ? imageInputPath(source.toLocalFile(), &error) : QString{};
            QImageReader reader(input); reader.setAutoTransform(true);
            const auto size = reader.size();
            if (!source.isLocalFile() || !reader.canRead() || size.isEmpty()
                || qint64(size.width()) * size.height() > 64LL * 1024 * 1024
                || QFileInfo(source.toLocalFile()).size() > 256LL * 1024 * 1024 || reader.read().isNull()) {
                error = tr("Choose readable local images for video keyframes."); return false;
            }
            if (directory.isEmpty()) directory = m_fileSystem.ensureDirectory("files",
                "Dreamscapes/GenerationInputs/" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        error = m_fileSystem.errorString();
            const auto target = directory.isEmpty() ? QString{} : preserveImageInput(source.toLocalFile(), iiFileProvider::Directory(directory).filePath(QString::number(imageIndex++) + ".iisc"), &error);
            if (target.isEmpty()) {
                if (error.isEmpty()) error = tr("Cannot preserve video image conditions."); return false;
            }
            condition["image"] = target; owned.append(condition);
        }
        *conditions = owned; return true;
    };
    auto shots = video.value("shots").toArray();
    for (int index = 0; index < shots.size(); ++index) {
        auto shot = shots[index].toObject(); auto conditions = shot.value("conditions").toArray();
        if (!preserve(&conditions)) { if (!directory.isEmpty()) m_fileSystem.removeGenerationInputs(directory); fail(error); return {}; }
        shot["conditions"] = conditions; shots[index] = shot;
    }
    video["shots"] = shots;
    video["recipe"] = true;
    const QSize size(video.value("width").toInt(), video.value("height").toInt());
    const auto divisor = std::gcd(size.width(), size.height());
    const auto id = enqueueRequest(video.value("prompt").toString(), size,
        QString::number(size.width() / divisor) + ":" + QString::number(size.height() / divisor),
        video.value("outputCount").toInt(), video.value("seed").toInteger(), video.value("steps").toInt(), {}, video);
    if (id.isEmpty() && !directory.isEmpty()) m_fileSystem.removeGenerationInputs(directory);
    return id;
}

QString GenerationController::enqueueAdvanced(const QVariantMap &parameters)
{
    return enqueueAdvancedRequest(parameters);
}

QString GenerationController::enqueueHomeCanvas(const QVariantMap &parameters, const QString &aspectRatio, bool explicitControls)
{
    const auto size = imageSize(aspectRatio, 1024);
    if (!explicitControls && size != QSize(parameters.value("width").toInt(), parameters.value("height").toInt())) {
        fail(tr("The canvas dimensions do not match the selected ratio.")); return {};
    }
    if (!connected()) { fail(tr("Open Society on this device to load its storage map.")); return {}; }
    // Own immutable inputs before enqueueing: queued jobs survive draft edits and removal.
    auto snapshot = parameters;
    if (!explicitControls) snapshot["negativePrompt"] = QString::fromStdString(m_modelPreferences.prompts[0].negativePrompt);
    // QuickGenerate owns these defaults; the advanced form owns explicit values.
    // Resolve from model tensor metadata rather than its user-editable filename.
    const auto requestedModel = snapshot.value("model").toString();
    const auto modelId = requestedModel.isEmpty() ? m_selected : requestedModel;
    const auto selected = std::find_if(m_models.cbegin(), m_models.cend(), [&](const auto &model) {
        return model.id == modelId;
    });
    if (!explicitControls && m_runtime.nativeInference && selected != m_models.cend() && selected->available) {
        QString modelError;
        const auto path = m_fileSystem.resolveModel(selected->reference(container()->containerId()), &modelError);
        if (!path.isEmpty()) {
            const auto defaults = dreamscapes::imageParametersToMap(
                iiLocalDiffusion::nativeImageParameterDefaults(iiFileProvider::FileDevice::encodeName(path).toStdString()));
            snapshot["steps"] = defaults.value("steps");
            snapshot["cfgScale"] = defaults.value("cfgScale");
        }
    }
    const auto references = parameters.value("referenceImages").toList();
    if (references.isEmpty()) return enqueueAdvancedRequest(snapshot, aspectRatio);
    QString error;
    const auto directory = m_fileSystem.ensureDirectory("files",
        "Dreamscapes/GenerationInputs/" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        error = m_fileSystem.errorString();
    if (directory.isEmpty()) { fail(error); return {}; }
    QVariantList owned;
    int index = 0;
    for (const auto &reference : references) {
        const auto source = QUrl(reference.toString());
        const auto input = source.isLocalFile() ? imageInputPath(source.toLocalFile(), &error) : QString{};
        QImageReader reader(input); reader.setAutoTransform(true);
        const auto dimensions = reader.size();
        if (!source.isLocalFile() || !reader.canRead() || dimensions.isEmpty()
            || qint64(dimensions.width()) * dimensions.height() > 64LL * 1024 * 1024
            || QFileInfo(source.toLocalFile()).size() > 256LL * 1024 * 1024) {
            m_fileSystem.removeGenerationInputs(directory); fail(tr("A reference image is no longer readable.")); return {};
        }
        const auto target = preserveImageInput(source.toLocalFile(), iiFileProvider::Directory(directory).filePath(QString::number(index++) + ".iisc"), &error);
        if (target.isEmpty()) {
            m_fileSystem.removeGenerationInputs(directory); fail(error); return {};
        }
        owned.append(QUrl::fromLocalFile(target).toString());
    }
    snapshot["referenceImages"] = owned;
    const auto id = enqueueAdvancedRequest(snapshot, aspectRatio);
    if (id.isEmpty()) m_fileSystem.removeGenerationInputs(directory);
    return id;
}

QString GenerationController::enqueueAdvancedRequest(const QVariantMap &parameters, const QString &aspectRatio)
{
    iiLocalDiffusion::ImageParameters values;
    QString error;
    if (!dreamscapes::imageParametersFromMap(parameters, &values, &error)) { fail(error); return {}; }
    auto issues = iiLocalDiffusion::validateImageParameters(values, true);
    const auto unsupported = iiLocalDiffusion::nativeParameterIssues(values, !m_runtime.nativeInference, iiLocalDiffusion::nativePoseAvailable());
    issues.insert(issues.end(), unsupported.begin(), unsupported.end());
    if (!issues.empty()) {
        QStringList messages;
        for (const auto &issue : issues) messages.append(QString::fromStdString(issue.field + ": " + issue.message));
        fail(messages.join('\n')); return {};
    }
    const auto snapshot = dreamscapes::imageParametersToMap(values);
    const auto width = snapshot.value("width").toInt(), height = snapshot.value("height").toInt();
    const auto divisor = std::gcd(width, height);
    return enqueueRequest(snapshot.value("prompt").toString(), QSize(width, height),
        aspectRatio.isEmpty() ? QString::number(width / divisor) + ":" + QString::number(height / divisor) : aspectRatio,
        snapshot.value("outputCount").toInt(), snapshot.value("seed").toLongLong(), snapshot.value("steps").toInt(),
        QJsonObject::fromVariantMap(snapshot));
}

QString GenerationController::enqueueRequest(const QString &prompt, const QSize &size, const QString &aspectRatio,
    int count, qint64 seed, int steps, QJsonObject advanced, QJsonObject video)
{
    const bool isVideo = !video.isEmpty();
    const auto &explicitParameters = isVideo ? video : advanced;
    const auto negativePrompt = explicitParameters.contains("negativePrompt")
        ? explicitParameters.value("negativePrompt").toString()
        : QString::fromStdString(m_modelPreferences.prompts[isVideo ? 1 : 0].negativePrompt);
    if (isVideo) video["negativePrompt"] = negativePrompt;
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID)
    if (!runtimeAvailable()) { fail(tr("Local image generation is unavailable in this build.")); return {}; }
#endif
    if (count < 1 || count > 1000) { fail(tr("Choose a generation count from 1 to 1000.")); return {}; }
    if (seed < -1 || seed > std::numeric_limits<quint32>::max() - qint64(count - 1)) {
        fail(tr("Choose a seed from 0 to 4294967295, with room for the generation count.")); return {};
    }
    if (!connected()) { fail(tr("Open Society on this device to load its storage map.")); return {}; }
    const auto trimmed = prompt.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 32000 || size.isEmpty() || size.width() > 4096 || size.height() > 4096
        || steps < 1 || steps > 1000) { fail(tr("Enter a prompt and a supported image size.")); return {}; }
    const auto requestedModel = explicitParameters.value("model").toString();
    const auto modelId = requestedModel.isEmpty() ? (isVideo ? m_selectedVideo : m_selected) : requestedModel;
    auto selected = std::find_if(m_models.cbegin(), m_models.cend(), [&](const auto &model) { return model.id == modelId; });
    if (selected == m_models.cend() || !m_modelModalities.value(modelId).contains(isVideo ? "video" : "image")) {
        fail(isVideo ? tr("Select a video model from Society's Models folder, then refresh models.")
            : tr("Add a Diffusion model to Society, then refresh the model list.")); return {};
    }
    auto &map = m_fileSystem;
    auto required = map.files("models/" + selected->id);
    if (!isVideo) required.append(map.files("models/.generation-resources/iiLocalDiffusion")); required.removeDuplicates();
    const bool localReady = required.isEmpty() ? selected->available : map.available(required);
    if (isVideo && !localReady) { fail(tr("Download the selected LTX model in Society before generating video.")); return {}; }
    if (!advanced.isEmpty() && (!localReady || selected->format != "safetensors")) {
        fail(tr("Advanced parameters require a locally available native model. Remote and packaged worker routes do not advertise this contract yet.")); return {};
    }
    if (!isVideo && localReady && m_runtime.nativeInference && selected->format != "safetensors" && selected->format != "unified") {
        fail(tr("Choose a checkpoint or unified model for on-device generation.")); return {};
    }
    QJsonObject vaeReference;
    const auto vaeId = isVideo ? QString() : advanced.isEmpty() ? m_selectedVae : advanced.value("vae").toString();
    if (!vaeId.isEmpty()) {
        const auto vae = std::find_if(m_models.cbegin(), m_models.cend(), [&](const auto &value) { return value.id == vaeId; });
        if (vae == m_models.cend() || !vae->available || !localReady || (m_runtime.nativeInference && advanced.isEmpty())) {
            fail(advanced.isEmpty()
                ? tr("Explicit VAE selection currently requires locally available models and the desktop runtime.")
                : tr("Select a locally available VAE from the Society inventory.")); return {};
        }
        vaeReference = vae->reference(container()->containerId());
    }
    const auto reference = selected->reference(container()->containerId());
    QString error;
    if (localReady && m_fileSystem.resolveModel(reference, &error).isEmpty()) { fail(error); return {}; }
    if (!advanced.isEmpty()) {
        advanced["model"] = selected->id;
        advanced["prompt"] = trimmed;
        QJsonArray loras;
        for (const auto &value : advanced.value("loras").toArray()) {
            auto lora = value.toObject();
            const auto source = lora.value("source").toString();
            const QUrl url(source);
            QString path;
            if (url.isLocalFile()) path = url.toLocalFile();
            else if (QFileInfo(source).isAbsolute()) path = source;
            else if (!url.scheme().isEmpty()) { fail(tr("LoRA sources must be local files.")); return {}; }
            else path = m_fileSystem.path(StoreSection::Models, source, &error);
            const QFileInfo info(path);
            if (!info.isFile() || info.size() == 0
                || !QStringList{"safetensors", "safetensor", "gguf"}.contains(info.suffix().toLower())) {
                fail(tr("Choose an existing safetensors or GGUF LoRA file: %1").arg(source)); return {};
            }
            lora["source"] = info.canonicalFilePath(); loras.append(lora);
        }
        advanced["loras"] = loras;
        QJsonArray references;
        for (const auto &value : advanced.value("referenceImages").toArray()) {
            const auto source = value.toString();
            const QUrl url(source);
            QString path;
            if (url.isLocalFile()) path = url.toLocalFile();
            else if (QFileInfo(source).isAbsolute()) path = source;
            else if (!url.scheme().isEmpty()) { fail(tr("Reference images must be local files.")); return {}; }
            else path = m_fileSystem.path(StoreSection::Files, source, &error);
            const QFileInfo info(path);
            const auto input = imageInputPath(info.canonicalFilePath(), &error);
            if (input.isEmpty()) { fail(error); return {}; }
            QImageReader reader(input);
            const auto dimensions = reader.size();
            if (!info.isFile() || info.size() == 0 || !reader.canRead() || dimensions.isEmpty()
                || qint64(dimensions.width()) * dimensions.height() > 64 * 1024 * 1024) {
                fail(tr("Choose a readable local reference image of at most 64 megapixels: %1").arg(source)); return {};
            }
            references.append(input);
        }
        advanced["referenceImages"] = references;
        QJsonArray controls;
        for (const auto &value : advanced.value("controlNets").toArray()) {
            auto item = value.toObject();
            if (item.value("applied").toBool()) {
                auto fields = QStringList{"imageSource"};
                const auto process = item.value("process").toString();
                if (process != "None" && process != "IP-Adapter") fields.append("model");
                if (item.value("ipAdapter").toBool()) fields.append({"ipAdapterModel", "ipAdapterVision"});
                if (process == "Pose") { fail(tr("Automatic pose preprocessing has been retired.")); return {}; }
                if (item.value("regionalMask").toBool()) fields.append("maskSource");
                for (const auto &field : fields) {
                    const bool weightFile = field == "model" || field == "ipAdapterModel" || field == "ipAdapterVision";
                    const auto source = item.value(field).toString();
                    const QUrl url(source);
                    QString path;
                    if (url.isLocalFile()) path = url.toLocalFile();
                    else if (QFileInfo(source).isAbsolute()) path = source;
                    else if (!url.scheme().isEmpty()) { fail(tr("ControlNet sources must be local files.")); return {}; }
                    else path = m_fileSystem.path(weightFile ? StoreSection::Models : StoreSection::Files, source, &error);
                    const QFileInfo info(path);
                    if (!info.isFile() || info.size() < 1) { fail(tr("Choose an available local ControlNet source: %1").arg(source)); return {}; }
                    if (weightFile) {
                        if (!QStringList{"safetensors", "safetensor", "gguf"}.contains(info.suffix().toLower())) {
                            fail(tr("Choose safetensors or GGUF ControlNet/IP-Adapter/vision weights.")); return {};
                        }
                    } else {
                        const auto input = imageInputPath(info.canonicalFilePath(), &error);
                        if (input.isEmpty()) { fail(error); return {}; }
                        QImageReader reader(input);
                        const auto dimensions = reader.size();
                        if (!reader.canRead() || dimensions.isEmpty()
                            || qint64(dimensions.width()) * dimensions.height() > 64 * 1024 * 1024) {
                            fail(tr("Choose a readable ControlNet image of at most 64 megapixels.")); return {};
                        }
                        item[field] = input;
                        continue;
                    }
                    item[field] = info.canonicalFilePath();
                }
            }
            controls.append(item);
        }
        advanced["controlNets"] = controls;
        for (const auto &key : QStringList{"upscalerModel", "detailerModel", "refinerModel"}) {
            const bool upscaler = key == "upscalerModel";
            const bool refiner = key == "refinerModel";
            const bool active = upscaler
                ? advanced.value("hiresFix").toBool() && advanced.value("upscaler").toString() == "4x-ultra"
                : advanced.value(refiner ? "refiner" : "detailer").toBool();
            if (!active) continue;
            const auto label = upscaler ? tr("4x ESRGAN")
                : refiner ? tr("SDXL Refiner") : tr("converted YOLOv8 detector");
            const auto source = advanced.value(key).toString();
            const QUrl url(source);
            QString path;
            if (url.isLocalFile()) path = url.toLocalFile();
            else if (QFileInfo(source).isAbsolute()) path = source;
            else if (!url.scheme().isEmpty()) { fail(tr("%1 weights must be local files.").arg(label)); return {}; }
            else path = m_fileSystem.path(StoreSection::Models, source, &error);
            const QFileInfo info(path);
            if (!info.isFile() || info.size() < 1
                || !QStringList{"safetensors", "safetensor", "gguf"}.contains(info.suffix().toLower())) {
                fail(tr("Choose a local safetensors or GGUF %1 model.").arg(label)); return {};
            }
            advanced[key] = info.canonicalFilePath();
        }
        const auto embeddingSource = advanced.value("textualEmbeddings").toString();
        if (!embeddingSource.isEmpty()) {
            const QUrl url(embeddingSource);
            QString path;
            if (url.isLocalFile()) path = url.toLocalFile();
            else if (QFileInfo(embeddingSource).isAbsolute()) path = embeddingSource;
            else if (!url.scheme().isEmpty()) { fail(tr("Textual embeddings must be local files.")); return {}; }
            else path = m_fileSystem.path(StoreSection::Models, embeddingSource, &error);
            const QFileInfo info(path);
            if (!info.isFile() || info.size() < 1 || info.size() > 100 * 1024 * 1024
                || !QStringList{"safetensors", "safetensor", "gguf"}.contains(info.suffix().toLower())) {
                fail(tr("Choose a local safetensors or GGUF embedding of at most 100 MiB.")); return {};
            }
            advanced["textualEmbeddings"] = info.canonicalFilePath();
        }
    }
    // Preserve submission order even when multiple requests share a millisecond.
    auto created = QDateTime::currentDateTimeUtc();
    if (!m_jobs.isEmpty()) {
        const auto last = QDateTime::fromString(m_jobs.last().value("createdAt").toString(), Qt::ISODateWithMs);
        if (created <= last) created = last.addMSecs(1);
    }
    QStringList jobIds;
    jobIds.reserve(count);
    const auto updated = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (int index = 0; index < count; ++index) {
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        jobIds.append(id);
        QJsonObject job{{"schemaVersion", 1}, {"id", id}, {"appId", "com.iisacc.dreamscapes"},
            {"createdAt", created.addMSecs(index).toString(Qt::ISODateWithMs)}, {"updatedAt", updated},
            {"state", "queued"}, {"execution", localReady ? "local" : "host"}, {"prompt", trimmed}, {"negativePrompt", negativePrompt},
            {"aspectRatio", aspectRatio}, {"width", size.width()}, {"height", size.height()},
            {"steps", steps}, {"seed", double(seed < 0 ? QRandomGenerator::global()->generate() : seed + index)},
            {"device", m_runtime.device}, {"model", reference}, {"modelName", selected->name}, {"vae", vaeReference}};
        job["mediaType"] = isVideo ? "Video" : "Image";
        if (isVideo) job["videoParameters"] = video;
        if (!advanced.isEmpty()) job["advancedParameters"] = advanced;
        m_jobs.append(job);
    }
    // Validate once and publish the whole submission before starting the serial worker.
    emit jobsChanged();
    fail({});
    emit submissionQueued(jobIds);
    QTimer::singleShot(0, this, &GenerationController::pump);
    return jobIds.first();
}
