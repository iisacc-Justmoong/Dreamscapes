#pragma once

#include "EditorFrameSnapshot.h"
#include <QFutureWatcher>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QString>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>

class EditorCanvas;
class QQuickWindow;
Q_MOC_INCLUDE("EditorCanvas.h")

// A viewport-sized display surface. Only immutable value snapshots enter work.
class EditorClippingOverlay : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *canvas READ canvas WRITE setCanvas NOTIFY canvasChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
public:
    struct Request {
        EditorFrameSnapshot snapshot;
        int white = 255, black = 0;
        bool highlights = false, shadows = false;
    };
    struct Result { QImage image; QString error; };
    using RenderOperation = std::function<Result(const Request &)>;
    explicit EditorClippingOverlay(QQuickItem *parent = nullptr);
    EditorClippingOverlay(RenderOperation operation, QQuickItem *parent);
    ~EditorClippingOverlay() override;
    EditorCanvas *canvas() const;
    void setCanvas(EditorCanvas *canvas);
    bool busy() const { return m_busy; }
    bool ready() const { return !m_key.isEmpty() && !m_busy && m_error.isEmpty(); }
    bool available() const { return !m_image.isNull(); }
    QString error() const { return m_error; }
    QImage image() const { return m_image; }
    static Result render(const Request &request);
    void paint(QPainter *painter) override;
signals:
    void canvasChanged();
    void stateChanged();
    void previewFailed(const QString &message);
private:
    struct Work { Request request; quint64 generation = 0; };
    QString contextKey() const;
    void refreshIntent();
    void prepare();
    void start(Work work);
    void finish();
    QPointer<EditorCanvas> m_canvas;
    QPointer<QQuickWindow> m_observedWindow;
    RenderOperation m_operation;
    QFutureWatcher<Result> m_completion;
    std::optional<Work> m_pending;
    QImage m_image;
    QString m_key, m_error;
    quint64 m_documentEpoch = 0, m_generation = 0, m_runningGeneration = 0;
    bool m_busy = false, m_running = false, m_prepareScheduled = false;
};
