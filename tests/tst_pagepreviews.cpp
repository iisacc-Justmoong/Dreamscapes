#include "App/Views/Editor/EditorPagePreviews.h"
#include <QSignalSpy>
#include <QtTest>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <semaphore>
#include <stdexcept>

namespace {
struct Gate {
    std::atomic_bool started{false};
    std::counting_semaphore<100> allow{0};
    std::atomic_int calls{0},active{0},maximum{0};
    std::mutex mutex;
    std::vector<int> frames;
};
struct Release {std::shared_ptr<Gate> gate;~Release(){gate->allow.release();}};
EditorPagePreviews::Request request(int page=0,int frame=0) {
    auto document=std::make_shared<iiSharedCanvas::Document>();
    document->extent={8,4};document->timeline.frameCount=1000;
    return {std::move(document),iiSharedCanvas::FrameIndex(frame),{}, {8,4},page};
}
QUrl resultFor(const EditorPagePreviews::Request &request) {
    return QUrl("test:/page/"+QString::number(request.page)+"/frame/"+QString::number(request.frame));
}
EditorPagePreviews::Operation gatedOperation(std::shared_ptr<Gate> gate) {
    return [gate](const EditorPagePreviews::Request &request) {
        const auto call=++gate->calls;const auto active=++gate->active;
        gate->maximum.store(std::max(gate->maximum.load(),active));
        {std::lock_guard lock(gate->mutex);gate->frames.push_back(int(request.frame));}
        if(call==1){gate->started.store(true);(void)gate->allow.try_acquire_for(std::chrono::seconds(5));}
        --gate->active;return resultFor(request);
    };
}
}
class PagePreviewTests : public QObject {
    Q_OBJECT
private slots:
    void latestFrameWinsAndOtherPagesAreNotStarved() {
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorPagePreviews previews(gatedOperation(gate));QSignalSpy ready(&previews,&EditorPagePreviews::ready);
        QVERIFY(previews.source("first",request()).isEmpty());
        QTRY_VERIFY(gate->started.load());
        for(int frame=1;frame<480;++frame) {
            QVERIFY(previews.source(QString::number(frame),request(0,frame)).isEmpty());
            if(frame==100)previews.source("other",request(1));
        }
        QCOMPARE(gate->calls.load(),1);gate->allow.release();
        QTRY_COMPARE(ready.size(),2);
        QCOMPARE(previews.source("479",request(0,479)),resultFor(request(0,479)));
        QCOMPARE(previews.source("other",request(1)),resultFor(request(1)));
        QCOMPARE(gate->calls.load(),3);QCOMPARE(gate->maximum.load(),1);
        {std::lock_guard lock(gate->mutex);QCOMPARE(gate->frames,(std::vector<int>{0,479,0}));}
    }
    void aCachedFrameSupersedesAnOlderPendingFrame() {
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorPagePreviews previews(gatedOperation(gate));QSignalSpy ready(&previews,&EditorPagePreviews::ready);
        previews.source("cached",request());QTRY_VERIFY(gate->started.load());gate->allow.release();
        QTRY_COMPARE(ready.size(),1);
        // Hold a different page so the old frame is definitely still pending.
        const auto blocker=std::make_shared<Gate>();Release releaseBlocker{blocker};
        EditorPagePreviews held([gate,blocker](const EditorPagePreviews::Request &r) {
            if(r.page==1){blocker->started.store(true);(void)blocker->allow.try_acquire_for(std::chrono::seconds(5));}
            ++gate->calls;return resultFor(r);
        });
        QSignalSpy done(&held,&EditorPagePreviews::ready);
        held.source("cached",request());QTRY_COMPARE(done.size(),1);
        held.source("blocker",request(1));QTRY_VERIFY(blocker->started.load());
        held.source("obsolete",request(0,1));
        QCOMPARE(held.source("cached",request()),resultFor(request()));
        blocker->allow.release();QTRY_COMPARE(done.size(),2);
        QCOMPARE(gate->calls.load(),3);
    }
    void aLargeUnseenBacklogIsBounded() {
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorPagePreviews previews(gatedOperation(gate));QSignalSpy ready(&previews,&EditorPagePreviews::ready);
        previews.source("0",request());QTRY_VERIFY(gate->started.load());
        for(int page=1;page<1000;++page)previews.source(QString::number(page),request(page));
        QCOMPARE(gate->calls.load(),1);gate->allow.release();
        QTRY_VERIFY(std::ranges::any_of(ready,[](const auto &args){return args[0].toInt()==999;}));
        QCOMPARE(gate->calls.load(),129);QCOMPARE(gate->maximum.load(),1);
        QCOMPARE(ready.size(),129);
        QCOMPARE(previews.source("999",request(999)),resultFor(request(999)));
    }
    void invalidationSuppressesOldCompletionAndDiscardsPendingWork() {
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorPagePreviews previews(gatedOperation(gate));QSignalSpy ready(&previews,&EditorPagePreviews::ready);
        previews.source("old",request());QTRY_VERIFY(gate->started.load());
        previews.source("pending",request(1));previews.invalidate();
        previews.source("new",request(0,2));gate->allow.release();QTRY_COMPARE(ready.size(),1);
        QCOMPARE(gate->calls.load(),2);
        QCOMPARE(previews.source("new",request(0,2)),resultFor(request(0,2)));
        {std::lock_guard lock(gate->mutex);QCOMPARE(gate->frames,(std::vector<int>{0,2}));}
    }
    void rendererFailuresAreCachedUntilDocumentInvalidation() {
        std::atomic_int calls=0;
        EditorPagePreviews previews([&calls](const auto &)->QUrl{++calls;throw std::runtime_error("optional render failure");});
        QSignalSpy ready(&previews,&EditorPagePreviews::ready);
        previews.source("failure",request());QTRY_COMPARE(ready.size(),1);
        for(int i=0;i<50;++i)QVERIFY(previews.source("failure",request()).isEmpty());
        QCOMPARE(calls.load(),1);previews.invalidate();previews.source("failure",request());
        QTRY_COMPARE(ready.size(),2);QCOMPARE(calls.load(),2);
    }
    void closingTheViewDropsOptionalWorkAndRetainsCompletedImages() {
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorPagePreviews previews([gate](const auto &r) {
            ++gate->calls;
            if(r.page==1){gate->started=true;(void)gate->allow.try_acquire_for(std::chrono::seconds(5));}
            return resultFor(r);
        });
        QSignalSpy ready(&previews,&EditorPagePreviews::ready);
        previews.source("cached",request());QTRY_COMPARE(ready.size(),1);
        previews.source("running",request(1));QTRY_VERIFY(gate->started.load());
        previews.source("discard",request(2));previews.cancelPending();
        QCOMPARE(previews.source("cached",request()),resultFor(request()));
        gate->allow.release();previews.source("resume",request(3));
        QTRY_COMPARE(ready.size(),2);QCOMPARE(ready[1][0].toInt(),3);
        QCOMPARE(gate->calls.load(),3);
    }
};
QTEST_GUILESS_MAIN(PagePreviewTests)
#include "tst_pagepreviews.moc"
