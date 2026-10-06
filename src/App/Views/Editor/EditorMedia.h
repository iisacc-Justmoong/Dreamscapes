#pragma once
#include <QObject>
#include <QVariantMap>
#include <QBuffer>
#include <QImage>
#include <memory>
#include <iiSharedCanvas/Document/Document.h>
#include <iiSharedCanvas/Audio/AudioCodec.h>
class QCamera;
class QImageCapture;
class QMediaCaptureSession;
class QAudioSink;
class EditorMedia final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool cameraAvailable READ cameraAvailable NOTIFY changed)
    Q_PROPERTY(bool capturing READ capturing NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit EditorMedia(QObject *parent = nullptr);
    ~EditorMedia() override;
    bool cameraAvailable() const;
    bool capturing() const { return m_pending; }
    bool playing() const;
    QString error() const { return m_error; }
    Q_INVOKABLE bool capture(const QVariantMap &settings);
    bool play(const iiSharedCanvas::AudioAsset &audio);
    static iiSharedCanvas::AudioImportResult mixTimelineAudio(const iiSharedCanvas::Document &, iiSharedCanvas::FrameIndex firstFrame);
    Q_INVOKABLE void stop();
    Q_INVOKABLE QVariantList library(const QString &directory, const QVariantMap &settings) const;
signals:
    void changed();
    void captured(const QImage &image);
private:
    void startCapture(const QVariantMap &settings);
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QImageCapture> m_capture;
    std::unique_ptr<QMediaCaptureSession> m_session;
    std::unique_ptr<QAudioSink> m_sink;
    QBuffer m_pcm;
    bool m_pending = false;
    QString m_error;
};
