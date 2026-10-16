#include "App/Views/Editor/EditorCanvas.h"
#include <iiSharedCanvas.h>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace iiSharedCanvas;
class TransformCanvas final : public EditorCanvas {
public:
    using EditorCanvas::mousePressEvent;
    using EditorCanvas::mouseMoveEvent;
    using EditorCanvas::mouseReleaseEvent;
    using EditorCanvas::mouseUngrabEvent;
};
class ElementTransformTests : public QObject {
    Q_OBJECT
    static QVariantMap settings(const QString &shape = "Rectangle", const QString &fill = "Solid") {
        return {{"selector", shape}, {"field-0", QVariantList{1080, 1080}}, {"field-1", 0},
            {"field-2", fill}, {"field-3", false}, {"field-4", 360}, {"field-5", 0},
            {"field-6", "Inside"}, {"field-7", 5}, {"field-8", 0}, {"field-9", false},
            {"field-10", 0}, {"field-11", "Round"}, {"field-12", "None"}, {"field-13", "Arrow"},
            {"field-14", 100}, {"field-15", "Normal"}, {"field-16", "#FF0000"}, {"field-17", "#0000FF"}};
    }
    static void prepare(EditorCanvas &canvas, const QString &fill = "Solid") {
        canvas.setSize({800, 600});
        QVERIFY(canvas.createCanvas({{"width", 160}, {"height", 120}, {"unit", "px"}, {"background", "Transparent"}}));
        canvas.configureTool("elements", settings("Rectangle", fill));
        QVERIFY(canvas.createElement({10, 12, 40, 24}));
    }
    static QPointF itemPoint(const EditorCanvas &canvas, QPointF world) {
        return {canvas.panX() + (world.x() - canvas.canvasOriginX()) * canvas.zoom(),
            canvas.panY() + (world.y() - canvas.canvasOriginY()) * canvas.zoom()};
    }
    static QPointF handle(const EditorCanvas &canvas, const QString &id) {
        if (id == "rotate") return canvas.elementTransform().value("rotate").toPointF();
        for (const auto &value : canvas.elementTransform().value("handles").toList()) {
            const auto entry = value.toMap(); if (entry.value("id").toString() == id) return entry.value("point").toPointF();
        }
        return {};
    }
    static QVariantList size(const EditorCanvas &canvas) { return canvas.selectedElementSettings().value("field-0").toList(); }
    static void close(double actual, double expected) { QVERIFY2(std::abs(actual - expected) < 0.00001, qPrintable(QString("%1 != %2").arg(actual, 0, 'g', 14).arg(expected, 0, 'g', 14))); }
    static void pointer(TransformCanvas &canvas, QEvent::Type type, QPointF point, Qt::KeyboardModifiers modifiers = {}) {
        QMouseEvent event(type, point, point, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
        if (type == QEvent::MouseButtonPress) canvas.mousePressEvent(&event);
        else if (type == QEvent::MouseButtonRelease) canvas.mouseReleaseEvent(&event);
        else canvas.mouseMoveEvent(&event);
        QVERIFY(event.isAccepted());
    }
private slots:
    void allEightHandlesResizeOneUndoTransaction_data() {
        QTest::addColumn<QString>("id"); QTest::addColumn<QPointF>("delta");
        QTest::addColumn<QSizeF>("expected"); QTest::addColumn<QPointF>("corner");
        QTest::newRow("north-west") << QString("nw") << QPointF(-8, -6) << QSizeF(48, 30) << QPointF(2, 6);
        QTest::newRow("north") << QString("n") << QPointF(0, -6) << QSizeF(40, 30) << QPointF(10, 6);
        QTest::newRow("north-east") << QString("ne") << QPointF(8, -6) << QSizeF(48, 30) << QPointF(10, 6);
        QTest::newRow("east") << QString("e") << QPointF(8, 0) << QSizeF(48, 24) << QPointF(10, 12);
        QTest::newRow("south-east") << QString("se") << QPointF(8, 6) << QSizeF(48, 30) << QPointF(10, 12);
        QTest::newRow("south") << QString("s") << QPointF(0, 6) << QSizeF(40, 30) << QPointF(10, 12);
        QTest::newRow("south-west") << QString("sw") << QPointF(-8, 6) << QSizeF(48, 30) << QPointF(2, 12);
        QTest::newRow("west") << QString("w") << QPointF(-8, 0) << QSizeF(48, 24) << QPointF(2, 12);
    }
    void allEightHandlesResizeOneUndoTransaction() {
        QFETCH(QString, id); QFETCH(QPointF, delta); QFETCH(QSizeF, expected); QFETCH(QPointF, corner);
        EditorCanvas canvas; prepare(canvas);
        const auto before = encodeIisc(*canvas.document()).bytes;
        const auto beforePixels = renderFrame(*canvas.document(), 0).pixels.pixels;
        const auto start = handle(canvas, id), finish = start + delta * canvas.zoom();
        QSignalSpy history(&canvas, &EditorCanvas::undoRedoChanged);
        QVERIFY(canvas.beginElementTransform(id, start));
        QVERIFY(canvas.updateElementTransform(start + delta * canvas.zoom() / 2));
        QVERIFY(canvas.updateElementTransform(finish));
        close(size(canvas)[0].toDouble(), expected.width()); close(size(canvas)[1].toDouble(), expected.height());
        QCOMPARE(history.count(), 0);
        QVERIFY(canvas.endElementTransform(finish)); QCOMPARE(history.count(), 1);
        close(canvas.elementTransform().value("x").toDouble(), corner.x()); close(canvas.elementTransform().value("y").toDouble(), corner.y());
        const auto after = encodeIisc(*canvas.document()).bytes; QVERIFY(after != before);
        const auto afterPixels = renderFrame(*canvas.document(), 0).pixels.pixels; QVERIFY(afterPixels != beforePixels);
        QVERIFY(canvas.undo()); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        QVERIFY(canvas.redo()); QCOMPARE(encodeIisc(*canvas.document()).bytes, after);
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, afterPixels);
    }
    void pointerSelectsTopmostActualShapeAndDragsIt() {
        TransformCanvas canvas; prepare(canvas);
        const auto rectangle = canvas.selectedLayerId();
        canvas.configureTool("elements", settings("Ellipse")); QVERIFY(canvas.createElement({24, 16, 32, 32}));
        const auto ellipse = canvas.selectedLayerId();
        QVERIFY(canvas.clearElementSelection());
        const auto layerCount = canvas.document()->layers.size();
        const auto at = itemPoint(canvas, {40, 32});
        const auto before = encodeIisc(*canvas.document()).bytes;
        pointer(canvas, QEvent::MouseButtonPress, at);
        QCOMPARE(canvas.selectedLayerId(), ellipse);
        QVERIFY(canvas.elementTransform().value("visible").toBool());
        pointer(canvas, QEvent::MouseMove, at + QPointF(12, 8) * canvas.zoom());
        pointer(canvas, QEvent::MouseButtonRelease, at + QPointF(12, 8) * canvas.zoom());
        QCOMPARE(canvas.document()->layers.size(), layerCount);
        close(canvas.elementTransform().value("x").toDouble(), 36); close(canvas.elementTransform().value("y").toDouble(), 24);
        QVERIFY(canvas.undo()); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        // This is inside the ellipse bounding box but outside its filled path.
        QVERIFY(canvas.clearElementSelection()); const auto corner = itemPoint(canvas, {25, 17});
        pointer(canvas, QEvent::MouseButtonPress, corner); pointer(canvas, QEvent::MouseButtonRelease, corner);
        QCOMPARE(canvas.selectedLayerId(), rectangle);
        QCOMPARE(canvas.selectedElementSettings().value("selector").toString(), QString("Rectangle"));
    }
    void rotationKeepsOrientedSizeAndSubsequentResizeWorks() {
        EditorCanvas canvas; prepare(canvas);
        const auto start = handle(canvas, "rotate"), center = canvas.elementTransform().value("center").toPointF();
        const auto offset = start - center;
        const auto finish = center + QPointF(-offset.y(), offset.x());
        QVERIFY(canvas.beginElementTransform("rotate", start)); QVERIFY(canvas.endElementTransform(finish));
        close(canvas.elementTransform().value("rotation").toDouble(), 90);
        close(size(canvas)[0].toDouble(), 40); close(size(canvas)[1].toDouble(), 24);
        const auto east = handle(canvas, "e");
        QVERIFY(canvas.beginElementTransform("e", east)); QVERIFY(canvas.endElementTransform(east + QPointF(0, 10) * canvas.zoom()));
        close(size(canvas)[0].toDouble(), 50); close(size(canvas)[1].toDouble(), 24);
        QVERIFY(canvas.applyToolField("elements", "field-0", QVariantList{60.0, 30.0}));
        close(size(canvas)[0].toDouble(), 60); close(size(canvas)[1].toDouble(), 30);
        close(canvas.elementTransform().value("rotation").toDouble(), 90);
    }
    void modifiersConstrainRatioResizeFromCenterAndFlip() {
        EditorCanvas canvas; prepare(canvas);
        auto start = handle(canvas, "se");
        QVERIFY(canvas.beginElementTransform("se", start)); QVERIFY(canvas.endElementTransform(start + QPointF(8, 6) * canvas.zoom(), Qt::ShiftModifier));
        close(size(canvas)[0].toDouble(), 50); close(size(canvas)[1].toDouble(), 30);
        QVERIFY(canvas.undo());
        start = handle(canvas, "e");
        QVERIFY(canvas.beginElementTransform("e", start)); QVERIFY(canvas.endElementTransform(start + QPointF(8, 0) * canvas.zoom(), Qt::AltModifier));
        close(size(canvas)[0].toDouble(), 56); close(size(canvas)[1].toDouble(), 24);
        close(canvas.elementTransform().value("x").toDouble(), 2);
        QVERIFY(canvas.undo());
        start = handle(canvas, "w");
        QVERIFY(canvas.beginElementTransform("w", start)); QVERIFY(canvas.endElementTransform(start + QPointF(50, 0) * canvas.zoom()));
        close(size(canvas)[0].toDouble(), 10); close(size(canvas)[1].toDouble(), 24);
        QVERIFY(layerProperties(canvas.document()->layers.back()).transform.m11 < 0);
        const auto pixels = renderFrame(*canvas.document(), 0).pixels;
        QCOMPARE(pixels.pixels[20 * 160 + 54], 0xffff0000U);
    }
    void escapeUngrabToolChangeAndNoopKeepBytesAndRedo() {
        TransformCanvas canvas; prepare(canvas);
        QVERIFY(canvas.createElement({80, 12, 20, 20})); QVERIFY(canvas.undo());
        QVERIFY(canvas.canRedo()); const auto before = encodeIisc(*canvas.document()).bytes;
        const auto start = canvas.elementTransform().value("center").toPointF();
        QSignalSpy history(&canvas, &EditorCanvas::undoRedoChanged);
        QVERIFY(canvas.beginElementTransform("move", start));
        QVERIFY(canvas.updateElementTransform(start + QPointF(20, 15)));
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier); QCoreApplication::sendEvent(&canvas, &escape);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QVERIFY(canvas.canRedo()); QCOMPARE(history.count(), 0);
        QVERIFY(canvas.beginElementTransform("move", start)); QVERIFY(canvas.endElementTransform(start));
        QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QCOMPARE(history.count(), 0);
        QVERIFY(canvas.beginElementTransform("move", start)); QVERIFY(canvas.updateElementTransform(start + QPointF(20, 15)));
        canvas.mouseUngrabEvent(); QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QVERIFY(canvas.canRedo());
        QVERIFY(canvas.beginElementTransform("move", start)); QVERIFY(canvas.updateElementTransform(start + QPointF(20, 15)));
        canvas.configureTool("brush", {}); QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QVERIFY(canvas.canRedo());
        QVERIFY(!canvas.elementTransform().value("visible").toBool());
    }
    void enterCommitsCurrentPreviewAndArrowsMoveExactlyOnePixel() {
        EditorCanvas canvas; prepare(canvas);
        const auto start = canvas.elementTransform().value("center").toPointF();
        QVERIFY(canvas.beginElementTransform("move", start)); QVERIFY(canvas.updateElementTransform(start + QPointF(10, 5) * canvas.zoom()));
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier); QCoreApplication::sendEvent(&canvas, &enter);
        close(canvas.elementTransform().value("x").toDouble(), 20); close(canvas.elementTransform().value("y").toDouble(), 17);
        QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier); QCoreApplication::sendEvent(&canvas, &right);
        close(canvas.elementTransform().value("x").toDouble(), 21);
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier); QCoreApplication::sendEvent(&canvas, &down);
        close(canvas.elementTransform().value("y").toDouble(), 27);
        QVERIFY(canvas.undo()); close(canvas.elementTransform().value("y").toDouble(), 17);
    }
    void reopenedVectorUsesActualGeometryAndCanResize() {
        EditorCanvas original; prepare(original);
        const auto encoded = encodeIisc(*original.document()); QVERIFY(encoded.ok());
        const auto decoded = decodeIisc(encoded.bytes); QVERIFY(decoded.ok());
        EditorCanvas canvas; canvas.setSize({800, 600}); QVERIFY(canvas.loadProjectPage(decoded.document, "Reopened"));
        canvas.configureTool("elements", settings());
        QVERIFY(canvas.selectElement(original.selectedLayerId()));
        QCOMPARE(canvas.selectedElementSettings().value("selector").toString(), QString("Vector"));
        QCOMPARE(size(canvas), QVariantList({40.0, 24.0}));
        QVERIFY(!canvas.selectedElementSettings().contains("field-1"));
        QVERIFY(canvas.elementTransform().value("editable").toBool());
        QVERIFY(canvas.applyToolField("elements", "field-0", QVariantList{80.0, 36.0}));
        close(size(canvas)[0].toDouble(), 80); close(size(canvas)[1].toDouble(), 36);
        QVERIFY(canvas.undo()); QCOMPARE(size(canvas), QVariantList({40.0, 24.0}));
    }
    void zoomArtboardAndAnimatedFrameUseRenderedCoordinates() {
        Document document; document.extent = {600, 240}; document.timeline.frameCount = 24;
        document.artboards = {{"one", "First", {{0, 0}, {160, 120}}, 0}, {"two", "Second", {{300, 60}, {160, 120}}, 0}};
        VectorPath path; path.commands = {MoveTo{{10, 12}}, LineTo{{50, 12}}, LineTo{{50, 36}}, LineTo{{10, 36}}, ClosePath{}}; path.fill = SolidPaint{0xffff0000};
        document.assets.emplace_back(VectorAsset{"asset", {160, 120}, {path}});
        LayerProperties properties{"vector", "Animated rectangle"}; properties.artboardId = "two";
        MotionValue first; first.position = {8, 4}; first.scale = {2, 1.5};
        properties.motion = {{0, first}};
        document.layers.emplace_back(StaticVectorLayer{properties, StaticSource{"asset"}});
        EditorCanvas canvas; canvas.setSize({800, 600}); QVERIFY(canvas.loadProjectPage(document, "Artboards"));
        canvas.configureTool("elements", settings()); QVERIFY(canvas.selectArtboard(1, false)); QVERIFY(canvas.selectElement("vector")); canvas.setZoom(1.7);
        const auto corners = canvas.elementTransform().value("points").toList();
        QCOMPARE(corners[0].toPointF(), itemPoint(canvas, {328, 82}));
        close(size(canvas)[0].toDouble(), 80); close(size(canvas)[1].toDouble(), 36);
        const auto start = handle(canvas, "e");
        QVERIFY(canvas.beginElementTransform("e", start)); QVERIFY(canvas.endElementTransform(start + QPointF(20, 0) * canvas.zoom()));
        close(size(canvas)[0].toDouble(), 100); close(size(canvas)[1].toDouble(), 36);
        const auto moved = canvas.elementTransform().value("points").toList(); QCOMPARE(moved[0].toPointF(), corners[0].toPointF());
        const auto &layer = canvas.document()->layers.front(); QCOMPARE(layerProperties(layer).motion.size(), std::size_t{1});
        QCOMPARE(layerProperties(layer).motion.front().value.scale.x, 2.0);
    }
    void locksHiddenLayersAndMultipleSelectionStayCoherent() {
        EditorCanvas canvas; prepare(canvas); const auto first = canvas.selectedLayerId();
        QVERIFY(canvas.createElement({80, 12, 20, 20})); const auto second = canvas.selectedLayerId();
        QVERIFY(canvas.setElementSelected(first, true, true));
        QCOMPARE(canvas.elementTransform().value("selections").toList().size(), 1);
        canvas.configureTool("layers", {{"field-8", "None"}});
        QVERIFY(canvas.applyToolField("layers", "field-8", "Position"));
        canvas.configureTool("elements", settings()); QVERIFY(canvas.selectElement(first));
        QVERIFY(canvas.elementTransform().value("visible").toBool()); QVERIFY(!canvas.elementTransform().value("editable").toBool());
        QVERIFY(!canvas.beginElementTransform("move", itemPoint(canvas, {30, 24})));
        QVERIFY(canvas.selectElement(second)); QVERIFY(canvas.setLayerVisible(second, false));
        QVERIFY(!canvas.elementTransform().value("visible").toBool());
    }
    void dynamicVectorFrameChangesReadGeometryAndCancelAnUnfinishedDrag() {
        Document document; document.extent = {160, 120}; document.timeline.frameCount = 24;
        const auto rectangle = [](double x, double y, double width, double height) {
            VectorPath path; path.commands = {MoveTo{{x, y}}, LineTo{{x + width, y}}, LineTo{{x + width, y + height}}, LineTo{{x, y + height}}, ClosePath{}};
            path.fill = SolidPaint{0xffff0000}; return path;
        };
        document.assets.emplace_back(VectorAsset{"first", document.extent, {rectangle(10, 12, 40, 24)}});
        document.assets.emplace_back(VectorAsset{"second", document.extent, {rectangle(20, 16, 60, 30)}});
        document.layers.emplace_back(DynamicVectorLayer{{"animated", "Animated vector"}, KeyframedSource{{0, 12}}});
        document.frames = {{0, {{"animated", "first"}}}, {12, {{"animated", "second"}}}};
        EditorCanvas canvas; canvas.setSize({800, 600}); QVERIFY(canvas.loadProjectPage(document, "Frames"));
        canvas.configureTool("elements", settings()); QVERIFY(canvas.selectElement("animated"));
        QCOMPARE(size(canvas), QVariantList({40.0, 24.0}));
        const auto before = encodeIisc(*canvas.document()).bytes;
        const auto start = handle(canvas, "se");
        QVERIFY(canvas.beginElementTransform("se", start)); QVERIFY(canvas.updateElementTransform(start + QPointF(10, 10) * canvas.zoom()));
        canvas.setFrame(12); QVERIFY(!canvas.elementTransform().value("dragging").toBool());
        QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        QCOMPARE(size(canvas), QVariantList({60.0, 30.0}));
        QVERIFY(canvas.applyToolField("elements", "field-0", QVariantList{90.0, 60.0}));
        QCOMPARE(size(canvas), QVariantList({90.0, 60.0}));
        canvas.setFrame(0); QCOMPARE(size(canvas), QVariantList({60.0, 48.0}));
        QCOMPARE(canvas.document()->frames.size(), std::size_t{2});
    }
    void gradientShapeHitTestingRespectsItsTransparentCorners() {
        TransformCanvas canvas; prepare(canvas); const auto rectangle = canvas.selectedLayerId();
        canvas.configureTool("elements", settings("Ellipse", "Gradient")); QVERIFY(canvas.createElement({24, 16, 32, 32}));
        const auto ellipse = canvas.selectedLayerId(); QVERIFY(canvas.clearElementSelection());
        const auto corner = itemPoint(canvas, {25, 17});
        pointer(canvas, QEvent::MouseButtonPress, corner); pointer(canvas, QEvent::MouseButtonRelease, corner);
        QCOMPARE(canvas.selectedLayerId(), rectangle);
        QVERIFY(canvas.clearElementSelection()); const auto center = itemPoint(canvas, {40, 32});
        pointer(canvas, QEvent::MouseButtonPress, center); pointer(canvas, QEvent::MouseButtonRelease, center);
        QCOMPARE(canvas.selectedLayerId(), ellipse);
    }
    void workingFilePersistsOnlyAcceptedTransformAndRejectsConflicts() {
        EditorCanvas canvas; prepare(canvas);
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/element-transform-file-XXXXXX"); QVERIFY(dir.isValid());
        const auto path = dir.filePath("working.iisc");
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        const auto before = encodeIisc(*canvas.document()).bytes;
        const auto readStored = [&]() {
            iiSocietyHelper::FileSystem files;
            if (!files.openForSource(path)) return std::vector<std::uint8_t>{};
            const auto loaded = files.readCanvas(path);
            return loaded.ok() ? encodeIisc(loaded.document).bytes : std::vector<std::uint8_t>{};
        };
        QCOMPARE(readStored(), before);
        const auto start = canvas.elementTransform().value("center").toPointF();
        QVERIFY(canvas.beginElementTransform("move", start)); QVERIFY(canvas.updateElementTransform(start + QPointF(10, 5) * canvas.zoom()));
        QCOMPARE(readStored(), before);
        canvas.cancelElementTransform(); QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QCOMPARE(readStored(), before);
        QVERIFY(canvas.beginElementTransform("move", start)); QVERIFY(canvas.updateElementTransform(start + QPointF(10, 5) * canvas.zoom()));
        QVERIFY(canvas.saveDocument()); // Save cancels the uncommitted preview.
        QCOMPARE(readStored(), before); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        QVERIFY(canvas.beginElementTransform("move", start)); QVERIFY(canvas.endElementTransform(start + QPointF(10, 5) * canvas.zoom()));
        const auto accepted = encodeIisc(*canvas.document()).bytes; QVERIFY(accepted != before);
        // The existing helper refreshes authorship independently when saving.
        // Compare the complete stored geometry with that attribution normalized.
        auto stored = decodeIisc(readStored()); QVERIFY(stored.ok());
        stored.document.authorship = canvas.document()->authorship;
        QCOMPARE(encodeIisc(stored.document).bytes, accepted);
        QVERIFY(canvas.undo()); QCOMPARE(readStored(), before);
        EditorCanvas writer; QVERIFY(writer.openDocumentSource(QUrl::fromLocalFile(path)));
        QVERIFY(writer.setLayerOpacity(canvas.selectedLayerId(), 0.25));
        const auto external = readStored(); QVERIFY(external != before);
        const auto historyBefore = canvas.canRedo();
        QVERIFY(canvas.beginElementTransform("move", start)); QVERIFY(canvas.updateElementTransform(start + QPointF(10, 5) * canvas.zoom()));
        QVERIFY(!canvas.endElementTransform(start + QPointF(10, 5) * canvas.zoom()));
        QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QCOMPARE(readStored(), external);
        QCOMPARE(canvas.canRedo(), historyBefore);
        QVERIFY(!canvas.elementTransform().value("dragging").toBool());
    }
    void importedVectorPresenterSynchronizationDoesNotCancelDragging() {
        EditorCanvas source; prepare(source);
        const auto encoded = encodeIisc(*source.document()); const auto decoded = decodeIisc(encoded.bytes); QVERIFY(decoded.ok());
        EditorCanvas canvas; canvas.setSize({800, 600}); QVERIFY(canvas.loadProjectPage(decoded.document, "Imported"));
        canvas.configureTool("elements", settings()); QVERIFY(canvas.selectElement(source.selectedLayerId()));
        connect(&canvas, &EditorCanvas::elementSelectionChanged, &canvas, [&] {
            auto draft = settings(); const auto selected = canvas.selectedElementSettings();
            for (auto it = selected.cbegin(); it != selected.cend(); ++it) draft[it.key()] = it.value();
            canvas.configureTool("elements", draft);
        });
        const auto start = handle(canvas, "se");
        QVERIFY(canvas.beginElementTransform("se", start));
        QVERIFY(canvas.updateElementTransform(start + QPointF(20, 12) * canvas.zoom()));
        QVERIFY(canvas.elementTransform().value("dragging").toBool());
        QVERIFY(canvas.endElementTransform(start + QPointF(20, 12) * canvas.zoom()));
        close(size(canvas)[0].toDouble(), 60); close(size(canvas)[1].toDouble(), 36);
    }
    void gradientAndImageFillsKeepTheirAssetsDuringTransform() {
        for (const auto &fill : {QString("Gradient"), QString("Image")}) {
            EditorCanvas canvas; canvas.setSize({800, 600});
            QVERIFY(canvas.createCanvas({{"width", 160}, {"height", 120}, {"unit", "px"}, {"background", "Transparent"}}));
            const auto draft = settings("Rectangle", fill); canvas.configureTool("elements", draft);
            QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/transform-fill-XXXXXX"); QVERIFY(dir.isValid());
            if (fill == "Image") { QImage source(2, 2, QImage::Format_ARGB32); source.fill(Qt::green); const auto path = dir.filePath("fill.png"); QVERIFY(source.save(path)); QVERIFY(canvas.importToolSource(QUrl::fromLocalFile(path), "elements", draft)); }
            QVERIFY(canvas.createElement({10, 12, 40, 24}));
            const auto *asset = resolveAssetAt(*canvas.document(), canvas.document()->layers.back(), 0);
            const auto pixels = std::get<RasterAsset>(*asset).pixels.pixels;
            const auto before = encodeIisc(*canvas.document()).bytes;
            const auto start = handle(canvas, "se");
            QVERIFY(canvas.beginElementTransform("se", start)); QVERIFY(canvas.endElementTransform(start + QPointF(20, 12) * canvas.zoom()));
            close(size(canvas)[0].toDouble(), 60); close(size(canvas)[1].toDouble(), 36);
            asset = resolveAssetAt(*canvas.document(), canvas.document()->layers.back(), 0);
            QCOMPARE(std::get<RasterAsset>(*asset).pixels.pixels, pixels);
            QVERIFY(canvas.undo()); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        }
    }
};
QTEST_MAIN(ElementTransformTests)
#include "tst_elementtransform.moc"
