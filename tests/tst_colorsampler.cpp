#include "App/Views/Editor/EditorColorSampler.h"
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <mutex>
#include <semaphore>
#include <thread>
#include <stdexcept>
using namespace iiSharedCanvas;
namespace {
struct Gate {
    std::atomic_int calls=0,finished=0,active=0,maximum=0;
    std::atomic_bool started=false;
    std::counting_semaphore<100> release{0};
    std::mutex mutex;
    std::vector<int> points;
};
struct Release {std::shared_ptr<Gate> gate;~Release(){gate->release.release();}};
EditorColorSampler::Request request(const Document &document,CanvasOrigin point={0,0},FrameIndex frame=0,std::string artboard={}) {
    EditorColorSampler::Request r;r.snapshot={std::make_shared<const Document>(document),frame,std::move(artboard)};r.worldPosition=point;
    for(const auto &board:document.artboards)r.visibility.artboards.push_back(board.visible);
    for(const auto &layer:document.layers)r.visibility.layers.push_back(layerProperties(layer).visible);
    return r;
}
Document document() {
    Document d;d.extent={32,4};d.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(32,4,0xff336699U)});
    d.layers.emplace_back(StaticBitmapLayer{{"layer","Layer"},StaticSource{"pixels"}});return d;
}
EditorColorSampler::Operation gated(const std::shared_ptr<Gate> &gate) {
    return [gate](const EditorColorSampler::Request &input,std::stop_token stop) {
        const auto call=++gate->calls,active=++gate->active;gate->maximum.store(std::max(gate->maximum.load(),active));
        {std::lock_guard lock(gate->mutex);gate->points.push_back(input.worldPosition.x);}
        if(call==1){gate->started=true;(void)gate->release.try_acquire_for(std::chrono::seconds(8));}
        EditorColorSampler::Result r;r.cancelled=stop.stop_requested();r.argb=0xff000000U|std::uint32_t(input.worldPosition.x);
        --gate->active;++gate->finished;return r;
    };
}
}
class ColorSamplerTests:public QObject {
    Q_OBJECT
private slots:
    void nativeReadRetainsFrameWorldPointArtboardAndAuthoredVisibility() {
        Document d;d.extent={2,1};d.timeline.frameCount=2;
        d.artboards={{"a","A",{{-3,4},{2,1}},0},{"b","B",{{-3,4},{2,1}},0xff0000ffU}};
        auto red=makeRasterLayer(2,1,0xffff0000U),green=makeRasterLayer(2,1,0xff00ff00U);green.pixels[1]=0x80663399U;
        d.assets.emplace_back(RasterAsset{"red",red});d.assets.emplace_back(RasterAsset{"green",green});
        LayerProperties p{"animated","Animated"};p.artboardId="a";p.opacity=.75;
        d.layers.emplace_back(DynamicBitmapLayer{p,KeyframedSource{{0,1}}});
        d.frames={{0,{{"animated","red"}}},{1,{{"animated","green"}}}};
        auto r=request(d,{-2,4},1,"a");auto focused=d;focused.artboards[0].visible=false;layerProperties(focused.layers[0]).visible=false;
        r.snapshot.document=std::make_shared<const Document>(focused);
        const auto before=encodeIisc(*r.snapshot.document).bytes,authored=encodeIisc(d).bytes;
        const auto oracle=renderArtboard(d,1,"a");QVERIFY(oracle.ok());
        const auto result=EditorColorSampler::sample(r);QVERIFY2(result.ok(),result.error.c_str());
        QCOMPARE(result.argb,oracle.pixels.pixels[1]);QCOMPARE(encodeIisc(*r.snapshot.document).bytes,before);QCOMPARE(encodeIisc(d).bytes,authored);
        r.worldPosition={-4,4};QVERIFY(!EditorColorSampler::sample(r).ok());
        r.worldPosition={-2,4};r.visibility.layers.clear();QVERIFY(!EditorColorSampler::sample(r).ok());
    }
    void transparentPixelsAreValidAndCancellationIsExplicit() {
        Document d;d.extent={1,1};auto r=request(d);
        const auto result=EditorColorSampler::sample(r);QVERIFY2(result.ok(),result.error.c_str());QCOMPARE(result.argb,0U);
        std::stop_source stop;stop.request_stop();const auto retired=EditorColorSampler::sample(r,stop.get_token());
        QVERIFY(retired.cancelled);QVERIFY(retired.error.empty());QVERIFY(!retired.ok());
        r.snapshot.document.reset();QVERIFY(!EditorColorSampler::sample(r).ok());
    }
    void heldReadLeavesInputResponsiveAndNeverPublishesBeforeCompletion() {
        const auto gate=std::make_shared<Gate>();Release release{gate};EditorColorSampler sampler(gated(gate),nullptr);
        QSignalSpy done(&sampler,&EditorColorSampler::finished);QElapsedTimer elapsed;elapsed.start();
        const auto token=sampler.submit(request(document()));const auto microseconds=elapsed.nsecsElapsed()/1000;
        QVERIFY(token);QVERIFY(microseconds<250000);QVERIFY(sampler.active());QCOMPARE(done.count(),0);QTRY_VERIFY(gate->started.load());
        bool heartbeat=false;QTimer::singleShot(0,&sampler,[&]{heartbeat=true;});QTRY_VERIFY(heartbeat);QCOMPARE(done.count(),0);
        gate->release.release();QTRY_COMPARE(done.count(),1);QCOMPARE(done.front()[0].toULongLong(),token);QVERIFY(!sampler.active());
        qInfo()<<"color_sampler_accepted_us"<<microseconds;
    }
    void newerPointsKeepOneRunningAndOneLatestPendingRead() {
        const auto gate=std::make_shared<Gate>();Release release{gate};EditorColorSampler sampler(gated(gate),nullptr);QSignalSpy done(&sampler,&EditorColorSampler::finished);
        sampler.submit(request(document()));QTRY_VERIFY(gate->started.load());qulonglong latest=0;
        for(int x=1;x<20;++x)latest=sampler.submit(request(document(),{x,0}));
        gate->release.release();QTRY_COMPARE(done.count(),1);QCOMPARE(done.front()[0].toULongLong(),latest);QCOMPARE(done.front()[1].toUInt(),0xff000013U);
        QCOMPARE(gate->calls.load(),2);QCOMPARE(gate->maximum.load(),1);
        std::lock_guard lock(gate->mutex);QCOMPARE(gate->points,(std::vector<int>{0,19}));
    }
    void cancelledReadsCannotPublishOrBlockANewerRead() {
        const auto gate=std::make_shared<Gate>();Release release{gate};EditorColorSampler sampler(gated(gate),nullptr);QSignalSpy done(&sampler,&EditorColorSampler::finished);
        sampler.submit(request(document()));QTRY_VERIFY(gate->started.load());QVERIFY(sampler.cancel());QVERIFY(!sampler.active());QVERIFY(!sampler.cancel());
        const auto latest=sampler.submit(request(document(),{2,0}));gate->release.release();QTRY_COMPARE(done.count(),1);
        QCOMPARE(done.front()[0].toULongLong(),latest);QCOMPARE(done.front()[1].toUInt(),0xff000002U);QCOMPARE(gate->calls.load(),2);
    }
    void deletionDoesNotJoinAndTheWorkerOwnsItsNativeInput() {
        const auto gate=std::make_shared<Gate>();Release release{gate};auto sampler=std::make_unique<EditorColorSampler>(gated(gate),nullptr);
        auto r=request(document());const std::weak_ptr<const Document> weak=r.snapshot.document;int delivered=0;
        connect(sampler.get(),&EditorColorSampler::finished,this,[&]{++delivered;});sampler->submit(std::move(r));QTRY_VERIFY(gate->started.load());
        QElapsedTimer elapsed;elapsed.start();sampler.reset();const auto microseconds=elapsed.nsecsElapsed()/1000;
        QVERIFY(microseconds<250000);QVERIFY(!weak.expired());gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTRY_VERIFY(weak.expired());
        QCOMPARE(delivered,0);qInfo()<<"color_sampler_destruction_us"<<microseconds;
    }
    void observersCanDeleteTheOwnerOrStartAnotherRead() {
        auto sampler=std::make_unique<EditorColorSampler>();bool delivered=false;
        connect(sampler.get(),&EditorColorSampler::finished,this,[&]{delivered=true;sampler.reset();});
        sampler->submit(request(document()));QTRY_VERIFY(delivered);QVERIFY(!sampler);
        EditorColorSampler next;int count=0;bool terminal=false;
        connect(&next,&EditorColorSampler::finished,this,[&] {
            terminal=!next.active();if(++count==1)next.submit(request(document(),{1,0}));
        });
        next.submit(request(document()));QTRY_COMPARE(count,2);QVERIFY(terminal);QVERIFY(!next.active());
    }
    void failuresAreDeliveredOnTheOwnerThreadAndTheNextReadCanRecover() {
        const auto owner=std::this_thread::get_id();std::atomic_bool worker=false,ownerDelivery=false;
        EditorColorSampler sampler([&](const auto &r,std::stop_token stop) {
            worker=std::this_thread::get_id()!=owner;
            if(r.worldPosition.x==0)throw std::runtime_error("color sample fixture failure");
            return EditorColorSampler::sample(r,stop);
        },nullptr);
        connect(&sampler,&EditorColorSampler::finished,this,[&]{ownerDelivery=std::this_thread::get_id()==owner;},Qt::DirectConnection);
        QSignalSpy done(&sampler,&EditorColorSampler::finished);sampler.submit(request(document()));QTRY_COMPARE(done.count(),1);
        QVERIFY(worker.load());QVERIFY(ownerDelivery.load());QCOMPARE(done.front()[2].toString(),QString("color sample fixture failure"));QVERIFY(!done.front()[3].toBool());
        sampler.submit(request(document(),{1,0}));QTRY_COMPARE(done.count(),2);QCOMPARE(done.back()[1].toUInt(),0xff336699U);QVERIFY(done.back()[2].toString().isEmpty());
    }
    void aLargeArtboardReadAllocatesOnlyItsRequestedOutput() {
        Document d;d.extent={32768,32768};d.artboards={{"large","Large",{{0,0},d.extent},0xff102030U}};
        EditorColorSampler sampler;QSignalSpy done(&sampler,&EditorColorSampler::finished);QElapsedTimer elapsed;elapsed.start();
        sampler.submit(request(d,{32767,32767},0,"large"));const auto microseconds=elapsed.nsecsElapsed()/1000;
        QVERIFY(microseconds<250000);QCOMPARE(done.count(),0);QTRY_COMPARE(done.count(),1);QCOMPARE(done.front()[1].toUInt(),0xff102030U);QVERIFY(done.front()[2].toString().isEmpty());
    }
};
QTEST_GUILESS_MAIN(ColorSamplerTests)
#include "tst_colorsampler.moc"
