#include <iiFileProviderDevice.h>
#include <File.h>
#include <iiFileProviderNative.h>
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
#include <QPromise>
#include <QProcess>
#include <QStandardPaths>
#include <QVideoSink>
#include <QUuid>
#include <QtConcurrent>
#include <QtCore/qpermissions.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>
#include <vector>

namespace {
std::filesystem::path cacheRoot() {
    const auto root = qEnvironmentVariable("DREAMSCAPES_EDITOR_SOURCE_CACHE", QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    return std::filesystem::path(root.toStdU16String());
}
QString localPath(const std::filesystem::path &path) { return QString::fromStdU16String(path.u16string()); }
struct GalleryResult { QVariantList items; QString status; };
struct GalleryImportResult { dreamscapes::PhotoOriginals originals; QString status; bool cancelled = false; };
struct CaptureStorageResult { QUrl source; QString error; };
int delayFor(const QVariantMap &settings, const char *key) {
    bool ok = false; const double seconds = settings.value(key, 0).toDouble(&ok);
    return ok && std::isfinite(seconds) ? std::clamp(int(std::round(seconds)), 0, 60) : 0;
}
}
struct EditorCameraPhoto::OutputCache final {
    explicit OutputCache(std::filesystem::path root) : root(std::move(root)) {}
    std::filesystem::path filePath(const std::filesystem::path &name) {
        std::lock_guard lock(mutex);
        if (root.empty()) throw iiFileProvider::native::FileError("The acquisition cache location is empty.");
        if (!directory) directory = std::make_unique<iiFileProvider::native::TemporaryDirectory>(root);
        return directory->path() / name;
    }
    std::filesystem::path root;
    std::mutex mutex;
    std::unique_ptr<iiFileProvider::native::TemporaryDirectory> directory;
};
EditorCameraPhoto::EditorCameraPhoto(QObject *parent)
    : EditorCameraPhoto(iiPhotoLibrary::nativePhotoLibrary(), parent) {}
EditorCameraPhoto::EditorCameraPhoto(std::shared_ptr<iiPhotoLibrary::PhotoLibrary> library, QObject *parent)
    : EditorCameraPhoto(std::move(library), editorNativeScreenCaptureSupported() ? ScreenOperation(editorCaptureImage) : ScreenOperation{}, parent) {}
EditorCameraPhoto::EditorCameraPhoto(std::shared_ptr<iiPhotoLibrary::PhotoLibrary> library, ScreenOperation screen, QObject *parent)
    : EditorCameraPhoto(std::move(library), std::move(screen), [] { return std::make_unique<QMediaDevices>(); }, parent) {}
EditorCameraPhoto::EditorCameraPhoto(std::shared_ptr<iiPhotoLibrary::PhotoLibrary> library, ScreenOperation screen, DeviceMonitorFactory devices, QObject *parent)
    : QObject(parent), m_outputCache(std::make_shared<OutputCache>(cacheRoot())), m_library(std::move(library)),
      m_screenOperation(std::move(screen)), m_deviceMonitorFactory(std::move(devices)) {
    QPromise<void> ready; ready.start(); ready.finish(); m_photoTail = ready.future();
    qRegisterMetaType<dreamscapes::PhotoOriginals>();
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (--m_countdown <= 0) {
            m_timer.stop(); auto action = std::move(m_delayed); m_delayed = {};
            const auto generation = m_generation; const QPointer<EditorCameraPhoto> self(this);
            emit changed(); if (self && generation == self->m_generation && action) action();
        } else emit changed();
    });
}
EditorCameraPhoto::~EditorCameraPhoto() {
    m_photoClosed->store(true); if (m_importCancellation) m_importCancellation->store(true);
    if (m_galleryCancellation) m_galleryCancellation->store(true);
    ++m_galleryAuthorizationSequence; ++m_galleryPresenterSequence; m_galleryReadWanted = false;
    disconnect(m_galleryPresenterDestroyed);
    if (m_captureCancellation) m_captureCancellation->store(true);
    ++m_generation; ++m_galleryGeneration; ++m_previewOutputGeneration; m_timer.stop(); m_delayed = {};
    for (auto *child : children()) disconnect(child, nullptr, this, nullptr);
    if (m_camera) disconnect(m_camera.get(), nullptr, this, nullptr);
    if (m_recorder) disconnect(m_recorder.get(), nullptr, this, nullptr);
    if (m_imageCapture) disconnect(m_imageCapture.get(), nullptr, this, nullptr);
    if (m_devices) disconnect(m_devices.get(), nullptr, this, nullptr);
    if (m_library) m_library->cancel();
}
void EditorCameraPhoto::ensureDeviceMonitor() const {
    if (m_deviceMonitorRequested) return;
    m_deviceMonitorRequested = true;
    if (m_deviceMonitorFactory) m_devices = m_deviceMonitorFactory();
    if (m_devices) connect(m_devices.get(), &QMediaDevices::videoInputsChanged, this, &EditorCameraPhoto::refreshDevices);
}
QVariantList EditorCameraPhoto::devices() const {
    ensureDeviceMonitor();
    QVariantList result;
    for (const auto &device : QMediaDevices::videoInputs())
        result.append(QVariantMap{{"id", QString::fromUtf8(device.id())}, {"label", device.description()}});
    return result;
}
QString EditorCameraPhoto::deviceId() const {
    ensureDeviceMonitor();
    return m_selectedDevice.isEmpty() ? QString::fromUtf8(QMediaDevices::defaultVideoInput().id()) : m_selectedDevice;
}
bool EditorCameraPhoto::cameraAvailable() const { ensureDeviceMonitor(); return !QMediaDevices::videoInputs().isEmpty(); }
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
    if (m_captureCancellation) m_captureCancellation->store(true);
    m_storingPhoto = false;
    m_error = error; m_busy = false; m_remainingPhotos = 0; m_shotId = -1;
    m_timer.stop(); m_countdown = 0; m_delayed = {}; ++m_generation;
    emit changed();
}
void EditorCameraPhoto::ensureCamera() {
    if (m_session) return;
    ensureDeviceMonitor();
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
        m_shotId = -1; const auto image = frame.toImage();
        if (image.isNull()) { fail(tr("The camera returned no full resolution image.")); return; }
        storeCapture(image, true);
    });
    connect(m_imageCapture.get(), &QImageCapture::errorOccurred, this, [this](int id, QImageCapture::Error, const QString &error) { if (id == m_shotId || m_busy) fail(error); });
    connect(m_recorder.get(), &QMediaRecorder::durationChanged, this, &EditorCameraPhoto::changed);
    connect(m_recorder.get(), &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString &error) { fail(error); });
    connect(m_recorder.get(), &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
        emit changed(); if (state == QMediaRecorder::StoppedState && !m_video.isEmpty()) finishVideo();
    });
}
void EditorCameraPhoto::attachPreview(QObject *output) {
    if (m_videoOutput == output) return;
    if (!output) { releasePreview(); return; }
    const auto generation = ++m_previewOutputGeneration; m_videoOutput = output;
    connect(output, &QObject::destroyed, this, [this, generation] { if (generation == m_previewOutputGeneration) releasePreview(); });
    if (m_session) m_session->setVideoOutput(output);
}
bool EditorCameraPhoto::detachPreview(QObject *expected) {
    if (!expected || m_videoOutput != expected) return false;
    releasePreview(); return true;
}
void EditorCameraPhoto::releasePreview() {
    ++m_previewOutputGeneration; m_videoOutput = nullptr; m_previewWanted = false;
    if (m_session) m_session->setVideoOutput(nullptr);
    if (m_camera && !m_busy) m_camera->stop();
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
    const auto generation = m_generation; const QPointer<EditorCameraPhoto> self(this);
    m_countdown = seconds; m_delayed = std::move(action); emit changed();
    if (!self || generation != self->m_generation) return;
    if (seconds > 0) m_timer.start();
    else { auto ready = std::move(m_delayed); m_delayed = {}; ready(); }
}
bool EditorCameraPhoto::capturePhoto(const QVariantMap &settings) {
    if (m_busy || recording()) return false;
    m_busy = true; m_settings = settings; ++m_generation;
    m_captureCancellation = std::make_shared<std::atomic_bool>(false); m_storingPhoto = false;
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
    if (m_busy && !m_storingPhoto && m_remainingPhotos > 0 && m_shotId < 0 && m_imageCapture && m_imageCapture->isReadyForCapture()) m_shotId = m_imageCapture->capture();
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
            QString error; const auto path = newPath(".mp4", &error);
            if (path.isEmpty()) { fail(error); return; }
            m_video = QUrl::fromLocalFile(path); m_recorder->setOutputLocation(m_video); m_recorder->record();
            emit changed();
        });
    });
}
QString EditorCameraPhoto::newPath(const QString &suffix, QString *error) const {
    try { return localPath(m_outputCache->filePath(std::filesystem::path((QUuid::createUuid().toString(QUuid::WithoutBraces) + suffix).toStdU16String()))); }
    catch (const std::exception &failure) {
        if (error) *error = tr("Could not prepare capture storage: %1").arg(QString::fromUtf8(failure.what()));
        return {};
    }
}
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
void EditorCameraPhoto::storeCapture(const QImage &image, bool cameraTransform) {
    // Detach capture-provider memory before it can be reused; transforms and
    // encoding run on owned pixels without retaining this GUI object.
    auto pixels = image.copy();
    if (pixels.isNull()) { fail(tr("Could not retain the captured image.")); return; }
    m_storingPhoto = true;
    const auto generation = m_generation; const auto cancellation = m_captureCancellation;
    const auto settings = m_settings; const auto saveToPhotos = cameraTransform && settings.value("cameraSavePhotos").toBool();
    auto *watcher = new QFutureWatcher<CaptureStorageResult>(this);
    connect(watcher, &QFutureWatcher<CaptureStorageResult>::finished, this, [this, watcher, generation, cameraTransform, saveToPhotos] {
        const auto result = watcher->result(); watcher->deleteLater();
        if (generation != m_generation) return;
        m_storingPhoto = false;
        if (!result.error.isEmpty()) { fail(result.error); return; }
        if (result.source.isEmpty()) { fail(tr("The capture produced no stored image.")); return; }
        m_current = m_thumbnail = result.source;
        const bool more = cameraTransform && --m_remainingPhotos > 0;
        m_busy = more;
        const QPointer<EditorCameraPhoto> self(this);
        if (cameraTransform && !more && !m_previewWanted && m_camera) m_camera->stop();
        if (!self || generation != self->m_generation) return;
        emit changed();
        if (!self || generation != self->m_generation) return;
        if (saveToPhotos) saveCaptureInPhotos(result.source);
        if (self && generation == self->m_generation && more) QTimer::singleShot(150, this, &EditorCameraPhoto::takeNextPhoto);
    });
    watcher->setFuture(QtConcurrent::run([pixels = std::move(pixels), settings, cameraTransform, cancellation, closed = m_photoClosed, cache = m_outputCache]() mutable {
        const auto cancelled = [&] { return closed->load() || (cancellation && cancellation->load()); };
        CaptureStorageResult result;
        if (cancelled()) return result;
        std::filesystem::path output;
        try {
            if (cameraTransform) pixels = frameForSettings(pixels, settings);
            if (cancelled()) return result;
            if (pixels.isNull()) { result.error = tr("The camera returned no full resolution image."); return result; }
            output = cache->filePath(std::filesystem::path((QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png").toStdU16String()));
            const auto path = localPath(output);
            if (!iiFileProvider::File::saveImage(pixels, path, "PNG")) result.error = tr("Could not store the captured image.");
            else if (!cancelled()) result.source = QUrl::fromLocalFile(path);
        } catch (const std::exception &failure) {
            result.error = tr("Could not store the captured image: %1").arg(QString::fromUtf8(failure.what()));
        } catch (...) { result.error = tr("Could not store the captured image."); }
        if (!output.empty() && result.source.isEmpty()) { std::error_code ignored; std::filesystem::remove(output, ignored); }
        return result;
    }));
}
void EditorCameraPhoto::finishVideo() {
    const auto source = m_recorder->actualLocation().isEmpty() ? m_video : m_recorder->actualLocation(); m_video = QUrl{};
    if (m_recorder->error() != QMediaRecorder::NoError) { fail(m_recorder->errorString()); return; }
    if (!QFileInfo(source.toLocalFile()).isFile()) { fail(tr("The recording could not be saved.")); return; }
    m_thumbnail = QUrl{};
    if (m_session->videoSink()) {
        const auto frame = frameForSettings(m_session->videoSink()->videoFrame().toImage(), m_settings);
        if (!frame.isNull()) {
            QString error; const auto path = newPath(".png", &error);
            if (!path.isEmpty() && iiFileProvider::File::saveImage(frame, path)) m_thumbnail = QUrl::fromLocalFile(path);
        }
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
    QString error; const auto path = newPath(".mp4", &error);
    if (path.isEmpty()) { fail(error); return; }
    const auto output = QUrl::fromLocalFile(path); const auto generation = m_generation;
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
        watcher->setFuture(enqueuePhotoWork([source, library, cache = m_outputCache] {
            (void)cache;
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
    m_captureCancellation = std::make_shared<std::atomic_bool>(false); m_storingPhoto = false;
    delayed(delayFor(settings, "captureDelay"), [this, settings, generation] {
        m_screenOperation(settings, [self = QPointer<EditorCameraPhoto>(this), generation](QImage image, QString error) {
            QMetaObject::invokeMethod(qApp, [self, generation, image, error] {
                if (!self || generation != self->m_generation) return;
                if (!error.isEmpty() || image.isNull()) { self->fail(error.isEmpty() ? tr("Screen capture returned no image.") : error); return; }
                self->storeCapture(image, false);
            }, Qt::QueuedConnection);
        });
    });
    return true;
}
void EditorCameraPhoto::cancel() {
    if (m_captureCancellation) m_captureCancellation->store(true);
    m_storingPhoto = false;
    ++m_generation; m_timer.stop(); m_countdown = 0; m_delayed = {}; m_remainingPhotos = 0; m_shotId = -1;
    if (recording()) m_recorder->stop();
    if (m_camera) m_camera->stop(); m_busy = false; m_previewWanted = false; emit changed();
}
QVariantList EditorCameraPhoto::gallery() const { return m_gallery.visibleItems(); }
int EditorCameraPhoto::selectionCount() const { return m_gallery.selectedCount(); }
void EditorCameraPhoto::filterGallery(const QString &query, const QString &sort) {
    const QPointer<EditorCameraPhoto> owner(this); const auto generation = m_galleryGeneration;
    const bool changed = m_gallery.setFilter(query, sort);
    if (owner && generation == m_galleryGeneration && changed) emit galleryChanged();
}
void EditorCameraPhoto::clearError() { if (!m_error.isEmpty()) { m_error.clear(); emit changed(); } }
bool EditorCameraPhoto::galleryAuthorizationRequired() const {
    return m_galleryReadWanted && m_gallerySource == "iCloud Photo" && m_library
        && m_library->access() == iiPhotoLibrary::Access::NotDetermined && !m_galleryAuthorizationPending;
}
void EditorCameraPhoto::attachGallery(QObject *presenter) {
    if (!presenter || m_galleryPresenter == presenter) return;
    disconnect(m_galleryPresenterDestroyed);
    m_galleryPresenter = presenter; m_galleryReadWanted = true;
    if (!m_galleryCancellation || m_galleryCancellation->load()) m_galleryCancellation = std::make_shared<std::atomic_bool>(false);
    const auto sequence = ++m_galleryPresenterSequence;
    m_galleryPresenterDestroyed = connect(presenter, &QObject::destroyed, this, [this, sequence] { if (sequence == m_galleryPresenterSequence) retireGallery(); });
}
bool EditorCameraPhoto::detachGallery(QObject *expectedPresenter) {
    if (!expectedPresenter || m_galleryPresenter != expectedPresenter) return false;
    retireGallery(); return true;
}
void EditorCameraPhoto::retireGallery() {
    disconnect(m_galleryPresenterDestroyed); m_galleryPresenterDestroyed = {};
    ++m_galleryPresenterSequence; m_galleryPresenter = nullptr; m_galleryReadWanted = false;
    ++m_galleryGeneration; if (m_galleryCancellation) m_galleryCancellation->store(true);
    m_galleryBusy = false; m_galleryStatus.clear();
    QList<EditorPhotoGalleryModel::Update> updates; const auto &items = m_gallery.items();
    for (int row = 0; row < items.size(); ++row) {
        auto item = items[row].toMap();
        if (item.value("native").toBool() && item.value("thumbnailRequested").toBool() && item.value("thumbnail").toUrl().isEmpty()) {
            item["thumbnailRequested"] = false; updates.append({row, item});
        }
    }
    const QPointer<EditorCameraPhoto> owner(this); const auto generation = m_galleryGeneration;
    m_gallery.updateItems(updates);
    if (owner && generation == m_galleryGeneration && !m_galleryReadWanted) emit galleryChanged();
}
void EditorCameraPhoto::requestGalleryAccess() {
    if (!galleryAuthorizationRequired()) return;
    const QPointer<EditorCameraPhoto> owner(this); const auto library = m_library;
    const auto authorization = ++m_galleryAuthorizationSequence;
    m_galleryAuthorizationPending = true; m_galleryStatus = tr("Waiting for Photos access…"); emit galleryChanged();
    if (!owner || !m_galleryAuthorizationPending || authorization != m_galleryAuthorizationSequence) return;
    if (!m_galleryReadWanted || m_gallerySource != "iCloud Photo") {
        m_galleryAuthorizationPending = false; emit galleryChanged(); return;
    }
    const auto complete = [self = owner, authorization] {
        if (!qApp) return;
        QMetaObject::invokeMethod(qApp, [self, authorization] {
            if (!self || !self->m_galleryAuthorizationPending || authorization != self->m_galleryAuthorizationSequence) return;
            self->m_galleryAuthorizationPending = false;
            if (self->m_galleryReadWanted && self->m_gallerySource == "iCloud Photo") self->loadGallery(self->m_gallerySource, self->m_galleryFolder);
            else emit self->galleryChanged();
        }, Qt::QueuedConnection);
    };
    const auto failed = [self = owner, authorization](const QString &error) {
        if (!self || !self->m_galleryAuthorizationPending || authorization != self->m_galleryAuthorizationSequence) return;
        self->m_galleryAuthorizationPending = false;
        if (self->m_galleryReadWanted && self->m_gallerySource == "iCloud Photo") self->m_galleryStatus = error;
        emit self->galleryChanged();
    };
    try {
        if (library->access() != iiPhotoLibrary::Access::NotDetermined) complete();
        else library->authorize(complete);
    } catch (const std::exception &error) {
        failed(tr("Could not request Photos access: %1").arg(QString::fromUtf8(error.what())));
    } catch (...) {
        failed(tr("The Photos access request failed."));
    }
}
void EditorCameraPhoto::loadGallery(const QString &source, const QUrl &folder) {
    const QPointer<EditorCameraPhoto> owner(this); const auto generation = ++m_galleryGeneration;
    m_galleryReadWanted = true;
    if (m_galleryCancellation) m_galleryCancellation->store(true);
    clearError(); if (!owner || generation != m_galleryGeneration) return;
    m_gallerySource = source; m_galleryFolder = folder;
    const auto cancellation = m_galleryCancellation = std::make_shared<std::atomic_bool>(false);
    m_galleryBusy = true; m_galleryStatus = tr("Loading photos…");
    m_gallery.setItems({}); if (!owner || generation != m_galleryGeneration) return;
    emit galleryChanged();
    if (!owner || generation != m_galleryGeneration) return;
    const auto library = m_library; const bool photos = source == "iCloud Photo";
    auto launch = [this, library, photos, folder, generation, cancellation] {
        if (generation != m_galleryGeneration) return;
        const auto access = photos && library ? library->access() : iiPhotoLibrary::Access::Unsupported;
        if (photos && access != iiPhotoLibrary::Access::Full && access != iiPhotoLibrary::Access::Limited) {
            m_galleryBusy = false;
            m_galleryStatus = access == iiPhotoLibrary::Access::NotDetermined
                ? m_galleryAuthorizationPending ? tr("Waiting for Photos access…") : tr("Allow Photos access to browse this library.")
                : access == iiPhotoLibrary::Access::Unsupported ? tr("Photo library browsing is unavailable on this device.")
                : tr("Allow Dreamscapes to access Photos in System Settings.");
            emit galleryChanged(); return;
        }
        auto *watcher = new QFutureWatcher<GalleryResult>(this);
        connect(watcher, &QFutureWatcher<GalleryResult>::finished, this, [this, watcher, generation] {
            const auto result = watcher->result(); watcher->deleteLater();
            if (generation != m_galleryGeneration) return;
            m_galleryStatus = result.status; m_galleryBusy = false;
            const QPointer<EditorCameraPhoto> owner(this); m_gallery.setItems(result.items);
            if (!owner || generation != m_galleryGeneration) return;
            emit galleryChanged();
        });
        watcher->setFuture(enqueuePhotoWork([library, photos, folder, cancellation] {
            GalleryResult result;
            if (photos) {
                QString error; auto snapshot = library->scan(&error);
                if (cancellation->load()) return result;
                std::stable_sort(snapshot.assets.begin(), snapshot.assets.end(), [](const auto &a, const auto &b) { return a.created > b.created; });
                for (const auto &asset : snapshot.assets) {
                    if (cancellation->load()) return GalleryResult{};
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
                while (!cancellation->load() && files.hasNext() && result.items.size() < 400) {
                    files.next(); const auto info = files.fileInfo(); QImageReader reader(info.absoluteFilePath());
                    if (!reader.canRead()) continue;
                    result.items.append(QVariantMap{{"id", info.absoluteFilePath()}, {"label", info.fileName()}, {"source", QUrl::fromLocalFile(info.absoluteFilePath())},
                        {"thumbnail", QUrl::fromLocalFile(info.absoluteFilePath())}, {"created", info.lastModified()}, {"size", qint64(reader.size().width()) * reader.size().height()}, {"selected", false}, {"native", false}});
                }
                result.status = result.items.isEmpty() ? tr("No images in this folder. Choose a folder from Library.") : QString{};
            }
            return result;
        }, cancellation));
    };
    launch();
}
void EditorCameraPhoto::requestThumbnail(const QString &id) {
    if (!m_galleryReadWanted) return;
    const auto &items = m_gallery.items();
    auto found = std::find_if(items.begin(), items.end(), [&](const auto &entry) { return entry.toMap().value("id").toString() == id; });
    if (found == items.end()) return;
    const int row = int(found - items.begin()); auto item = found->toMap();
    if (!item.value("native").toBool() || item.value("thumbnailRequested").toBool()) return;
    const QPointer<EditorCameraPhoto> owner(this); const auto generation = m_galleryGeneration;
    QString error; const auto path = newPath(".jpg", &error);
    if (path.isEmpty()) {
        if (item.value("previewError").toString() == error && m_galleryStatus == error) return;
        item["previewError"] = error; m_galleryStatus = error; m_gallery.updateItems({{row, item}});
        if (owner && generation == m_galleryGeneration) emit galleryChanged(); return;
    }
    if (!item.value("previewError").toString().isEmpty() && m_galleryStatus == item.value("previewError").toString()) m_galleryStatus.clear();
    item["thumbnailRequested"] = true; m_gallery.updateItems({{row, item}});
    if (!owner || generation != m_galleryGeneration) return;
    const auto library = m_library; const auto cancellation = m_galleryCancellation;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, id, path, generation] {
        const auto error = watcher->result(); watcher->deleteLater(); if (generation != m_galleryGeneration) return;
        QList<EditorPhotoGalleryModel::Update> updates;
        const auto &items = m_gallery.items();
        for (int row = 0; row < items.size(); ++row) if (items[row].toMap().value("id").toString() == id) {
            auto item = items[row].toMap(); item["thumbnail"] = error.isEmpty() ? QUrl::fromLocalFile(path) : QUrl{}; item["previewError"] = error; updates.append({row, item});
        }
        const QPointer<EditorCameraPhoto> owner(this); m_gallery.updateItems(updates);
        if (owner && generation == m_galleryGeneration) emit galleryChanged();
    });
    watcher->setFuture(enqueuePhotoWork([library, id, path, cache = m_outputCache] {
        (void)cache;
        QString error; if (!library->preview(id, path, &error) && error.isEmpty()) error = tr("Photo preview is unavailable."); return error;
    }, cancellation));
}
void EditorCameraPhoto::setSelected(const QString &id, bool multiple) {
    QList<EditorPhotoGalleryModel::Update> updates; const auto &items = m_gallery.items();
    for (int row = 0; row < items.size(); ++row) {
        auto item = items[row].toMap(); const bool match = item.value("id").toString() == id;
        item["selected"] = match ? !item.value("selected").toBool() : multiple && item.value("selected").toBool(); updates.append({row, item});
    }
    const QPointer<EditorCameraPhoto> owner(this); const auto generation = m_galleryGeneration;
    m_gallery.updateItems(updates); if (owner && generation == m_galleryGeneration) emit galleryChanged();
}
void EditorCameraPhoto::retainSingleSelection() {
    bool retained = false; QList<EditorPhotoGalleryModel::Update> updates; const auto &items = m_gallery.items();
    for (int row = 0; row < items.size(); ++row) {
        auto item = items[row].toMap(); if (!item.value("selected").toBool()) continue;
        item["selected"] = !retained; retained = true; updates.append({row, item});
    }
    const QPointer<EditorCameraPhoto> owner(this); const auto generation = m_galleryGeneration;
    m_gallery.updateItems(updates); if (owner && generation == m_galleryGeneration) emit galleryChanged();
}
void EditorCameraPhoto::selectAll(bool multiple) {
    const auto &items = m_gallery.items(); std::vector<bool> visible(items.size(), false);
    for (const auto row : m_gallery.visibleRows()) visible[row] = true;
    bool retained = false; QList<EditorPhotoGalleryModel::Update> updates;
    for (int row = 0; row < items.size(); ++row) {
        auto item = items[row].toMap(); item["selected"] = visible[row] && (multiple || !retained);
        if (item.value("selected").toBool()) retained = true; updates.append({row, item});
    }
    const QPointer<EditorCameraPhoto> owner(this); const auto generation = m_galleryGeneration;
    m_gallery.updateItems(updates); if (owner && generation == m_galleryGeneration) emit galleryChanged();
}
qulonglong EditorCameraPhoto::beginImport(bool gallery) {
    if (importing()) return 0;
    auto token = ++m_importSerial; if (!token) token = ++m_importSerial;
    m_importToken = token; m_importClaimed = false; m_importFromGallery = gallery;
    m_importGalleryGeneration = m_galleryGeneration; m_importOriginals.reset();
    m_importCaptureGeneration = m_generation; m_error.clear();
    m_importError.clear();
    if (gallery) m_galleryStatus = tr("Preparing originals…");
    m_importCancellation = std::make_shared<std::atomic_bool>(false);
    const QPointer<EditorCameraPhoto> owner(this); emit importStarted(token);
    if (!owner || m_importToken != token) return 0;
    emit changed(); if (!owner || m_importToken != token) return 0;
    emit galleryChanged(); return owner && m_importToken == token ? token : 0;
}
bool EditorCameraPhoto::claimImport(qulonglong token) {
    if (m_importToken != token || !token || m_importClaimed) return false;
    m_importClaimed = true; return true;
}
void EditorCameraPhoto::abandonImport(qulonglong token) {
    if (!token || token != m_importToken) return;
    if (m_importCancellation) m_importCancellation->store(true);
    if (m_importFromGallery && m_importGalleryGeneration == m_galleryGeneration) m_galleryStatus.clear();
    m_importToken = 0; m_importClaimed = false; m_importOriginals.reset(); m_importCancellation.reset();
}
void EditorCameraPhoto::cancelImport() {
    const auto token = m_importToken; if (!token) return;
    abandonImport(token); const QPointer<EditorCameraPhoto> owner(this);
    emit importCancelled(token); if (!owner || m_importSerial != token) return;
    emit changed(); if (owner && m_importSerial == token) emit galleryChanged();
}
void EditorCameraPhoto::finishImport(qulonglong token, const QString &error) {
    if (!token || token != m_importToken) return;
    auto originals = std::move(m_importOriginals);
    const bool gallery = m_importFromGallery && m_galleryGeneration == m_importGalleryGeneration;
    const bool currentCapture = m_importCaptureGeneration == m_generation;
    abandonImport(token); m_importError = error; if (currentCapture) m_error = error;
    if (gallery) m_galleryStatus = error;
    const QPointer<EditorCameraPhoto> owner(this);
    emit changed(); if (!owner || m_importSerial != token) return;
    emit galleryChanged(); if (!owner || m_importSerial != token || !error.isEmpty() || !originals) return;
    for (const auto &path : originals->paths) {
        emit importReady(QUrl::fromLocalFile(localPath(path)));
        if (!owner || m_importSerial != token) return;
    }
}
void EditorCameraPhoto::deliverOriginals(qulonglong token, dreamscapes::PhotoOriginals originals) {
    if (token != m_importToken) return;
    m_importOriginals = std::move(originals);
    const QPointer<EditorCameraPhoto> owner(this); const auto ready = *m_importOriginals;
    emit originalsReady(token, ready);
    if (owner && m_importToken == token && !m_importClaimed) finishImport(token, {});
}
bool EditorCameraPhoto::importSelection() {
    if (m_galleryBusy || importing() || !selectionCount()) return false;
    QVariantList selected; for (const auto &entry : m_gallery.items()) if (entry.toMap().value("selected").toBool()) selected.append(entry);
    const auto token = beginImport(true); if (!token) return false;
    auto *watcher = new QFutureWatcher<GalleryImportResult>(this);
    connect(watcher, &QFutureWatcher<GalleryImportResult>::finished, this, [this, watcher, token] {
        auto result = watcher->future().takeResult(); watcher->deleteLater(); if (token != m_importToken) return;
        if (result.cancelled) { cancelImport(); return; }
        if (!result.status.isEmpty()) { finishImport(token, result.status); return; }
        deliverOriginals(token, std::move(result.originals));
    });
    const auto cache = m_outputCache; const auto library = m_library; const auto cancelled = m_importCancellation;
    watcher->setFuture(enqueuePhotoWork([selected, cache, library, cancelled] {
        GalleryImportResult result; result.originals.sourceLease = cache;
        try {
            const auto outputFolder = [&] {
                const auto path = cache->filePath(std::filesystem::path(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdU16String()));
                iiFileProvider::native::File::createDirectories(path); return localPath(path);
            };
            for (const auto &entry : selected) {
                if (cancelled->load()) break;
                const auto item = entry.toMap(); auto url = item.value("source").toUrl();
                if (item.value("native").toBool()) {
                    const auto folder = outputFolder();
                    const auto name = QFileInfo(item.value("label").toString()).fileName();
                    const auto path = folder + '/' + (name.isEmpty() ? QStringLiteral("Photo.png") : name);
                    QString error;
                    if (!library->exportResource(item.value("id").toString(), item.value("token").toString(), path, &error)) {
                        result.status = error.isEmpty() ? tr("The original photo could not be exported.") : error; break;
                    }
                    url = QUrl::fromLocalFile(path);
                }
                if (cancelled->load()) break;
                QImageReader reader(url.toLocalFile()); const bool readable = reader.canRead();
                if (!readable || reader.transformation() != QImageIOHandler::TransformationNone) {
                    reader.setAutoTransform(true);
                    const auto decoded = readable ? reader.read() : editorDecodeOriginalPhoto(url.toLocalFile());
                    const auto folder = outputFolder(); const auto path = folder + '/' + QFileInfo(url.toLocalFile()).completeBaseName() + ".png";
                    if (decoded.isNull() || !iiFileProvider::File::saveImage(decoded, path, "PNG")) { result.status = tr("The selected original image could not be decoded on this device."); break; }
                    url = QUrl::fromLocalFile(path);
                }
                result.originals.paths.emplace_back(url.toLocalFile().toStdU16String());
            }
        } catch (const std::exception &failure) { result.status = tr("Could not prepare the original image for import: %1").arg(QString::fromUtf8(failure.what())); }
        result.cancelled = cancelled->load();
        if (result.cancelled || !result.status.isEmpty()) result.originals.paths.clear();
        return result;
    }));
    return true;
}
bool EditorCameraPhoto::importCurrentCapture() {
    if (m_busy || importing() || m_current.isEmpty()) return false;
    const auto source = m_current; const auto token = beginImport(false); if (!token) return false;
    dreamscapes::PhotoOriginals originals; originals.paths.emplace_back(source.toLocalFile().toStdU16String()); originals.sourceLease = m_outputCache;
    deliverOriginals(token, std::move(originals)); return true;
}
void EditorCameraPhoto::reportImportFailure(const QString &message) { m_error = message; emit changed(); }
