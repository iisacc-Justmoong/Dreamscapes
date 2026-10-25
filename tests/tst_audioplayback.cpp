#include "App/Views/Editor/EditorAudioPlayback.h"
#include "App/Views/Editor/EditorTimelineModel.h"
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <QScopeGuard>
#include <atomic>
#include <bit>
#include <mutex>
#include <semaphore>
#include <stdexcept>

using namespace iiSharedCanvas;
namespace {
std::shared_ptr<const Document> fixture() {
    Document d;d.extent={1,1};d.timeline={{24,1},3};
    d.assets.emplace_back(RasterAsset{"pixel",makeRasterLayer(1,1,0xff4080c0U)});
    d.layers.emplace_back(StaticBitmapLayer{{"image","Image"},StaticSource{"pixel"}});
    d.audioAssets.push_back({"sound",48000,1,std::vector<std::int16_t>(6000,1200)});
    d.audioTracks.push_back({"track","Audio",false,0,{{"clip","Clip","sound",0,3,0,0,true}}});
    return std::make_shared<const Document>(std::move(d));
}
struct OutputState {
    std::vector<qulonglong> starts;
    std::shared_ptr<const EditorAudioBuffer> buffer;
    qint64 processed=0;
    int stops=0;
    std::string failure;
};
class FakeOutput : public EditorAudioOutput {
public:
    explicit FakeOutput(std::shared_ptr<OutputState> state):state(std::move(state)) {}
    std::string start(qulonglong id,std::shared_ptr<const EditorAudioBuffer> buffer) override {
        state->starts.push_back(id);state->buffer=std::move(buffer);state->processed=0;return state->failure;
    }
    void stop() override {++state->stops;state->buffer.reset();}
    qint64 processedUsecs() const override {return state->processed;}
    void event(qulonglong id,State status,std::string error={}) {notify(id,status,std::move(error));}
    std::shared_ptr<OutputState> state;
};
struct Gate {
    std::counting_semaphore<8> release{0};
    std::atomic<bool> entered=false,finished=false;
    std::mutex mutex;
    std::vector<FrameIndex> frames;
};
}
class AudioPlaybackTests : public QObject {
    Q_OBJECT
private slots:
    void nativePreparationPreservesSamplesAndBorrowsAnAsset() {
        auto document=fixture();EditorAudioRequest request{document,0,{}};
        const auto mixed=EditorAudioPlayback::prepareAudio(request,{});QVERIFY(mixed.error.empty());QVERIFY(mixed.audio);
        QCOMPARE(mixed.audio->sampleRate,48000U);QCOMPARE(mixed.audio->channelCount,2U);
        QCOMPARE(mixed.audio->samples.size(),std::size_t(12000));QCOMPARE(mixed.audio->samples.front(),std::int16_t(1200));
        QCOMPARE(mixed.byteSize(),std::size_t(24000));
        request.assetId="sound";const auto borrowed=EditorAudioPlayback::prepareAudio(request,{});QVERIFY(borrowed.error.empty());
        QCOMPARE(borrowed.audio.get(),&document->audioAssets.front());
        if constexpr(std::endian::native==std::endian::little) {
            QVERIFY(borrowed.portableBytes.empty());QCOMPARE(borrowed.data(),reinterpret_cast<const char *>(document->audioAssets.front().samples.data()));
        }
        document.reset();request.document.reset();QCOMPARE(borrowed.audio->samples.back(),std::int16_t(1200));
    }
    void preparationCoalescesAndNeverBlocksTheOwner() {
        const auto gate=std::make_shared<Gate>();const auto output=std::make_shared<OutputState>();
        auto sink=std::make_unique<FakeOutput>(output);auto *fake=sink.get();
        EditorAudioPlayback playback([gate](const EditorAudioRequest &request,std::stop_token stop) {
            {std::scoped_lock lock(gate->mutex);gate->frames.push_back(request.firstFrame);}
            if(request.firstFrame==0){gate->entered=true;if(!gate->release.try_acquire_for(std::chrono::seconds(5)))return EditorAudioBuffer{{}, {},"Gate timed out"};return EditorAudioBuffer{{}, {},"Obsolete failure"};}
            return EditorAudioPlayback::prepareAudio(request,stop);
        },std::move(sink),nullptr);
        const auto unblock=qScopeGuard([gate]{gate->release.release();});auto document=fixture();const auto original=encodeIisc(*document).bytes;
        QSignalSpy failed(&playback,&EditorAudioPlayback::failed),started(&playback,&EditorAudioPlayback::started);
        QVERIFY(playback.play({document,0,{}})>0);QTRY_VERIFY(gate->entered.load());QVERIFY(playback.preparing());QVERIFY(!playback.playing());
        bool input=false;QTimer::singleShot(0,[&]{input=true;});QTRY_VERIFY(input);
        QVERIFY(playback.play({document,1,{}})>0);const auto current=playback.play({document,2,{}});QVERIFY(current>0);
        gate->release.release();QTRY_COMPARE(output->starts.size(),std::size_t(1));QCOMPARE(output->starts.front(),current);
        QVERIFY(playback.preparing());QCOMPARE(started.count(),0);fake->event(current,EditorAudioOutput::State::Started);
        QTRY_VERIFY(playback.playing());QVERIFY(!playback.preparing());QCOMPARE(started.count(),1);QCOMPARE(failed.count(),0);
        {std::scoped_lock lock(gate->mutex);QCOMPARE(gate->frames,std::vector<FrameIndex>({0,2}));}
        QCOMPARE(encodeIisc(*document).bytes,original);playback.stop();QVERIFY(!playback.playing());
    }
    void cancellingAndDeletingDoNotWaitForPreparation() {
        const auto gate=std::make_shared<Gate>();const auto output=std::make_shared<OutputState>();
        auto *playback=new EditorAudioPlayback([gate](const EditorAudioRequest &request,std::stop_token) {
            gate->entered=true;(void)gate->release.try_acquire_for(std::chrono::seconds(5));
            auto result=EditorAudioPlayback::prepareAudio(request,{});gate->finished=true;return result;
        },std::make_unique<FakeOutput>(output),nullptr);
        const auto unblock=qScopeGuard([gate]{gate->release.release();});QSignalSpy failed(playback,&EditorAudioPlayback::failed);
        QVERIFY(playback->play({fixture(),0,{}})>0);QTRY_VERIFY(gate->entered.load());playback->stop();QVERIFY(!playback->preparing());
        QElapsedTimer elapsed;elapsed.start();delete playback;QVERIFY(elapsed.elapsed()<100);
        gate->release.release();QTRY_VERIFY(gate->finished.load());QCOMPARE(output->starts.size(),std::size_t(0));QCOMPARE(failed.count(),0);
    }
    void outputFailuresAndOldEventsCannotRevivePlayback() {
        const auto output=std::make_shared<OutputState>();auto sink=std::make_unique<FakeOutput>(output);auto *fake=sink.get();
        EditorAudioPlayback playback(EditorAudioPlayback::prepareAudio,std::move(sink),nullptr);
        QSignalSpy failed(&playback,&EditorAudioPlayback::failed);output->failure="Device unavailable";
        const auto first=playback.play({fixture(),0,{}});QTRY_COMPARE(failed.count(),1);QVERIFY(!playback.preparing());QVERIFY(!playback.playing());
        QCOMPARE(failed.at(0).at(0).toULongLong(),first);output->failure.clear();
        const auto second=playback.play({fixture(),1,{}});QTRY_COMPARE(output->starts.size(),std::size_t(2));
        fake->event(first,EditorAudioOutput::State::Started);QVERIFY(!playback.playing());
        fake->event(second,EditorAudioOutput::State::Started);QVERIFY(playback.playing());
        playback.stop();fake->event(second,EditorAudioOutput::State::Started);QVERIFY(!playback.playing());
    }
    void outputMustActuallyStartAndExceptionsBecomeFailures() {
        const auto output=std::make_shared<OutputState>();
        EditorAudioPlayback playback(EditorAudioPlayback::prepareAudio,std::make_unique<FakeOutput>(output),nullptr);
        QSignalSpy failed(&playback,&EditorAudioPlayback::failed);QVERIFY(playback.play({fixture(),0,{}})>0);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,7000);QVERIFY(playback.error().contains("start",Qt::CaseInsensitive));QVERIFY(!playback.preparing());
    }
    void workerFailuresCancellationAndReentrantStopAreOrdered() {
        const auto output=std::make_shared<OutputState>();
        EditorAudioPlayback failing([](const EditorAudioRequest &,std::stop_token)->EditorAudioBuffer {throw std::runtime_error("Fixture preparation failed");},std::make_unique<FakeOutput>(output),nullptr);
        QSignalSpy failed(&failing,&EditorAudioPlayback::failed);const auto id=failing.play({fixture(),0,{}});
        QTRY_COMPARE(failed.count(),1);QCOMPARE(failed.at(0).at(0).toULongLong(),id);QVERIFY(failing.error().contains("Fixture preparation failed"));
        std::stop_source cancel;cancel.request_stop();const auto cancelled=EditorAudioPlayback::prepareAudio({fixture(),0,{}},cancel.get_token());
        QVERIFY(!cancelled.error.empty());QVERIFY(!cancelled.audio);
        auto sink=std::make_unique<FakeOutput>(output);auto *fake=sink.get();
        EditorAudioPlayback playback(EditorAudioPlayback::prepareAudio,std::move(sink),nullptr);
        QSignalSpy started(&playback,&EditorAudioPlayback::started);
        connect(&playback,&EditorAudioPlayback::changed,this,[&]{if(playback.playing())playback.stop();});
        const auto current=playback.play({fixture(),0,{}});QTRY_COMPARE(output->starts.size(),std::size_t(1));
        fake->event(current,EditorAudioOutput::State::Started);QVERIFY(!playback.playing());QCOMPARE(started.count(),0);
    }
    void destroyingAnOwnerFromItsPreparingNotificationIsSafe() {
        const auto gate=std::make_shared<Gate>();const auto output=std::make_shared<OutputState>();
        QPointer<EditorAudioPlayback> playback=new EditorAudioPlayback([gate](const EditorAudioRequest &request,std::stop_token) {
            gate->entered=true;if(!gate->release.try_acquire_for(std::chrono::seconds(5)))return EditorAudioBuffer{{},{},"Gate timed out"};
            auto result=EditorAudioPlayback::prepareAudio(request,{});gate->finished=true;return result;
        },std::make_unique<FakeOutput>(output),nullptr);
        const auto unblock=qScopeGuard([gate]{gate->release.release();});
        connect(playback,&EditorAudioPlayback::changed,this,[&]{if(playback&&playback->preparing())delete playback.data();});
        QVERIFY(playback->play({fixture(),0,{}})>0);QVERIFY(playback.isNull());
        gate->release.release();QTRY_VERIFY(gate->finished.load());QCOMPARE(output->starts.size(),std::size_t(0));
    }
    void aSilentTimelineCancelsAnUnrelatedAssetPreview() {
        const auto output=std::make_shared<OutputState>();auto sink=std::make_unique<FakeOutput>(output);auto *fake=sink.get();
        EditorAudioPlayback playback(EditorAudioPlayback::prepareAudio,std::move(sink),nullptr);
        const auto id=playback.play({fixture(),0,std::string("sound")});QTRY_COMPARE(output->starts.size(),std::size_t(1));
        fake->event(id,EditorAudioOutput::State::Started);QVERIFY(playback.playing());
        auto document=*fixture();document.audioTracks.clear();EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(document,"Silent timeline"));
        EditorTimelineModel model(&playback,nullptr);model.setCanvas(&canvas);const auto original=encodeIisc(*canvas.document()).bytes;
        model.setPlaying(true);QVERIFY(model.playing());QVERIFY(!playback.playing());
        QTRY_VERIFY(!model.playing());QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    }
    void timelineWaitsForAudioAndHoldsTheLastFrameForItsDuration() {
        QFETCH(bool,fractional);
        const auto output=std::make_shared<OutputState>();auto sink=std::make_unique<FakeOutput>(output);auto *fake=sink.get();
        EditorAudioPlayback playback(EditorAudioPlayback::prepareAudio,std::move(sink),nullptr);
        auto document=*fixture();if(fractional)document.timeline.frameRate={30000,1001};
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(document,"Audio clock"));
        EditorTimelineModel model(&playback,nullptr);model.setCanvas(&canvas);const auto original=encodeIisc(*canvas.document()).bytes;
        QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged),projections(&model,&EditorTimelineModel::projectionChanged);
        model.setPlaying(true);QVERIFY(model.playing());QVERIFY(model.preparing());
        QTRY_COMPARE(output->starts.size(),std::size_t(1));QTest::qWait(60);QCOMPARE(model.playhead(),0);
        const auto id=output->starts.front();fake->event(id,EditorAudioOutput::State::Started);QVERIFY(!model.preparing());
        output->processed=fractional?33367:41667;model.updatePlaybackPosition();QCOMPARE(model.playhead(),1);QVERIFY(model.playing());
        output->processed=fractional?66734:83334;model.updatePlaybackPosition();QCOMPARE(model.playhead(),1);QVERIFY(model.playing());
        // The unfinished frame owns the renderer. Once ready, use the latest
        // audio position instead of cancelling it for an intermediate request.
        QTRY_VERIFY(!canvas.rendering());model.updatePlaybackPosition();QCOMPARE(model.playhead(),2);QVERIFY(model.playing());
        output->processed=fractional?100100:125000;model.updatePlaybackPosition();QCOMPARE(model.playhead(),2);QVERIFY(!model.playing());
        QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    }
    void timelineWaitsForAudioAndHoldsTheLastFrameForItsDuration_data() {
        QTest::addColumn<bool>("fractional");QTest::newRow("24fps")<<false;QTest::newRow("30000-over-1001")<<true;
    }
    void navigationAndReplacementCancelPendingAudio_data() {
        QTest::addColumn<QString>("operation");
        for(const auto *operation:{"seek","external-frame","document","canvas"})QTest::newRow(operation)<<QString(operation);
    }
    void navigationAndReplacementCancelPendingAudio() {
        QFETCH(QString,operation);
        const auto gate=std::make_shared<Gate>();const auto output=std::make_shared<OutputState>();
        EditorAudioPlayback playback([gate](const EditorAudioRequest &request,std::stop_token) {
            gate->entered=true;if(!gate->release.try_acquire_for(std::chrono::seconds(5)))return EditorAudioBuffer{{},{},"Gate timed out"};
            auto result=EditorAudioPlayback::prepareAudio(request,{});gate->finished=true;return result;
        },std::make_unique<FakeOutput>(output),nullptr);
        const auto unblock=qScopeGuard([gate]{gate->release.release();});
        const auto document=fixture();EditorCanvas canvas,other;QVERIFY(canvas.loadProjectPage(*document,"Original"));
        QVERIFY(other.loadProjectPage(*document,"Other"));EditorTimelineModel model(&playback,nullptr);model.setCanvas(&canvas);
        const auto original=encodeIisc(*canvas.document()).bytes;QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged);
        model.setPlaying(true);QTRY_VERIFY(gate->entered.load());QVERIFY(model.preparing());
        if(operation=="seek")model.seek(1);
        else if(operation=="external-frame")canvas.setFrame(1);
        else if(operation=="document")QVERIFY(canvas.loadProjectPage(*document,"Replacement"));
        else model.setCanvas(&other);
        QVERIFY(!model.playing());QVERIFY(!playback.preparing());gate->release.release();QTRY_VERIFY(gate->finished.load());
        QTest::qWait(20);QCOMPARE(output->starts.size(),std::size_t(0));
        // Binding a document legitimately publishes its own revisions.
        if(operation!="document")QCOMPARE(revisions.count(),0);
        QCOMPARE(encodeIisc(*canvas.document()).bytes,original);QCOMPARE(encodeIisc(*other.document()).bytes,original);
    }
};
int main(int argc,char **argv) {
    QTemporaryDir settings(DREAMSCAPES_TEST_DIRECTORY "/audio-playback-settings-XXXXXX");if(!settings.isValid())return 2;
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH",(settings.path()+"/storage.json").toUtf8());qputenv("SOCIETY_DISABLE_SESSION_RESTORE","1");
    QGuiApplication app(argc,argv);AudioPlaybackTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "tst_audioplayback.moc"
