#pragma once
#include "EditorGraphicImage.h"
#include <QObject>
#include <QFutureWatcher>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <optional>
class EditorCanvas;
Q_MOC_INCLUDE("App/Views/Editor/EditorCanvas.h")

class EditorGraphicCommand : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *canvas READ canvas WRITE setCanvas NOTIFY changed)
    Q_PROPERTY(QVariantMap context READ context WRITE setContext NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
public:
    using Input = graphicImage::Input;
    using Result = graphicImage::Result;
    using Operation = std::function<Result(const Input &, std::stop_token)>;
    explicit EditorGraphicCommand(QObject *parent = nullptr);
    EditorGraphicCommand(Operation operation, QObject *parent);
    ~EditorGraphicCommand() override;
    EditorCanvas *canvas() const;
    QVariantMap context() const { return m_context; }
    QVariantMap state() const { return m_state; }
    void setCanvas(EditorCanvas *canvas);
    void setContext(const QVariantMap &context);
    Q_INVOKABLE bool contextMatches(const QVariantMap &context) const { return context == m_context; }
    Q_INVOKABLE qulonglong request(const QString &action, const QString &tool, const QString &operation, const QVariantMap &parameters);
    Q_INVOKABLE bool cancel();
signals:
    void changed();
private:
    struct Job { qulonglong token; QString action; std::shared_ptr<const Input> input; std::stop_source stop; };
    void retire();
    void invalidate(bool notify = false);
    void project(const QString &status, const QString &error = {}, const Result *result = nullptr);
    void launch();
    void complete();
    const Job *current() const;
    Operation m_operation;
    QFutureWatcher<Result> m_completion;
    QPointer<EditorCanvas> m_canvas;
    QVariantMap m_context, m_state;
    std::optional<Job> m_running, m_pending;
    qulonglong m_serial = 0, m_wanted = 0, m_sequence = 0;
    QString m_action, m_requestedOperation;
    bool m_adopting = false;
    std::uint64_t m_adoptingDocumentSequence = 0;
};
