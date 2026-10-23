#include "App/Views/Editor/EditorGraphicCommand.h"
#include "App/Views/Editor/EditorGraphicAdjust.h"
#include "App/Views/Editor/EditorGraphicHelper.h"
#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorToolUtils.h"
#include <iiFileProviderNative.h>
#include <SocietyDrive.h>
#include <QJSEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <semaphore>
#include <thread>
using namespace iiSharedCanvas;
namespace {
Document picture() {
    Document d; d.extent = {8, 8}; d.timeline.frameCount = 3;
    d.assets.emplace_back(RasterAsset{"a", makeRasterLayer(8, 8, 0xff336699U)});
    d.assets.emplace_back(RasterAsset{"b", makeRasterLayer(8, 8, 0xff996633U)});
    d.layers.emplace_back(DynamicBitmapLayer{{"layer", "Image"}, KeyframedSource{{0, 1, 2}}});
    d.layers.emplace_back(StaticBitmapLayer{{"second", "Second"}, StaticSource{"a"}});
    d.frames = {{0, {{"layer", "a"}}}, {1, {{"layer", "b"}}}, {2, {{"layer", "a"}}}}; return d;
}
bool open(EditorCanvas &canvas, Document d = picture()) { return canvas.loadProjectPage(std::move(d), "Image command") && canvas.selectLayer("layer"); }
QVariantMap exposure(double value = .5) { return {{"exposure", value}}; }
QVariantMap gradient(bool reverse = false) { return {{"start", QVariantMap{{"x", reverse ? 1.0 : 0.0}, {"y", 0.0}}}, {"end", QVariantMap{{"x", reverse ? 0.0 : 1.0}, {"y", 0.0}}}}; }
QImage decoded(QUrl source) { const auto s = source.toString(); return QImage::fromData(QByteArray::fromBase64(s.mid(s.indexOf(',') + 1).toLatin1())); }
struct Gate {
    std::atomic_int calls = 0, finished = 0, active = 0, maximum = 0;
    std::atomic_bool started = false, worker = false;
    std::counting_semaphore<64> release{0};
};
struct Release { std::shared_ptr<Gate> gate; ~Release() { gate->release.release(); } };
EditorGraphicCommand::Operation gated(const std::shared_ptr<Gate> &gate) {
    const auto owner = std::this_thread::get_id();
    return [gate, owner](const graphicImage::Input &input, std::stop_token stop) {
        gate->worker = std::this_thread::get_id() != owner;
        const auto active = ++gate->active; gate->maximum = std::max(gate->maximum.load(), active);
        if (++gate->calls == 1) { gate->started = true; (void)gate->release.try_acquire_for(std::chrono::seconds(8)); }
        auto result = graphicImage::calculate(input, stop); --gate->active; ++gate->finished; return result;
    };
}
void setup(EditorGraphicCommand &command, EditorCanvas &canvas) { command.setCanvas(&canvas); command.setContext({{"view", "test"}}); }
}
class GraphicCommandTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/graphic-command-documents-XXXXXX.societycontainer"};
private slots:
    void initTestCase() {
        QVERIFY(m_storage.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH", m_storage.path().toUtf8());
    }
    void previewCalculatesAndEncodesOnWorkerWithoutFileMutation() {
        EditorCanvas canvas; QVERIFY(open(canvas));
        const auto path = m_storage.filePath("Files/preview.iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        const auto native = std::filesystem::path(path.toStdU16String()); const auto saved = iiFileProvider::native::File::read(native);
        const auto bytes = encodeIisc(*canvas.document()).bytes; const auto alias = canvas.fileAlias(); const auto revision = canvas.revision();
        const auto expected = EditorGraphicAdjust::process(editorTools::image(*canvas.selectedRasterPixels()), "developLight", exposure()); QVERIFY(expected.ok());
        auto gate = std::make_shared<Gate>(); Release release{gate}; EditorGraphicCommand command(gated(gate), nullptr); setup(command, canvas);
        QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged), documents(&canvas, &EditorCanvas::documentInfoChanged);
        QElapsedTimer timer; timer.start(); QVERIFY(command.request("preview", "color", "developLight", exposure()));
        const auto us = timer.nsecsElapsed() / 1000; QVERIFY(us < 250000); QTRY_VERIFY(gate->started.load());
        bool heartbeat = false; QTimer::singleShot(0, &command, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
        const auto pending = command.state(); QSignalSpy changed(&command, &EditorGraphicCommand::changed);
        for (int i = 0; i < 1000; ++i) QCOMPARE(command.state(), pending);
        QCOMPARE(changed.count(), 0); QCOMPARE(gate->calls.load(), 1); gate->release.release(); QTRY_VERIFY(command.state()["valid"].toBool());
        QVERIFY(gate->worker.load()); QCOMPARE(decoded(command.state()["imageSource"].toUrl()).pixel(1, 1), expected.image.pixel(1, 1));
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes); QCOMPARE(canvas.fileAlias(), alias);
        QCOMPARE(iiFileProvider::native::File::read(native), saved); QCOMPARE(files.count(), 0); QCOMPARE(documents.count(), 0);
        qInfo() << "graphic_image_dispatch_us" << us;
    }
    void applyPersistsTheCapturedAssetAndKeepsSharedOriginals() {
        EditorCanvas canvas; QVERIFY(open(canvas)); const auto original = editorTools::image(*canvas.selectedRasterPixels());
        const auto path = m_storage.filePath("Files/apply.iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        EditorGraphicCommand command; setup(command, canvas);
        QVERIFY(command.request("apply", "color", "developLight", exposure(1))); QTRY_VERIFY(command.state()["valid"].toBool());
        canvas.setFrame(1); QCOMPARE(canvas.selectedRasterPixels()->pixels[9], 0xff996633U);
        canvas.setFrame(2); QVERIFY(command.request("apply", "color", "developLight", exposure(.25))); QTRY_VERIFY(command.state()["valid"].toBool());
        const auto expected = EditorGraphicAdjust::process(original, "developLight", exposure(.25)); QVERIFY(expected.ok());
        QCOMPARE(canvas.selectedRasterPixels()->pixels[9], expected.image.pixel(1, 1));
        EditorCanvas reopened; QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path))); QVERIFY(reopened.selectLayer("layer"));
        QCOMPARE(reopened.selectedRasterPixels()->pixels[9], expected.image.pixel(1, 1)); reopened.setFrame(1);
        QCOMPARE(reopened.selectedRasterPixels()->pixels[9], 0xff996633U);
        QVERIFY(canvas.resetTool("color", {})); QCOMPARE(canvas.selectedRasterPixels()->pixels[9], original.pixel(1, 1));
    }
    void maskMappingEncodingAndLocalAdjustmentMatchSdk() {
        auto d = picture(); d.extent = {12, 12}; layerProperties(d.layers[0]).transform.translationX = 2; layerProperties(d.layers[0]).transform.translationY = 3;
        EditorCanvas canvas; QVERIFY(open(canvas, d)); const auto source = editorTools::image(*canvas.selectedRasterPixels());
        const auto expected = EditorGraphicAdjust::makeMask(source, "linearMask", gradient()); QVERIFY(expected.ok());
        const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        EditorGraphicCommand command; setup(command, canvas); QSignalSpy overlay(&canvas, &EditorCanvas::selectionOverlayChanged);
        QVERIFY(command.request("mask-helper", "masking", "linearMask", gradient())); QTRY_VERIFY(command.state()["valid"].toBool());
        QVERIFY(overlay.count() > 0); const auto image = decoded(canvas.selectionOverlay());
        const auto alpha = qRound(expected.coverage[4 * 8 + 4] * 255); QCOMPARE(qAlpha(image.pixel(6, 7)), alpha / 3);
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        QVariantList coverage; for (auto value : expected.coverage) coverage.append(qRound(value * 255) / 255.0);
        const auto adjusted = EditorGraphicAdjust::process(source, "applyLocalAdjustments", {{"exposure", 1.0}, {"mask", QVariantMap{{"width", 8}, {"height", 8}, {"coverage", coverage}}}}); QVERIFY(adjusted.ok());
        QVERIFY(command.request("apply", "masking", "applyLocalAdjustments", exposure(1))); QTRY_VERIFY(command.state()["valid"].toBool());
        QCOMPARE(canvas.selectedRasterPixels()->pixels[4 * 8 + 4], adjusted.image.pixel(4, 4));
    }
    void changedTargetsDiscardTheOldCommand_data() {
        QTest::addColumn<QString>("change");
        for (auto s : {"cancel", "context", "frame", "selection", "artboard", "revision", "replacement", "tool", "lock", "mask", "stroke", "binding"}) QTest::newRow(s) << QString(s);
    }
    void changedTargetsDiscardTheOldCommand() {
        QFETCH(QString, change); auto d = picture();
        if (change == "artboard") {
            d.artboards = {{"a", "A", {{0, 0}, {8, 8}}, 0}, {"b", "B", {{12, 0}, {8, 8}}, 0}};
            for (auto &layer : d.layers) layerProperties(layer).artboardId = "a";
        }
        EditorCanvas canvas; QVERIFY(open(canvas, d)); canvas.configureTool("select", {});
        auto gate = std::make_shared<Gate>(); Release release{gate}; EditorGraphicCommand command(gated(gate), nullptr); setup(command, canvas);
        QVERIFY(command.request("apply", "color", "developLight", exposure())); QTRY_VERIFY(gate->started.load());
        if (change == "cancel") QVERIFY(command.cancel());
        else if (change == "context") command.setContext({{"view", "other"}});
        else if (change == "frame") canvas.setFrame(1);
        else if (change == "selection") QVERIFY(canvas.selectLayer("second"));
        else if (change == "artboard") QVERIFY(canvas.selectArtboard(1, false));
        else if (change == "revision") QVERIFY(canvas.setLayerName("layer", "Other edit"));
        else if (change == "replacement") QVERIFY(open(canvas));
        else if (change == "tool") canvas.configureTool("brush", {});
        else if (change == "lock") QVERIFY(canvas.applyToolField("layers", "field-8", "Pixels"));
        else if (change == "mask") QVERIFY(canvas.selectArea({QPointF(0, 0), QPointF(4, 8)}));
        else if (change == "stroke") QVERIFY(canvas.beginStrokeAt({1, 1}));
        else command.setCanvas(nullptr);
        const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        QVERIFY(!command.state()["pending"].toBool()); gate->release.release(); QTRY_COMPARE(gate->finished.load(), 1); QTest::qWait(30);
        QVERIFY(!command.state()["valid"].toBool()); QCOMPARE(command.state()["sequence"].toInt(), 0);
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        if (canvas.liveStrokeActive()) canvas.configureTool("brush", {});
    }
    void zoomAndPanKeepTheAcceptedCommand() {
        EditorCanvas canvas; QVERIFY(open(canvas)); auto gate = std::make_shared<Gate>(); Release release{gate}; EditorGraphicCommand command(gated(gate), nullptr); setup(command, canvas);
        QVERIFY(command.request("preview", "color", "developLight", exposure())); QTRY_VERIFY(gate->started.load());
        for (int i = 0; i < 100; ++i) { canvas.setZoom(1 + i * .01); canvas.setPanX(i); command.setContext(command.context()); command.setCanvas(&canvas); }
        QVERIFY(command.state()["pending"].toBool()); gate->release.release(); QTRY_VERIFY(command.state()["valid"].toBool()); QCOMPARE(gate->calls.load(), 1);
    }
    void pendingRequestsCoalesceAndOwnerCloseDoesNotJoin() {
        EditorCanvas canvas; QVERIFY(open(canvas)); auto gate = std::make_shared<Gate>(); Release release{gate};
        auto command = std::make_unique<EditorGraphicCommand>(gated(gate), nullptr); setup(*command, canvas);
        QVERIFY(command->request("preview", "color", "developLight", exposure())); QTRY_VERIFY(gate->started.load());
        for (int i = 1; i < 20; ++i) QVERIFY(command->request("preview", "color", "developLight", exposure(i / 20.0)));
        gate->release.release(); QTRY_VERIFY(command->state()["valid"].toBool()); QCOMPARE(gate->calls.load(), 2); QCOMPARE(gate->maximum.load(), 1);
        QCOMPARE(command->state()["sequence"].toInt(), 1);
        const auto expected = EditorGraphicAdjust::process(editorTools::image(*canvas.selectedRasterPixels()), "developLight", exposure(.95)); QVERIFY(expected.ok());
        QCOMPARE(decoded(command->state()["imageSource"].toUrl()).pixel(1, 1), expected.image.pixel(1, 1));
        auto closing = std::make_shared<Gate>(); Release closingRelease{closing}; command = std::make_unique<EditorGraphicCommand>(gated(closing), nullptr); setup(*command, canvas);
        QVERIFY(command->request("preview", "color", "developLight", exposure())); QTRY_VERIFY(closing->started.load());
        QElapsedTimer elapsed; elapsed.start(); command.reset(); const auto us = elapsed.nsecsElapsed() / 1000; QVERIFY(us < 250000);
        closing->release.release(); QTRY_COMPARE(closing->finished.load(), 1); qInfo() << "graphic_image_close_us" << us;
    }
    void settingsAndImageCommandsHaveDifferentChunkedCapabilities_data() {
        QTest::addColumn<QString>("operation"); QTest::addColumn<QVariantMap>("parameters");
        QTest::newRow("point") << QString("samplePointColor") << QVariantMap{{"sample", "#336699"}};
        QTest::newRow("neutral") << QString("sampledWhiteBalance") << QVariantMap{{"sample", "#336699"}};
        QTest::newRow("preset") << QString("whiteBalancePreset") << QVariantMap{{"preset", "Daylight"}};
    }
    void boundedChunkedImagesUseTheRawSourceWithoutWritingIt() {
        Document d; d.canvasMode = CanvasMode::Infinite; d.extent = {8, 8}; d.infiniteCanvas.origin = {-1, -1}; d.infiniteCanvas.chunkSize = 32;
        d.assets.emplace_back(ChunkedRasterAsset{"a", {{-1, -1, makeRasterLayer(32, 32, 0xff336699U)}, {0, 0, makeRasterLayer(32, 32, 0xff996633U)}}});
        LayerProperties p{"layer", "Chunks"}; p.transform.translationX = 9; p.opacity = .25;
        d.layers.emplace_back(StaticBitmapLayer{p, StaticSource{"a"}});
        EditorCanvas canvas; QVERIFY(open(canvas, d)); const auto *raw = canvas.selectedRasterPixels(); QVERIFY(raw);
        const auto expected = EditorGraphicAdjust::process(editorTools::image(*raw), "developLight", exposure()); QVERIFY(expected.ok());
        const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        EditorGraphicCommand command; setup(command, canvas); QVERIFY(command.request("preview", "color", "developLight", exposure()));
        QTRY_VERIFY(command.state()["valid"].toBool()); const auto actual = decoded(command.state()["imageSource"].toUrl());
        QCOMPARE(actual.pixel(0, 0), expected.image.pixel(0, 0)); QCOMPARE(actual.pixel(4, 4), expected.image.pixel(4, 4));
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
    }
    void anApplyObserverCannotPublishTheOldOriginalIntoANewDocument() {
        EditorCanvas canvas; QVERIFY(open(canvas)); EditorGraphicCommand command; setup(command, canvas);
        bool replaced = false; bool loaded = false;
        connect(&canvas, &EditorCanvas::revisionChanged, this, [&] {
            if (replaced) return; replaced = true;
            auto next = picture(); std::get<RasterAsset>(next.assets[0]).pixels = makeRasterLayer(8, 8, 0xff33dd44U);
            loaded = open(canvas, std::move(next));
        });
        QVERIFY(command.request("apply", "color", "developLight", exposure()));
        QTRY_VERIFY(replaced); QVERIFY(loaded); QTRY_VERIFY(!command.state()["pending"].toBool());
        QVERIFY(!command.state()["valid"].toBool()); const auto revision = canvas.revision(); const auto before = encodeIisc(*canvas.document()).bytes;
        QVERIFY(canvas.resetTool("color", {})); QCOMPARE(canvas.selectedRasterPixels()->pixels[9], 0xff33dd44U);
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
    }
    void settingsAndImageCommandsHaveDifferentChunkedCapabilities() {
        QFETCH(QString, operation); QFETCH(QVariantMap, parameters);
        Document d; d.canvasMode = CanvasMode::Infinite; d.extent = {16384, 16384}; d.infiniteCanvas.chunkSize = 32;
        d.assets.emplace_back(ChunkedRasterAsset{"a", {{0, 0, makeRasterLayer(32, 32, 0xff336699U)}}});
        d.layers.emplace_back(StaticBitmapLayer{{"layer", "Chunks"}, StaticSource{"a"}});
        EditorCanvas canvas; QVERIFY(open(canvas, d)); const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        const auto state = canvas.graphicOperationState(operation); QVERIFY(state["enabled"].toBool()); QVERIFY(!state["imageEnabled"].toBool());
        QVERIFY(canvas.previewGraphicOperation("color", operation, parameters).isEmpty()); QVERIFY(!canvas.applyGraphicOperation("color", operation, parameters));
        EditorGraphicCommand command; setup(command, canvas); QVERIFY(!command.request("preview", "color", operation, parameters));
        EditorGraphicHelper helper; helper.setCanvas(&canvas); QVERIFY(helper.request(operation, parameters)); QTRY_VERIFY(helper.state()["valid"].toBool());
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
    }
    void scriptParametersAreDetachedAndGuiObjectsAreRejected() {
        EditorCanvas canvas; QVERIFY(open(canvas)); auto gate = std::make_shared<Gate>(); Release release{gate}; EditorGraphicCommand command(gated(gate), nullptr); setup(command, canvas);
        auto engine = std::make_unique<QJSEngine>(); const auto js = engine->evaluate("({exposure: 0.5})");
        QVERIFY(command.request("preview", "color", "developLight", {{"exposure", QVariant::fromValue(js.property("exposure"))}})); QTRY_VERIFY(gate->started.load());
        engine.reset(); gate->release.release(); QTRY_VERIFY(command.state()["valid"].toBool());
        QObject object; QVERIFY(!command.request("preview", "color", "developLight", {{"exposure", QVariant::fromValue(&object)}}));
        QVERIFY(command.state()["error"].toString().contains("GUI objects"));
        QPointer<QObject> observed(&object);
        QVERIFY(!command.request("preview", "color", "developLight", {{"exposure", QVariant::fromValue(observed)}}));
        const auto shared = QSharedPointer<QObject>::create();
        QVERIFY(!command.request("preview", "color", "developLight", {{"exposure", QVariant::fromValue(shared)}}));
    }
    void malformedMaskResultsPreserveSelectionAndCanRetry_data() {
        QTest::addColumn<QString>("change");
        for (const auto value : {"extent", "alpha-size", "count-overflow", "count-mismatch", "missing-bits", "bits-size", "bits-format", "bits-content", "bits-palette", "missing-overlay", "invalid-overlay", "overlay-size", "overlay-content", "operand", "wrong-kind"}) QTest::newRow(value) << QString(value);
    }
    void malformedMaskResultsPreserveSelectionAndCanRetry() {
        QFETCH(QString, change); EditorCanvas canvas; QVERIFY(open(canvas)); canvas.configureTool("select", {}); QVERIFY(canvas.selectArea({QPointF(0, 0), QPointF(4, 8)}));
        const auto path = m_storage.filePath("Files/invalid-mask-" + change + ".iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        const auto native = std::filesystem::path(path.toStdU16String()); const auto saved = iiFileProvider::native::File::read(native); const auto bytes = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision(); const auto overlay = canvas.selectionOverlay();
        auto calls = std::make_shared<std::atomic_int>(0); EditorGraphicCommand task([change, calls](const graphicImage::Input &input, std::stop_token stop) {
            auto r = graphicImage::calculate(input, stop);
            if (++*calls != 1 || !r.ok()) return r;
            if (change == "extent") r.selection.extent = {0, 0};
            else if (change == "alpha-size") r.selection.alpha.pop_back();
            else if (change == "count-overflow") r.selectedPixels = 65;
            else if (change == "count-mismatch") r.selectedPixels = 0;
            else if (change == "missing-bits") r.selectionBits = {};
            else if (change == "bits-size") r.selectionBits = QImage(1, 1, QImage::Format_MonoLSB);
            else if (change == "bits-format") r.selectionBits = r.selectionBits.convertToFormat(QImage::Format_ARGB32);
            else if (change == "bits-content") r.selectionBits.fill(0);
            else if (change == "bits-palette") r.selectionBits.setColor(0, qRgb(0, 0, 0));
            else if (change == "missing-overlay") r.selectionOverlay = QUrl{};
            else if (change == "invalid-overlay") r.selectionOverlay = QUrl("data:image/png;base64,eA==");
            else if (change == "overlay-size" || change == "overlay-content") { QImage image(change == "overlay-size" ? QSize(1, 1) : QSize(8, 8), QImage::Format_ARGB32); image.fill(Qt::transparent); r.selectionOverlay = editorTools::dataImage(image); }
            else if (change == "operand") { r.replaceOperand = true; r.operand = std::make_shared<const RasterMask>(RasterMask{{1, 1}, {255}}); }
            else if (change == "wrong-kind") { r.mask = false; r.pixels = std::make_shared<const RasterLayer>(makeRasterLayer(8, 8, 0xff00ff00U)); }
            return r;
        }, nullptr); setup(task, canvas); QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged), revisions(&canvas, &EditorCanvas::revisionChanged);
        QVERIFY(task.request("mask-helper", "masking", "linearMask", {})); QTRY_VERIFY(!task.state()["pending"].toBool());
        QVERIFY2(!task.state()["valid"].toBool(), "A malformed mask result was reported as applied."); QCOMPARE(task.state()["status"].toString(), QString("error")); QVERIFY(!task.state()["error"].toString().isEmpty());
        QCOMPARE(canvas.selectionOverlay(), overlay); QVERIFY(canvas.toolState()["selectionActive"].toBool()); QCOMPARE(canvas.revision(), revision); QCOMPARE(revisions.count(), 0); QCOMPARE(files.count(), 0); QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes); QCOMPARE(iiFileProvider::native::File::read(native), saved);
        QVERIFY(task.request("mask-helper", "masking", "linearMask", {})); QTRY_VERIFY(task.state()["valid"].toBool()); QCOMPARE(calls->load(), 2); QCOMPARE(canvas.revision(), revision);
    }
    void zeroCoverageMasksAreValidResults() {
        EditorCanvas canvas; QVERIFY(open(canvas)); canvas.configureTool("select", {}); QVERIFY(canvas.selectArea({QPointF(0, 0), QPointF(8, 8)}));
        EditorGraphicCommand task; setup(task, canvas); QVERIFY(task.request("mask-helper", "masking", "invertMask", {})); QTRY_VERIFY(!task.state()["pending"].toBool());
        QVERIFY2(task.state()["valid"].toBool(), qPrintable(task.state()["error"].toString())); QVERIFY(!canvas.toolState()["selectionActive"].toBool());
    }
    void failedImageEncodingDoesNotPublishADataUrl() {
        QVERIFY(editorTools::dataImage(QImage{}).isEmpty());
        QImage valid(1, 1, QImage::Format_ARGB32); valid.fill(Qt::transparent); const auto url = editorTools::dataImage(valid); QVERIFY(!url.isEmpty()); QCOMPARE(decoded(url).size(), QSize(1, 1));
    }
    void maskHelperRejectsPixelEditingOperations() {
        EditorCanvas canvas; QVERIFY(open(canvas)); const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        EditorGraphicCommand task; setup(task, canvas); const auto token = task.request("mask-helper", "color", "developLight", exposure());
        QTRY_VERIFY(!task.state()["pending"].toBool()); QCOMPARE(token, qulonglong(0)); QVERIFY(!task.state()["valid"].toBool()); QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QCOMPARE(canvas.revision(), revision);
    }
    void failureRecoversAndReadyObserversMayRestartOrDeleteTheOwner() {
        EditorCanvas canvas; QVERIFY(open(canvas));
        EditorGraphicCommand failing([](const auto &input, std::stop_token stop) -> graphicImage::Result {
            if (input.parameters.value("exposure").toDouble() == 0) throw std::runtime_error("Image fixture failure");
            return graphicImage::calculate(input, stop);
        }, nullptr); setup(failing, canvas);
        QVERIFY(failing.request("preview", "color", "developLight", exposure(0))); QTRY_COMPARE(failing.state()["status"].toString(), QString("error"));
        QVERIFY(failing.request("preview", "color", "developLight", exposure(.5))); QTRY_VERIFY(failing.state()["valid"].toBool());
        auto owner = std::make_unique<EditorGraphicCommand>(); setup(*owner, canvas); bool deleted = false;
        connect(owner.get(), &EditorGraphicCommand::changed, this, [&] { if (owner->state()["valid"].toBool()) { deleted = true; owner.reset(); } });
        QVERIFY(owner->request("preview", "color", "developLight", exposure())); QTRY_VERIFY(deleted); QVERIFY(!owner);
        EditorGraphicCommand restart; setup(restart, canvas); int ready = 0;
        connect(&restart, &EditorGraphicCommand::changed, this, [&] { if (restart.state()["valid"].toBool() && ++ready == 1) restart.request("preview", "color", "developLight", exposure(1)); });
        QVERIFY(restart.request("preview", "color", "developLight", exposure())); QTRY_COMPARE(ready, 2);
    }
};
QTEST_MAIN(GraphicCommandTests)
#include "tst_graphiccommand.moc"
