#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorColorSampler.h"
#include <iiSharedCanvas.h>
#include <iiFileProviderNative.h>
#include <SocietyDrive.h>
#include <QCursor>
#include <QElapsedTimer>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <limits>
#include <semaphore>

using namespace iiSharedCanvas;
namespace {
struct Gate {
    std::atomic_int calls = 0, finished = 0;
    std::atomic_bool started = false;
    std::counting_semaphore<10> release{0};
};
struct Release { std::shared_ptr<Gate> gate; ~Release() { gate->release.release(); } };
std::unique_ptr<EditorColorSampler> gated(const std::shared_ptr<Gate> &gate) {
    return std::make_unique<EditorColorSampler>([gate](const auto &input, std::stop_token stop) {
        if (++gate->calls == 1) { gate->started = true; (void)gate->release.try_acquire_for(std::chrono::seconds(8)); }
        auto result = EditorColorSampler::sample(input, stop); ++gate->finished; return result;
    }, nullptr);
}
Document pixels() {
    Document d; d.extent = {4, 4}; d.timeline.frameCount = 24;
    d.assets.emplace_back(RasterAsset{"pixels", makeRasterLayer(4, 4, 0xff336699U)});
    d.layers.emplace_back(StaticBitmapLayer{{"layer", "Layer"}, StaticSource{"pixels"}}); return d;
}
void click(EditorCanvas &canvas, QPointF point = {1, 1}) {
    point += canvas.editingOrigin() - QPointF(canvas.canvasOriginX(), canvas.canvasOriginY());
    point = point * canvas.zoom() + QPointF(canvas.panX(), canvas.panY());
    QMouseEvent press(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, point, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&canvas, &release);
}
}
class CanvasColorSamplingTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/color-sampling-documents-XXXXXX.societycontainer"};
private slots:
    void initTestCase() {
        QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH", m_storage.path().toUtf8());
    }
    void heldReadAndRepeatedGettersLeaveFilesAndInputUntouched() {
        auto gate = std::make_shared<Gate>(); Release release{gate}; EditorCanvas canvas({}, gated(gate), nullptr);
        QVERIFY(canvas.loadProjectPage(pixels(), "Sample"));
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(m_storage.filePath("Files/read.iisc"))));
        const auto path = std::filesystem::path(canvas.filePath().toStdU16String());
        const auto bytes = iiFileProvider::native::File::read(path);
        const auto native = encodeIisc(*canvas.document()).bytes;
        const auto alias = canvas.fileAlias(); const auto revision = canvas.revision();
        QSignalSpy files(&canvas, &EditorCanvas::fileStateChanged), docs(&canvas, &EditorCanvas::documentInfoChanged), info(&canvas, &EditorCanvas::fileInformationChanged);
        QVERIFY(canvas.beginLayerColorSample()); QVERIFY(canvas.sampleLayerColor({1, 1})); QTRY_VERIFY(gate->started.load());
        const auto pending = canvas.layerColorSample(); QVERIFY(pending["pending"].toBool()); QVERIFY(!pending["valid"].toBool());
        for (int i = 0; i < 1000; ++i) QCOMPARE(canvas.layerColorSample(), pending);
        bool heartbeat = false; QTimer::singleShot(0, &canvas, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
        gate->release.release(); QTRY_COMPARE(canvas.layerColorSample()["sequence"].toInt(), 1);
        QCOMPARE(canvas.layerColorSample()["hex"].toString(), QString("#336699")); QVERIFY(!canvas.layerColorSample()["pending"].toBool());
        QCOMPARE(files.count(), 0); QCOMPARE(docs.count(), 0); QCOMPARE(info.count(), 0); QCOMPARE(gate->calls.load(), 1);
        QCOMPARE(canvas.revision(), revision); QCOMPARE(canvas.fileAlias(), alias); QCOMPARE(encodeIisc(*canvas.document()).bytes, native);
        QCOMPARE(iiFileProvider::native::File::read(path), bytes);
    }
    void onePointRetainsItsCapturedFrameAndBoardAcrossNavigation() {
        auto d = pixels(); d.extent = {2, 1};
        d.artboards = {{"a", "A", {{-3, 4}, {2, 1}}, 0}, {"b", "B", {{-3, 4}, {2, 1}}, 0xff0000ffU}};
        d.assets = {RasterAsset{"red", makeRasterLayer(2, 1, 0xffff0000U)}, RasterAsset{"green", makeRasterLayer(2, 1, 0xff00ff00U)}};
        LayerProperties p{"layer", "Moving"}; p.artboardId = "a";
        d.layers = {DynamicBitmapLayer{p, KeyframedSource{{0, 1}}}};
        d.frames = {{0, {{"layer", "red"}}}, {1, {{"layer", "green"}}}};
        auto gate = std::make_shared<Gate>(); Release release{gate}; EditorCanvas canvas({}, gated(gate), nullptr);
        QVERIFY(canvas.loadProjectPage(d, "Captured source")); canvas.setPagePresentation(true); canvas.setFrame(1);
        const auto revision = canvas.revision(); QSignalSpy docs(&canvas, &EditorCanvas::documentInfoChanged), files(&canvas, &EditorCanvas::fileStateChanged);
        QVERIFY(canvas.sampleLayerColor({1, 0})); QTRY_VERIFY(gate->started.load());
        canvas.setFrame(0); canvas.setZoom(2); canvas.setPanX(4); QVERIFY(canvas.selectArtboard(1, false));
        QVERIFY(canvas.layerColorSample()["pending"].toBool()); gate->release.release();
        QTRY_COMPARE(canvas.layerColorSample()["sequence"].toInt(), 1);
        QCOMPARE(canvas.layerColorSample()["hex"].toString(), QString("#00FF00"));
        QCOMPARE(canvas.layerColorSample()["frame"].toUInt(), 1U); QCOMPARE(canvas.layerColorSample()["artboard"].toString(), QString("a"));
        QCOMPARE(canvas.layerColorSample()["position"].toPointF(), QPointF(1, 0));
        QCOMPARE(canvas.revision(), revision); QCOMPARE(docs.count(), 0); QCOMPARE(files.count(), 0);
    }
    void obsoleteReadsCannotChangeTheSample_data() {
        QTest::addColumn<QString>("intent");
        for (const auto *s : {"cancel", "tool", "revision", "replacement", "stroke", "escape"}) QTest::newRow(s) << QString(s);
    }
    void obsoleteReadsCannotChangeTheSample() {
        QFETCH(QString, intent); auto gate = std::make_shared<Gate>(); Release release{gate}; EditorCanvas canvas({}, gated(gate), nullptr);
        QVERIFY(canvas.loadProjectPage(pixels(), "Old"));
        if (intent == "stroke") canvas.configureTool("brush", {});
        QVERIFY(canvas.sampleLayerColor({1, 1})); QTRY_VERIFY(gate->started.load());
        if (intent == "cancel") canvas.cancelLayerColorSample();
        else if (intent == "tool") canvas.configureTool("fill", {});
        else if (intent == "revision") QVERIFY(canvas.setLayerOpacity("layer", .5));
        else if (intent == "replacement") QVERIFY(canvas.loadProjectPage(pixels(), "New"));
        else if (intent == "stroke") { canvas.configureTool("brush", {}); QVERIFY(canvas.beginStrokeAt({1, 1})); }
        else { QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier); QCoreApplication::sendEvent(&canvas, &escape); }
        QVERIFY(!canvas.layerColorSample()["pending"].toBool()); gate->release.release(); QTRY_COMPARE(gate->finished.load(), 1); QTest::qWait(30);
        QCOMPARE(canvas.layerColorSample()["sequence"].toInt(), 0); QVERIFY(!canvas.layerColorSample()["valid"].toBool());
        if (canvas.liveStrokeActive()) canvas.configureTool("select", {});
    }
    void invalidPointsDoNotDispatchOrDiscardThePreviousColor() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(pixels(), "Input")); QVERIFY(canvas.sampleLayerColor({0, 0}));
        QTRY_COMPARE(canvas.layerColorSample()["sequence"].toInt(), 1);
        const auto revision = canvas.revision(); QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged);
        for (auto point : {QPointF(-1, 0), QPointF(4, 0), QPointF(0, 4), QPointF(std::numeric_limits<double>::quiet_NaN(), 0), QPointF(0, std::numeric_limits<double>::infinity())})
            QVERIFY(!canvas.sampleLayerColor(point));
        QVERIFY(!canvas.layerColorSample()["error"].toString().isEmpty()); QCOMPARE(canvas.layerColorSample()["sequence"].toInt(), 1);
        QCOMPARE(canvas.layerColorSample()["hex"].toString(), QString("#336699")); QCOMPARE(canvas.revision(), revision); QCOMPARE(files.count(), 0);
        QVERIFY(canvas.sampleLayerColor({1, 1})); QTRY_COMPARE(canvas.layerColorSample()["sequence"].toInt(), 2); QVERIFY(canvas.layerColorSample()["error"].toString().isEmpty());
    }
    void canvasDeletionDoesNotJoinTheHeldRead() {
        auto gate = std::make_shared<Gate>(); Release release{gate}; auto canvas = std::make_unique<EditorCanvas>(std::shared_ptr<EditorFrameExporter>{}, gated(gate), nullptr);
        QVERIFY(canvas->loadProjectPage(pixels(), "Close")); QVERIFY(canvas->sampleLayerColor({1, 1})); QTRY_VERIFY(gate->started.load());
        QElapsedTimer timer; timer.start(); canvas.reset(); const auto us = timer.nsecsElapsed() / 1000;
        QVERIFY(us < 250000); gate->release.release(); QTRY_COMPARE(gate->finished.load(), 1); qInfo() << "canvas_sample_close_us" << us;
    }
    void repeatedPointerSamplesTakePriorityOverTheCurrentEditingTool_data() {
        QTest::addColumn<QString>("tool"); QTest::addColumn<QVariantMap>("settings");
        QTest::newRow("brush") << QString("brush") << QVariantMap{{"field-4", 2}};
        QTest::newRow("elements") << QString("elements") << QVariantMap{};
        QTest::newRow("face") << QString("retouch") << QVariantMap{{"selector", "Face Refine"}};
        QTest::newRow("background-mask") << QString("background") << QVariantMap{{"backgroundView", "Mask"}, {"maskTool", "Brush"}};
    }
    void repeatedPointerSamplesTakePriorityOverTheCurrentEditingTool() {
        QFETCH(QString, tool); QFETCH(QVariantMap, settings);
        auto d = pixels(); std::get<RasterAsset>(d.assets.front()).pixels.pixels[2 + 2 * 4] = 0xff00ff00U;
        auto gate = std::make_shared<Gate>(); Release release{gate}; EditorCanvas canvas({}, gated(gate), nullptr);
        QVERIFY(canvas.loadProjectPage(d, "Pointer")); canvas.setSize({100, 100}); canvas.fitToView(); canvas.configureTool(tool, settings);
        const auto before = encodeIisc(*canvas.document()).bytes;
        const auto revision = canvas.revision(), sequence = canvas.layerColorSample()["sequence"].toULongLong();
        QVERIFY(canvas.beginLayerColorSample()); click(canvas); QTRY_VERIFY(gate->started.load());
        QVERIFY(canvas.layerColorSample()["pending"].toBool()); click(canvas, {2, 2});
        QVERIFY(!canvas.liveStrokeActive()); QVERIFY(canvas.layerColorSample()["pending"].toBool()); QCOMPARE(canvas.revision(), revision);
        gate->release.release(); QTRY_COMPARE(canvas.layerColorSample()["sequence"].toULongLong(), sequence + 1);
        QCOMPARE(gate->calls.load(), 2); QCOMPARE(canvas.layerColorSample()["hex"].toString(), QString("#00FF00"));
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
    }
    void readFailureIsPresentationStateAndTheNextReadCanRecover() {
        auto operation = [](const EditorColorSampler::Request &r, std::stop_token stop) {
            if (r.worldPosition.x == 0) { EditorColorSampler::Result result; result.error = "Sample fixture failure"; return result; }
            return EditorColorSampler::sample(r, stop);
        };
        EditorCanvas canvas({}, std::make_unique<EditorColorSampler>(operation, nullptr), nullptr);
        QVERIFY(canvas.loadProjectPage(pixels(), "Failure")); const auto revision = canvas.revision();
        QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged), docs(&canvas, &EditorCanvas::documentInfoChanged);
        QVERIFY(canvas.sampleLayerColor({0, 0})); QTRY_VERIFY(!canvas.layerColorSample()["pending"].toBool());
        QCOMPARE(canvas.layerColorSample()["error"].toString(), QString("Sample fixture failure")); QVERIFY(canvas.error().isEmpty());
        QCOMPARE(canvas.layerColorSample()["sequence"].toInt(), 0); QCOMPARE(files.count(), 0); QCOMPARE(docs.count(), 0);
        QVERIFY(canvas.sampleLayerColor({1, 1})); QTRY_COMPARE(canvas.layerColorSample()["sequence"].toInt(), 1);
        QVERIFY(canvas.layerColorSample()["error"].toString().isEmpty()); QCOMPARE(canvas.revision(), revision); QCOMPARE(files.count(), 0);
    }
    void neutralSampleMakesOneDurableAdjustmentAndMatchesTheNativeOracle() {
        auto gate = std::make_shared<Gate>(); Release release{gate}; EditorCanvas canvas({}, gated(gate), nullptr);
        QVERIFY(canvas.loadProjectPage(pixels(), "Neutral")); canvas.setSize({100, 100}); canvas.fitToView();
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(m_storage.filePath("Files/neutral.iisc"))));
        const auto initial = *canvas.selectedRasterPixels(); const auto revision = canvas.revision();
        const auto values = QVariantMap{{"field-24", "Relative"}, {"field-25", 0}, {"field-28", 0}};
        canvas.configureTool("color", values); QVERIFY(canvas.executeToolAction("color", "field-31", values)); click(canvas);
        QTRY_VERIFY(gate->started.load()); QVERIFY(canvas.toolState()["colorReadPending"].toBool()); QCOMPARE(canvas.revision(), revision);
        bool heartbeat = false; QTimer::singleShot(0, &canvas, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
        ColorAdjustment a; a.temperature = (153 - 51) / 255.0 * .6; a.tint = (102 - (51 + 153) / 2.0) / 255;
        const auto expected = adjustRaster(initial, a); QVERIFY(expected.ok());
        QSignalSpy revisions(&canvas, &EditorCanvas::revisionChanged), files(&canvas, &EditorCanvas::fileStateChanged);
        gate->release.release(); QTRY_VERIFY(!canvas.toolState()["colorReadPending"].toBool()); QTRY_COMPARE(canvas.revision(), revision + 1);
        QCOMPARE(revisions.count(), 1); QCOMPARE(files.count(), 1); QCOMPARE(canvas.selectedRasterPixels()->pixels, expected.pixels.pixels);
        DocumentFile reopened; QVERIFY(reopened.open(std::filesystem::path(canvas.filePath().toStdU16String())).ok());
        QCOMPARE(std::get<RasterAsset>(reopened.document()->assets.front()).pixels.pixels, expected.pixels.pixels);
        QCOMPARE(canvas.toolValues("color")["field-24"].toString(), QString("Relative")); QCOMPARE(canvas.toolValues("color")["field-25"].toDouble(), 2400.0);
    }
    void neutralReadRetiresWhenItsEditTargetChanges_data() {
        QTest::addColumn<QString>("intent");
        for (const auto *s : {"frame", "selection", "values", "tool"}) QTest::newRow(s) << QString(s);
    }
    void neutralReadRetiresWhenItsEditTargetChanges() {
        QFETCH(QString, intent); auto gate = std::make_shared<Gate>(); Release release{gate}; EditorCanvas canvas({}, gated(gate), nullptr);
        auto d = pixels(); d.layers.push_back(StaticBitmapLayer{{"second", "Second"}, StaticSource{"pixels"}});
        QVERIFY(canvas.loadProjectPage(d, "Neutral")); canvas.setSize({100, 100}); canvas.fitToView();
        QVariantMap values{{"field-24", "Relative"}, {"field-25", 0}, {"field-28", 0}};
        canvas.configureTool("color", values); QVERIFY(canvas.executeToolAction("color", "field-31", values)); click(canvas); QTRY_VERIFY(gate->started.load());
        const auto revision = canvas.revision(); const auto bytes = encodeIisc(*canvas.document()).bytes;
        if (intent == "frame") canvas.setFrame(1);
        else if (intent == "selection") QVERIFY(canvas.selectLayer("layer"));
        else if (intent == "values") { values["field-25"] = 100; canvas.configureTool("color", values); }
        else canvas.configureTool("select", {});
        QVERIFY(!canvas.toolState()["colorReadPending"].toBool()); gate->release.release(); QTRY_COMPARE(gate->finished.load(), 1); QTest::qWait(30);
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
    }
    void samplingOverridesTheBrushCursorAndEscapeRestoresIt() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(pixels(), "Cursor")); canvas.setSize({100, 100}); canvas.fitToView();
        canvas.configureTool("brush", {{"field-4", 2}});
        const QPointF point(canvas.panX() + canvas.zoom(), canvas.panY() + canvas.zoom());
        QHoverEvent hover(QEvent::HoverMove, point, point, point, Qt::NoModifier); QCoreApplication::sendEvent(&canvas, &hover);
        QVERIFY(canvas.interactionFeedback()["pointerVisible"].toBool());
        QVERIFY(canvas.beginLayerColorSample()); QCOMPARE(canvas.cursor().shape(), Qt::CrossCursor); QVERIFY(!canvas.interactionFeedback()["pointerVisible"].toBool());
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier); QCoreApplication::sendEvent(&canvas, &escape);
        QVERIFY(!canvas.layerColorSample()["picking"].toBool()); QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor); QVERIFY(canvas.interactionFeedback()["pointerVisible"].toBool());
    }
};
QTEST_MAIN(CanvasColorSamplingTests)
#include "tst_canvascolorsampling.moc"
