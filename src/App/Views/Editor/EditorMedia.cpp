#include "EditorMedia.h"
#include "EditorToolUtils.h"
#include "EditorAudioPlayback.h"
#include "TimelineAudioMix.h"
#include <QCamera>
#include <QtCore/qpermissions.h>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageCapture>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QTimer>
using namespace editorTools;
EditorMedia::EditorMedia(QObject *parent) : QObject(parent),m_audioPlayback(std::make_unique<EditorAudioPlayback>(this)) {
    connect(this,&EditorMedia::cameraChanged,this,&EditorMedia::changed);
    connect(this,&EditorMedia::audioChanged,this,&EditorMedia::changed);
    connect(m_audioPlayback.get(),&EditorAudioPlayback::changed,this,&EditorMedia::audioChanged);
}
EditorMedia::~EditorMedia() {disconnect(m_audioPlayback.get(),nullptr,this,nullptr);m_audioPlayback.reset();}
bool EditorMedia::cameraAvailable() const { return !QMediaDevices::videoInputs().isEmpty(); }
bool EditorMedia::playing() const {return m_audioPlayback&&m_audioPlayback->playing();}
bool EditorMedia::preparing() const {return m_audioPlayback&&m_audioPlayback->preparing();}
QString EditorMedia::error() const {return !m_error.isEmpty()||!m_audioPlayback?m_error:m_audioPlayback->error();}
QObject *EditorMedia::audioPlaybackObject() const {return m_audioPlayback.get();}
qulonglong EditorMedia::playTimeline(std::shared_ptr<const iiSharedCanvas::Document> document,iiSharedCanvas::FrameIndex frame) {
    return m_audioPlayback->play({std::move(document),frame,{}});
}
qulonglong EditorMedia::playAsset(std::shared_ptr<const iiSharedCanvas::Document> document,const std::string &id) {
    return m_audioPlayback->play({std::move(document),0,id});
}
bool EditorMedia::capture(const QVariantMap &v) {
    if (!cameraAvailable()) { m_error = tr("No camera is connected."); emit cameraChanged(); return false; }
    if (m_pending) return false;
    m_error.clear(); m_pending = true; emit cameraChanged();
    QCameraPermission permission;
    const auto status = qApp->checkPermission(permission);
    if (status == Qt::PermissionStatus::Undetermined) {
        qApp->requestPermission(permission, this, [this, v](const QPermission &p) {
            if (p.status() == Qt::PermissionStatus::Granted) startCapture(v);
            else { m_pending = false; m_error = tr("Camera access was denied by the operating system."); emit cameraChanged(); }
        });
    } else if (status == Qt::PermissionStatus::Granted) startCapture(v);
    else { m_pending = false; m_error = tr("Enable camera access for Dreamscapes in System Settings."); emit cameraChanged(); return false; }
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
            m_camera->stop(); emit cameraChanged();
        });
        connect(m_capture.get(), &QImageCapture::errorOccurred, this, [this](int, QImageCapture::Error, const QString &error) { m_error = error; m_pending = false; m_camera->stop(); emit cameraChanged(); });
        connect(m_camera.get(), &QCamera::errorOccurred, this, [this](QCamera::Error, const QString &error) { m_error = error; m_pending = false; emit cameraChanged(); });
    }
    m_camera->setCameraDevice(QMediaDevices::defaultVideoInput());
    m_camera->setExposureCompensation(float(number(v, 1)));
    m_camera->setZoomFactor(std::clamp(float(text(v, 0, "1x").remove(QRegularExpression("[x×]" )).toDouble()), m_camera->minimumZoomFactor(), m_camera->maximumZoomFactor()));
    m_capture->setQuality(QImageCapture::VeryHighQuality);
    m_camera->start();
    QTimer::singleShot(15000, this, [this] { if (m_pending) { m_pending = false; m_camera->stop(); m_error = tr("Camera capture timed out."); emit cameraChanged(); } });
}
iiSharedCanvas::AudioImportResult EditorMedia::mixTimelineAudio(const iiSharedCanvas::Document &document, iiSharedCanvas::FrameIndex firstFrame) {
    return dreamscapes::mixTimelineAudio(document,firstFrame);
}

void EditorMedia::stop() {m_audioPlayback->stop();}
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
