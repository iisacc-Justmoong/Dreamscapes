#pragma once
#include <QObject>
#include <QVariantMap>
#include <QImage>
#include <memory>
#include <iiSharedCanvas/Document/Document.h>
#include <iiSharedCanvas/Audio/AudioCodec.h>
class QCamera;
class QImageCapture;
class QMediaCaptureSession;
class EditorAudioPlayback;
class EditorMedia final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable NOTIFY cameraChanged)
    Q_PROPERTY(bool capturing READ capturing NOTIFY cameraChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY audioChanged)
    Q_PROPERTY(bool preparing READ preparing NOTIFY audioChanged)
    Q_PROPERTY(QObject *audioPlayback READ audioPlaybackObject CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit EditorMedia(QObject *parent = nullptr);
    ~EditorMedia() override;
    bool cameraAvailable() const;
    bool capturing() const { return m_pending; }
    bool playing() const;
    bool preparing() const;
    QString error() const;
    EditorAudioPlayback *audioPlayback() const {return m_audioPlayback.get();}
    QObject *audioPlaybackObject() const;
    Q_INVOKABLE bool capture(const QVariantMap &settings);
    qulonglong playTimeline(std::shared_ptr<const iiSharedCanvas::Document>,iiSharedCanvas::FrameIndex);
    qulonglong playAsset(std::shared_ptr<const iiSharedCanvas::Document>,const std::string &id);
    static iiSharedCanvas::AudioImportResult mixTimelineAudio(const iiSharedCanvas::Document &, iiSharedCanvas::FrameIndex firstFrame);
    Q_INVOKABLE void stop();
    Q_INVOKABLE QVariantList library(const QString &directory, const QVariantMap &settings) const;
signals:
    void changed();
    void cameraChanged();
    void audioChanged();
    void captured(const QImage &image);
private:
    void startCapture(const QVariantMap &settings);
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QImageCapture> m_capture;
    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<EditorAudioPlayback> m_audioPlayback;
    bool m_pending = false;
    QString m_error;
};
