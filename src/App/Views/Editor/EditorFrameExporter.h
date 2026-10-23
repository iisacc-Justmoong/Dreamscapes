#pragma once
#include "EditorFrameSnapshot.h"
#include <QObject>
#include <QFutureWatcher>
#include <QUrl>
#include <functional>
#include <optional>

// One accepted export owns detached inputs through completion, independently of
// a live canvas, file binding, dialog or current selection.
class EditorFrameExporter : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool prepared READ prepared NOTIFY stateChanged)
    Q_PROPERTY(QUrl destination READ destination NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
public:
    struct Request {
        EditorFrameSnapshot snapshot;
        QUrl destination;
        QString format;
        double pixelsPerInch=300;
    };
    using ExportOperation=std::function<QString(const Request &)>;
    explicit EditorFrameExporter(QObject *parent=nullptr);
    EditorFrameExporter(ExportOperation operation,QObject *parent);
    ~EditorFrameExporter() override;
    bool busy() const {return m_busy;}
    bool prepared() const {return m_prepared.has_value();}
    QUrl destination() const {return m_destination;}
    QString error() const {return m_error;}
    QString lastRejection() const {return m_rejection;}
    bool prepare(EditorFrameSnapshot snapshot,double pixelsPerInch);
    void cancelPrepared();
    // true accepts work; saved/failed is the terminal result. A rejected request
    // does not replace accepted inputs or emit an accepted job's failure signal.
    bool start(const QUrl &destination,const QString &format);
    static QString exportSnapshot(const Request &request);
signals:
    void stateChanged();
    void saved(const QUrl &destination);
    void failed(const QUrl &destination,const QString &message);
    void rejected(const QString &message);
private:
    bool reject(const QString &message);
    ExportOperation m_operation;
    QFutureWatcher<QString> m_completion;
    std::optional<Request> m_prepared;
    QUrl m_destination;
    QString m_error,m_rejection;
    qulonglong m_request=0;
    bool m_busy=false;
};
