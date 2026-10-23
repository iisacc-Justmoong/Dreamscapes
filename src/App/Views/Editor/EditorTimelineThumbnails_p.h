#pragma once

#include <algorithm>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtConcurrentRun>
#include <exception>
#include <functional>
#include <list>
#include <optional>
#include <utility>

// One native renderer, with at most one latest pending thumbnail per layer.
// Replacing a pending request keeps its FIFO position so other rows do not starve.
class EditorTimelineThumbnails final : public QObject {
public:
    using Operation=std::function<QUrl()>;
    using Completion=std::function<void(const QUrl &)>;
    EditorTimelineThumbnails() {
        connect(&m_completion,&QFutureWatcher<QUrl>::finished,this,[this] { complete(); });
    }
    ~EditorTimelineThumbnails() {
        disconnect(&m_completion,nullptr,this,nullptr);
        // The future owns only the render operation and its immutable snapshot.
        // Closing a view drops delivery without joining optional work on the GUI.
    }
    QString request(QString layer,Operation render,Completion deliver) {
        QString discarded;
        auto it=std::find_if(m_pending.begin(),m_pending.end(),[&](const Job &job){return job.layer==layer;});
        Job job{std::move(layer),std::move(render),std::move(deliver),m_epoch};
        if(it!=m_pending.end())*it=std::move(job);
        else {
            if(m_pending.size()==128){discarded=m_pending.front().layer;m_pending.pop_front();}
            m_pending.push_back(std::move(job));
        }
        launchPending();return discarded;
    }
    void cancel(const QString &layer) {
        m_pending.remove_if([&](const Job &job){return job.layer==layer;});
    }
    void cancelPending() { ++m_epoch;m_pending.clear(); }
private:
    struct Job {QString layer;Operation render;Completion deliver;qulonglong epoch;};
    void launchPending() {
        if(m_running||m_pending.empty())return;
        m_running=std::move(m_pending.front());m_pending.pop_front();
        m_completion.setFuture(QtConcurrent::run([operation=m_running->render] {
            try {return operation();}catch(const std::exception &){return QUrl{};}catch(...){return QUrl{};}
        }));
    }
    void complete() {
        auto job=std::move(*m_running);m_running.reset();
        const auto result=m_completion.result();const bool current=job.epoch==m_epoch;
        launchPending();
        // Delivery can synchronously delete the model and this queue. Publish last.
        if(current&&job.deliver)job.deliver(result);
    }
    QFutureWatcher<QUrl> m_completion;
    std::list<Job> m_pending;
    std::optional<Job> m_running;
    qulonglong m_epoch=0;
};
