#pragma once

#include "EditorFrameSnapshot.h"
#include <QObject>
#include <QFutureWatcher>
#include <functional>
#include <optional>
#include <stop_token>
#include <vector>

// Owns one point read, independent of the live canvas and document commands.
// Only detached native values cross the worker boundary.
class EditorColorSampler : public QObject {
    Q_OBJECT
public:
    struct Visibility { std::vector<bool> artboards, layers; };
    struct Request {
        EditorFrameSnapshot snapshot;
        iiSharedCanvas::CanvasOrigin worldPosition;
        Visibility visibility;
    };
    struct Result {
        std::uint32_t argb = 0;
        std::string error;
        bool cancelled = false;
        bool ok() const { return !cancelled && error.empty(); }
    };
    using Operation = std::function<Result(const Request &, std::stop_token)>;
    explicit EditorColorSampler(QObject *parent = nullptr);
    EditorColorSampler(Operation operation, QObject *parent);
    ~EditorColorSampler() override;

    qulonglong submit(Request request);
    bool cancel();
    bool active() const { return m_wanted != 0; }
    static Result sample(const Request &request, std::stop_token stop = {});
signals:
    void finished(qulonglong token, quint32 argb, QString error, bool cancelled);
private:
    struct Job {
        qulonglong token;
        std::shared_ptr<const Request> input;
        std::stop_source stop;
    };
    void launch();
    void complete();
    Operation m_operation;
    QFutureWatcher<Result> m_completion;
    std::optional<Job> m_running, m_pending;
    qulonglong m_serial = 0, m_wanted = 0;
};
