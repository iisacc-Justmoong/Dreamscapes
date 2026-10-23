#pragma once
#include <iiSharedCanvas/Document/Document.h>
#include <QObject>
#include <QCache>
#include <QFutureWatcher>
#include <QHash>
#include <QUrl>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <vector>

// Page presentation consumes the whole document, independently of the active
// artboard's canvas render focus. Requests never own a live canvas or file.
class EditorPagePreviews : public QObject {
    Q_OBJECT
public:
    struct Visibility {
        std::vector<bool> artboards,layers;
    };
    struct Request {
        std::shared_ptr<const iiSharedCanvas::Document> document;
        iiSharedCanvas::FrameIndex frame = 0;
        std::string artboardId;
        iiSharedCanvas::CanvasExtent output;
        int page = 0;
        std::shared_ptr<const Visibility> visibility;
    };
    using Operation = std::function<QUrl(const Request &)>;
    explicit EditorPagePreviews(Operation operation = render, QObject *parent = nullptr);
    ~EditorPagePreviews() override;
    // An uncached request returns empty. ready(page) lets that page re-read its
    // result; a cached empty result also suppresses repeated failed rendering.
    QUrl source(const QString &key, Request request);
    void cancelPending();
    void invalidate();
    static QUrl render(const Request &request);
signals:
    void ready(int page);
private:
    struct Job {QString slot,key;Request request;qulonglong epoch;};
    void launchPending();
    void complete();
    Operation m_operation;
    QFutureWatcher<QUrl> m_completion;
    QCache<QString,QUrl> m_cache{32*1024*1024};
    QHash<QString,QString> m_latest;
    std::list<Job> m_pending;
    std::optional<Job> m_running;
    qulonglong m_epoch = 0;
};
