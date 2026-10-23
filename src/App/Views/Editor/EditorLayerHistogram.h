#pragma once

#include "EditorFrameSnapshot.h"
#include <QObject>
#include <QCache>
#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

class EditorCanvas;
Q_MOC_INCLUDE("App/Views/Editor/EditorCanvas.h")

// A presentation owner per panel, independent of document/file commands.
// Workers receive detached native values and never retain a live canvas.
class EditorLayerHistogram : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *canvas READ canvas WRITE setCanvas NOTIFY changed)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
    Q_PROPERTY(bool composite READ composite WRITE setComposite NOTIFY changed)
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY changed)
    Q_PROPERTY(QVariantMap histogram READ histogram NOTIFY changed)
public:
    struct Channel {
        std::array<std::uint64_t, 256> bins{};
        double mean = 0, standardDeviation = 0;
    };
    struct Result {
        // Encoded R/G/B and the existing integer (11R+16G+5B)/32 gray.
        std::array<Channel, 4> channels;
        std::uint64_t pixelCount = 0;
        std::string error;
        bool cancelled = false;
    };
    struct Visibility { std::vector<bool> artboards, layers; };
    struct Request {
        EditorFrameSnapshot snapshot;
        std::string selectedLayer;
        bool composite = true;
        Visibility visibility;
        std::shared_ptr<std::atomic_bool> cancelled;
    };
    using Operation = std::function<Result(const Request &)>;
    explicit EditorLayerHistogram(QObject *parent = nullptr);
    EditorLayerHistogram(Operation operation, QObject *parent);
    ~EditorLayerHistogram() override;

    EditorCanvas *canvas() const;
    bool active() const { return m_active; }
    bool composite() const { return m_composite; }
    QString channel() const { return m_channel; }
    QVariantMap histogram() const { return m_histogram; }
    void setCanvas(EditorCanvas *canvas);
    void setActive(bool active);
    void setComposite(bool composite);
    void setChannel(const QString &channel);
    Q_INVOKABLE void refresh();

    static Result calculate(const Request &request);
    // Synchronous native utility; not a QML/view dispatch API.
    static Result analyze(const iiSharedCanvas::Document &document,
        iiSharedCanvas::FrameIndex frame, const std::string &artboard,
        bool composite, const std::string &selection);
    static QVariantMap describe(const Result &result, const QString &channel,
        bool available, bool pending = false);
signals:
    void changed();
private:
    struct Job { QString key; Request request; qulonglong epoch = 0; };
    bool available() const;
    void retire(bool clearCache);
    void invalidate(bool clearCache = false, bool configurationChanged = false);
    bool project();
    void prepare();
    void launch();
    void complete();
    QPointer<EditorCanvas> m_canvas;
    Operation m_operation;
    QTimer m_delay;
    QFutureWatcher<Result> m_completion;
    QCache<QString, Result> m_cache{32};
    std::optional<Job> m_running, m_pending;
    std::optional<Result> m_result;
    QString m_wanted, m_observedSelection, m_channel = QStringLiteral("RGB");
    QVariantMap m_histogram;
    qulonglong m_epoch = 0;
    bool m_active = false, m_composite = true, m_waiting = false, m_timeDependent = true;
};
