#include "App/Views/Editor/EditorLayerHistogram.h"
#include "App/Views/Editor/EditorCanvas.h"
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>
#include <atomic>
#include <cmath>
#include <mutex>
#include <semaphore>

namespace {
using namespace iiSharedCanvas;
using Histogram = EditorLayerHistogram;
Document pixels(std::uint32_t color = 0xffff0000U) {
    Document document; document.extent = {4, 4};
    document.assets.emplace_back(RasterAsset{"pixels", makeRasterLayer(4, 4, color)});
    document.layers.emplace_back(StaticBitmapLayer{{"layer", "Layer"}, StaticSource{"pixels"}});
    return document;
}
Document movingPixels() {
    auto document = pixels(); document.timeline.frameCount = 24;
    document.assets.emplace_back(RasterAsset{"blue", makeRasterLayer(4, 4, 0xff0000ffU)});
    DynamicBitmapLayer layer; layer.properties = {"layer", "Moving layer"}; layer.content.frameIndices = {0, 19};
    document.layers = {layer}; document.frames = {{0, {{"layer", "pixels"}}}, {19, {{"layer", "blue"}}}};
    return document;
}
struct Gate {
    std::atomic_int calls = 0, finished = 0, active = 0, maximum = 0;
    std::atomic_bool started = false;
    std::counting_semaphore<10> release{0};
    std::mutex mutex;
    std::vector<FrameIndex> frames;
};
struct Release { std::shared_ptr<Gate> gate; ~Release() { gate->release.release(); } };
Histogram::Operation gated(const std::shared_ptr<Gate> &gate) {
    return [gate](const Histogram::Request &request) {
        const auto calls = ++gate->calls, active = ++gate->active;
        gate->maximum = std::max(gate->maximum.load(), active);
        { std::lock_guard lock(gate->mutex); gate->frames.push_back(request.snapshot.frame); }
        if (calls == 1) { gate->started = true; (void)gate->release.try_acquire_for(std::chrono::seconds(10)); }
        auto result = Histogram::calculate(request);
        --gate->active; ++gate->finished; return result;
    };
}
}

class LayerHistogramTests : public QObject {
    Q_OBJECT
private slots:
    void staticFrameNavigationRetainsItsPublishedHistogram() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(pixels(), "Static frames"));
        std::atomic_int calls = 0;
        Histogram analysis([&](const Histogram::Request &request) { ++calls; return Histogram::calculate(request); }, nullptr);
        analysis.setCanvas(&canvas); analysis.setActive(true); QTRY_VERIFY(!analysis.histogram()["pending"].toBool());
        const auto before = analysis.histogram(); const auto revision = canvas.revision();
        QSignalSpy changed(&analysis, &Histogram::changed);
        for (int i = 1; i < 20; ++i) canvas.setFrame(i);
        QTRY_VERIFY(!analysis.histogram()["pending"].toBool());
        QCOMPARE(calls.load(), 1); QCOMPARE(changed.count(), 0);
        QCOMPARE(analysis.histogram(), before); QCOMPARE(canvas.revision(), revision);
    }
    void unavailableCanvasDestructionPublishesItsBindingChange() {
        auto canvas = std::make_unique<EditorCanvas>();
        Histogram analysis; analysis.setCanvas(canvas.get());
        QVERIFY(!analysis.histogram()["available"].toBool());
        QSignalSpy changed(&analysis, &Histogram::changed);
        canvas.reset();
        QVERIFY(!analysis.canvas());
        QCOMPARE(changed.count(), 1);
    }
    void nativeStatisticsPreserveEveryEncodedByteAndPositiveAlphaRule() {
        auto document = pixels(); auto &raster = std::get<RasterAsset>(document.assets[0]).pixels;
        raster.pixels = {0xffff0000U, 0xff00ff00U, 0xff0000ffU, 0x00010203U,
            0x01808080U, 0x7fabcdefU, 0x01010203U, 0x00000000U,
            0xff808080U, 0xff010101U, 0xfffefefeU, 0xffffffffU,
            0xff000000U, 0xff7f7f7fU, 0xff020304U, 0x00445566U};
        const auto rendered = renderFrame(document, 0); QVERIFY(rendered.ok());
        const auto result = Histogram::analyze(document, 0, {}, true, {});
        std::array<std::array<std::uint64_t, 256>, 4> bins{};
        std::array<double, 4> sums{}, squares{}; std::uint64_t count = 0;
        for (const auto pixel : rendered.pixels.pixels) {
            if (!qAlpha(pixel)) continue;
            const std::array<int, 4> values{qRed(pixel), qGreen(pixel), qBlue(pixel), qGray(pixel)};
            ++count;
            for (int i = 0; i < 4; ++i) { ++bins[i][values[i]]; sums[i] += values[i]; squares[i] += values[i] * values[i]; }
        }
        QCOMPARE(result.pixelCount, count); QCOMPARE(result.pixelCount, 13U);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(result.channels[i].bins, bins[i]);
            QCOMPARE(result.channels[i].mean, sums[i] / count);
            QVERIFY(std::abs(result.channels[i].standardDeviation - std::sqrt(std::max(0., squares[i] / count - std::pow(sums[i] / count, 2)))) < 1e-10);
        }
        // A separate full-byte ramp catches endpoint/quantization changes.
        document.extent = {256, 1}; raster = makeRasterLayer(256, 1);
        for (std::uint32_t i = 0; i < 256; ++i) raster.pixels[i] = 0xff000000U | i << 16 | (255 - i) << 8 | i;
        const auto ramp = Histogram::analyze(document, 0, {}, true, {});
        QCOMPARE(ramp.pixelCount, 256U);
        for (int i = 0; i < 256; ++i) { QCOMPARE(ramp.channels[0].bins[i], 1U); QCOMPARE(ramp.channels[1].bins[i], 1U); QCOMPARE(ramp.channels[2].bins[i], 1U); }
        QCOMPARE(ramp.channels[0].mean, 127.5);
    }
    void visibleOwnerDispatchAndReadsStayResponsiveWhileRenderingIsHeld() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(pixels(), "Histogram"));
        const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        const auto gate = std::make_shared<Gate>(); Release release{gate}; Histogram analysis(gated(gate), nullptr);
        analysis.setCanvas(&canvas); QSignalSpy files(&canvas, &EditorCanvas::fileStateChanged), docs(&canvas, &EditorCanvas::documentInfoChanged);
        QElapsedTimer elapsed; elapsed.start(); analysis.setActive(true);
        const auto dispatch = elapsed.nsecsElapsed() / 1000; qInfo() << "histogram accepted microseconds" << dispatch; QVERIFY(dispatch < 250000);
        QTRY_VERIFY(gate->started.load()); bool heartbeat = false; QTimer::singleShot(0, &analysis, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
        const auto pending = analysis.histogram(); QVERIFY(pending["pending"].toBool());
        QSignalSpy notifications(&analysis, &Histogram::changed);
        for (int i = 0; i < 1000; ++i) QCOMPARE(analysis.histogram(), pending);
        QCOMPARE(gate->calls.load(), 1); QCOMPARE(notifications.count(), 0);
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QCOMPARE(files.count(), 0); QCOMPARE(docs.count(), 0);
        gate->release.release(); QTRY_VERIFY(!analysis.histogram()["pending"].toBool());
        QCOMPARE(analysis.histogram()["pixelCount"].toULongLong(), 16U); QCOMPARE(analysis.histogram()["mean"].toDouble(), qGray(0xffff0000U));
    }
    void latestFrameQueueIsBoundedAndChannelsReuseTheSameAnalysis() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(movingPixels(), "Queue"));
        const auto gate = std::make_shared<Gate>(); Release release{gate}; Histogram analysis(gated(gate), nullptr);
        analysis.setCanvas(&canvas); analysis.setActive(true); QTRY_VERIFY(gate->started.load());
        QSignalSpy pendingNotifications(&analysis, &Histogram::changed);
        for (int i = 1; i < 20; ++i) canvas.setFrame(i);
        QCOMPARE(pendingNotifications.count(), 0); // The pending presentation is already published.
        QTest::qWait(90); gate->release.release(); QTRY_VERIFY(!analysis.histogram()["pending"].toBool());
        QCOMPARE(gate->calls.load(), 2); QCOMPARE(gate->maximum.load(), 1);
        { std::lock_guard lock(gate->mutex); QCOMPARE(gate->frames, (std::vector<FrameIndex>{0, 19})); }
        analysis.setChannel("Red"); QCOMPARE(analysis.histogram()["mean"].toDouble(), 0.);
        analysis.setChannel("Green"); QCOMPARE(analysis.histogram()["mean"].toDouble(), 0.);
        analysis.setChannel("Blue"); QCOMPARE(analysis.histogram()["mean"].toDouble(), 255.);
        analysis.setChannel("RGB"); QCOMPARE(analysis.histogram()["mean"].toDouble(), double(qGray(0xff0000ffU)));
        QTest::qWait(100); QCOMPARE(gate->calls.load(), 2);
        canvas.setFrame(0); QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(gate->calls.load(), 3); // First frame's retired result was not cached.
        canvas.setFrame(19); QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(gate->calls.load(), 3);
    }
    void selectedStaticAndPartialRangesKeepTheirOwnTimeDependency() {
        auto document = movingPixels();
        document.layers.insert(document.layers.begin(), StaticBitmapLayer{{"still", "Still"}, StaticSource{"pixels"}});
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(std::move(document), "Scoped time"));
        std::atomic_int calls = 0;
        Histogram analysis([&](const Histogram::Request &r) { ++calls; return Histogram::calculate(r); }, nullptr);
        analysis.setCanvas(&canvas); analysis.setActive(true); analysis.setChannel("Red");
        QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(analysis.histogram()["mean"].toDouble(), 255.);
        canvas.setFrame(19); QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(calls.load(), 2); QCOMPARE(analysis.histogram()["mean"].toDouble(), 0.);
        QVERIFY(canvas.selectLayer("still")); analysis.setComposite(false);
        QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(calls.load(), 3);
        for (int i = 1; i < 19; ++i) canvas.setFrame(i);
        QTest::qWait(90); QCOMPARE(calls.load(), 3); QCOMPARE(analysis.histogram()["mean"].toDouble(), 255.);
        auto ranged = pixels(); ranged.timeline.frameCount = 24; layerProperties(ranged.layers[0]).frameRange = LayerFrameRange{2, 4};
        QVERIFY(canvas.loadProjectPage(std::move(ranged), "Ranged still")); analysis.setComposite(true);
        QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(analysis.histogram()["pixelCount"].toULongLong(), 0U);
        canvas.setFrame(3); QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(analysis.histogram()["pixelCount"].toULongLong(), 16U);
    }
    void hiddenViewsAndLiveStrokesDoNotScheduleAnalysis() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(pixels(), "Deferral"));
        std::atomic_int calls = 0; Histogram analysis([&](const Histogram::Request &r) { ++calls; return Histogram::calculate(r); }, nullptr);
        analysis.setCanvas(&canvas); canvas.setFrame(1); QTest::qWait(90); QCOMPARE(calls.load(), 0);
        canvas.configureTool("brush", {{"field-4", 2}, {"field-5", 100}, {"field-8", 100}, {"field-9", 100}});
        QVERIFY(canvas.beginStrokeAt({1, 1})); analysis.setActive(true); QTest::qWait(90); QCOMPARE(calls.load(), 0);
        QVERIFY(canvas.continueStrokeAt({2, 1})); QTest::qWait(90); QCOMPARE(calls.load(), 0);
        QVERIFY(canvas.endStrokeAt({2, 1})); QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(calls.load(), 1);
        analysis.setActive(false); QVERIFY(canvas.setLayerOpacity(canvas.selectedLayerId(), .5)); canvas.setFrame(2);
        QTest::qWait(100); QCOMPARE(calls.load(), 1);
        analysis.setActive(true); QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(calls.load(), 2);
    }
    void closedAndReboundOwnersDiscardTheOldResultWithoutJoining() {
        auto canvas = std::make_unique<EditorCanvas>(); QVERIFY(canvas->loadProjectPage(pixels(), "Retired"));
        const auto gate = std::make_shared<Gate>(); Release release{gate}; auto analysis = std::make_unique<Histogram>(gated(gate), nullptr);
        analysis->setCanvas(canvas.get()); analysis->setActive(true); QTRY_VERIFY(gate->started.load());
        QElapsedTimer elapsed; elapsed.start(); analysis.reset(); const auto us = elapsed.nsecsElapsed() / 1000;
        qInfo() << "histogram owner destruction microseconds" << us; QVERIFY(us < 250000);
        canvas.reset(); gate->release.release(); QTRY_COMPARE(gate->finished.load(), 1);
        EditorCanvas first, second; QVERIFY(first.loadProjectPage(pixels(), "First")); QVERIFY(second.loadProjectPage(pixels(0xff0000ffU), "Second"));
        const auto next = std::make_shared<Gate>(); Release nextRelease{next}; Histogram rebound(gated(next), nullptr);
        rebound.setCanvas(&first); rebound.setActive(true); QTRY_VERIFY(next->started.load());
        rebound.setCanvas(&second); QTest::qWait(90); next->release.release(); QTRY_VERIFY(!rebound.histogram()["pending"].toBool());
        rebound.setChannel("Blue"); QCOMPARE(rebound.histogram()["mean"].toDouble(), 255.); QCOMPARE(next->maximum.load(), 1);
    }
    void sourceDestructionAndOptionalFailureDoNotChangeEditingErrors() {
        auto canvas = std::make_unique<EditorCanvas>(); QVERIFY(canvas->loadProjectPage(pixels(), "Lifetime"));
        QVERIFY(!canvas->removeLayer("missing")); const auto editingError = canvas->error();
        std::atomic_int calls = 0; Histogram failed([&](const Histogram::Request &) { ++calls; Histogram::Result r; r.error = "optional histogram failure"; return r; }, nullptr);
        failed.setCanvas(canvas.get()); failed.setActive(true); QTRY_VERIFY(!failed.histogram()["pending"].toBool());
        QCOMPARE(failed.histogram()["error"].toString(), QString("optional histogram failure")); QCOMPARE(canvas->error(), editingError);
        failed.setChannel("Red"); QTest::qWait(90); QCOMPARE(calls.load(), 1);
        failed.refresh(); QTRY_COMPARE(calls.load(), 2); QTRY_VERIFY(!failed.histogram()["pending"].toBool());
        canvas.reset(); QVERIFY(!failed.canvas()); QVERIFY(!failed.histogram()["available"].toBool());
    }
    void deletedCanvasRetiresAReadAndTerminalObserversCanCloseThePanel() {
        auto canvas = std::make_unique<EditorCanvas>(); QVERIFY(canvas->loadProjectPage(pixels(), "Deleted"));
        const auto gate = std::make_shared<Gate>(); Release release{gate}; Histogram analysis(gated(gate), nullptr);
        analysis.setCanvas(canvas.get()); analysis.setActive(true); QTRY_VERIFY(gate->started.load());
        canvas.reset(); QVERIFY(!analysis.canvas()); QVERIFY(!analysis.histogram()["available"].toBool());
        QSignalSpy changed(&analysis, &Histogram::changed); gate->release.release(); QTRY_COMPARE(gate->finished.load(), 1);
        QTest::qWait(50); QCOMPARE(changed.count(), 0);
        EditorCanvas next; QVERIFY(next.loadProjectPage(pixels(), "Terminal"));
        auto owner = std::make_unique<Histogram>(); owner->setCanvas(&next);
        connect(owner.get(), &Histogram::changed, &next, [&] {
            if (owner && owner->histogram()["pixelCount"].toULongLong()) owner.reset();
        });
        owner->setActive(true); QTRY_VERIFY(!owner);
    }
    void nativeArtboardAndSelectedScopeRetainAuthoredVisibility() {
        auto document = pixels(); document.artboards = {{"one", "One", {{0, 0}, {4, 4}}, 0xff000000U}, {"two", "Two", {{8, 0}, {4, 4}}, 0xff000000U}};
        layerProperties(document.layers[0]).artboardId = "one";
        document.assets.emplace_back(RasterAsset{"blue", makeRasterLayer(4, 4, 0xff0000ffU)});
        LayerProperties properties{"other", "Other"}; properties.artboardId = "two";
        document.layers.emplace_back(StaticBitmapLayer{properties, StaticSource{"blue"}});
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(std::move(document), "Boards")); canvas.setPagePresentation(true);
        Histogram analysis; analysis.setCanvas(&canvas); analysis.setActive(true); analysis.setChannel("Red");
        QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(analysis.histogram()["mean"].toDouble(), 255.);
        QVERIFY(canvas.selectArtboard(1, false)); QTRY_VERIFY(!analysis.histogram()["pending"].toBool());
        analysis.setChannel("Blue"); QCOMPARE(analysis.histogram()["mean"].toDouble(), 255.);
        analysis.setComposite(false); QTRY_VERIFY(!analysis.histogram()["pending"].toBool()); QCOMPARE(analysis.histogram()["pixelCount"].toULongLong(), 16U);
        const auto revision = canvas.revision(); QVERIFY(canvas.selectArtboard(0, false)); QTRY_VERIFY(!analysis.histogram()["pending"].toBool());
        analysis.setChannel("Red"); QCOMPARE(analysis.histogram()["mean"].toDouble(), 255.); QCOMPARE(canvas.revision(), revision);
        // Native render focus is a view mask. Authored hidden layers remain
        // hidden in composite analysis, while selected scope keeps its prior rule.
        auto authored = pixels(); authored.artboards = {{"one", "One", {{0, 0}, {4, 4}}, 0xff000000U}};
        layerProperties(authored.layers[0]).artboardId = "one"; layerProperties(authored.layers[0]).visible = false;
        auto focused = std::make_shared<Document>(authored); focused->artboards[0].visible = false;
        Histogram::Request request; request.snapshot = {focused, 0, "one"}; request.visibility = {{true}, {false}};
        const auto before = encodeIisc(*focused).bytes;
        const auto composite = Histogram::calculate(request); QCOMPARE(composite.pixelCount, 16U); QCOMPARE(composite.channels[0].mean, 0.);
        request.composite = false; request.selectedLayer = "layer";
        const auto selected = Histogram::calculate(request); QCOMPARE(selected.pixelCount, 16U); QCOMPARE(selected.channels[0].mean, 255.);
        QCOMPARE(encodeIisc(*focused).bytes, before);
    }
    void largeNativeDocumentReturnsBeforeRenderingCompletes() {
        auto document = pixels(0xff336699U); document.extent = {2048, 2048};
        std::get<RasterAsset>(document.assets[0]).pixels = makeRasterLayer(2048, 2048, 0xff336699U);
        document.layers.clear();
        for (int i = 0; i < 32; ++i) { LayerProperties p{"large." + std::to_string(i), "Layer"}; p.opacity = .5; document.layers.emplace_back(StaticBitmapLayer{p, StaticSource{"pixels"}}); }
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(std::move(document), "Large")); Histogram analysis; analysis.setCanvas(&canvas);
        QElapsedTimer elapsed; elapsed.start(); analysis.setActive(true);
        qInfo() << "large histogram accepted microseconds" << elapsed.nsecsElapsed() / 1000;
        QVERIFY(elapsed.elapsed() < 250);
        qint64 heartbeatUs = -1;
        QTimer::singleShot(100, &analysis, [&] { heartbeatUs = elapsed.nsecsElapsed() / 1000; });
        QTRY_VERIFY_WITH_TIMEOUT(heartbeatUs >= 0, 500);
        qInfo() << "large histogram input heartbeat microseconds" << heartbeatUs;
        QVERIFY(heartbeatUs < 500000);
        QTRY_VERIFY_WITH_TIMEOUT(!analysis.histogram()["pending"].toBool(), 30000);
        QCOMPARE(analysis.histogram()["pixelCount"].toULongLong(), 2048ULL * 2048);
    }
};
QTEST_MAIN(LayerHistogramTests)
#include "tst_layerhistogram.moc"
