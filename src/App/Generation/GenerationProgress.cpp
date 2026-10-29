#include "GenerationController.h"
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <algorithm>

void GenerationController::checkWorkerDeadline(quint64 token)
{
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    if (token != m_workerProbe || m_active.value("id") != m_workerProbeJob || !busy()
        || m_cacheReleasePending || m_publicationPending || m_finishing || m_cancelled || m_remoteActive
        || m_workerDeadline.isActive()) return;
    m_nativeTimedOut = true;
    const auto telemetry = m_active.value("telemetry").toObject();
    const bool video = m_active.value("mediaType") == "Video";
    const auto phase = video ? m_inferenceStatus.value("state").toString()
        : telemetry.value("phase").toString(m_inferenceStatus.value("state").toString());
    const auto step = video ? m_inferenceStatus.value("step").toInt()
        : telemetry.value("step").toInt(m_inferenceStatus.value("step").toInt());
    const auto total = video ? m_inferenceStatus.value("total").toInt()
        : telemetry.value("total").toInt(m_inferenceStatus.value("total").toInt());
    auto diagnostic = telemetry.value("trace_path").toString();
    if (diagnostic.isEmpty()) diagnostic = QString::fromUtf8(m_log).right(2000).trimmed();
    if (diagnostic.isEmpty()) diagnostic = tr("The worker did not emit diagnostics.");
    m_active["timeoutError"] = tr("Generation stopped making progress at %1 (%2/%3). Diagnostics: %4")
        .arg(phase).arg(step).arg(total).arg(diagnostic);
    stopProcess(false);
    const auto id = m_active.value("id");
    QTimer::singleShot(2000, this, [this, id] {
        if (m_active.value("id") == id && m_nativeTimedOut) stopProcess(true);
    });
#else
    Q_UNUSED(token);
#endif
}

void GenerationController::readProcessOutput(const QByteArray &data)
{
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    const QPointer<GenerationController> owner(this);
    m_log = (m_log + data).right(16384);
    m_pendingOutput += data;
    qsizetype newline;
    while ((newline = m_pendingOutput.indexOf('\n')) >= 0) {
        const auto line = m_pendingOutput.left(newline);
        m_pendingOutput.remove(0, newline + 1);
        // Progress bars can leave a carriage-return prefix on the merged stream.
        const auto start = line.indexOf("IILD_PREVIEW ");
        if (start >= 0 && line.size() - start <= 4096) acceptPreview(line.mid(start + 13));
        if (!owner) return;
        const auto videoProgress = line.indexOf("IILD_VIDEO_PROGRESS ");
        if (videoProgress >= 0 && line.size()-videoProgress <= 4096 && busy() && !m_publicationPending && !m_finishing
            && m_active.value("mediaType") == "Video" && !m_cancelled && !m_nativeTimedOut) {
            const auto event = QJsonDocument::fromJson(line.mid(videoProgress+20)).object();
            const auto stage = event.value("stage").toString();
            const int step=event.value("step").toInt(-1), total=event.value("total").toInt(-1);
            const bool validCount = stage == "computing" ? step > 0 && total == 0 : step >= 0 && total >= step;
            if (event.value("schema") == "iild-video-progress-v1" && validCount
                && QStringList{"waiting","preparing-model","loading","encoding","computing","denoising","decoding","interpolating","encoding-video","complete"}.contains(stage)) {
                const auto key = stage+':'+QString::number(step)+'/'+QString::number(total);
                if (key != m_workerProgressKey) {
                    m_workerProgressKey=key;
                    m_workerDeadline.start(std::max(1,m_runtime.nativeTimeoutMilliseconds));
                }
                setInferenceStatus({{"state",stage},{"ready",false},{"backend","video"},{"step",step},{"total",total}});
                if (!owner) return;
                if (stage == "denoising") { m_previewStep=step; m_previewTotalSteps=total; emit previewChanged(); }
                if (!owner) return;
            }
        }
        const auto nativeProgress = line.indexOf("IILD_NATIVE_PROGRESS ");
        if (nativeProgress >= 0 && line.size() - nativeProgress <= 4096)
            acceptNativeWorkerProgress(line.mid(nativeProgress + 21));
        if (!owner) return;
        const auto telemetryStart = line.indexOf("IILD_NATIVE_TELEMETRY ");
        if (telemetryStart >= 0 && line.size() - telemetryStart <= 8192 && busy() && !m_cancelled && !m_publicationPending && !m_finishing) {
            const auto event = QJsonDocument::fromJson(line.mid(telemetryStart + 22)).object();
            if (event.value("schema") == "iild-native-telemetry-v1") {
                m_active["telemetry"] = event;
                auto status = m_inferenceStatus;
                status["telemetry"] = event;
                setInferenceStatus(status);
                if (!owner) return;
                // Heartbeats are observability, never evidence of forward progress.
            }
        }
        const auto modelProgress = line.indexOf("IILD_MODEL_PROGRESS ");
        if (modelProgress >= 0 && line.size() - modelProgress <= 4096 && (busy() || !m_controlId.isEmpty()) && !m_cancelled && !m_publicationPending && !m_finishing) {
            const auto event = QJsonDocument::fromJson(line.mid(modelProgress + 20)).object();
            const auto completed = event.value("completed_bytes").toDouble(-1), total = event.value("total_bytes").toDouble(-1);
            if (event.value("schema") == "iild-model-progress-v1" && completed >= 0 && total >= completed) {
                const auto key = QString("model:%1/%2").arg(completed).arg(total);
                if (busy() && !m_nativeTimedOut && key != m_workerProgressKey) {
                    m_workerProgressKey = key;
                    m_workerDeadline.start(std::max(1, m_runtime.nativeTimeoutMilliseconds));
                }
                setInferenceStatus({{"state", completed == total ? "preparing" : "checking-model"}, {"ready", false},
                    {"completedBytes", completed}, {"totalBytes", total}});
                if (!owner) return;
            }
        }
        const auto ready = line.indexOf("IILD_READY ");
        if (ready >= 0 && line.size() - ready <= 4096
            && QJsonDocument::fromJson(line.mid(ready + 11)).object().value("schema") == "iild-worker-v1") {
            m_workerReady = true;
            m_workerForegroundSupported = QJsonDocument::fromJson(line.mid(ready + 11)).object()
                .value("capabilities").toArray().contains(QStringLiteral("foreground-residency"));
            sendWorkerRequest();
        if (!owner) return;
        }
        const auto result = line.indexOf("IILD_RESULT ");
        if (result >= 0 && line.size() - result <= 65536) acceptWorkerResult(line.mid(result + 12));
        if (!owner) return;
    }
    // A 4,000-character SDK error can expand to 48 KB when JSON escapes Unicode.
    m_pendingOutput = m_pendingOutput.right(65536);
#endif
}

void GenerationController::acceptNativeWorkerProgress(const QByteArray &line)
{
    if (!busy() || m_cancelled || !m_controlId.isEmpty() || m_publicationPending || m_finishing) return;
    const QPointer<GenerationController> owner(this);
    const auto id = m_active.value("id");
    const auto event = QJsonDocument::fromJson(line).object();
    if (event.value("schema") != "iild-native-progress-v1") return;
    const auto stage = event.value("stage").toString();
    if (stage == "computing") {
        const int batch = event.value("step").toInt(-1);
        if (batch <= 0 || event.value("total").toInt(-1) != 0 || m_nativeTimedOut) return;
        const auto key = "computing:" + QString::number(batch);
        if (key != m_workerProgressKey) {
            m_workerProgressKey = key;
            m_workerDeadline.start(std::max(1, m_runtime.nativeTimeoutMilliseconds));
        }
        return;
    }
    if (!QStringList{"waiting", "loading", "encoding", "denoising", "decoding", "preparing-model"}.contains(stage)) return;
    const int step = event.value("step").toInt(-1), total = event.value("total").toInt(-1);
    if (step < 0 || total < 0 || step > total || total > 1000000) return;
    if (m_nativeTimedOut) return;
    if (stage == "denoising") {
        const auto steps = m_active.value("steps").toInt();
        if ((total != steps && total != std::max(1, int(steps * 0.35f)))
            || (total == m_previewTotalSteps && step <= m_previewStep)) return;
        m_previewStep = step;
        m_previewTotalSteps = total;
        emit previewChanged();
        if (!owner || m_cancelled || m_nativeTimedOut || m_finishing || m_publicationPending || m_active.value("id") != id) return;
    }
    const auto progressKey = stage + ':' + QString::number(step) + '/' + QString::number(total);
    if (progressKey != m_workerProgressKey) {
        m_workerProgressKey = progressKey;
        m_workerDeadline.start(std::max(1, m_runtime.nativeTimeoutMilliseconds));
    }
    setInferenceStatus({{"state", stage}, {"ready", false}, {"backend", "native"},
                        {"step", step}, {"total", total}, {"telemetry", m_active.value("telemetry")}});
}

void GenerationController::sendWorkerRequest()
{
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    const QPointer<GenerationController> owner(this);
    if (m_cacheReleasePending || (!busy() && m_controlId.isEmpty()) || !m_workerReady || m_workerRequest.isEmpty()) return;
    if (!m_controlId.isEmpty() && !m_workerForegroundSupported) {
        m_controlId.clear();
        m_workerRequest.clear();
        setInferenceStatus({{"state", "error"}, {"ready", false},
                            {"error", tr("Update iiLocalDiffusion to enable foreground model preparation.")}});
        if (owner) QTimer::singleShot(0, this, &GenerationController::pump);
        return;
    }
    if (busy() && m_cancelled) { stopProcess(false); return; }
    if (busy()) {
        m_workerProgressKey.clear();
        m_workerDeadline.start(std::max(1, m_runtime.nativeTimeoutMilliseconds));
    }
    const auto files = busy()
        ? std::make_shared<std::pair<decltype(m_workDirectory), decltype(m_previewDirectory)>>(m_workDirectory, m_previewDirectory)
        : nullptr;
    if (m_process.write(m_workerRequest, files) != m_workerRequest.size()) {
        m_log += "\nCannot send the inference request to iiLocalDiffusion.";
        stopProcess(true);
    }
    m_workerRequest.clear();
#endif
}

void GenerationController::acceptWorkerResult(const QByteArray &line)
{
    const QPointer<GenerationController> owner(this);
    const auto event = QJsonDocument::fromJson(line).object();
    if (event.value("schema") != "iild-worker-result-v1" || !event.value("ok").isBool()) return;
    if (!m_controlId.isEmpty() && event.value("id") == m_controlId) {
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
        m_process.releaseInput();
#endif
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
        if (owner) QTimer::singleShot(0, this, &GenerationController::pump);
        return;
    }
    if (!busy() || m_cancelled || m_nativeTimedOut || m_publicationPending || m_finishing) return;
    if (event.value("schema") != "iild-worker-result-v1" || event.value("id") != m_active.value("id")
        || !event.value("ok").isBool()) return;
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    m_process.releaseInput();
#endif
    m_active["worker"] = event; // Timing/cache diagnostics remain in the app's memory.
    auto status = event.value("residency").toObject();
    status["pid"] = event.value("pid");
    // The result's residency describes the idle worker after execution, not
    // the last generation stage captured for a failure report.
    setInferenceStatus(status, false);
    if (!owner || !busy() || m_active.value("id") != event.value("id") || m_cancelled || m_finishing || m_publicationPending) return;
    if (!event.value("ok").toBool()) {
        auto error = event.value("error").toString();
        if (error.isEmpty()) error = QString::fromUtf8(m_log).right(4000);
        finish("failed", error);
        return;
    }
    beginPublication(dreamscapes::PublicationOrigin::Worker);
}

void GenerationController::acceptPreview(const QByteArray &line)
{
    if (!busy() || m_cancelled || !m_previewDirectory || m_publicationPending || m_finishing) return;
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
    if (!m_runtime.nativeInference && !m_nativeTimedOut)
        m_workerDeadline.start(std::max(1, m_runtime.nativeTimeoutMilliseconds));
    m_previewImage = QUrl::fromLocalFile(path);
    m_previewJobId = m_active.value("id").toString();
    m_previewSequence = sequence;
    m_previewStep = step;
    m_previewTotalSteps = total;
    m_active["previewSteps"] = step;
    m_active["previewTotalSteps"] = total;
    emit previewChanged();
}

void GenerationController::acceptNativeProgress(const QString &id, const iiLocalDiffusion::NativeGenerationProgress &event)
{
    const QPointer<GenerationController> owner(this);
    const auto current = [owner, id] {
        return owner && owner->m_active.value("id") == id && !owner->m_cancelled && !owner->m_nativeTimedOut
            && !owner->m_publicationPending && !owner->m_finishing && !owner->m_closing;
    };
    if (!current()) return;
    if (m_runtime.backgroundActivity) m_runtime.backgroundActivity->update(event);
    if (!current()) return;
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
    if (!current()) return;
    const auto steps = m_active.value("steps").toInt();
    const auto advancedRecipe = m_active.value("advancedParameters").toObject();
    const float refinementStrength = advancedRecipe.isEmpty() ? 0.35f : float(advancedRecipe.value("denoiseStrength").toDouble());
    if (event.stage == Stage::Denoising
        && (event.total == steps || event.total == std::max(1, int(steps * refinementStrength)))
        && event.step >= 0 && event.step <= event.total
        && (event.total != m_previewTotalSteps || event.step >= m_previewStep)) {
        m_previewStep = event.step; m_previewTotalSteps = event.total;
        emit previewChanged();
    }
}

void GenerationController::acceptNativePreview(const QString &id, const dreamscapes::NativePreviewArtifact &preview)
{
    const auto name = QString::fromStdString(preview.name);
    if (!busy() || m_active.value("id") != id || m_cancelled || m_nativeTimedOut
        || m_publicationPending || m_finishing || m_closing || !m_previewDirectory
        || preview.sequence <= m_previewSequence || preview.step < 1 || preview.total < preview.step
        || preview.total > 10000 || name != QString("step-%1.png").arg(preview.sequence, 6, 10, QLatin1Char('0'))
        || (m_previewTotalSteps == preview.total && preview.step < m_previewStep)) return;
    // The native worker encoded this exact PNG; publication does not decode it
    // again on the owner. Image presentation uses the existing image loader.
    m_previewImage = QUrl::fromLocalFile(m_previewDirectory->filePath(name));
    m_previewJobId = id; m_previewSequence = preview.sequence;
    m_previewStep = preview.step; m_previewTotalSteps = preview.total;
    m_active["previewSteps"] = preview.step; m_active["previewTotalSteps"] = preview.total;
    emit previewChanged();
}

void GenerationController::clearPreview()
{
    m_previewImage.clear();
    m_previewJobId.clear();
    m_previewStep = m_previewTotalSteps = 0;
    m_previewSequence = 0;
    // clearWorkingFiles owns directory retirement; this only clears UI state.
    if (!m_closing) emit previewChanged();
}
void GenerationController::stopProcess(bool force)
{
    Q_UNUSED(force);
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
    m_process.cancel();
#endif
}
