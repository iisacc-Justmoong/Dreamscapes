#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorGraphicAdjust.h"
#include "App/Views/Editor/EditorGraphicHelper.h"
#include "App/Views/Editor/EditorToolUtils.h"
#include <iiSharedCanvas.h>
#include <iiFileProviderNative.h>
#include <SocietyDrive.h>
#include <QMouseEvent>
#include <QColorSpace>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
using namespace iiSharedCanvas;
namespace {
Document picture(bool dynamic = true, bool repeated = false) {
    Document d; d.extent = {8, 8}; d.timeline.frameCount = 3;
    auto a = makeRasterLayer(8, 8, 0xff663322U), b = makeRasterLayer(8, 8, 0xff223366U);
    a.pixels[0] = 0xff445566U; b.pixels[0] = 0xff665544U;
    d.assets.emplace_back(RasterAsset{"a", a}); d.assets.emplace_back(RasterAsset{"b", b});
    LayerProperties p{"layer", "Image"};
    if (dynamic) {
        d.layers.emplace_back(DynamicBitmapLayer{p, KeyframedSource{{0, 1, 2}}});
        d.frames = {{0, {{"layer", "a"}}}, {1, {{"layer", repeated ? "a" : "b"}}}, {2, {{"layer", "a"}}}};
    } else d.layers.emplace_back(StaticBitmapLayer{p, StaticSource{"a"}});
    return d;
}
// IISC raster words are tested here. The GUI adapter tags its QImage output as
// sRGB, while a raw CanvasItem raster has no QImage profile metadata.
QImage srgb(QImage image) { image.setColorSpace(QColorSpace::SRgb); return image; }
QImage pixels(EditorCanvas &canvas) { const auto *p = canvas.selectedRasterPixels(); return p ? srgb(editorTools::image(*p)) : QImage{}; }
QImage previewImage(const QVariantMap &preview, const QString &field = "imageSource") {
    const auto data = preview.value(field).toString();
    return srgb(QImage::fromData(QByteArray::fromBase64(data.mid(data.indexOf(',') + 1).toLatin1())));
}
bool open(EditorCanvas &canvas, Document d) { return canvas.loadProjectPage(std::move(d), "Dynamic pixels") && canvas.selectLayer("layer") && canvas.rasterLayerSelected(); }
QVariantMap light(double exposure) { return {{"exposure", exposure}}; }
}
class DynamicRasterTests final : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/dynamic-documents-XXXXXX.societycontainer"};
private slots:
    void initTestCase() {
        QVERIFY(m_storage.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH", m_storage.path().toUtf8());
    }
    void selectionAndSeekingDoNotMutateTheDocumentOrStartStorage() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Selection"));
        auto *storage = canvas.findChild<iiSocietyHelper::FileSystem *>("editorDocumentStorage"); QVERIFY(storage); QVERIFY(!storage->isAvailable());
        const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        QSignalSpy documents(&canvas, &EditorCanvas::documentInfoChanged), files(&canvas, &EditorCanvas::fileInformationChanged);
        QSignalSpy revisions(&canvas, &EditorCanvas::revisionChanged), activated(storage, &iiSocietyHelper::FileSystem::storageChanged);
        QVERIFY(canvas.selectLayer("layer")); QVERIFY(canvas.rasterLayerSelected());
        QVERIFY(canvas.graphicOperationState("developLight")["enabled"].toBool());
        for (int i = 0; i < 12; ++i) {
            canvas.setFrame(i % 3); QVERIFY(canvas.rasterLayerSelected());
            QCOMPARE(pixels(canvas).pixel(1, 1), i % 3 == 1 ? 0xff223366U : 0xff663322U);
        }
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        QCOMPARE(documents.count(), 0); QCOMPARE(files.count(), 0); QCOMPARE(revisions.count(), 0); QCOMPARE(activated.count(), 0);
    }
    void previewAndApplyUseTheCurrentDynamicAsset() {
        EditorCanvas canvas; QVERIFY(open(canvas, picture()));
        QVERIFY(canvas.applyGraphicOperation("color", "developLight", light(1)));
        canvas.setFrame(1); const auto current = pixels(canvas); const auto bytes = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged);
        const auto expected = EditorGraphicAdjust::process(current, "developLight", light(.5)); QVERIFY(expected.ok());
        const auto preview = canvas.previewGraphicOperation("color", "developLight", light(.5));
        QCOMPARE(previewImage(preview, "originalImageSource"), current); QCOMPARE(previewImage(preview), expected.image);
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes); QCOMPARE(files.count(), 0);
        QVERIFY(canvas.applyGraphicOperation("color", "developLight", light(.5))); QCOMPARE(pixels(canvas), expected.image);
        canvas.setFrame(0); const auto first = EditorGraphicAdjust::process(editorTools::image(std::get<RasterAsset>(picture().assets[0]).pixels), "developLight", light(1));
        QVERIFY(first.ok()); QCOMPARE(pixels(canvas), first.image); QVERIFY(std::holds_alternative<DynamicBitmapLayer>(canvas.document()->layers[0]));
    }
    void sharedSourcesKeepNonCumulativeOriginals_data() {
        QTest::addColumn<bool>("dynamic"); QTest::newRow("static") << false; QTest::newRow("repeated-dynamic-source") << true;
    }
    void sharedSourcesKeepNonCumulativeOriginals() {
        QFETCH(bool, dynamic); EditorCanvas canvas; QVERIFY(open(canvas, picture(dynamic, true))); const auto original = pixels(canvas);
        QVERIFY(canvas.applyGraphicOperation("color", "developLight", light(1))); canvas.setFrame(1);
        const auto expected = EditorGraphicAdjust::process(original, "developLight", light(.5)); QVERIFY(expected.ok());
        const auto preview = canvas.previewGraphicOperation("color", "developLight", light(.5)); QCOMPARE(previewImage(preview, "originalImageSource"), original);
        QVERIFY(canvas.applyGraphicOperation("color", "developLight", light(.5))); QCOMPARE(pixels(canvas), expected.image);
        QVERIFY(canvas.resetTool("color", {})); QCOMPARE(pixels(canvas), original);
    }
    void parameterHelperUsesTheCurrentFrameAfterAnOriginalSession() {
        EditorCanvas canvas; QVERIFY(open(canvas, picture())); QVERIFY(canvas.applyGraphicOperation("color", "autoWhiteBalance", {}));
        canvas.setFrame(1); const auto current = pixels(canvas); const auto expected = EditorGraphicAdjust::process(current, "autoWhiteBalance", {}); QVERIFY(expected.ok());
        const auto revision = canvas.revision(); const auto bytes = encodeIisc(*canvas.document()).bytes;
        EditorGraphicHelper helper; helper.setCanvas(&canvas); helper.setContext({{"tool", "color"}, {"view", "test"}});
        QVERIFY(helper.request("autoWhiteBalance", {})); QTRY_VERIFY(helper.state()["valid"].toBool());
        QCOMPARE(helper.state()["parameters"].toMap(), expected.parameters); QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
    }
    void resetOfAnUnrelatedHugeChunkedSourceUsesOnlyMetadata() {
        Document d; d.canvasMode = CanvasMode::Infinite; d.extent = {16384, 16384}; d.infiniteCanvas.chunkSize = 32; d.timeline.frameCount = 2;
        d.assets.emplace_back(RasterAsset{"small", makeRasterLayer(32, 32, 0xff663322U)});
        d.assets.emplace_back(ChunkedRasterAsset{"huge", {{0, 0, makeRasterLayer(32, 32, 0xff223366U)}}});
        d.layers.emplace_back(DynamicBitmapLayer{{"layer", "Chunked"}, KeyframedSource{{0, 1}}});
        d.frames = {{0, {{"layer", "small"}}}, {1, {{"layer", "huge"}}}};
        EditorCanvas canvas; QVERIFY2(open(canvas, d), qPrintable(canvas.error()));
        QVERIFY(canvas.applyGraphicOperation("color", "developLight", light(1))); canvas.setFrame(1);
        QVERIFY(canvas.rasterLayerSelected()); QVERIFY(!canvas.selectedRasterPixels());
        const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        QElapsedTimer elapsed; elapsed.start();
        QVERIFY(canvas.resetTool("color", {}));
        const auto microseconds = elapsed.nsecsElapsed() / 1000;
        QVERIFY(microseconds < 250000); qInfo() << "unrelated_chunked_reset_us" << microseconds;
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
    }
    void operationKindsKeepTheirOwnOriginals() {
        EditorCanvas canvas; QVERIFY(open(canvas, picture(false))); QVERIFY(canvas.applyGraphicOperation("color", "developLight", light(1)));
        const auto beforeLegacy = *canvas.selectedRasterPixels(); ColorAdjustment adjustment; adjustment.exposure = .5;
        const auto expected = adjustRaster(beforeLegacy, adjustment); QVERIFY(expected.ok());
        QVERIFY(canvas.applyToolField("color", "field-1", .5)); QCOMPARE(pixels(canvas), srgb(editorTools::image(expected.pixels)));
        const auto beforeGraphic = pixels(canvas); const auto next = EditorGraphicAdjust::process(beforeGraphic, "developLight", light(.25)); QVERIFY(next.ok());
        QVERIFY(canvas.applyGraphicOperation("color", "developLight", light(.25))); QCOMPARE(pixels(canvas), next.image);
    }
    void resetMustNotWriteAnotherFramesOriginal() {
        EditorCanvas canvas; QVERIFY(open(canvas, picture())); QVERIFY(canvas.applyGraphicOperation("color", "developLight", light(1)));
        canvas.setFrame(1); const auto original = pixels(canvas); const auto revision = canvas.revision();
        const auto before = encodeIisc(*canvas.document()).bytes; QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged);
        QVERIFY(canvas.resetTool("color", {})); QCOMPARE(pixels(canvas), original);
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QCOMPARE(files.count(), 0);
    }
    void legacyAdjustmentsUseTheCurrentAsset() {
        EditorCanvas canvas; QVERIFY(open(canvas, picture()));
        QVERIFY(canvas.applyToolField("color", "field-1", 1)); canvas.setFrame(1); const auto current = *canvas.selectedRasterPixels();
        ColorAdjustment adjustment; adjustment.exposure = .5; const auto expected = adjustRaster(current, adjustment); QVERIFY(expected.ok());
        QVERIFY(canvas.applyToolField("color", "field-1", .5)); QCOMPARE(pixels(canvas), srgb(editorTools::image(expected.pixels)));
    }
    void paintingWritesOnlyTheSampledDynamicAsset() {
        EditorCanvas canvas; QVERIFY(open(canvas, picture())); canvas.setSize({32, 32}); canvas.setZoom(1);
        const auto before = pixels(canvas); canvas.configureTool("brush", {{"field-4", 2}, {"field-5", 100}, {"field-8", 100}, {"field-9", 100}});
        canvas.setBrushColor(QColor(Qt::green)); QVERIFY(canvas.beginStrokeAt({3, 3})); QVERIFY(canvas.endStrokeAt({3, 3}));
        QVERIFY(pixels(canvas) != before); const auto first = pixels(canvas); canvas.setFrame(1);
        QCOMPARE(pixels(canvas).pixel(1, 1), 0xff223366U); canvas.setFrame(2); QCOMPARE(pixels(canvas), first);
        QVERIFY(std::holds_alternative<DynamicBitmapLayer>(canvas.document()->layers[0]));
    }
    void eraserOverlayMustNotFollowAnotherFrame() {
        EditorCanvas canvas; QVERIFY(open(canvas, picture())); canvas.setSize({32, 32}); canvas.setZoom(1);
        canvas.configureTool("eraser", {{"selector", "Pixel"}, {"field-0", 2}, {"field-1", 100}, {"field-2", 100}, {"field-3", true}, {"field-13", true}});
        QVERIFY(canvas.beginStrokeAt({3, 3})); QVERIFY(canvas.endStrokeAt({3, 3}));
        QVERIFY(!canvas.toolState()["eraserOverlay"].toUrl().isEmpty()); canvas.setFrame(1);
        QVERIFY(canvas.toolState()["eraserOverlay"].toUrl().isEmpty());
        canvas.setFrame(2); QVERIFY(!canvas.toolState()["eraserOverlay"].toUrl().isEmpty());
    }
    void frameChangeCancelsUncommittedPainting() {
        EditorCanvas canvas; QVERIFY(open(canvas, picture())); canvas.setSize({32, 32}); canvas.setZoom(1);
        canvas.configureTool("brush", {{"field-4", 2}, {"field-5", 100}, {"field-8", 100}, {"field-9", 100}});
        const auto path = m_storage.filePath("Files/cancelled-stroke.iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        const auto file = std::filesystem::path(path.toStdU16String()); const auto saved = iiFileProvider::native::File::read(file); const auto alias = canvas.fileAlias();
        const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        QVERIFY(canvas.beginStrokeAt({3, 3})); canvas.setFrame(1); QVERIFY(!canvas.liveStrokeActive());
        QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QCOMPARE(iiFileProvider::native::File::read(file), saved); QCOMPARE(canvas.fileAlias(), alias);
        // Cancelling real paint rolls pixels back and may publish an edit
        // revision. The subsequent pure seek must remain revision-free.
        QVERIFY(canvas.revision() >= revision); const auto cancelledRevision = canvas.revision(); canvas.setFrame(2); QCOMPARE(canvas.revision(), cancelledRevision);
        QVERIFY(canvas.setLayerName("layer", "After seek")); QVERIFY(canvas.undo()); QCOMPARE(canvas.selectedLayer()["name"].toString(), QString("Image"));
    }
    void persistedDynamicPixelsAndPureNavigationStaySeparate() {
        EditorCanvas canvas; QVERIFY(open(canvas, picture()));
        const auto path = m_storage.filePath("Files/dynamic.iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        const auto native = std::filesystem::path(path.toStdU16String()); const auto before = iiFileProvider::native::File::read(native); const auto alias = canvas.fileAlias(); const auto revision = canvas.revision();
        QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged); canvas.setFrame(1); QVERIFY(canvas.selectLayer("layer"));
        QCOMPARE(iiFileProvider::native::File::read(native), before); QCOMPARE(canvas.fileAlias(), alias); QCOMPARE(canvas.revision(), revision); QCOMPARE(files.count(), 0);
        const auto original = pixels(canvas); const auto expected = EditorGraphicAdjust::process(original, "developLight", light(.5)); QVERIFY(expected.ok());
        QVERIFY(canvas.applyGraphicOperation("color", "developLight", light(.5))); QVERIFY(iiFileProvider::native::File::read(native) != before);
        EditorCanvas reopened; QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path))); reopened.setFrame(1); QVERIFY(reopened.selectLayer("layer"));
        QCOMPARE(pixels(reopened), expected.image); reopened.setFrame(0); QCOMPARE(pixels(reopened).pixel(1, 1), 0xff663322U);
        QVERIFY(std::holds_alternative<DynamicBitmapLayer>(reopened.document()->layers[0]));
    }
};
QTEST_MAIN(DynamicRasterTests)
#include "tst_dynamicraster.moc"
