#include "EditorMedia.h"
#include "EditorToolUtils.h"
#include <QAudioSink>
#include <QAudioDevice>
#include <QCamera>
#include <QtCore/qpermissions.h>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageCapture>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QTimer>
#include <QtEndian>
using namespace editorTools;
EditorMedia::EditorMedia(QObject *parent) : QObject(parent) {}
EditorMedia::~EditorMedia() { stop(); }
bool EditorMedia::cameraAvailable() const { return !QMediaDevices::videoInputs().isEmpty(); }
bool EditorMedia::playing() const { return m_sink && m_sink->state() == QAudio::ActiveState; }
bool EditorMedia::capture(const QVariantMap &v) {
    if (!cameraAvailable()) { m_error = tr("No camera is connected."); emit changed(); return false; }
    if (m_pending) return false;
    m_error.clear(); m_pending = true; emit changed();
    QCameraPermission permission;
    const auto status = qApp->checkPermission(permission);
    if (status == Qt::PermissionStatus::Undetermined) {
        qApp->requestPermission(permission, this, [this, v](const QPermission &p) {
            if (p.status() == Qt::PermissionStatus::Granted) startCapture(v);
            else { m_pending = false; m_error = tr("Camera access was denied by the operating system."); emit changed(); }
        });
    } else if (status == Qt::PermissionStatus::Granted) startCapture(v);
    else { m_pending = false; m_error = tr("Enable camera access for Dreamscapes in System Settings."); emit changed(); return false; }
    return true;
}
void EditorMedia::startCapture(const QVariantMap &v) {
    if (!m_camera) {
        m_camera = std::make_unique<QCamera>(); m_capture = std::make_unique<QImageCapture>(); m_session = std::make_unique<QMediaCaptureSession>();
        m_session->setCamera(m_camera.get()); m_session->setImageCapture(m_capture.get());
        connect(m_capture.get(), &QImageCapture::readyForCaptureChanged, this, [this](bool ready) { if (ready && m_pending) m_capture->capture(); });
        // imageAvailable is the full resolution captured frame; imageCaptured is only a preview.
        connect(m_capture.get(), &QImageCapture::imageAvailable, this, [this](int, const QVideoFrame &frame) {
            if (!m_pending) return; const auto pixels = frame.toImage();
            m_pending = false; if (!pixels.isNull()) emit captured(pixels); else m_error = tr("The camera did not return a decodable full resolution frame.");
            m_camera->stop(); emit changed();
        });
        connect(m_capture.get(), &QImageCapture::errorOccurred, this, [this](int, QImageCapture::Error, const QString &error) { m_error = error; m_pending = false; m_camera->stop(); emit changed(); });
        connect(m_camera.get(), &QCamera::errorOccurred, this, [this](QCamera::Error, const QString &error) { m_error = error; m_pending = false; emit changed(); });
    }
    m_camera->setCameraDevice(QMediaDevices::defaultVideoInput());
    m_camera->setExposureCompensation(float(number(v, 1)));
    m_camera->setZoomFactor(std::clamp(float(text(v, 0, "1x").remove(QRegularExpression("[x×]" )).toDouble()), m_camera->minimumZoomFactor(), m_camera->maximumZoomFactor()));
    m_capture->setQuality(QImageCapture::VeryHighQuality);
    m_camera->start();
    QTimer::singleShot(15000, this, [this] { if (m_pending) { m_pending = false; m_camera->stop(); m_error = tr("Camera capture timed out."); emit changed(); } });
}
bool EditorMedia::play(const iiSharedCanvas::AudioAsset &audio) {
    stop(); QAudioFormat format; format.setSampleRate(int(audio.sampleRate)); format.setChannelCount(audio.channelCount); format.setSampleFormat(QAudioFormat::Int16);
    const auto device = QMediaDevices::defaultAudioOutput();
    if (device.isNull() || !device.isFormatSupported(format)) { m_error = tr("The current audio output does not support this track's sample rate and channels."); emit changed(); return false; }
    QByteArray bytes(qsizetype(audio.samples.size() * 2), Qt::Uninitialized);
    for (std::size_t i = 0; i < audio.samples.size(); ++i) qToLittleEndian(audio.samples[i], reinterpret_cast<uchar *>(bytes.data() + i * 2));
    m_pcm.setData(bytes); m_pcm.open(QIODevice::ReadOnly); m_sink = std::make_unique<QAudioSink>(device, format);
    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State) { emit changed(); });
    m_sink->start(&m_pcm); emit changed(); return true;
}
iiSharedCanvas::AudioImportResult EditorMedia::mixTimelineAudio(const iiSharedCanvas::Document &document, iiSharedCanvas::FrameIndex firstFrame) {
    using namespace iiSharedCanvas;
    if (firstFrame >= document.timeline.frameCount) return {{}, {MediaIoCode::InvalidArgument, "Choose a frame inside the audio timeline."}};
    constexpr std::uint32_t rate = 48000;
    const auto count = audioSampleFrameCount(document.timeline.frameCount - firstFrame, document.timeline.frameRate, rate);
    if (!count || *count > 16777216) return {{}, {MediaIoCode::LimitExceeded, "The playback mix exceeds its PCM allocation limit."}};
    const double fps = double(document.timeline.frameRate.numerator) / document.timeline.frameRate.denominator;
    AudioAsset result{"editor.timeline.playback", rate, 2, std::vector<std::int16_t>(*count * 2)};
    std::vector<double> mixed(*count * 2);
    for (const auto &track : document.audioTracks) if (!track.muted) for (const auto &clip : track.clips) if (clip.enabled) {
        const auto *source = findAudioAsset(document, clip.assetId);
        if (!source || source->sampleRate == 0 || source->channelCount < 1 || source->channelCount > 2 || source->samples.empty()) continue;
        const double clipStart = clip.startFrame / fps, clipEnd = (clip.startFrame + clip.durationFrames) / fps;
        const auto start = std::max<std::int64_t>(0, std::int64_t(std::ceil((clipStart - firstFrame / fps) * rate)));
        const auto end = std::min<std::int64_t>(*count, std::int64_t(std::ceil((clipEnd - firstFrame / fps) * rate)));
        const double gain = std::pow(10.0, (track.gainDb + clip.gainDb) / 20);
        const auto sourceCount = source->samples.size() / source->channelCount;
        for (auto i = start; i < end; ++i) {
            const double position = ((firstFrame / fps + double(i) / rate) - clipStart) * source->sampleRate + clip.sourceOffsetSamples;
            if (position < 0 || position >= sourceCount) continue;
            const auto left = std::size_t(position), right = std::min(left + 1, sourceCount - 1); const double fraction = position - left;
            for (int channel = 0; channel < 2; ++channel) {
                const auto c = std::min<int>(channel, source->channelCount - 1);
                const double sample = source->samples[left * source->channelCount + c] * (1 - fraction) + source->samples[right * source->channelCount + c] * fraction;
                mixed[i * 2 + channel] += sample * gain;
            }
        }
    }
    for (std::size_t i = 0; i < mixed.size(); ++i) result.samples[i] = std::int16_t(std::clamp(std::lround(mixed[i]), -32768L, 32767L));
    return {std::move(result), {}};
}
void EditorMedia::stop() { if (m_sink) { m_sink->stop(); m_sink.reset(); } m_pcm.close(); emit changed(); }
QVariantList EditorMedia::library(const QString &directory, const QVariantMap &v) const {
    QVariantList result; if (!QFileInfo(directory).isDir()) return result;
    const auto query = text(v, 0).trimmed(); const auto type = text(v, 1, "All");
    QDirIterator files(directory, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (files.hasNext() && result.size() < 500) {
        files.next(); const auto info = files.fileInfo(); const auto suffix = info.suffix().toLower();
        const bool image = QStringList{"png", "jpg", "jpeg", "webp", "tif", "tiff", "bmp"}.contains(suffix);
        const bool vector = suffix == "svg", audio = suffix == "wav", video = QStringList{"mp4", "mov", "mkv", "webm"}.contains(suffix);
        if (!(image || vector || audio || video) || (!query.isEmpty() && !info.fileName().contains(query, Qt::CaseInsensitive))) continue;
        if ((type == "Image" && !image) || (type == "Vector" && !vector) || (type == "Audio" && !audio) || (type == "Video" && !video)) continue;
        result.append(QVariantMap{{"label", info.fileName()}, {"source", QUrl::fromLocalFile(info.absoluteFilePath())}, {"action", "importAsset"}, {"modified", info.lastModified()}, {"type", image ? "Image" : vector ? "Vector" : audio ? "Audio" : "Video"}, {"description", tr("Local file · %1 bytes · license not supplied").arg(info.size())}});
    }
    const bool latest = text(v, 5, "Recent") == "Recent";
    std::sort(result.begin(), result.end(), [latest](const QVariant &a, const QVariant &b) { return latest ? a.toMap().value("modified").toDateTime() > b.toMap().value("modified").toDateTime() : a.toMap().value("label").toString().localeAwareCompare(b.toMap().value("label").toString()) < 0; });
    return result;
}
