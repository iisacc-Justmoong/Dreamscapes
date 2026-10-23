#include "App/Views/Editor/EditorGraphicHelper.h"
#include "App/Views/Editor/EditorGraphicAdjust.h"
#include "App/Views/Editor/EditorCanvas.h"
#include <iiSharedCanvas.h>
#include <iiFileProviderNative.h>
#include <SocietyDrive.h>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <semaphore>
#include <thread>
using namespace iiSharedCanvas;
namespace {
using Helper = EditorGraphicHelper;
Document document() {
    Document d; d.extent = {8, 8}; d.timeline.frameCount = 24;
    d.assets.emplace_back(RasterAsset{"pixels", makeRasterLayer(8, 8, 0xff336699U)});
    d.layers.emplace_back(StaticBitmapLayer{{"layer", "Layer"}, StaticSource{"pixels"}}); return d;
}
struct Gate {
    std::atomic_int calls = 0, finished = 0, active = 0, maximum = 0;
    std::atomic_bool started = false, sourceCaptured = false, previewCaptured = false;
    std::counting_semaphore<20> release{0};
};
struct Release { std::shared_ptr<Gate> gate; ~Release() { gate->release.release(); } };
Helper::Operation gated(const std::shared_ptr<Gate> &gate) {
    return [gate](const Helper::Request &request, std::stop_token stop) {
        const int active = ++gate->active; gate->maximum = std::max(gate->maximum.load(), active);
        gate->sourceCaptured = bool(request.snapshot.document); gate->previewCaptured = bool(request.previewPixels);
        if (++gate->calls == 1) { gate->started = true; (void)gate->release.try_acquire_for(std::chrono::seconds(8)); }
        auto result = Helper::calculate(request, stop); --gate->active; ++gate->finished; return result;
    };
}
QVariantMap sample(double red = .2) { return {{"sample", QVariantList{red, .4, .6, 1.0}}}; }
void setup(Helper &helper, EditorCanvas &canvas) { helper.setCanvas(&canvas); helper.setContext({{"tool", "color"}, {"view", "test"}}); }
}
class GraphicHelperTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/graphic-helper-documents-XXXXXX.societycontainer"};
private slots:
    void initTestCase() {
        QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path())); qputenv("SOCIETY_CONTAINER_PATH", m_storage.path().toUtf8());
    }
    void pureCalculationDoesNotCapturePixelsAndKeepsInputAndFilesResponsive() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(document(), "Parameters"));
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(m_storage.filePath("Files/read.iisc"))));
        const auto path = std::filesystem::path(canvas.filePath().toStdU16String()); const auto file = iiFileProvider::native::File::read(path);
        const auto bytes = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision(); const auto alias = canvas.fileAlias();
        auto gate = std::make_shared<Gate>(); Release release{gate}; Helper helper(gated(gate), nullptr); setup(helper, canvas);
        QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged), docs(&canvas, &EditorCanvas::documentInfoChanged);
        QElapsedTimer timer; timer.start(); QVERIFY(helper.request("samplePointColor", sample())); const auto us = timer.nsecsElapsed() / 1000;
        QVERIFY(us < 250000); QVERIFY(helper.state()["pending"].toBool()); QTRY_VERIFY(gate->started.load());
        QVERIFY(!gate->sourceCaptured.load()); QVERIFY(!gate->previewCaptured.load());
        bool heartbeat = false; QTimer::singleShot(0, &helper, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
        const auto pending = helper.state(); QSignalSpy changed(&helper, &Helper::changed);
        for (int i = 0; i < 1000; ++i) QCOMPARE(helper.state(), pending);
        QCOMPARE(changed.count(), 0); QCOMPARE(gate->calls.load(), 1);
        gate->release.release(); QTRY_VERIFY(helper.state()["valid"].toBool());
        QCOMPARE(helper.state()["parameters"].toMap().size(), 11); QVERIFY(!helper.state().contains("imageSource"));
        QCOMPARE(canvas.revision(), revision); QCOMPARE(canvas.fileAlias(), alias); QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
        QCOMPARE(iiFileProvider::native::File::read(path), file); QCOMPARE(files.count(), 0); QCOMPARE(docs.count(), 0);
        qInfo() << "graphic_helper_dispatch_us" << us;
    }
    void largeSourceParameterQueryDoesNotRunThePreviewPipeline() {
        Document d; d.extent = {2048, 2048}; d.assets.emplace_back(RasterAsset{"pixels", makeRasterLayer(2048, 2048, 0xff336699U)});
        d.layers.emplace_back(StaticBitmapLayer{{"layer", "Layer"}, StaticSource{"pixels"}});
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(std::move(d), "Large helper")); Helper helper; setup(helper, canvas);
        const auto revision = canvas.revision(); QElapsedTimer timer; timer.start(); QVERIFY(helper.request("samplePointColor", sample()));
        const auto us = timer.nsecsElapsed() / 1000; QVERIFY(us < 250000); QVERIFY(helper.state()["pending"].toBool());
        QTRY_VERIFY(helper.state()["valid"].toBool()); QCOMPARE(helper.state()["parameters"].toMap().size(), 11);
        QVERIFY(!helper.state().contains("imageSource")); QVERIFY(!helper.state().contains("originalImageSource")); QCOMPARE(canvas.revision(), revision);
        qInfo() << "graphic_helper_large_dispatch_us" << us;
    }
    void nativeSourceUsesTheCapturedRawDynamicLayer() {
        auto d = document(); d.assets.emplace_back(RasterAsset{"second", makeRasterLayer(8, 8, 0xffdd6633U)});
        LayerProperties p{"layer", "Layer"}; p.opacity = .2; p.transform.translationX = 20;
        d.layers = {DynamicBitmapLayer{p, KeyframedSource{{0, 1}}}}; d.frames = {{0, {{"layer", "pixels"}}}, {1, {{"layer", "second"}}}};
        const auto prepared = EditorGraphicAdjust::prepareHelper("autoDevelopLight", {}); QVERIFY(prepared.ok());
        Helper::Request input{*prepared.request, {std::make_shared<const Document>(d), 1, {}}, "layer", {}};
        const auto before = encodeIisc(*input.snapshot.document).bytes;
        QImage source(8, 8, QImage::Format_ARGB32); source.fill(0xffdd6633U);
        const auto expected = EditorGraphicAdjust::process(source, "autoDevelopLight"); QVERIFY(expected.ok());
        const auto actual = Helper::calculate(input); QVERIFY2(actual.ok(), actual.error.c_str());
        QCOMPARE(EditorGraphicAdjust::helperParameters(actual), expected.parameters); QCOMPARE(encodeIisc(*input.snapshot.document).bytes, before);
    }
    void chunkedSourcesRetainTheRawCompatibilityViewAndQueryIsCheap() {
        Document d; d.canvasMode = CanvasMode::Infinite; d.extent = {2048, 2048}; d.infiniteCanvas.origin = {-1, -1}; d.infiniteCanvas.chunkSize = 32;
        d.assets.emplace_back(ChunkedRasterAsset{"chunks", {{-1, -1, makeRasterLayer(32, 32, 0xffdd6633U)}, {0, 0, makeRasterLayer(32, 32, 0xff336699U)}}});
        d.layers.emplace_back(StaticBitmapLayer{{"layer", "Chunks"}, StaticSource{"chunks"}});
        EditorCanvas canvas; QVERIFY2(canvas.loadProjectPage(d, "Chunked"), qPrintable(canvas.error()));
        QElapsedTimer elapsed; elapsed.start();
        for (int i = 0; i < 100; ++i) QVERIFY(canvas.graphicOperationState("autoDevelopLight")["enabled"].toBool());
        const auto us = elapsed.nsecsElapsed() / 1000; QVERIFY(us < 250000); qInfo() << "graphic_capability_100_reads_us" << us;
        const auto *pixels = canvas.selectedRasterPixels(); QVERIFY(pixels);
        const auto oracle = iiGraphicAdjust::fromArgb32(pixels->width, pixels->height, pixels->pixels); QVERIFY(oracle);
        const auto settings = iiGraphicAdjust::autoDevelopLight(oracle->view()); QVERIFY(settings);
        const auto prepared = EditorGraphicAdjust::prepareHelper("autoDevelopLight", {}); QVERIFY(prepared.ok());
        const auto result = Helper::calculate({*prepared.request, canvas.editingFrameSnapshot(), canvas.selectedLayerId().toStdString(), {}});
        QVERIFY2(result.ok(), result.error.c_str()); QCOMPARE(EditorGraphicAdjust::helperParameters(result)["exposure"].toDouble(), settings->exposure);
    }
    void repeatedRequestsKeepOneRunningAndOneLatestPendingJob() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(document(), "Latest")); auto gate = std::make_shared<Gate>(); Release release{gate};
        Helper helper(gated(gate), nullptr); setup(helper, canvas); QVERIFY(helper.request("samplePointColor", sample())); QTRY_VERIFY(gate->started.load());
        QSignalSpy changes(&helper, &Helper::changed);
        for (int i = 1; i < 20; ++i) QVERIFY(helper.request("samplePointColor", sample(i / 20.0)));
        QCOMPARE(changes.count(), 0);
        gate->release.release(); QTRY_VERIFY(helper.state()["valid"].toBool());
        const auto expected = EditorGraphicAdjust::prepareHelper("samplePointColor", sample(.95)); QVERIFY(expected.ok());
        QCOMPARE(helper.state()["parameters"].toMap(), EditorGraphicAdjust::helperParameters(graphicHelper::calculate(*expected.request)));
        QCOMPARE(helper.state()["sequence"].toInt(), 1); QCOMPARE(gate->calls.load(), 2); QCOMPARE(gate->maximum.load(), 1);
    }
    void anExistingAdjustmentSessionRetainsItsOriginalAnalysisSource() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(document(), "Original session"));
        QVERIFY(canvas.applyGraphicOperation("color", "autoWhiteBalance", {}));
        const auto expected = canvas.previewGraphicOperation("color", "autoWhiteBalance", {}).value("parameters").toMap(); QVERIFY(!expected.isEmpty());
        auto gate = std::make_shared<Gate>(); Release release{gate}; Helper helper(gated(gate), nullptr); setup(helper, canvas);
        QVERIFY(helper.request("autoWhiteBalance", {})); QTRY_VERIFY(gate->started.load());
        QVERIFY(gate->previewCaptured.load()); QVERIFY(!gate->sourceCaptured.load()); gate->release.release();
        QTRY_VERIFY(helper.state()["valid"].toBool()); QCOMPARE(helper.state()["parameters"].toMap(), expected);
    }
    void changedTargetsAndContextsRetireAnOldResult_data() {
        QTest::addColumn<QString>("change");
        for (const auto *s : {"cancel", "context", "frame", "selection", "artboard", "revision", "replacement", "tool", "lock", "stroke", "binding"}) QTest::newRow(s) << QString(s);
    }
    void changedTargetsAndContextsRetireAnOldResult() {
        QFETCH(QString, change); auto d = document(); d.layers.push_back(StaticBitmapLayer{{"second", "Second"}, StaticSource{"pixels"}});
        if (change == "artboard") {
            d.artboards = {{"a", "A", {{0, 0}, d.extent}, 0}, {"b", "B", {{10, 0}, d.extent}, 0}};
            for (auto &layer : d.layers) layerProperties(layer).artboardId = "a";
        }
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(d, "Old")); canvas.configureTool("brush", {});
        auto gate = std::make_shared<Gate>(); Release release{gate}; Helper helper(gated(gate), nullptr); setup(helper, canvas);
        QVERIFY(helper.request("samplePointColor", sample())); QTRY_VERIFY(gate->started.load());
        if (change == "cancel") QVERIFY(helper.cancel());
        else if (change == "context") helper.setContext({{"view", "other"}});
        else if (change == "frame") canvas.setFrame(1);
        else if (change == "selection") QVERIFY(canvas.selectLayer("layer"));
        else if (change == "artboard") QVERIFY(canvas.selectArtboard(1, false));
        else if (change == "revision") QVERIFY(canvas.setLayerOpacity("second", .5));
        else if (change == "replacement") QVERIFY(canvas.loadProjectPage(document(), "New"));
        else if (change == "tool") canvas.configureTool("select", {});
        else if (change == "lock") QVERIFY(canvas.applyToolField("layers", "field-8", "Pixels"));
        else if (change == "stroke") QVERIFY(canvas.beginStrokeAt({1, 1}));
        else helper.setCanvas(nullptr);
        QVERIFY(!helper.state()["pending"].toBool()); gate->release.release(); QTRY_COMPARE(gate->finished.load(), 1); QTest::qWait(30);
        QVERIFY(!helper.state()["valid"].toBool()); QCOMPARE(helper.state()["sequence"].toInt(), 0);
        if (canvas.liveStrokeActive()) canvas.configureTool("select", {});
    }
    void sameBindingsContextAndPureNavigationRetainTheRequest() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(document(), "View")); auto gate = std::make_shared<Gate>(); Release release{gate};
        Helper helper(gated(gate), nullptr); setup(helper, canvas); QVERIFY(helper.request("samplePointColor", sample())); QTRY_VERIFY(gate->started.load());
        QSignalSpy changed(&helper, &Helper::changed);
        for (int i = 0; i < 200; ++i) { helper.setCanvas(&canvas); helper.setContext(helper.context()); canvas.setZoom(1 + i * .01); canvas.setPanX(i); }
        QCOMPARE(changed.count(), 0); QVERIFY(helper.state()["pending"].toBool()); gate->release.release(); QTRY_VERIFY(helper.state()["valid"].toBool()); QCOMPARE(gate->calls.load(), 1);
    }
    void closingTheOwnerDoesNotJoinItsWorker() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(document(), "Close")); auto gate = std::make_shared<Gate>(); Release release{gate};
        auto helper = std::make_unique<Helper>(gated(gate), nullptr); setup(*helper, canvas); QVERIFY(helper->request("autoDevelopLight", {})); QTRY_VERIFY(gate->started.load());
        QVERIFY(gate->sourceCaptured.load()); QElapsedTimer timer; timer.start(); helper.reset(); const auto us = timer.nsecsElapsed() / 1000;
        QVERIFY(us < 250000); gate->release.release(); QTRY_COMPARE(gate->finished.load(), 1); qInfo() << "graphic_helper_close_us" << us;
    }
    void failuresAreLocalAndRecoverOnTheOwnerThread() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(document(), "Errors")); const auto thread = std::this_thread::get_id();
        std::atomic_bool worker = false; Helper helper([&](const Helper::Request &r, std::stop_token stop) {
            worker = std::this_thread::get_id() != thread;
            if (std::get<graphicHelper::PointColor>(r.calculation).sample.r == 0) throw std::runtime_error("Helper fixture failure");
            return Helper::calculate(r, stop);
        }, nullptr); setup(helper, canvas); bool ownerThread = false;
        connect(&helper, &Helper::changed, this, [&] { if (!helper.state()["pending"].toBool()) ownerThread = std::this_thread::get_id() == thread; }, Qt::DirectConnection);
        QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged), docs(&canvas, &EditorCanvas::documentInfoChanged);
        QVERIFY(helper.request("samplePointColor", sample(0))); QTRY_COMPARE(helper.state()["status"].toString(), QString("error"));
        QCOMPARE(helper.state()["error"].toString(), QString("Helper fixture failure")); QVERIFY(canvas.error().isEmpty());
        QVERIFY(helper.request("samplePointColor", sample())); QTRY_VERIFY(helper.state()["valid"].toBool());
        QVERIFY(worker.load()); QVERIFY(ownerThread); QCOMPARE(files.count(), 0); QCOMPARE(docs.count(), 0);
    }
    void observersMayDeleteRebindOrRestartTheOwner() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(document(), "Observers"));
        auto helper = std::make_unique<Helper>(); setup(*helper, canvas); bool completed = false;
        connect(helper.get(), &Helper::changed, this, [&] { if (helper->state()["valid"].toBool()) { completed = true; helper.reset(); } });
        QVERIFY(helper->request("samplePointColor", sample())); QTRY_VERIFY(completed); QVERIFY(!helper);
        Helper next; setup(next, canvas); int count = 0;
        connect(&next, &Helper::changed, this, [&] {
            if (!next.state()["valid"].toBool()) return;
            if (++count == 1) next.request("samplePointColor", sample(.5));
        });
        QVERIFY(next.request("samplePointColor", sample())); QTRY_COMPARE(count, 2);
        auto binding = std::make_unique<EditorCanvas>(); Helper idle; idle.setCanvas(binding.get()); QSignalSpy changes(&idle, &Helper::changed);
        binding.reset(); QVERIFY(!idle.canvas()); QCOMPARE(changes.count(), 1);
    }
};
QTEST_MAIN(GraphicHelperTests)
#include "tst_graphichelper.moc"
