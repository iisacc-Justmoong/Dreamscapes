#include "App/Views/Editor/EditorCanvasFeedback.h"
#include <iiSharedCanvas.h>
#include <QFile>
#include <QCursor>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

class FeedbackCanvas : public EditorCanvas {
public:
    using EditorCanvas::mousePressEvent;
    using EditorCanvas::mouseMoveEvent;
    using EditorCanvas::mouseReleaseEvent;
    using EditorCanvas::mouseUngrabEvent;
    using EditorCanvas::hoverMoveEvent;
    using EditorCanvas::hoverLeaveEvent;
};
class CanvasFeedbackTests : public QObject {
    Q_OBJECT
    static void prepare(FeedbackCanvas &canvas) {
        canvas.setSize({256, 256});
        QVERIFY(canvas.createCanvas({{"width", 96}, {"height", 96}, {"unit", "px"}, {"background", "Transparent"}}));
        QVERIFY(canvas.addPaintLayer());
        canvas.setZoom(2);
    }
    static QPointF screen(const FeedbackCanvas &canvas, QPointF point) {
        return {canvas.panX() + (point.x() + canvas.editingOrigin().x() - canvas.canvasOriginX()) * canvas.zoom(),
            canvas.panY() + (point.y() + canvas.editingOrigin().y() - canvas.canvasOriginY()) * canvas.zoom()};
    }
    static void pointer(FeedbackCanvas &canvas, QEvent::Type type, QPointF point) {
        const auto p = screen(canvas, point);
        QMouseEvent event(type, p, p, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        if (type == QEvent::MouseButtonPress) canvas.mousePressEvent(&event);
        else if (type == QEvent::MouseButtonRelease) canvas.mouseReleaseEvent(&event);
        else canvas.mouseMoveEvent(&event);
    }
    static void hover(FeedbackCanvas &canvas, QPointF point, bool leave = false) {
        QHoverEvent event(leave ? QEvent::HoverLeave : QEvent::HoverMove, screen(canvas, point), screen(canvas, point), screen(canvas, point), Qt::NoModifier);
        if (leave) canvas.hoverLeaveEvent(&event); else canvas.hoverMoveEvent(&event);
    }
    static QImage artwork(FeedbackCanvas &canvas) {
        EditorCanvasFeedback feedback; feedback.setSize(canvas.size()); feedback.setCanvas(&canvas);
        QImage image(256, 256, QImage::Format_ARGB32); image.fill(Qt::transparent);
        QPainter painter(&image); feedback.paint(&painter); return image;
    }
    static bool inkNear(const QImage &image, QPointF point) {
        for (int y = int(point.y()) - 2; y <= int(point.y()) + 2; ++y)
            for (int x = int(point.x()) - 2; x <= int(point.x()) + 2; ++x)
                if (image.rect().contains(x, y) && qAlpha(image.pixel(x, y)) > 128) return true;
        return false;
    }
private slots:
    void liveSelectionsMatchTheirAcceptedGeometry_data() {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"Rectangle", "Lasso", "Triangle"}) QTest::newRow(mode) << QString(mode);
    }
    void liveSelectionsMatchTheirAcceptedGeometry() {
        QFETCH(QString, mode);
        FeedbackCanvas canvas; prepare(canvas);
        canvas.configureTool("select", {{"selector", mode}, {"field-0", "New"}, {"field-1", 0}, {"field-2", 0}, {"field-3", true}});
        const auto bytes = iiSharedCanvas::encodeIisc(*canvas.document()).bytes;
        pointer(canvas, QEvent::MouseButtonPress, {12, 12}); pointer(canvas, QEvent::MouseMove, {44, 12});
        pointer(canvas, QEvent::MouseMove, {44, 40});
        const auto bounds = canvas.interactionFeedback()["gestureBounds"].toRectF();
        QVERIFY(!bounds.isEmpty()); QVERIFY(canvas.interactionFeedback()["gestureActive"].toBool());
        QVERIFY(inkNear(artwork(canvas), screen(canvas, mode == "Triangle" ? QPointF(28, 12) : QPointF(44, 26))));
        QCOMPARE(iiSharedCanvas::encodeIisc(*canvas.document()).bytes, bytes);
        pointer(canvas, QEvent::MouseButtonRelease, {44, 40});
        QVERIFY(!canvas.interactionFeedback()["gestureActive"].toBool());
        const auto accepted = canvas.interactionFeedback()["selectionBounds"].toRectF();
        QVERIFY(!accepted.isEmpty()); QVERIFY(qAbs(accepted.left() - bounds.left()) <= canvas.zoom());
        QVERIFY(qAbs(accepted.right() - bounds.right()) <= canvas.zoom());
        QCOMPARE(iiSharedCanvas::encodeIisc(*canvas.document()).bytes, bytes);
    }
    void cursorSizeFollowsToolSpaceAndZoom_data() {
        QTest::addColumn<QString>("tool"); QTest::addColumn<QVariantMap>("values"); QTest::addColumn<double>("width");
        for (const auto &shape : {"Round", "Square", "Diamond"})
            QTest::newRow(shape) << QString("brush") << QVariantMap{{"field-4", 12}, {"field-7", shape}, {"field-6", false}} << 24.;
        QTest::newRow("screen-size") << QString("brush") << QVariantMap{{"field-4", 12}, {"field-6", true}} << 12.;
        QTest::newRow("eraser") << QString("eraser") << QVariantMap{{"selector", "Pixel"}, {"field-0", 14}} << 28.;
        QTest::newRow("retouch") << QString("retouch") << QVariantMap{{"selector", "Remove Brush"}, {"field-0", 18}} << 36.;
        QTest::newRow("local-mask") << QString("masking") << QVariantMap{{"selector", "Brush"}, {"field-0", 16}} << 32.;
        QTest::newRow("background-mask") << QString("background") << QVariantMap{{"backgroundView", "Mask"}, {"maskTool", "Brush"}, {"maskSize", 20}} << 40.;
    }
    void cursorSizeFollowsToolSpaceAndZoom() {
        QFETCH(QString, tool); QFETCH(QVariantMap, values); QFETCH(double, width);
        FeedbackCanvas canvas; prepare(canvas); canvas.configureTool(tool, values); hover(canvas, {48, 48});
        QVERIFY(canvas.interactionFeedback()["pointerVisible"].toBool());
        QCOMPARE(canvas.interactionFeedback()["cursorBounds"].toRectF().width(), width);
        QCOMPARE(canvas.cursor().shape(), Qt::BlankCursor);
        QVERIFY(inkNear(artwork(canvas), screen(canvas, {48, 48}) + QPointF(width / 2, 0)));
        hover(canvas, {48, 48}, true); QVERIFY(!canvas.interactionFeedback()["pointerVisible"].toBool());
        QVERIFY(canvas.cursor().shape() != Qt::BlankCursor);
    }
    void selectionOutlineIncludesSubtractHoles() {
        FeedbackCanvas canvas; prepare(canvas);
        canvas.configureTool("select", {{"selector", "Rectangle"}, {"field-0", "New"}, {"field-2", 0}});
        QVERIFY(canvas.selectArea({QPointF(8, 8), QPointF(72, 72)}));
        canvas.configureTool("select", {{"selector", "Rectangle"}, {"field-0", "Subtract"}, {"field-2", 0}});
        QVERIFY(canvas.selectArea({QPointF(24, 24), QPointF(56, 56)}));
        const auto image = artwork(canvas);
        QVERIFY(inkNear(image, screen(canvas, {8, 40}))); QVERIFY(inkNear(image, screen(canvas, {24, 40})));
        QVERIFY(!inkNear(image, screen(canvas, {40, 40}))); QVERIFY(!inkNear(image, screen(canvas, {16, 40})));
        canvas.clearAreaSelection(); QVERIFY(canvas.interactionFeedback()["selectionBounds"].toRectF().isEmpty());
    }
    void oversizedBrushKeepsItsCenterVisible() {
        FeedbackCanvas canvas; prepare(canvas); canvas.configureTool("brush", {{"field-4", 512}, {"field-6", false}});
        hover(canvas, {48, 48}); QVERIFY(inkNear(artwork(canvas), screen(canvas, {48, 48})));
    }
    void linearGuideFollowsAngleAndSemanticModesDoNotInventAreas() {
        FeedbackCanvas canvas; prepare(canvas);
        canvas.configureTool("masking", {{"selector", "Linear"}, {"field-4", 90}});
        pointer(canvas, QEvent::MouseButtonPress, {20, 20}); pointer(canvas, QEvent::MouseMove, {60, 60});
        const auto bounds = canvas.interactionFeedback()["gestureBounds"].toRectF();
        QVERIFY(bounds.height() > bounds.width()); canvas.mouseUngrabEvent();
        canvas.configureTool("masking", {{"selector", "Range"}});
        pointer(canvas, QEvent::MouseButtonPress, {20, 20}); pointer(canvas, QEvent::MouseMove, {60, 60});
        QVERIFY(canvas.interactionFeedback()["gestureBounds"].toRectF().isEmpty()); canvas.mouseUngrabEvent();
    }
    void affineBrushTipAndFrameChange() {
        FeedbackCanvas canvas; prepare(canvas);
        QVERIFY(canvas.setLayerTransform(canvas.selectedLayerId(), {{"scaleX", 2}, {"scaleY", .5}}));
        canvas.configureTool("brush", {{"field-4", 12}, {"field-6", false}}); hover(canvas, {40, 40});
        const auto bounds = canvas.interactionFeedback()["cursorBounds"].toRectF();
        QCOMPARE(bounds.width(), 48.); QCOMPARE(bounds.height(), 12.);
        canvas.configureTool("select", {{"selector", "Rectangle"}});
        pointer(canvas, QEvent::MouseButtonPress, {12, 12}); pointer(canvas, QEvent::MouseMove, {30, 24});
        canvas.setFrame(1); QVERIFY(!canvas.interactionFeedback()["gestureActive"].toBool());
    }
    void backgroundGuides_data() {
        QTest::addColumn<QString>("mode");
        for (const auto &mode : {"Rectangle", "Ellipse", "Lasso", "Gradient", "Polygon"}) QTest::newRow(mode) << QString(mode);
    }
    void backgroundGuides() {
        QFETCH(QString, mode); FeedbackCanvas canvas; prepare(canvas);
        canvas.configureTool("background", {{"backgroundView", "Mask"}, {"maskTool", mode}, {"maskOverlay", false}});
        const auto bytes = iiSharedCanvas::encodeIisc(*canvas.document()).bytes;
        pointer(canvas, QEvent::MouseButtonPress, {16, 16}); pointer(canvas, QEvent::MouseMove, {60, 50});
        if (mode == "Lasso") pointer(canvas, QEvent::MouseMove, {32, 60});
        QVERIFY(canvas.interactionFeedback()["gestureActive"].toBool());
        QVERIFY(!canvas.interactionFeedback()["gestureBounds"].toRectF().isEmpty());
        QCOMPARE(iiSharedCanvas::encodeIisc(*canvas.document()).bytes, bytes);
        canvas.mouseUngrabEvent(); QVERIFY(!canvas.interactionFeedback()["gestureActive"].toBool());
    }
    void maskDabAndBrushCancellation() {
        FeedbackCanvas canvas; prepare(canvas);
        canvas.configureTool("masking", {{"selector", "Brush"}, {"field-0", 20}, {"field-1", 0}, {"field-23", 0}});
        pointer(canvas, QEvent::MouseButtonPress, {40, 40}); pointer(canvas, QEvent::MouseButtonRelease, {40, 40});
        QVERIFY(!canvas.interactionFeedback()["selectionBounds"].toRectF().isEmpty());
        canvas.clearAreaSelection(); canvas.configureTool("brush", {{"field-4", 12}, {"field-6", false}});
        const auto before = iiSharedCanvas::encodeIisc(*canvas.document()).bytes;
        pointer(canvas, QEvent::MouseButtonPress, {24, 24}); pointer(canvas, QEvent::MouseMove, {48, 32});
        QVERIFY(canvas.liveStrokeActive());
        QVERIFY(!canvas.undo()); QVERIFY(canvas.liveStrokeActive());
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, {}); QCoreApplication::sendEvent(&canvas, &key);
        QVERIFY(!canvas.liveStrokeActive()); QCOMPARE(iiSharedCanvas::encodeIisc(*canvas.document()).bytes, before);
    }
    void escapeAndCaptureLossDiscardDraftsAndKeepRedo() {
        FeedbackCanvas canvas; prepare(canvas);
        QVERIFY(canvas.addPaintLayer()); QVERIFY(canvas.undo()); QVERIFY(canvas.canRedo());
        canvas.configureTool("select", {{"selector", "Lasso"}});
        const auto bytes = iiSharedCanvas::encodeIisc(*canvas.document()).bytes;
        for (const bool captureLoss : {false, true}) {
            pointer(canvas, QEvent::MouseButtonPress, {12, 12}); pointer(canvas, QEvent::MouseMove, {30, 24});
            if (captureLoss) canvas.mouseUngrabEvent();
            else {
                QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_Escape, {}); override.ignore();
                QCoreApplication::sendEvent(&canvas, &override); QVERIFY(override.isAccepted());
                QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, {}); QCoreApplication::sendEvent(&canvas, &key);
            }
            QVERIFY(!canvas.interactionFeedback()["gestureActive"].toBool());
            QCOMPARE(iiSharedCanvas::encodeIisc(*canvas.document()).bytes, bytes); QVERIFY(canvas.canRedo());
        }
    }
    void elementPreviewUsesEllipseAndRemainsTransient() {
        FeedbackCanvas canvas; prepare(canvas);
        canvas.configureTool("elements", {{"selector", "Ellipse"}, {"field-3", false}});
        const auto bytes = iiSharedCanvas::encodeIisc(*canvas.document()).bytes;
        pointer(canvas, QEvent::MouseButtonPress, {12, 12}); pointer(canvas, QEvent::MouseMove, {44, 36});
        const auto image = artwork(canvas);
        QVERIFY(inkNear(image, screen(canvas, {28, 12}))); QVERIFY(!inkNear(image, screen(canvas, {12, 12})));
        QCOMPARE(iiSharedCanvas::encodeIisc(*canvas.document()).bytes, bytes);
        canvas.configureTool("select", {{"selector", "Rectangle"}});
        QVERIFY(!canvas.interactionFeedback()["gestureActive"].toBool());
    }
    void previewDoesNotWriteWorkingFile() {
        FeedbackCanvas canvas; prepare(canvas); QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/feedback-file-XXXXXX");
        const auto path = directory.filePath("Canvas.iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before = file.readAll(); file.close();
        canvas.configureTool("select", {{"selector", "Rectangle"}, {"field-0", "New"}});
        pointer(canvas, QEvent::MouseButtonPress, {12, 12}); pointer(canvas, QEvent::MouseMove, {44, 36});
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); file.close();
        pointer(canvas, QEvent::MouseButtonRelease, {44, 36});
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); file.close();
        canvas.clearAreaSelection(); canvas.configureTool("brush", {{"field-4", 12}, {"field-6", false}});
        pointer(canvas, QEvent::MouseButtonPress, {24, 24}); pointer(canvas, QEvent::MouseMove, {48, 32});
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before); file.close();
        QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, {}); QCoreApplication::sendEvent(&canvas, &key);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before);
    }
};
QTEST_MAIN(CanvasFeedbackTests)
#include "tst_canvasfeedback.moc"
