#include <iiFileProviderDevice.h>
#include <File.h>
#include "EditorCameraPhoto.h"
#include "EditorPhotoImageNative.h"
#include <QCamera>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageCapture>
#include <QImageReader>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QVideoSink>
#include <QUuid>
#include <QtConcurrent>
#include <QtCore/qpermissions.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
QString cacheTemplate() {
    const auto root = qEnvironmentVariable("DREAMSCAPES_EDITOR_SOURCE_CACHE", QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    iiFileProvider::Directory().mkpath(root);
    return root + "/editor-photo-XXXXXX";
}
struct GalleryResult { QVariantList items; QString status; };
int delayFor(const QVariantMap &settings, const char *key) {
    bool ok = false; const double seconds = settings.value(key, 0).toDouble(&ok);
    return ok && std::isfinite(seconds) ? std::clamp(int(std::round(seconds)), 0, 60) : 0;
}
}
EditorCameraPhoto::EditorCameraPhoto(QObject *parent)
    : EditorCameraPhoto(iiPhotoLibrary::nativePhotoLibrary(), parent) {}
EditorCameraPhoto::EditorCameraPhoto(std::shared_ptr<iiPhotoLibrary::PhotoLibrary> library, QObject *parent)
    : EditorCameraPhoto(std::move(library), editorNativeScreenCaptureSupported() ? ScreenOperation(editorCaptureImage) : ScreenOperation{}, parent) {}
EditorCameraPhoto::EditorCameraPhoto(std::shared_ptr<iiPhotoLibrary::PhotoLibrary> library, ScreenOperation screen, QObject *parent)
    : QObject(parent), m_directory(cacheTemplate()), m_library(std::move(library)),
      m_screenOperation(std::move(screen)),
      m_devices(std::make_unique<QMediaDevices>()) {
    m_photoPool.setMaxThreadCount(1);
    connect(m_devices.get(), &QMediaDevices::videoInputsChanged, this, &EditorCameraPhoto::refreshDevices);
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (--m_countdown <= 0) {
            m_timer.stop(); auto action = std::move(m_delayed); m_delayed = {};
            emit changed(); if (action) action();
        } else emit changed();
    });
}
EditorCameraPhoto::~EditorCameraPhoto() { cancel(); if (m_library) m_library->cancel(); }
QVariantList EditorCameraPhoto::devices() const {
    QVariantList result;
    for (const auto &device : QMediaDevices::videoInputs())
        result.append(QVariantMap{{"id", QString::fromUtf8(device.id())}, {"label", device.description()}});
    return result;
}
QString EditorCameraPhoto::deviceId() const {
    return m_selectedDevice.isEmpty() ? QString::fromUtf8(QMediaDevices::defaultVideoInput().id()) : m_selectedDevice;
}
bool EditorCameraPhoto::cameraAvailable() const { return !QMediaDevices::videoInputs().isEmpty(); }
bool EditorCameraPhoto::previewActive() const { return m_camera && m_camera->isActive(); }
bool EditorCameraPhoto::recording() const { return m_recorder && m_recorder->recorderState() == QMediaRecorder::RecordingState; }
qint64 EditorCameraPhoto::recordingDuration() const { return m_recorder ? m_recorder->duration() : 0; }
QString EditorCameraPhoto::cameraStatus() const {
    if (!cameraAvailable()) return tr("No camera");
    if (m_camera && m_camera->error() != QCamera::NoError) return tr("Camera unavailable");
    if (previewActive()) return tr("Health - OK");
    const auto status = qApp->checkPermission(QCameraPermission{});
    return status == Qt::PermissionStatus::Granted ? tr("Ready") : tr("Camera access required");
}
QString EditorCameraPhoto::cameraDescription() const {
    if (!m_camera) return cameraAvailable() ? tr("Click to enable camera") : tr("Connect a camera to preview");
    const auto format = m_camera->cameraFormat(); const auto size = format.resolution();
    return size.isValid() ? tr("%1 · %2 × %3 · %4 fps").arg(m_camera->cameraDevice().description()).arg(size.width()).arg(size.height()).arg(format.maxFrameRate(), 0, 'f', 0)
        : m_camera->cameraDevice().description();
}
void EditorCameraPhoto::refreshDevices() {
    const auto current = deviceId();
    const auto inputs = QMediaDevices::videoInputs();
    if (std::none_of(inputs.begin(), inputs.end(), [&](const auto &device) { return QString::fromUtf8(device.id()) == current; })) {
        cancel(); m_selectedDevice.clear();
    }
    emit changed();
}
void EditorCameraPhoto::fail(const QString &error) {
    m_error = error; m_busy = false; m_remainingPhotos = 0; m_shotId = -1;
    m_timer.stop(); m_countdown = 0; m_delayed = {}; ++m_generation;
    emit changed();
}
void EditorCameraPhoto::ensureCamera() {
    if (m_session) return;
    m_session = std::make_unique<QMediaCaptureSession>();
    m_camera = std::make_unique<QCamera>();
    m_imageCapture = std::make_unique<QImageCapture>();
    m_recorder = std::make_unique<QMediaRecorder>();
    m_session->setCamera(m_camera.get()); m_session->setImageCapture(m_imageCapture.get());
    m_session->setRecorder(m_recorder.get()); m_session->setVideoOutput(m_videoOutput);
    m_imageCapture->setQuality(QImageCapture::VeryHighQuality);
    connect(m_camera.get(), &QCamera::activeChanged, this, &EditorCameraPhoto::changed);
    connect(m_camera.get(), &QCamera::errorOccurred, this, [this](QCamera::Error, const QString &error) { fail(error); });
    connect(m_imageCapture.get(), &QImageCapture::readyForCaptureChanged, this, [this](bool ready) { if (ready) takeNextPhoto(); });
    connect(m_imageCapture.get(), &QImageCapture::imageAvailable, this, [this](int id, const QVideoFrame &frame) {
        if (id != m_shotId || m_remainingPhotos <= 0) return;
        m_shotId = -1; const auto image = frameForSettings(frame.toImage(), m_settings);
        if (image.isNull()) { fail(tr("The camera returned no full resolution image.")); return; }
        storeCapture(image, "Camera");
        if (!m_busy) return;
        if (--m_remainingPhotos == 0) {
            m_busy = false; if (!m_previewWanted) m_camera->stop(); emit changed();
        } else QTimer::singleShot(150, this, &EditorCameraPhoto::takeNextPhoto);
    });
    connect(m_imageCapture.get(), &QImageCapture::errorOccurred, this, [this](int id, QImageCapture::Error, const QString &error) { if (id == m_shotId || m_busy) fail(error); });
    connect(m_recorder.get(), &QMediaRecorder::durationChanged, this, &EditorCameraPhoto::changed);
    connect(m_recorder.get(), &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString &error) { fail(error); });
    connect(m_recorder.get(), &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        emit changed(); if (state == QMediaRecorder::StoppedState && !m_video.isEmpty()) finishVideo();
    });
}
void EditorCameraPhoto::attachPreview(QObject *output) {
    m_videoOutput = output; if (m_session) m_session->setVideoOutput(output);
}
void EditorCameraPhoto::configureCamera(const QVariantMap &settings) {
    ensureCamera(); m_selectedDevice = settings.value("cameraDevice").toString();
    const auto inputs = QMediaDevices::videoInputs(); auto selected = QMediaDevices::defaultVideoInput();
    for (const auto &device : inputs) if (QString::fromUtf8(device.id()) == deviceId()) selected = device;
    if (m_camera->cameraDevice() != selected) { m_camera->stop(); m_camera->setCameraDevice(selected); }
    const auto formats = selected.videoFormats();
    auto best = m_camera->cameraFormat();
    for (const auto &format : formats) if (format.resolution().width() <= 1920 && format.maxFrameRate() >= 24
        && (best.isNull() || format.resolution().width() > best.resolution().width())) best = format;
    if (!best.isNull()) m_camera->setCameraFormat(best);
    m_camera->start(); emit changed();
}
bool EditorCameraPhoto::authorizeCamera(std::function<void()> ready) {
    if (!cameraAvailable()) { fail(tr("No camera is connected.")); return false; }
    const auto permission = QCameraPermission{}; const auto status = qApp->checkPermission(permission);
    if (status == Qt::PermissionStatus::Denied) { fail(tr("Enable camera access for Dreamscapes in System Settings.")); return false; }
    m_error.clear();
    emit changed();
    if (status == Qt::PermissionStatus::Granted) ready();
    else {
        const auto generation = m_generation;
        qApp->requestPermission(permission, this, [this, generation, ready = std::move(ready)](const QPermission &answer) {
            if (generation != m_generation) return;
            if (answer.status() == Qt::PermissionStatus::Granted) ready();
            else fail(tr("Camera access was denied by the operating system."));
        });
    }
    return true;
}
bool EditorCameraPhoto::startPreview(const QVariantMap &settings) {
    m_previewWanted = true;
    return authorizeCamera([this, settings] { configureCamera(settings); });
}
void EditorCameraPhoto::setPreviewEnabled(bool enabled, const QVariantMap &settings) {
    m_previewWanted = enabled;
    if (enabled && !m_busy && QGuiApplication::platformName() != "offscreen" && qApp->checkPermission(QCameraPermission{}) == Qt::PermissionStatus::Granted && cameraAvailable()) startPreview(settings);
    else if (!enabled && !m_busy && m_camera) m_camera->stop();
}
void EditorCameraPhoto::delayed(int seconds, std::function<void()> action) {
    m_countdown = seconds; m_delayed = std::move(action); emit changed();
    if (seconds > 0) m_timer.start();
    else { auto ready = std::move(m_delayed); m_delayed = {}; ready(); }
}
bool EditorCameraPhoto::capturePhoto(const QVariantMap &settings) {
    if (m_busy || recording()) return false;
    m_busy = true; m_settings = settings; ++m_generation;
    return authorizeCamera([this, settings] {
        configureCamera(settings);
        delayed(delayFor(settings, "cameraTimer"), [this] {
            const auto drive = m_settings.value("cameraDrive", "Photo").toString();
            m_remainingPhotos = drive == "3-shot burst" ? 3 : drive == "5-shot burst" ? 5 : 1;
            takeNextPhoto();
            const auto generation = m_generation;
            QTimer::singleShot(15000, this, [this, generation] { if (generation == m_generation && m_busy) fail(tr("Camera capture timed out.")); });
        });
    });
}
void EditorCameraPhoto::takeNextPhoto() {
    if (m_remainingPhotos > 0 && m_shotId < 0 && m_imageCapture && m_imageCapture->isReadyForCapture()) m_shotId = m_imageCapture->capture();
}
bool EditorCameraPhoto::toggleVideo(const QVariantMap &settings) {
    if (recording()) { m_recorder->stop(); return true; }
    if (m_busy) return false;
    m_busy = true; m_settings = settings; ++m_generation;
    return authorizeCamera([this, settings] {
        configureCamera(settings);
        delayed(delayFor(settings, "cameraTimer"), [this] {
            QMediaFormat format(QMediaFormat::MPEG4); format.setVideoCodec(QMediaFormat::VideoCodec::H264);
            m_recorder->setMediaFormat(format); m_recorder->setQuality(QMediaRecorder::HighQuality);
            m_video = QUrl::fromLocalFile(newPath(".mp4")); m_recorder->setOutputLocation(m_video); m_recorder->record();
            emit changed();
        });
    });
}
QString EditorCameraPhoto::newPath(const QString &suffix) const { return m_directory.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + suffix); }
QImage EditorCameraPhoto::frameForSettings(const QImage &input, const QVariantMap &settings) {
    if (input.isNull()) return {};
    auto image = input; const auto ratio = settings.value("cameraAspect", "16:9").toString().split(':');
    if (ratio.size() == 2) {
        const auto a = ratio[0].toDouble(), b = ratio[1].toDouble();
        if (a > 0 && b > 0 && std::isfinite(a / b) && std::isfinite(b / a)) {
            const auto width = int(std::clamp(image.height() * a / b, 1.0, double(image.width())));
            const auto height = int(std::clamp(image.width() * b / a, 1.0, double(image.height())));
            image = image.copy((image.width() - width) / 2, (image.height() - height) / 2, width, height);
        }
    }
    const auto angle = settings.value("cameraAngle", "0°").toString().remove(QChar(0x00b0)).toInt();
    if (angle == 90 || angle == 180 || angle == 270) image = image.transformed(QTransform().rotate(angle));
    return image;
}
void EditorCameraPhoto::storeCapture(const QImage &image, const QString &) {
    const auto path = newPath(".png");
    if (!iiFileProvider::File::saveImage(image, path, "PNG")) { fail(tr("Could not store the captured image.")); return; }
    m_current = m_thumbnail = QUrl::fromLocalFile(path);
    if (m_settings.value("selector", "Camera").toString() == "Camera" && m_settings.value("cameraSavePhotos").toBool()) saveCaptureInPhotos(m_current);
    emit changed();
}
void EditorCameraPhoto::finishVideo() {
    const auto source = m_recorder->actualLocation().isEmpty() ? m_video : m_recorder->actualLocation(); m_video = QUrl{};
    if (m_recorder->error() != QMediaRecorder::NoError) { fail(m_recorder->errorString()); return; }
    if (!QFileInfo(source.toLocalFile()).isFile()) { fail(tr("The recording could not be saved.")); return; }
    m_thumbnail = QUrl{};
    if (m_session->videoSink()) {
        const auto frame = frameForSettings(m_session->videoSink()->videoFrame().toImage(), m_settings);
        const auto path = newPath(".png"); if (!frame.isNull() && iiFileProvider::File::saveImage(frame, path)) m_thumbnail = QUrl::fromLocalFile(path);
    }
    if (!m_previewWanted) m_camera->stop();
    const auto finish = [this](const QUrl &url) {
        m_current = url; m_busy = false;
        if (m_settings.value("cameraSavePhotos").toBool()) saveCaptureInPhotos(url);
        emit changed();
    };
    QStringList filters;
    const auto ratio = m_settings.value("cameraAspect", "16:9").toString().split(':');
    if (ratio.size() == 2 && ratio[0].toDouble() > 0 && ratio[1].toDouble() > 0)
        filters.append(QString("crop=floor(min(iw\\,ih*%1/%2)/2)*2:floor(min(ih\\,iw*%2/%1)/2)*2").arg(ratio[0], ratio[1]));
    const auto angle = m_settings.value("cameraAngle", "0°").toString();
    if (angle == "90°") filters.append("transpose=1");
    else if (angle == "180°") filters.append({"hflip", "vflip"});
    else if (angle == "270°") filters.append("transpose=2");
    if (filters.isEmpty() || QStringLiteral(DREAMSCAPES_FFMPEG_EXECUTABLE).isEmpty()) { finish(source); return; }
    const auto output = QUrl::fromLocalFile(newPath(".mp4")); const auto generation = m_generation;
    auto *process = new QProcess(this);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process, generation, output, finish](int code, QProcess::ExitStatus status) {
        const auto error = QString::fromUtf8(process->readAllStandardError()); process->deleteLater();
        if (generation != m_generation) return;
        if (code != 0 || status != QProcess::NormalExit) { fail(tr("Could not apply the recording's crop and angle: %1").arg(error.right(600))); return; }
        finish(output);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { process->deleteLater(); if (generation == m_generation) fail(tr("The video processing runtime could not be started.")); }
    });
    process->start(QStringLiteral(DREAMSCAPES_FFMPEG_EXECUTABLE), {"-nostdin", "-y", "-i", source.toLocalFile(), "-vf", filters.join(','), "-an", "-c:v", "libx264", "-preset", "fast", "-crf", "18", output.toLocalFile()});
    emit changed();
}
void EditorCameraPhoto::saveCaptureInPhotos(const QUrl &source) {
    m_pendingPhotoSaves.append(source);
    if (!m_library) { m_error = tr("Saving to Photos is unavailable on this device."); m_pendingPhotoSaves.clear(); emit changed(); return; }
    if (m_library->access() == iiPhotoLibrary::Access::NotDetermined) {
        if (m_photoAuthorizationPending) return;
        m_photoAuthorizationPending = true;
        m_library->authorize([self = QPointer<EditorCameraPhoto>(this)] { if (!self) return; self->m_photoAuthorizationPending = false; self->flushPhotoSaves(); });
    } else flushPhotoSaves();
}
void EditorCameraPhoto::flushPhotoSaves() {
    if (m_library->access() != iiPhotoLibrary::Access::Full && m_library->access() != iiPhotoLibrary::Access::Limited) {
        m_pendingPhotoSaves.clear(); m_error = tr("The capture is retained here. Allow Photos access in System Settings to save captures in Photos."); emit changed(); return;
    }
    const auto sources = std::exchange(m_pendingPhotoSaves, {}); const auto library = m_library;
    for (const auto &source : sources) {
        auto *watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] { const auto error = watcher->result(); watcher->deleteLater(); if (!error.isEmpty()) { m_error = error; emit changed(); } });
        watcher->setFuture(QtConcurrent::run(&m_photoPool, [source, library] {
            QFile file(source.toLocalFile()); QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return tr("Could not read the capture for Photos.");
            const QFileInfo info(source.toLocalFile());
            const QJsonObject resource{{"name", info.fileName()}, {"role", info.suffix() == "mp4" ? "video" : "photo"}, {"hash", QString::fromLatin1(hash.result().toHex())}};
            const QJsonObject record{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"resources", QJsonArray{resource}}};
            QString error; if (library->import(record, {source.toLocalFile()}, &error).isEmpty() && error.isEmpty()) error = tr("Could not save the capture in Photos."); return error;
        }));
    }
}
void EditorCameraPhoto::refreshCaptureTargets() {
    editorCaptureTargets([self = QPointer<EditorCameraPhoto>(this)](QVariantList targets, QString error) {
        QMetaObject::invokeMethod(qApp, [self, targets = std::move(targets), error] {
            if (!self) return; self->m_targets = targets;
            if (!error.isEmpty()) self->m_error = error; emit self->changed();
        }, Qt::QueuedConnection);
    });
}
bool EditorCameraPhoto::captureScreen(const QVariantMap &settings) {
    if (m_busy || !screenCaptureSupported()) return false;
    m_busy = true; m_error.clear(); m_settings = settings; const auto generation = ++m_generation;
    delayed(delayFor(settings, "captureDelay"), [this, settings, generation] {
        m_screenOperation(settings, [self = QPointer<EditorCameraPhoto>(this), generation](QImage image, QString error) {
            QMetaObject::invokeMethod(qApp, [self, generation, image, error] {
                if (!self || generation != self->m_generation) return;
                if (!error.isEmpty() || image.isNull()) { self->fail(error.isEmpty() ? tr("Screen capture returned no image.") : error); return; }
                self->storeCapture(image, "Capture"); self->m_busy = false; emit self->changed();
            }, Qt::QueuedConnection);
        });
    });
    return true;
}
void EditorCameraPhoto::cancel() {
    ++m_generation; m_timer.stop(); m_countdown = 0; m_delayed = {}; m_remainingPhotos = 0; m_shotId = -1;
    if (recording()) m_recorder->stop();
    if (m_camera) m_camera->stop(); m_busy = false; m_previewWanted = false; emit changed();
}
QVariantList EditorCameraPhoto::gallery() const {
    QVariantList result;
    for (const auto &entry : m_assets) if (m_query.isEmpty() || entry.toMap().value("label").toString().contains(m_query, Qt::CaseInsensitive)) result.append(entry);
    std::stable_sort(result.begin(), result.end(), [this](const auto &left, const auto &right) {
        const auto a = left.toMap(), b = right.toMap();
        if (m_sort == "Name") return a.value("label").toString().localeAwareCompare(b.value("label").toString()) < 0;
        if (m_sort == "Size") return a.value("size").toLongLong() > b.value("size").toLongLong();
        return m_sort == "Oldest" ? a.value("created").toDateTime() < b.value("created").toDateTime() : a.value("created").toDateTime() > b.value("created").toDateTime();
    });
    return result;
}
int EditorCameraPhoto::selectionCount() const { return int(std::count_if(m_assets.begin(), m_assets.end(), [](const auto &entry) { return entry.toMap().value("selected").toBool(); })); }
void EditorCameraPhoto::filterGallery(const QString &query, const QString &sort) { m_query = query; m_sort = sort; emit galleryChanged(); }
void EditorCameraPhoto::clearError() { if (!m_error.isEmpty()) { m_error.clear(); emit changed(); } }
bool EditorCameraPhoto::galleryAuthorizationRequired() const {
    return m_gallerySource == "iCloud Photo" && m_library
        && m_library->access() == iiPhotoLibrary::Access::NotDetermined && !m_galleryAuthorizationPending;
}
void EditorCameraPhoto::requestGalleryAccess() {
    if (!galleryAuthorizationRequired()) return;
    m_galleryAuthorizationPending = true; m_galleryStatus = tr("Waiting for Photos access…"); emit galleryChanged();
    const auto generation = m_galleryGeneration;
    m_library->authorize([self = QPointer<EditorCameraPhoto>(this), generation] {
        QMetaObject::invokeMethod(qApp, [self, generation] {
            if (!self) return;
            self->m_galleryAuthorizationPending = false;
            if (generation == self->m_galleryGeneration) self->loadGallery(self->m_gallerySource, self->m_galleryFolder);
            else emit self->galleryChanged();
        }, Qt::QueuedConnection);
    });
}
void EditorCameraPhoto::loadGallery(const QString &source, const QUrl &folder) {
    clearError(); m_gallerySource = source; m_galleryFolder = folder;
    const auto generation = ++m_galleryGeneration;
    m_assets.clear(); m_galleryBusy = true; m_galleryStatus = tr("Loading photos…"); emit galleryChanged();
    const auto library = m_library; const bool photos = source == "iCloud Photo";
    auto launch = [this, library, photos, source, folder, generation] {
        if (generation != m_galleryGeneration) return;
        if (photos && (!library || (library->access() != iiPhotoLibrary::Access::Full && library->access() != iiPhotoLibrary::Access::Limited))) {
            m_galleryBusy = false;
            m_galleryStatus = library && library->access() == iiPhotoLibrary::Access::NotDetermined
                ? m_galleryAuthorizationPending ? tr("Waiting for Photos access…") : tr("Allow Photos access to browse this library.")
                : tr("Allow Dreamscapes to access Photos in System Settings.");
            emit galleryChanged(); return;
        }
        auto *watcher = new QFutureWatcher<GalleryResult>(this);
        connect(watcher, &QFutureWatcher<GalleryResult>::finished, this, [this, watcher, generation] {
            const auto result = watcher->result(); watcher->deleteLater();
            if (generation != m_galleryGeneration) return;
            m_assets = result.items; m_galleryStatus = result.status; m_galleryBusy = false; emit galleryChanged();
        });
        const auto directory = m_directory.path();
        watcher->setFuture(QtConcurrent::run(&m_photoPool, [library, photos, source, folder, directory] {
            GalleryResult result;
            if (photos) {
                QString error; auto snapshot = library->scan(&error);
                std::stable_sort(snapshot.assets.begin(), snapshot.assets.end(), [](const auto &a, const auto &b) { return a.created > b.created; });
                for (const auto &asset : snapshot.assets) {
                    if (asset.media != "photo" || result.items.size() >= 400) continue;
                    const auto resources = asset.resources;
                    const auto resource = std::find_if(resources.begin(), resources.end(), [](const auto &r) { return r.role == "photo"; });
                    if (resource == resources.end()) continue;
                    const auto dimensions = asset.stamp.split(':');
                    const auto area = dimensions.size() >= 3 ? dimensions[dimensions.size() - 2].toLongLong() * dimensions.last().toLongLong() : 0;
                    result.items.append(QVariantMap{{"id", asset.identifier}, {"label", asset.name.isEmpty() ? resource->name : asset.name}, {"source", QUrl{}},
                        {"thumbnail", QUrl{}}, {"token", resource->token}, {"created", asset.created}, {"size", area}, {"selected", false}, {"native", true}, {"thumbnailRequested", false}});
                }
                result.status = !error.isEmpty() ? error : result.items.isEmpty() ? tr("No photos are available in this library.")
                    : library->access() == iiPhotoLibrary::Access::Limited ? tr("Showing the photos allowed by your Photos permission.") : QString{};
                if (snapshot.assets.size() > 400) result.status = tr("Showing up to 400 photos. Use a local folder to browse additional sources.");
            } else {
                const auto path = folder.isLocalFile() ? folder.toLocalFile() : QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
                QDirIterator files(path, iiFileProvider::Directory::Files | iiFileProvider::Directory::NoSymLinks, QDirIterator::Subdirectories);
                while (files.hasNext() && result.items.size() < 400) {
                    files.next(); const auto info = files.fileInfo(); QImageReader reader(info.absoluteFilePath());
                    if (!reader.canRead()) continue;
                    result.items.append(QVariantMap{{"id", info.absoluteFilePath()}, {"label", info.fileName()}, {"source", QUrl::fromLocalFile(info.absoluteFilePath())},
                        {"thumbnail", QUrl::fromLocalFile(info.absoluteFilePath())}, {"created", info.lastModified()}, {"size", qint64(reader.size().width()) * reader.size().height()}, {"selected", false}, {"native", false}});
                }
                result.status = result.items.isEmpty() ? tr("No images in this folder. Choose a folder from Library.") : QString{};
            }
            return result;
        }));
    };
    launch();
}
void EditorCameraPhoto::requestThumbnail(const QString &id) {
    auto found = std::find_if(m_assets.begin(), m_assets.end(), [&](const auto &entry) { return entry.toMap().value("id").toString() == id; });
    if (found == m_assets.end()) return;
    auto item = found->toMap();
    if (!item.value("native").toBool() || item.value("thumbnailRequested").toBool()) return;
    item["thumbnailRequested"] = true; *found = item;
    const auto path = newPath(".jpg"); const auto library = m_library;
    const auto generation = m_galleryGeneration;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, id, path, generation] {
        const auto error = watcher->result(); watcher->deleteLater(); if (generation != m_galleryGeneration) return;
        for (auto &entry : m_assets) if (entry.toMap().value("id").toString() == id) {
            auto item = entry.toMap(); item["thumbnail"] = error.isEmpty() ? QUrl::fromLocalFile(path) : QUrl{};
            item["previewError"] = error; entry = item; break;
        }
        emit galleryChanged();
    });
    watcher->setFuture(QtConcurrent::run(&m_photoPool, [library, id, path] {
        QString error; if (!library->preview(id, path, &error) && error.isEmpty()) error = tr("Photo preview is unavailable."); return error;
    }));
}
void EditorCameraPhoto::setSelected(const QString &id, bool multiple) {
    for (auto &entry : m_assets) {
        auto item = entry.toMap(); const bool match = item.value("id").toString() == id;
        item["selected"] = match ? !item.value("selected").toBool() : multiple && item.value("selected").toBool(); entry = item;
    }
    emit galleryChanged();
}
void EditorCameraPhoto::retainSingleSelection() {
    bool retained = false;
    for (auto &entry : m_assets) { auto item = entry.toMap(); if (!item.value("selected").toBool()) continue; item["selected"] = !retained; retained = true; entry = item; }
    emit galleryChanged();
}
void EditorCameraPhoto::selectAll(bool multiple) {
    const auto displayed = gallery(); bool retained = false;
    for (auto &entry : m_assets) {
        auto item = entry.toMap(); const bool matches = std::any_of(displayed.begin(), displayed.end(), [&](const auto &shown) { return shown.toMap().value("id") == item.value("id"); });
        item["selected"] = matches && (multiple || !retained); if (item.value("selected").toBool()) retained = true; entry = item;
    }
    emit galleryChanged();
}
bool EditorCameraPhoto::importSelection() {
    if (m_galleryBusy || !selectionCount()) return false;
    QVariantList selected; for (const auto &entry : m_assets) if (entry.toMap().value("selected").toBool()) selected.append(entry);
    m_galleryBusy = true; emit galleryChanged(); const auto generation = m_galleryGeneration;
    auto *watcher = new QFutureWatcher<GalleryResult>(this);
    connect(watcher, &QFutureWatcher<GalleryResult>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result(); watcher->deleteLater(); if (generation != m_galleryGeneration) return;
        m_galleryBusy = false; m_galleryStatus = result.status;
        for (const auto &url : result.items) emit importReady(url.toUrl()); emit galleryChanged();
    });
    const auto directory = m_directory.path(); const auto library = m_library;
    watcher->setFuture(QtConcurrent::run(&m_photoPool, [selected, directory, library] {
        GalleryResult result;
        for (const auto &entry : selected) {
            const auto item = entry.toMap(); auto url = item.value("source").toUrl();
            if (item.value("native").toBool()) {
                const auto folder = directory + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
                if (!iiFileProvider::Directory().mkpath(folder)) { result.status = tr("Could not prepare the original image for import."); break; }
                const auto name = QFileInfo(item.value("label").toString()).fileName();
                const auto path = folder + '/' + (name.isEmpty() ? QStringLiteral("Photo.png") : name);
                QString error;
                if (!library->exportResource(item.value("id").toString(), item.value("token").toString(), path, &error)) { result.status = error; break; }
                url = QUrl::fromLocalFile(path);
            }
            QImageReader reader(url.toLocalFile());
            const bool readable = reader.canRead();
            if (!readable || reader.transformation() != QImageIOHandler::TransformationNone) {
                reader.setAutoTransform(true);
                const auto decoded = readable ? reader.read() : editorDecodeOriginalPhoto(url.toLocalFile());
                const auto folder = directory + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces);
                iiFileProvider::Directory().mkpath(folder);
                const auto path = folder + '/' + QFileInfo(url.toLocalFile()).completeBaseName() + ".png";
                if (decoded.isNull() || !iiFileProvider::File::saveImage(decoded, path, "PNG")) { result.status = tr("The selected original image could not be decoded on this device."); break; }
                url = QUrl::fromLocalFile(path);
            }
            result.items.append(url);
        }
        return result;
    }));
    return true;
}
bool EditorCameraPhoto::importCurrentCapture() { if (m_busy || m_current.isEmpty()) return false; emit importReady(m_current); return true; }
void EditorCameraPhoto::reportImportFailure(const QString &message) { m_error = message; emit changed(); }

