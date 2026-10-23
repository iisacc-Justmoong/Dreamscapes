#include "App/Views/Editor/EditorTimelineThumbnails_p.h"
#include <QElapsedTimer>
#include <QtTest>
#include <atomic>
#include <memory>
#include <semaphore>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {
struct Gate {
    std::atomic_bool started{false},finished{false};
    std::counting_semaphore<100> release{0};
    std::mutex mutex;
    std::vector<int> rendered;
};
struct Release {std::shared_ptr<Gate> gate;~Release(){gate->release.release();}};
EditorTimelineThumbnails::Operation operation(const std::shared_ptr<Gate> &gate,int frame) {
    return [gate,frame] {
        if(frame==0){gate->started=true;(void)gate->release.try_acquire_for(std::chrono::seconds(5));}
        {std::lock_guard lock(gate->mutex);gate->rendered.push_back(frame);}
        gate->finished=true;return QUrl("test:/frame/"+QString::number(frame));
    };
}
}

class TimelineThumbnailTests : public QObject {
    Q_OBJECT
private slots:
    void closingTheTimelineDoesNotWaitForItsRenderer() {
        const auto gate=std::make_shared<Gate>();Release release{gate};
        int delivered=0;
        auto worker=std::make_unique<EditorTimelineThumbnails>();
        worker->request("layer",operation(gate,0),[&](const QUrl &){++delivered;});
        QTRY_VERIFY(gate->started.load());
        QElapsedTimer elapsed;elapsed.start();worker.reset();
        const auto microseconds=elapsed.nsecsElapsed()/1000;
        gate->release.release();QTRY_VERIFY(gate->finished.load());
        qInfo()<<"timeline thumbnail owner destruction (us):"<<microseconds;
        QVERIFY2(microseconds<250000,"Closing a view must not join an optional renderer on the input thread");
        QCOMPARE(delivered,0);
    }
    void theLatestFrameKeepsItsQueuePosition() {
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorTimelineThumbnails worker;std::vector<QUrl> delivered;
        const auto done=[&](const QUrl &source){delivered.push_back(source);};
        worker.request("layer",operation(gate,0),done);QTRY_VERIFY(gate->started.load());
        for(int frame=1;frame<480;++frame) {
            worker.request("layer",operation(gate,frame),done);
            if(frame==100)worker.request("other",operation(gate,1000),done);
        }
        gate->release.release();QTRY_COMPARE(delivered.size(),std::size_t(3));
        {std::lock_guard lock(gate->mutex);QCOMPARE(gate->rendered,(std::vector<int>{0,479,1000}));}
    }
    void cancellationDiscardsOldDeliveryAndQueuedWork() {
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorTimelineThumbnails worker;std::vector<QUrl> delivered;
        const auto done=[&](const QUrl &source){delivered.push_back(source);};
        worker.request("layer",operation(gate,0),done);QTRY_VERIFY(gate->started.load());
        worker.request("layer",operation(gate,1),done);worker.cancelPending();
        worker.request("layer",operation(gate,2),done);gate->release.release();
        QTRY_COMPARE(delivered.size(),std::size_t(1));QCOMPARE(delivered.front(),QUrl("test:/frame/2"));
        {std::lock_guard lock(gate->mutex);QCOMPARE(gate->rendered,(std::vector<int>{0,2}));}
    }
    void backlogIsBoundedAndACachedSlotCanRetirePendingWork() {
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorTimelineThumbnails worker;int delivered=0,discarded=0;
        const auto done=[&](const QUrl &){++delivered;};
        worker.request("first",operation(gate,0),done);QTRY_VERIFY(gate->started.load());
        for(int frame=1;frame<1000;++frame)
            if(!worker.request(QString::number(frame),operation(gate,frame),done).isEmpty())++discarded;
        worker.cancel("999");gate->release.release();QTRY_COMPARE(delivered,128);
        QCOMPARE(discarded,871);
        {std::lock_guard lock(gate->mutex);QCOMPARE(gate->rendered.size(),std::size_t(128));QCOMPARE(gate->rendered.back(),998);}
    }
    void rendererFailureDoesNotKillTheQueue() {
        EditorTimelineThumbnails worker;std::vector<QUrl> delivered;
        const auto done=[&](const QUrl &source){delivered.push_back(source);};
        worker.request("failed",[]()->QUrl{throw std::runtime_error("optional renderer failed");},done);
        worker.request("next",[]{return QUrl("test:/next");},done);
        QTRY_COMPARE(delivered.size(),std::size_t(2));QVERIFY(delivered.front().isEmpty());
        QCOMPARE(delivered.back(),QUrl("test:/next"));
    }
    void aCompletionObserverCanDeleteTheOwner() {
        auto worker=std::make_unique<EditorTimelineThumbnails>();bool delivered=false;
        worker->request("layer",[]{return QUrl("test:/ready");},[&](const QUrl &){delivered=true;worker.reset();});
        QTRY_VERIFY(delivered);QVERIFY(!worker);
    }
};
QTEST_GUILESS_MAIN(TimelineThumbnailTests)
#include "tst_timelinethumbnails.moc"
