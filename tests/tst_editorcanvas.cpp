#include "App/Views/Editor/EditorCanvas.h"
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <iiFileProvider.h>
#include <Stroke/Rasterizer.h>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <array>

using namespace iiSharedCanvas;
namespace {
// Independent iiPaintEngine reference: the product is driven through its panel
// values and stroke API, while this reference has no CanvasItem/BitmapEditor.
void paintWithEngine(RasterLayer &pixels, const BrushState &brush,
                     const std::array<QPointF, 3> &positions,
                     double pressure, CanvasOrigin origin) {
    RasterDabStream stream;
    RasterProjection projection;
    projection.documentOrigin = {double(origin.x), double(origin.y)};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        StrokePoint point;
        point.position = {positions[index].x(), positions[index].y()};
        point.pressure = pressure;
        point.time = double(index + 1) / 120;
        const auto dabs = appendRasterDabs(stream, point, brush, index + 1 == positions.size());
        paintRasterSamples(pixels, projectBrushDabs(dabs, brush.rasterizer, projection, brush.material));
    }
}
}
class EditorCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void finiteViewportLocksFittingAxesAndClampsOverflow_data() {
        QTest::addColumn<QSize>("extent");
        QTest::addColumn<QSizeF>("viewport");
        QTest::addColumn<double>("scale");
        QTest::addColumn<QPointF>("xRange");
        QTest::addColumn<QPointF>("yRange");
        QTest::newRow("both-fit") << QSize(400, 200) << QSizeF(800, 600) << 1.0 << QPointF(200, 200) << QPointF(200, 200);
        QTest::newRow("wide") << QSize(800, 200) << QSizeF(400, 300) << 1.0 << QPointF(-400, 0) << QPointF(50, 50);
        QTest::newRow("tall") << QSize(200, 800) << QSizeF(400, 300) << 1.0 << QPointF(100, 100) << QPointF(-500, 0);
        QTest::newRow("both-overflow") << QSize(800, 600) << QSizeF(400, 300) << 1.0 << QPointF(-400, 0) << QPointF(-300, 0);
        QTest::newRow("exact-fit") << QSize(400, 300) << QSizeF(400, 300) << 1.0 << QPointF(0, 0) << QPointF(0, 0);
        QTest::newRow("fractional-fit") << QSize(101, 83) << QSizeF(333, 271) << 2.5 << QPointF(40.25, 40.25) << QPointF(31.75, 31.75);
        QTest::newRow("fractional-overflow") << QSize(201, 113) << QSizeF(300, 150) << 1.7 << QPointF(-41.7, 0) << QPointF(-42.1, 0);
    }
    void finiteViewportLocksFittingAxesAndClampsOverflow() {
        QFETCH(QSize, extent); QFETCH(QSizeF, viewport); QFETCH(double, scale);
        QFETCH(QPointF, xRange); QFETCH(QPointF, yRange);
        EditorCanvas canvas; canvas.setSize(viewport);
        QVERIFY(canvas.createCanvas({{"width", extent.width()}, {"height", extent.height()}, {"unit", "px"}, {"background", "Transparent"}}));
        canvas.setZoom(scale);
        const auto revision = canvas.revision();
        canvas.panBy(1000000, 1000000);
        QVERIFY(qAbs(canvas.panX() - xRange.y()) < 0.00001);
        QVERIFY(qAbs(canvas.panY() - yRange.y()) < 0.00001);
        canvas.setPanX(-1000000); canvas.setPanY(-1000000);
        QVERIFY(qAbs(canvas.panX() - xRange.x()) < 0.00001);
        QVERIFY(qAbs(canvas.panY() - yRange.x()) < 0.00001);
        canvas.panBy(7, 9);
        QVERIFY(qAbs(canvas.panX() - std::min(xRange.x() + 7, xRange.y())) < 0.00001);
        QVERIFY(qAbs(canvas.panY() - std::min(yRange.x() + 9, yRange.y())) < 0.00001);
        // Cursor-anchored zoom-out must restore central alignment once it fits.
        canvas.zoomAt(0.1, {viewport.width() - 1, 1});
        QVERIFY(qAbs(canvas.panX() - (viewport.width() - extent.width() * canvas.zoom()) / 2) < 0.00001);
        QVERIFY(qAbs(canvas.panY() - (viewport.height() - extent.height() * canvas.zoom()) / 2) < 0.00001);
        QCOMPARE(canvas.revision(), revision);
    }
    void viewportResizeResetAndInfiniteBoundary() {
        EditorCanvas canvas; canvas.setSize({400, 300});
        QVERIFY(canvas.createCanvas({{"width", 800}, {"height", 600}, {"unit", "px"}, {"background", "Transparent"}}));
        canvas.setZoom(1); canvas.setPanX(-250); canvas.setPanY(-175);
        canvas.setSize({1000, 800});
        QCOMPARE(canvas.zoom(), 1.0); QCOMPARE(canvas.panX(), 100.0); QCOMPARE(canvas.panY(), 100.0);
        canvas.setSize({400, 300});
        QVERIFY(canvas.panX() >= -400 && canvas.panX() <= 0);
        QVERIFY(canvas.panY() >= -300 && canvas.panY() <= 0);
        canvas.fitToView(); QCOMPARE(canvas.panX(), 0.0); QCOMPARE(canvas.panY(), 0.0);
        canvas.setSize({1000, 800}); canvas.resetView();
        QCOMPARE(canvas.panX(), 100.0); QCOMPARE(canvas.panY(), 100.0);
        QVERIFY(canvas.createInfiniteRasterDocument(32, 32, 32));
        const auto x = canvas.panX(), y = canvas.panY();
        canvas.panBy(1000000, -1000000);
        QCOMPARE(canvas.panX(), x + 1000000); QCOMPARE(canvas.panY(), y - 1000000);
    }
    void selectedImagesBecomeIndependentNativeLayers() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/editor-images-XXXXXX");
        QVERIFY(directory.isValid());
        const auto first = directory.filePath("red %20 #.png"), second = directory.filePath("blue.png");
        QImage red(64, 40, QImage::Format_ARGB32); red.fill(Qt::red);
        QImage blue(32, 60, QImage::Format_ARGB32); blue.fill(Qt::blue);
        QVERIFY(red.save(first));
        QVERIFY(blue.save(second));
        const auto originalRed = iiFileProvider::File::read(first), originalBlue = iiFileProvider::File::read(second);
        EditorCanvas canvas;
        QVERIFY(canvas.openImages({QUrl::fromLocalFile(first), QUrl::fromLocalFile(second), QUrl::fromLocalFile(first)}));
        QCOMPARE(canvas.canvasWidth(), 64);
        QCOMPARE(canvas.canvasHeight(), 60);
        QCOMPARE(canvas.document()->layers.size(), 2u);
        QCOMPARE(canvas.document()->assets.size(), 2u);
        QCOMPARE(std::get<RasterAsset>(canvas.document()->assets[0]).pixels.pixels.front(), 0xffff0000U);
        QCOMPARE(std::get<RasterAsset>(canvas.document()->assets[1]).pixels.pixels.front(), 0xff0000ffU);
        QCOMPARE(layerProperties(canvas.document()->layers[0]).transform.translationY, 10.0);
        QCOMPARE(layerProperties(canvas.document()->layers[1]).transform.translationX, 16.0);
        QVERIFY(canvas.rasterLayerSelected());
        const auto path = directory.filePath("layers.iisc");
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        const auto before = iiFileProvider::File::read(path);
        const auto revision = canvas.revision();
        QVERIFY(!canvas.openImages({QUrl::fromLocalFile(first), QUrl::fromLocalFile(directory.filePath("missing.png"))}));
        QVERIFY(!canvas.openImages({QUrl("https://example.com/image.png")}));
        QVERIFY(!canvas.openImages({}));
        QCOMPARE(canvas.filePath(), path);
        QCOMPARE(canvas.revision(), revision);
        QCOMPARE(canvas.document()->layers.size(), 2u);
        QCOMPARE(iiFileProvider::File::read(path), before);
        QCOMPARE(iiFileProvider::File::read(first), originalRed);
        QCOMPARE(iiFileProvider::File::read(second), originalBlue);
        EditorCanvas reopened;
        QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path)));
        QCOMPARE(reopened.document()->layers.size(), 2u);
        QCOMPARE(std::get<RasterAsset>(reopened.document()->assets[0]).pixels.pixels, std::get<RasterAsset>(canvas.document()->assets[0]).pixels.pixels);
        QCOMPARE(std::get<RasterAsset>(reopened.document()->assets[1]).pixels.pixels, std::get<RasterAsset>(canvas.document()->assets[1]).pixels.pixels);
    }
    void brushPixelsMatchIiPaintEngine_data() {
        QTest::addColumn<bool>("infinite");
        QTest::addColumn<double>("hardness");
        QTest::addColumn<double>("opacity");
        QTest::addColumn<double>("flow");
        QTest::addColumn<double>("spacing");
        QTest::addColumn<double>("pressure");
        for (bool infinite : {false, true}) {
            const auto prefix = infinite ? "infinite-" : "finite-";
            QTest::newRow(qPrintable(QString(prefix) + "opaque")) << infinite << 1.0 << 1.0 << 1.0 << 0.15 << 1.0;
            QTest::newRow(qPrintable(QString(prefix) + "soft-translucent")) << infinite << 0.35 << 0.65 << 0.3 << 0.12 << 1.0;
            QTest::newRow(qPrintable(QString(prefix) + "spaced-pressure")) << infinite << 0.7 << 0.8 << 0.6 << 0.7 << 0.4;
            QTest::newRow(qPrintable(QString(prefix) + "zero-flow")) << infinite << 1.0 << 1.0 << 0.0 << 0.15 << 1.0;
        }
    }
    void brushPixelsMatchIiPaintEngine() {
        QFETCH(bool, infinite);
        QFETCH(double, hardness);
        QFETCH(double, opacity);
        QFETCH(double, flow);
        QFETCH(double, spacing);
        QFETCH(double, pressure);
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/paint-engine-XXXXXX");
        QVERIFY(directory.isValid());
        Document initial;
        initial.extent = {96, 64};
        initial.canvasMode = infinite ? CanvasMode::Infinite : CanvasMode::Finite;
        if (infinite) initial.infiniteCanvas = {{-32, -32}, 32};
        const auto path = directory.filePath("paint.iisc");
        DocumentFile file;
        const auto created = file.create(path.toStdString(), initial);
        QVERIFY2(created.ok(), created.message.c_str());
        file.close();
        EditorCanvas canvas;
        QVERIFY(canvas.openDocumentSource(QUrl::fromLocalFile(path)));
        canvas.setBrushColor(QColor("#8B7CFF"));
        canvas.configureTool("brush", {{"field-4", 14}, {"field-5", hardness * 100},
            {"field-8", opacity * 100}, {"field-9", flow * 100},
            {"field-16", spacing * 100}, {"field-17", 0}});
        const std::array<QPointF, 3> positions = infinite
            ? std::array<QPointF, 3>{{{-18, -4}, {8, -2}, {38, 6}}}
            : std::array<QPointF, 3>{{{8, 16}, {32, 20}, {56, 24}}};
        const CanvasOrigin origin = infinite ? CanvasOrigin{-32, -32} : CanvasOrigin{};
        auto expected = makeRasterLayer(96, 64);
        BrushState reference;
        reference.rasterizer.brushSize = 14;
        reference.rasterizer.radius = 7;
        reference.rasterizer.argb = 0xff8b7cffU;
        reference.rasterizer.hardness = hardness;
        reference.rasterizer.opacity = opacity;
        reference.rasterizer.flow = flow;
        reference.rasterizer.spacingRatio = spacing;
        reference.dynamics.pressureToSize = 1;
        reference.dynamics.pressureToFlow = 1;
        reference.dynamics.pressureToOpacity = 1;
        reference.randomSeed = 1;
        paintWithEngine(expected, reference, positions, pressure, origin);
        QVERIFY(canvas.beginStrokeAt(positions[0], pressure));
        QVERIFY(canvas.continueStrokeAt(positions[1], pressure));
        QVERIFY(canvas.endStrokeAt(positions[2], pressure));
        QVERIFY(canvas.rasterLayerSelected());
        QVERIFY(file.open(path.toStdString()).ok());
        QCOMPARE(file.document()->layers.size(), 1u);
        QCOMPARE(infinite, std::holds_alternative<ChunkedRasterAsset>(file.document()->assets.front()));
        auto rendered = renderFrame(*file.document(), 0);
        QVERIFY(rendered.ok());
        QCOMPARE(rendered.pixels.pixels, expected.pixels);
        QCOMPARE(std::any_of(expected.pixels.begin(), expected.pixels.end(), [](auto pixel) { return pixel != 0; }), flow > 0);
        file.close();

        canvas.configureTool("eraser", {{"selector", "Pixel"}, {"field-0", 10},
            {"field-1", 50}, {"field-2", 60}});
        reference.rasterizer.brushSize = 10;
        reference.rasterizer.radius = 5;
        reference.rasterizer.argb = 0xff000000U;
        reference.rasterizer.hardness = 0.5;
        reference.rasterizer.opacity = 0.6;
        reference.rasterizer.flow = 1;
        reference.rasterizer.blendMode = RasterBlendMode::DestinationOut;
        reference.randomSeed = 2;
        const auto paintedPixels = expected.pixels;
        paintWithEngine(expected, reference, positions, pressure, origin);
        QVERIFY(canvas.beginStrokeAt(positions[0], pressure));
        QVERIFY(canvas.continueStrokeAt(positions[1], pressure));
        QVERIFY(canvas.endStrokeAt(positions[2], pressure));
        QVERIFY(file.open(path.toStdString()).ok());
        rendered = renderFrame(*file.document(), 0);
        QVERIFY(rendered.ok());
        QCOMPARE(rendered.pixels.pixels, expected.pixels);
        file.close();
        if (flow > 0) {
            QVERIFY(paintedPixels != expected.pixels);
            QVERIFY(canvas.undo());
            QVERIFY(file.open(path.toStdString()).ok());
            QCOMPARE(renderFrame(*file.document(), 0).pixels.pixels, paintedPixels);
            file.close();
            QVERIFY(canvas.redo());
        }
        QVERIFY(canvas.beginStrokeAt(positions[0], pressure));
        QVERIFY(canvas.continueStrokeAt(positions[1], pressure));
        canvas.cancelStroke();
        QVERIFY(!canvas.liveStrokeActive());
        QVERIFY(file.open(path.toStdString()).ok());
        QCOMPARE(renderFrame(*file.document(), 0).pixels.pixels, expected.pixels);
    }
    void nativeBlankRoundTrip() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/native-blank-XXXXXX");
        EditorCanvas canvas;
        QVERIFY(canvas.createCanvas({{"width", 64}, {"height", 48}, {"unit", "px"}, {"background", "White"}}));
        QVERIFY(std::holds_alternative<VectorAsset>(canvas.document()->assets.front()));
        const auto path = directory.filePath("blank.iisc");
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        QCOMPARE(canvas.filePath(), path);
        DocumentFile file;
        QVERIFY(file.open(path.toStdString()).ok());
        QCOMPARE(file.document()->extent.width, 64);
        QCOMPARE(file.document()->extent.height, 48);
        QCOMPARE(file.document()->layers.size(), 1u);
        QVERIFY(std::holds_alternative<VectorAsset>(file.document()->assets.front()));
        const auto rendered = renderFrame(*file.document(), 0);
        QVERIFY(rendered.ok());
        QCOMPARE(rendered.pixels.pixels.front(), 0xffffffffU);
    }
    void workingFilePaintingAndUndoAreDurable() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/native-edit-XXXXXX");
        const auto path = directory.filePath("edit.iisc");
        EditorCanvas canvas;
        QVERIFY(canvas.createCanvas({{"width", 32}, {"height", 32}, {"unit", "px"}, {"background", "Transparent"}}));
        QVERIFY(canvas.addPaintLayer());
        const auto layerId = canvas.selectedLayerId();
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        canvas.setBrushColor(Qt::red);
        canvas.setBrushSize(12);
        canvas.setBrushOpacity(1);
        canvas.setBrushFlow(1);
        canvas.setBrushHardness(1);
        QVERIFY(canvas.beginStrokeAt({8, 16}));
        QVERIFY(canvas.endStrokeAt({24, 16}));
        DocumentFile observer;
        QVERIFY(observer.open(path.toStdString()).ok());
        auto rendered = renderFrame(*observer.document(), 0);
        QVERIFY(rendered.ok());
        QCOMPARE(rendered.pixels.pixels[16 * 32 + 16], 0xffff0000U);
        observer.close();
        QVERIFY(canvas.undo());
        QVERIFY(observer.open(path.toStdString()).ok());
        QCOMPARE(renderFrame(*observer.document(), 0).pixels.pixels[16 * 32 + 16], 0U);
        observer.close();
        QVERIFY(canvas.redo());
        QVERIFY(canvas.setLayerOpacity(layerId, 0.5));
        QVERIFY(canvas.setLayerVisible(layerId, false));
        QVERIFY(observer.open(path.toStdString()).ok());
        QCOMPARE(layerProperties(observer.document()->layers.front()).opacity, 0.5);
        QCOMPARE(layerProperties(observer.document()->layers.front()).visible, false);
        observer.close();
        EditorCanvas reopened;
        QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path)));
        QCOMPARE(reopened.document()->layers.size(), 1u);
        QVERIFY(reopened.selectLayer(layerId));
        QVERIFY(reopened.rasterLayerSelected());
        QCOMPARE(reopened.selectedRasterPixels()->pixels[16 * 32 + 16], 0xffff0000U);
    }
    void snapshotAndImageBecomeNativeDocuments() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/native-import-XXXXXX");
        EditorCanvas source;
        QVERIFY(source.createCanvas({{"width", 40}, {"height", 24}, {"unit", "px"}, {"background", "Black"}}));
        const auto encoded = encodeIisc(*source.document());
        QVERIFY(encoded.ok());
        const auto snapshot = directory.filePath("snapshot.iisc");
        const QByteArray bytes(reinterpret_cast<const char *>(encoded.bytes.data()), encoded.bytes.size());
        iiFileProvider::File::create(snapshot, bytes);
        EditorCanvas canvas;
        QVERIFY(canvas.openDocumentSource(QUrl::fromLocalFile(snapshot)));
        QCOMPARE(canvas.canvasWidth(), 40);
        QVERIFY(canvas.filePath().isEmpty());
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(directory.filePath("working.iisc"))));
        QCOMPARE(iiFileProvider::File::read(snapshot), bytes);
        QImage image(24, 16, QImage::Format_ARGB32);
        image.fill(Qt::blue);
        const auto png = directory.filePath("result.png");
        QVERIFY(image.save(png));
        const auto original = iiFileProvider::File::read(png);
        QVERIFY(canvas.openDocumentSource(QUrl::fromLocalFile(png)));
        QVERIFY(canvas.rasterLayerSelected());
        QCOMPARE(canvas.document()->layers.size(), 1u);
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels.front(), 0xff0000ffU);
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(directory.filePath("image.iisc"))));
        QCOMPARE(iiFileProvider::File::read(png), original);
    }
    void nativeVectorPropertiesAndPlacedImageArePreserved() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/native-vector-XXXXXX");
        EditorCanvas canvas;
        QVERIFY(canvas.createCanvas({{"width", 64}, {"height", 64}, {"unit", "px"}, {"background", "Transparent"}}));
        const auto path = directory.filePath("mixed.iisc");
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        canvas.configureTool("elements", {{"selector", "Ellipse"}, {"field-10", 0}, {"field-16", "#00FF00"}});
        QVERIFY(canvas.createElement({8, 8, 24, 24}));
        const auto vector = canvas.selectedLayerId();
        QVERIFY(!vector.isEmpty());
        QVERIFY(!canvas.rasterLayerSelected());
        QVERIFY(canvas.setLayerName(vector, "Native ellipse"));
        QVERIFY(canvas.setLayerTransform(vector, {{"x", 4}, {"y", 3}, {"scaleX", 1.5}}));
        QVERIFY(canvas.setLayerOpacity(vector, 0.75));
        QImage image(8, 4, QImage::Format_ARGB32); image.fill(Qt::blue);
        const auto png = directory.filePath("placed.png");
        QVERIFY(image.save(png));
        QVERIFY(canvas.placeImage(QUrl::fromLocalFile(png), "1:1"));
        DocumentFile observer;
        QVERIFY(observer.open(path.toStdString()).ok());
        QCOMPARE(observer.document()->layers.size(), 2u);
        const auto &properties = layerProperties(observer.document()->layers.front());
        QCOMPARE(properties.name, std::string("Native ellipse"));
        QCOMPARE(properties.opacity, 0.75);
        QCOMPARE(properties.transform.translationX, 4.0);
        QCOMPARE(properties.transform.translationY, 3.0);
        QCOMPARE(properties.transform.m11, 1.5);
        QVERIFY(std::holds_alternative<VectorAsset>(observer.document()->assets.front()));
        const auto &ellipse = std::get<VectorAsset>(observer.document()->assets.front()).paths.front();
        QVERIFY(std::holds_alternative<MoveTo>(ellipse.commands.front()));
        QVERIFY(std::ranges::any_of(ellipse.commands, [](const auto &command) { return std::holds_alternative<CubicTo>(command); }));
        // Verify the saved curved geometry through its renderer, independently
        // of whether the final segment includes an explicit Close command.
        const auto vectorFrame = renderFrameLayerTiles(*observer.document(), 0, 0, {{{{33, 23}, {1, 1}}, {1, 1}}});
        QVERIFY(vectorFrame.ok() && !vectorFrame.tiles.empty());
        QCOMPARE(vectorFrame.tiles.front().pixels.pixels.front() >> 24, 255U);
        QCOMPARE(renderFrame(*observer.document(), 0).pixels.pixels[23 * 64 + 33] >> 24, 191U);
        const auto &placed = layerProperties(observer.document()->layers.back());
        QCOMPARE(placed.transform.translationX, 28.0);
        QCOMPARE(placed.transform.translationY, 30.0);
        QVERIFY(canvas.selectLayer(vector));
        QVERIFY(canvas.removeLayer(vector));
        QVERIFY(canvas.selectedLayerId().isEmpty());
    }
    void rejectedOpenAndEditsPreserveDocumentAndFile() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/native-reject-XXXXXX");
        EditorCanvas canvas;
        QVERIFY(canvas.createCanvas({{"width", 32}, {"height", 32}, {"unit", "px"}}));
        const auto path = directory.filePath("original.iisc");
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        const auto snapshot = encodeIisc(*canvas.document()).bytes;
        const auto revision = canvas.revision();
        iiFileProvider::File::create(directory.filePath("broken.iisc"), "broken");
        QVERIFY(!canvas.openDocumentSource(QUrl::fromLocalFile(directory.filePath("broken.iisc"))));
        QVERIFY(!canvas.openDocumentSource(QUrl("https://example.com/canvas.iisc")));
        QVERIFY(!canvas.setLayerOpacity(QStringLiteral("missing"), 0.5));
        QVERIFY(!canvas.setLayerOpacity(canvas.selectedLayerId(), 2));
        QVERIFY(!canvas.saveDocumentAs(QUrl::fromLocalFile(directory.filePath("broken.iisc"))));
        QCOMPARE(canvas.filePath(), path);
        QCOMPARE(canvas.revision(), revision);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, snapshot);
        DocumentFile observer;
        QVERIFY(observer.open(path.toStdString()).ok());
        QCOMPARE(encodeIisc(*observer.document()).bytes, snapshot);
        EditorCanvas copy;
        QVERIFY(copy.openDocumentSource(QUrl::fromLocalFile(path), true));
        QVERIFY(copy.filePath().isEmpty());
        QVERIFY(copy.setLayerName(copy.selectedLayerId(), "Detached copy"));
        observer.close();
        QVERIFY(observer.open(path.toStdString()).ok());
        QCOMPARE(encodeIisc(*observer.document()).bytes, snapshot);
    }
};
QTEST_MAIN(EditorCanvasTests)
#include "tst_editorcanvas.moc"
