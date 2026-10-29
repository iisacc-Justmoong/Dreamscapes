#pragma once
#include "EditorCaptureNative.h"
#include "../../ViewModels/Native/PhotoPresentation.h"
#include "EditorPhotoImport.h"
#include "EditorPhotoGalleryModel.h"
#include <QObject>
#include <QUrl>
#include <QTimer>
#include <QFuture>
#include <QPointer>
#include <QVariantList>
#include <functional>
#include <memory>
#include <optional>
#include <exception>
#include <type_traits>
#include <iiPhotoLibrary/PhotoLibrary.h>

class QCamera;
class QImageCapture;
class QMediaCaptureSession;
class QMediaRecorder;
class QMediaDevices;

class EditorCameraPhoto final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY changed)
    Q_PROPERTY(QString cameraStatus READ cameraStatus NOTIFY changed)
    Q_PROPERTY(QString cameraDescription READ cameraDescription NOTIFY changed)
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable NOTIFY changed)
    Q_PROPERTY(bool previewActive READ previewActive NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(qint64 recordingDuration READ recordingDuration NOTIFY changed)
    Q_PROPERTY(int countdown READ countdown NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantList captureTargets READ captureTargets NOTIFY changed)
    Q_PROPERTY(bool screenCaptureSupported READ screenCaptureSupported CONSTANT)
    Q_PROPERTY(QUrl currentCapture READ currentCapture NOTIFY changed)
    Q_PROPERTY(QUrl captureThumbnail READ captureThumbnail NOTIFY changed)
    Q_PROPERTY(QVariantList gallery READ gallery NOTIFY galleryChanged)
    Q_PROPERTY(QAbstractItemModel *galleryModel READ galleryModel CONSTANT)
    Q_PROPERTY(QString galleryStatus READ galleryStatus NOTIFY galleryChanged)
    Q_PROPERTY(bool galleryBusy READ galleryBusy NOTIFY galleryChanged)
    Q_PROPERTY(bool galleryAuthorizationRequired READ galleryAuthorizationRequired NOTIFY galleryChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY galleryChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY changed)
    Q_PROPERTY(QString importError READ importError NOTIFY changed)
public:
    using ScreenOperation = std::function<void(const QVariantMap &, CaptureImageReply)>;
    using DeviceMonitorFactory = std::function<std::unique_ptr<QMediaDevices>()>;
    explicit EditorCameraPhoto(QObject *parent = nullptr);
    EditorCameraPhoto(std::shared_ptr<iiPhotoLibrary::PhotoLibrary>, QObject *parent);
    EditorCameraPhoto(std::shared_ptr<iiPhotoLibrary::PhotoLibrary>, ScreenOperation, QObject *parent);
    EditorCameraPhoto(std::shared_ptr<iiPhotoLibrary::PhotoLibrary>, ScreenOperation, DeviceMonitorFactory, QObject *parent);
    ~EditorCameraPhoto() override;
    QVariantList devices() const;
    QString deviceId() const;
    QString cameraStatus() const;
    QString cameraDescription() const;
    bool cameraAvailable() const;
    bool previewActive() const;
    bool busy() const { return m_busy; }
    bool recording() const;
    qint64 recordingDuration() const;
    int countdown() const { return m_countdown; }
    QString error() const { return m_error; }
    QVariantList captureTargets() const { return m_targets; }
    bool screenCaptureSupported() const { return bool(m_screenOperation); }
    QUrl currentCapture() const { return m_current; }
    QUrl captureThumbnail() const { return m_thumbnail; }
    QVariantList gallery() const;
    QAbstractItemModel *galleryModel() { return &m_gallery; }
    QString galleryStatus() const { return m_galleryStatus; }
    bool galleryBusy() const { return m_galleryBusy || importing(); }
    bool importing() const { return m_importToken != 0; }
    QString importError() const { return m_importError; }
    bool galleryAuthorizationRequired() const;
    int selectionCount() const;
    Q_INVOKABLE QVariantMap presentation(const QVariantMap &settings) const;
    Q_INVOKABLE QVariantMap captureSettings(const QVariantMap &settings, const QVariantList &region = {}) const;
    Q_INVOKABLE void refreshGalleryFor(QObject *presenter, const QVariantMap &settings);
    Q_INVOKABLE bool importFor(const QString &view);
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void attachPreview(QObject *output);
    Q_INVOKABLE bool detachPreview(QObject *expectedOutput);
    Q_INVOKABLE void setPreviewEnabled(bool enabled, const QVariantMap &settings);
    Q_INVOKABLE bool startPreview(const QVariantMap &settings);
    Q_INVOKABLE bool capturePhoto(const QVariantMap &settings);
    Q_INVOKABLE bool toggleVideo(const QVariantMap &settings);
    Q_INVOKABLE void refreshCaptureTargets();
    Q_INVOKABLE bool captureScreen(const QVariantMap &settings);
    Q_INVOKABLE void loadGallery(const QString &source, const QUrl &folder = {});
    Q_INVOKABLE void attachGallery(QObject *presenter);
    Q_INVOKABLE bool detachGallery(QObject *expectedPresenter);
    Q_INVOKABLE void requestGalleryAccess();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void filterGallery(const QString &query, const QString &sort);
    Q_INVOKABLE void requestThumbnail(const QString &id);
    Q_INVOKABLE void setSelected(const QString &id, bool multiple);
    Q_INVOKABLE void selectAll(bool multiple);
    Q_INVOKABLE void retainSingleSelection();
    Q_INVOKABLE bool importSelection();
    Q_INVOKABLE bool importCurrentCapture();
    Q_INVOKABLE void cancelImport();
    // One native placement owner claims an accepted token during importStarted.
    bool claimImport(qulonglong token);
    void finishImport(qulonglong token, const QString &error);
    void abandonImport(qulonglong token);
    Q_INVOKABLE void cancel();
    void reportImportFailure(const QString &message);
    // Pixel transforms are shared by photo capture, preview thumbnails and tests.
    static QImage frameForSettings(const QImage &, const QVariantMap &);
signals:
    void changed();
    void galleryChanged();
    void importReady(const QUrl &);
    void importStarted(qulonglong token);
    void originalsReady(qulonglong token, const dreamscapes::PhotoOriginals &originals);
    void importCancelled(qulonglong token);
private:
    dreamscapes::vm::PhotoPresentation m_presentation;
    template<class Function> auto enqueuePhotoWork(Function function, std::shared_ptr<const std::atomic_bool> obsolete = {}) {
        using Result = std::invoke_result_t<Function>;
        auto next = m_photoTail.then(QtFuture::Launch::Async, [closed = m_photoClosed, obsolete = std::move(obsolete), function = std::move(function)]() mutable -> Result {
            if (closed->load() || (obsolete && obsolete->load())) return {};
            try { return function(); }
            catch (const std::exception &failure) {
                auto error = QString::fromUtf8(failure.what());
                if (error.isEmpty()) error = QStringLiteral("Photo work failed.");
                if constexpr (std::is_same_v<Result, QString>) return error;
                else { Result result; result.status = error; return result; }
            } catch (...) {
                if constexpr (std::is_same_v<Result, QString>) return QStringLiteral("Photo work failed.");
                else { Result result; result.status = QStringLiteral("Photo work failed."); return result; }
            }
        });
        // A value-only continuation chain preserves FIFO without a joining pool
        // owned by a visible object. View tokens skip obsolete reads before SDK
        // entry; accepted file commands omit them. Failed work cannot poison retries.
        m_photoTail = next.then(QtFuture::Launch::Sync, [](QFuture<Result>) {}).onFailed([] {}).onCanceled([] {});
        return next;
    }
    qulonglong beginImport(bool gallery);
    void deliverOriginals(qulonglong token, dreamscapes::PhotoOriginals);
    struct OutputCache;
    void ensureDeviceMonitor() const;
    void ensureCamera();
    void releasePreview();
    bool authorizeCamera(std::function<void()>);
    void configureCamera(const QVariantMap &);
    void takeNextPhoto();
    void storeCapture(const QImage &, bool cameraTransform);
    void finishVideo();
    void saveCaptureInPhotos(const QUrl &);
    void flushPhotoSaves();
    void retireGallery();
    void fail(const QString &);
    void delayed(int seconds, std::function<void()>);
    QString newPath(const QString &suffix, QString *error) const;
    EditorPhotoGalleryModel m_gallery{this};
    std::shared_ptr<OutputCache> m_outputCache;
    std::shared_ptr<iiPhotoLibrary::PhotoLibrary> m_library;
    ScreenOperation m_screenOperation;
    DeviceMonitorFactory m_deviceMonitorFactory;
    mutable std::unique_ptr<QMediaDevices> m_devices;
    mutable bool m_deviceMonitorRequested = false;
    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QImageCapture> m_imageCapture;
    std::unique_ptr<QMediaRecorder> m_recorder;
    QPointer<QObject> m_videoOutput;
    qulonglong m_previewOutputGeneration = 0;
    QTimer m_timer;
    std::function<void()> m_delayed;
    QVariantMap m_settings;
    QVariantList m_targets;
    QString m_selectedDevice, m_error, m_galleryStatus;
    QString m_gallerySource;
    QString m_importError;
    QUrl m_galleryFolder;
    QUrl m_current, m_thumbnail, m_video;
    QList<QUrl> m_pendingPhotoSaves;
    int m_countdown = 0, m_remainingPhotos = 0, m_shotId = -1;
    quint64 m_generation = 0, m_galleryGeneration = 0;
    bool m_busy = false, m_previewWanted = false, m_galleryBusy = false;
    bool m_photoAuthorizationPending = false;
    bool m_galleryAuthorizationPending = false;
    quint64 m_galleryAuthorizationSequence = 0;
    QPointer<QObject> m_galleryPresenter;
    QMetaObject::Connection m_galleryPresenterDestroyed;
    quint64 m_galleryPresenterSequence = 0;
    bool m_galleryReadWanted = false;
    QFuture<void> m_photoTail;
    std::shared_ptr<std::atomic_bool> m_photoClosed = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<std::atomic_bool> m_galleryCancellation;
    std::shared_ptr<std::atomic_bool> m_importCancellation;
    std::shared_ptr<std::atomic_bool> m_captureCancellation;
    bool m_storingPhoto = false;
    std::optional<dreamscapes::PhotoOriginals> m_importOriginals;
    qulonglong m_importToken = 0, m_importSerial = 0, m_importGalleryGeneration = 0, m_importCaptureGeneration = 0;
    bool m_importClaimed = false, m_importFromGallery = false;
};
Q_DECLARE_METATYPE(dreamscapes::PhotoOriginals)

