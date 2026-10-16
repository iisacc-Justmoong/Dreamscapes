#include <iiFileProviderDevice.h>
#pragma once
#include "EditorCaptureNative.h"
#include <QObject>
#include <QUrl>
#include <QTimer>
#include <QThreadPool>
#include <QVariantList>
#include <memory>
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
    Q_PROPERTY(QString galleryStatus READ galleryStatus NOTIFY galleryChanged)
    Q_PROPERTY(bool galleryBusy READ galleryBusy NOTIFY galleryChanged)
    Q_PROPERTY(bool galleryAuthorizationRequired READ galleryAuthorizationRequired NOTIFY galleryChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY galleryChanged)
public:
    using ScreenOperation = std::function<void(const QVariantMap &, CaptureImageReply)>;
    explicit EditorCameraPhoto(QObject *parent = nullptr);
    EditorCameraPhoto(std::shared_ptr<iiPhotoLibrary::PhotoLibrary>, QObject *parent);
    EditorCameraPhoto(std::shared_ptr<iiPhotoLibrary::PhotoLibrary>, ScreenOperation, QObject *parent);
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
    QString galleryStatus() const { return m_galleryStatus; }
    bool galleryBusy() const { return m_galleryBusy; }
    bool galleryAuthorizationRequired() const;
    int selectionCount() const;
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void attachPreview(QObject *output);
    Q_INVOKABLE void setPreviewEnabled(bool enabled, const QVariantMap &settings);
    Q_INVOKABLE bool startPreview(const QVariantMap &settings);
    Q_INVOKABLE bool capturePhoto(const QVariantMap &settings);
    Q_INVOKABLE bool toggleVideo(const QVariantMap &settings);
    Q_INVOKABLE void refreshCaptureTargets();
    Q_INVOKABLE bool captureScreen(const QVariantMap &settings);
    Q_INVOKABLE void loadGallery(const QString &source, const QUrl &folder = {});
    Q_INVOKABLE void requestGalleryAccess();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void filterGallery(const QString &query, const QString &sort);
    Q_INVOKABLE void requestThumbnail(const QString &id);
    Q_INVOKABLE void setSelected(const QString &id, bool multiple);
    Q_INVOKABLE void selectAll(bool multiple);
    Q_INVOKABLE void retainSingleSelection();
    Q_INVOKABLE bool importSelection();
    Q_INVOKABLE bool importCurrentCapture();
    Q_INVOKABLE void cancel();
    void reportImportFailure(const QString &message);
    // Pixel transforms are shared by photo capture, preview thumbnails and tests.
    static QImage frameForSettings(const QImage &, const QVariantMap &);
signals:
    void changed();
    void galleryChanged();
    void importReady(const QUrl &);
private:
    void ensureCamera();
    bool authorizeCamera(std::function<void()>);
    void configureCamera(const QVariantMap &);
    void takeNextPhoto();
    void storeCapture(const QImage &, const QString &prefix);
    void finishVideo();
    void saveCaptureInPhotos(const QUrl &);
    void flushPhotoSaves();
    void fail(const QString &);
    void delayed(int seconds, std::function<void()>);
    QString newPath(const QString &suffix) const;
    iiFileProvider::TemporaryDirectory m_directory;
    std::shared_ptr<iiPhotoLibrary::PhotoLibrary> m_library;
    ScreenOperation m_screenOperation;
    std::unique_ptr<QMediaDevices> m_devices;
    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QImageCapture> m_imageCapture;
    std::unique_ptr<QMediaRecorder> m_recorder;
    QObject *m_videoOutput = nullptr;
    QTimer m_timer;
    std::function<void()> m_delayed;
    QVariantMap m_settings;
    QVariantList m_targets, m_assets;
    QString m_selectedDevice, m_error, m_query, m_sort = "Newest", m_galleryStatus;
    QString m_gallerySource;
    QUrl m_galleryFolder;
    QUrl m_current, m_thumbnail, m_video;
    QList<QUrl> m_pendingPhotoSaves;
    int m_countdown = 0, m_remainingPhotos = 0, m_shotId = -1;
    quint64 m_generation = 0, m_galleryGeneration = 0;
    bool m_busy = false, m_previewWanted = false, m_galleryBusy = false;
    bool m_photoAuthorizationPending = false;
    bool m_galleryAuthorizationPending = false;
    QThreadPool m_photoPool;
};

