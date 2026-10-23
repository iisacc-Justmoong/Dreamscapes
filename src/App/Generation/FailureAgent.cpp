#include "FailureAgent.h"
#include "FailureEvidence.h"
#include "FailureReportStorage.h"
#include <QJsonDocument>
#include <QPointer>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <utility>

FailureAgent::FailureAgent(QString executable, int timeoutMilliseconds, QObject* parent)
    : QObject(parent), m_executable(std::move(executable)), m_timeoutMilliseconds(std::max(1, timeoutMilliseconds)) {
    m_timeout.setSingleShot(true);
    connect(&m_process, &GenerationProcess::started, this, [this] {
        if (!m_current) return;
        m_process.write(QJsonDocument(m_current->evidence).toJson(QJsonDocument::Compact) + '\n');
        m_process.releaseInput();
        emit started(m_current->baseline["jobId"].toString());
    });
    connect(&m_process, &GenerationProcess::outputAvailable, this, [this](const QByteArray& data) {
        m_output += data;
        if (m_output.size() > 512 * 1024) { m_process.cancel(); complete(tr("The failure agent exceeded its output limit.")); }
    });
    connect(&m_process, &GenerationProcess::finished, this, [this](qint64 code, auto) {
        if (m_current) complete(code == 0 ? QString() : tr("The failure agent exited with code %1.").arg(code));
        pump();
    });
    connect(&m_process, &GenerationProcess::errorOccurred, this, [this](auto) { complete(m_process.errorString()); });
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_process.cancel(); complete(tr("The failure agent exceeded its analysis deadline. The evidence report remains available."));
    });
}
QJsonObject FailureAgent::submit(QJsonObject evidence, std::shared_ptr<void> files) {
    auto baseline = dreamscapes::failureEvidenceReport(evidence);
#if defined(Q_OS_IOS) || defined(Q_OS_ANDROID) || defined(Q_OS_WASM)
    baseline["state"] = "evidence-only";
    baseline["modelUnavailableReason"] = tr("This build provides native evidence diagnostics. An external explanation process is unavailable on this platform.");
    if (!m_closed) deliver(baseline, true);
    return baseline;
#endif
    if (m_closed || m_queue.size() >= 8) {
        baseline["state"] = "evidence-only";
        baseline["agentError"] = tr("The diagnostic queue is unavailable or full. The original failure and evidence are retained.");
        if (!m_closed) deliver(baseline, true);
        return baseline;
    }
    m_queue.enqueue({std::move(evidence), baseline, std::move(files)});
    QTimer::singleShot(0, this, &FailureAgent::pump);
    return baseline;
}
void FailureAgent::pump() {
    if (m_closed || m_current || m_queue.isEmpty() || m_process.state() != GenerationProcess::NotRunning) return;
    m_current = m_queue.dequeue(); m_output.clear();
    m_timeout.start(m_timeoutMilliseconds);
    m_process.start(m_executable, {}, m_current->files);
}
void FailureAgent::complete(QString error) {
    if (!m_current) return;
    m_timeout.stop();
    auto report = m_current->baseline;
    if (error.isEmpty()) {
        // Native runtimes may log on stdout; only the tagged, bounded object is a reply.
        bool received = false;
        for (const auto& line : m_output.split('\n')) if (line.startsWith("DREAMSCAPES_FAILURE_REPORT ")) {
            const auto reply = QJsonDocument::fromJson(line.mid(27)).object();
            if (reply["schema"] == report["schema"] && reply["jobId"] == report["jobId"] && !reply["details"].toString().isEmpty()) {
                report = reply; received = true;
            }
        }
        if (!received) error = tr("The failure agent returned no valid report.");
    }
    if (!error.isEmpty()) { report["state"] = "evidence-only"; report["agentError"] = error; }
    auto files = std::move(m_current->files); m_current.reset();
    if (files) (void)QtConcurrent::run([files = std::move(files)] {});
    const QPointer<FailureAgent> owner(this);
    deliver(report, !error.isEmpty());
    if (owner) QTimer::singleShot(0, this, &FailureAgent::pump);
}
void FailureAgent::deliver(QJsonObject report, bool persist) {
    if (persist && !report["reportPath"].toString().isEmpty()) {
        auto* watcher = new QFutureWatcher<QJsonObject>(this);
        connect(watcher, &QFutureWatcher<QJsonObject>::finished, this, [this, watcher] {
            const auto saved = watcher->result(); watcher->deleteLater();
            if (!m_closed) emit explained(saved["jobId"].toString(), saved);
        });
        watcher->setFuture(QtConcurrent::run([report = std::move(report)] { return dreamscapes::persistFailureReport(report); }));
    } else emit explained(report["jobId"].toString(), report);
}
void FailureAgent::close() {
    if (m_closed) return;
    m_closed = true; m_timeout.stop();
    auto queued = std::exchange(m_queue, {}); auto current = std::exchange(m_current, {});
    if (!queued.isEmpty() || current) (void)QtConcurrent::run([queued = std::move(queued), current = std::move(current)] {});
    disconnect(&m_process, nullptr, this, nullptr); m_process.retire();
}
