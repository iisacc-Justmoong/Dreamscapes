#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorClippingOverlay.h"
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <semaphore>
#include <thread>
#include <vector>

using namespace iiSharedCanvas;
namespace {
struct Gate {
    std::counting_semaphore<1024> release{0};
    std::atomic<int> started{0}, finished{0};
    std::mutex mutex;
    std::vector<FrameIndex> frames;
    std::vector<std::shared_ptr<const Document>> snapshots;
};
EditorClippingOverlay::RenderOperation gated(const std::shared_ptr<Gate> &gate, bool fail = false) {
    return [gate, fail](const EditorClippingOverlay::Request &request) {
        { std::lock_guard lock(gate->mutex); gate->frames.push_back(request.snapshot.frame); gate->snapshots.push_back(request.snapshot.document); }
        ++gate->started;
        gate->release.acquire();
        QImage result(2, 2, QImage::Format_ARGB32); result.fill(qRgb(255, 0, int(request.snapshot.frame) * 60));
        ++gate->finished;
        return EditorClippingOverlay::Result{fail ? QImage{} : result, fail ? QStringLiteral("gated failure") : QString{}};
    };
}
struct ReleaseOnExit {
    std::shared_ptr<Gate> gate;
    ~ReleaseOnExit() { gate->release.release(8); }
};
}
class ClippingOverlayTests : public QObject {
    Q_OBJECT
private slots:
    void nativeWindowPaintsClippingAtCanvasCoordinates() {
        if (QGuiApplication::platformName() != "cocoa") QSKIP("Pixel capture runs separately in the native Cocoa/Metal integration.");
        EditorCanvas canvas; canvas.setSize({120, 80});
        Document document; document.extent = {4, 1};
        auto input = makeRasterLayer(4, 1); input.pixels = {0xffffffffu, 0xff000000u, 0xff808080u, 0x00ffffffu};
        document.assets.emplace_back(RasterAsset{"sample", std::move(input)});
        document.layers.emplace_back(StaticBitmapLayer{{"sample.layer", "Clipping sample"}, StaticSource{"sample"}});
        QVERIFY2(canvas.loadProjectPage(std::move(document), "Clipping sample"), qPrintable(canvas.error()));
        canvas.setZoom(15); canvas.setLayerHistogramClipping(true, true);
        const auto before = encodeIisc(*canvas.document()).bytes;
        QQuickWindow window; window.resize(120, 80); window.setColor(Qt::white);
        EditorClippingOverlay overlay(window.contentItem()); overlay.setSize({120, 80}); overlay.setCanvas(&canvas);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window)); QTRY_VERIFY(overlay.ready()); QVERIFY(overlay.available());
        const auto pixel = [&](int x) {
            const auto image = window.grabWindow();
            if (image.isNull()) return QColor{};
            const auto scale = qreal(image.width()) / window.width();
            return image.pixelColor(qRound((canvas.panX() + (x + 0.5) * canvas.zoom()) * scale),
                                    qRound((canvas.panY() + 0.5 * canvas.zoom()) * scale));
        };
        QTRY_VERIFY(pixel(0).isValid() && pixel(0).red() > pixel(0).green() + 50);
        QVERIFY(pixel(1).blue() > pixel(1).red() + 80);
        QCOMPARE(pixel(2), QColor(Qt::white)); QCOMPARE(pixel(3), QColor(Qt::white));
        QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        overlay.setVisible(false); QTRY_COMPARE(pixel(0), QColor(Qt::white));
        window.close();
    }
    void nativePixelsRespectThresholdsAlphaFramesAndArtboards() {
        Document document; document.extent = {4, 1}; document.timeline.frameCount = 2;
        auto pixels = makeRasterLayer(4, 1, 0xff808080u);
        pixels.pixels = {0xffffffffu, 0xff000000u, 0xff808080u, 0x00ffffffu};
        document.assets.emplace_back(RasterAsset{"first", pixels});
        document.assets.emplace_back(RasterAsset{"second", makeRasterLayer(4, 1, 0xff808080u)});
        document.layers.emplace_back(DynamicBitmapLayer{{"layer", "Sample"}, KeyframedSource{{0, 1}}});
        document.frames = {{0, {{"layer", "first"}}}, {1, {{"layer", "second"}}}};
        const auto before = encodeIisc(document); QVERIFY(before.ok());
        EditorClippingOverlay::Request request{{std::make_shared<const Document>(document), 0, {}}, 255, 0, true, true};
        const auto source = request.snapshot.document;
        auto result = EditorClippingOverlay::render(request); QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.image.pixel(0, 0), 0x88ff453au);
        QCOMPARE(result.image.pixel(1, 0), 0x880a84ffu);
        QCOMPARE(result.image.pixel(2, 0), 0u); QCOMPARE(result.image.pixel(3, 0), 0u);
        request.snapshot.frame = 1; result = EditorClippingOverlay::render(request);
        QVERIFY(result.error.isEmpty()); QVERIFY(result.image.isNull());
        request.snapshot.frame = 7; QVERIFY(!EditorClippingOverlay::render(request).error.isEmpty());
        document.artboards = {{"white", "White", {{0, 0}, {4, 1}}, 0xffffffffu}, {"black", "Black", {{0, 0}, {4, 1}}, 0xff000000u}};
        document.layers.clear(); document.assets.clear(); document.frames.clear();
        request.snapshot = {std::make_shared<const Document>(document), 0, "white"};
        QCOMPARE(EditorClippingOverlay::render(request).image.pixel(0, 0), 0x88ff453au);
        request.snapshot.artboardId = "black";
        QCOMPARE(EditorClippingOverlay::render(request).image.pixel(0, 0), 0x880a84ffu);
        request.snapshot.artboardId = "missing";
        QVERIFY(!EditorClippingOverlay::render(request).error.isEmpty());
        QCOMPARE(encodeIisc(*source).bytes, before.bytes);
    }
    void hiddenReadsDoNoWorkAndNavigationCoalescesLatest() {
        auto gate = std::make_shared<Gate>(); ReleaseOnExit release{gate};
        EditorCanvas canvas; QVERIFY(canvas.createRasterDocument(4, 1, 3));
        canvas.setLayerHistogramClipping(false, true);
        const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        QQuickWindow window; window.resize(120, 80);
        EditorClippingOverlay overlay(gated(gate), window.contentItem()); overlay.setSize({120, 80});
        overlay.setVisible(false); overlay.setCanvas(&canvas); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (int i = 0; i < 100; ++i) QVERIFY(canvas.toolState().value("clipping").toMap().value("enabled").toBool());
        QCoreApplication::processEvents(); QCOMPARE(gate->started.load(), 0);
        overlay.setVisible(true);
        QTRY_COMPARE(gate->started.load(), 1); QVERIFY(overlay.busy()); QVERIFY(!overlay.available());
        bool inputDelivered = false; QTimer::singleShot(0, &canvas, [&] { inputDelivered = true; });
        QTRY_VERIFY(inputDelivered); // The renderer is deliberately blocked.
        canvas.setFrame(1); canvas.setFrame(2); QCoreApplication::processEvents();
        gate->release.release();
        QTRY_COMPARE(gate->started.load(), 2); QVERIFY(overlay.busy()); QVERIFY(!overlay.available());
        gate->release.release(); QTRY_VERIFY(overlay.ready());
        QCOMPARE(overlay.image().pixel(0, 0), qRgb(255, 0, 120));
        { std::lock_guard lock(gate->mutex);
          QCOMPARE(gate->frames, std::vector<FrameIndex>({0, 2}));
          QVERIFY(gate->snapshots[0] == gate->snapshots[1]); }
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        overlay.setVisible(false); QVERIFY(!overlay.available()); QVERIFY(!overlay.busy()); QVERIFY(!overlay.ready());
    }
    void hidingSuppressesObsoleteFailures() {
        auto gate = std::make_shared<Gate>(); ReleaseOnExit release{gate};
        EditorCanvas canvas; QVERIFY(canvas.createRasterDocument(4, 1)); canvas.setLayerHistogramClipping(false, true);
        QQuickWindow window; window.resize(120, 80);
        EditorClippingOverlay overlay(gated(gate, true), window.contentItem()); overlay.setSize({120, 80}); overlay.setCanvas(&canvas);
        QSignalSpy failed(&overlay, &EditorClippingOverlay::previewFailed);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window)); QTRY_COMPARE(gate->started.load(), 1);
        overlay.setVisible(false); gate->release.release();
        QTRY_COMPARE(gate->finished.load(), 1); QTest::qWait(30);
        QCOMPARE(failed.size(), 0); QVERIFY(overlay.error().isEmpty()); QVERIFY(!overlay.ready());
    }
    void replacingCanvasRejectsOldErrorsAndReentrantNotifications_data() {
        QTest::addColumn<bool>("hideOnError");
        QTest::newRow("current-failure") << false;
        QTest::newRow("hide-from-result-observer") << true;
    }
    void replacingCanvasRejectsOldErrorsAndReentrantNotifications() {
        QFETCH(bool, hideOnError);
        auto gate = std::make_shared<Gate>(); ReleaseOnExit release{gate};
        EditorCanvas first, second;
        QVERIFY(first.createRasterDocument(4, 1, 3)); QVERIFY(second.createRasterDocument(4, 1, 3));
        first.setLayerHistogramClipping(false, true); second.setLayerHistogramClipping(false, true); second.setFrame(2);
        const auto firstBytes = encodeIisc(*first.document()).bytes, secondBytes = encodeIisc(*second.document()).bytes;
        QQuickWindow window; window.resize(120, 80);
        EditorClippingOverlay overlay(gated(gate, true), window.contentItem()); overlay.setSize({120, 80}); overlay.setCanvas(&first);
        QSignalSpy failed(&overlay, &EditorClippingOverlay::previewFailed);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window)); QTRY_COMPARE(gate->started.load(), 1);
        overlay.setCanvas(&second); QCoreApplication::processEvents(); gate->release.release();
        QTRY_COMPARE(gate->started.load(), 2); QCOMPARE(failed.size(), 0); QVERIFY(overlay.error().isEmpty());
        connect(&overlay, &EditorClippingOverlay::stateChanged, &overlay, [&] {
            if (hideOnError && !overlay.error().isEmpty()) overlay.setVisible(false);
        });
        gate->release.release(); QTRY_COMPARE(gate->finished.load(), 2); QTRY_VERIFY(!overlay.busy());
        if (hideOnError) { QVERIFY(overlay.error().isEmpty()); QCOMPARE(failed.size(), 0); }
        else { QCOMPARE(overlay.error(), QString("gated failure")); QCOMPARE(failed.size(), 1); }
        { std::lock_guard lock(gate->mutex);
          QCOMPARE(gate->frames, std::vector<FrameIndex>({0, 2}));
          QVERIFY(gate->snapshots[0] != gate->snapshots[1]); }
        QCOMPARE(encodeIisc(*first.document()).bytes, firstBytes);
        QCOMPARE(encodeIisc(*second.document()).bytes, secondBytes);
    }
    void deletingTheViewDoesNotWaitForItsWorker() {
        auto gate = std::make_shared<Gate>(); ReleaseOnExit release{gate};
        EditorCanvas canvas; QVERIFY(canvas.createRasterDocument(4, 1)); canvas.setLayerHistogramClipping(false, true);
        QQuickWindow window; window.resize(120, 80);
        auto *overlay = new EditorClippingOverlay(gated(gate), window.contentItem()); overlay->setSize({120, 80}); overlay->setCanvas(&canvas);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window)); QTRY_COMPARE(gate->started.load(), 1);
        std::mutex mutex; std::condition_variable changed; bool destroyed = false; std::atomic<bool> neededRescue = false;
        std::jthread rescue([&] {
            std::unique_lock lock(mutex);
            if (!changed.wait_for(lock, std::chrono::seconds(2), [&] { return destroyed; })) { neededRescue = true; gate->release.release(); }
        });
        delete overlay;
        { std::lock_guard lock(mutex); destroyed = true; } changed.notify_all();
        QVERIFY(!neededRescue.load()); gate->release.release(); QTRY_COMPARE(gate->finished.load(), 1);
    }
};
QTEST_MAIN(ClippingOverlayTests)
#include "tst_clippingoverlay.moc"
