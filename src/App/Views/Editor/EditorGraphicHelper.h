#pragma once

#include "EditorFrameSnapshot.h"
#include "EditorRasterSource.h"
#include "EditorGraphicHelperValues.h"
#include <QObject>
#include <QFutureWatcher>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>

class EditorCanvas;
Q_MOC_INCLUDE("App/Views/Editor/EditorCanvas.h")

// A panel owns parameter calculations. Workers own only detached native input.
class EditorGraphicHelper : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *canvas READ canvas WRITE setCanvas NOTIFY changed)
    Q_PROPERTY(QVariantMap context READ context WRITE setContext NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
public:
    struct Request {
        graphicHelper::Request calculation;
        EditorFrameSnapshot snapshot;
        std::string selectedLayer;
        std::shared_ptr<const RasterLayer> previewPixels;
    };
    using Result = graphicHelper::Result;
    using Operation = std::function<Result(const Request &, std::stop_token)>;
    explicit EditorGraphicHelper(QObject *parent = nullptr);
    EditorGraphicHelper(Operation operation, QObject *parent);
    ~EditorGraphicHelper() override;
    EditorCanvas *canvas() const;
    QVariantMap context() const { return m_context; }
    QVariantMap state() const { return m_state; }
    void setCanvas(EditorCanvas *canvas);
    void setContext(const QVariantMap &context);
    Q_INVOKABLE bool contextMatches(const QVariantMap &context) const { return context == m_context; }
    Q_INVOKABLE qulonglong request(const QString &operation, const QVariantMap &parameters);
    Q_INVOKABLE bool cancel();
    static Result calculate(const Request &request, std::stop_token stop = {});
    static constexpr std::uint64_t MaximumChunkedPixels = editorRaster::MaximumContiguousPixels;
signals:
    void changed();
private:
    struct Job { qulonglong token; std::shared_ptr<const Request> input; std::stop_source stop; };
    void retire();
    void invalidate(bool forceNotify = false);
    void launch();
    void complete();
    bool project(const QString &status, const QString &error = {}, QVariantMap parameters = {});
    Operation m_operation;
    QFutureWatcher<Result> m_completion;
    QPointer<EditorCanvas> m_canvas;
    QVariantMap m_context, m_state;
    QString m_requestedOperation, m_observedTool;
    std::optional<Job> m_running, m_pending;
    qulonglong m_serial = 0, m_wanted = 0, m_sequence = 0;
};
