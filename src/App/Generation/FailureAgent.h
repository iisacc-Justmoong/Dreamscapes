#pragma once
#include <QObject>
#include <QJsonObject>
#include <QQueue>
#include <QTimer>
#include <optional>
#include "GenerationProcess.h"

// Owns only diagnosis work. It has no generation/storage-controller callbacks,
// so analysis cannot retry jobs, mutate canvases, save revisions or block a queue.
class FailureAgent final : public QObject {
    Q_OBJECT
public:
    FailureAgent(QString executable, int timeoutMilliseconds, QObject* parent = nullptr);
    ~FailureAgent() override { close(); }
    QJsonObject submit(QJsonObject evidence, std::shared_ptr<void> files = {});
    void close();
signals:
    void started(const QString& jobId);
    void explained(const QString& jobId, const QJsonObject& report);
private:
    struct Request { QJsonObject evidence, baseline; std::shared_ptr<void> files; };
    void pump();
    void complete(QString error = {});
    void deliver(QJsonObject report, bool persist);
    QString m_executable;
    int m_timeoutMilliseconds;
    GenerationProcess m_process;
    QTimer m_timeout;
    QQueue<Request> m_queue;
    std::optional<Request> m_current;
    QByteArray m_output;
    bool m_closed = false;
};
