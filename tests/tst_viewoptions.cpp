#include "App/Views/Editor/EditorAlignmentOverlay.h"
#include "App/Views/Editor/EditorClippingOverlay.h"
#include "App/Views/Editor/EditorCanvas.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <QJSValue>
#include <QSignalSpy>
#include <QStyleHints>
#include <QtTest>
#include <backend/fonts/fontpolicy.h>
#include <cmath>
#include <iiSharedCanvas.h>
#include <memory>
#include <numbers>

namespace {
QQuickItem *visual(QQuickItem *root, const QString &name) {
    if (root->objectName() == name && root->isVisible())
        return root;
    for (auto *c : root->childItems())
        if (auto *v = visual(c, name))
            return v;
    return nullptr;
}
void collect(QQuickItem *root, QList<QQuickItem *> &items) {
    items.append(root);
    for (auto *c : root->childItems())
        collect(c, items);
}
QPointF at(const EditorCanvas &c, QPointF p) {
    return {c.panX() + (p.x() - c.canvasOriginX()) * c.zoom(),
            c.panY() + (p.y() - c.canvasOriginY()) * c.zoom()};
}
void click(QQuickWindow &window, QQuickItem *item) {
    // QWindow events use a simulated clock. Keep independent test clicks
    // outside the user's native double-click interval, as the GUI host does.
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(item->boundingRect().center()).toPoint(),
                      QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
    QCoreApplication::processEvents();
}
QQuickItem *menuEntry(QQuickItem *root, const QString &label) {
    if (root->isVisible() && root->property("label").toString() == label) return root;
    for (auto *child : root->childItems()) if (auto *entry = menuEntry(child, label)) return entry;
    return nullptr;
}
void prepare(EditorCanvas &c) {
    c.setSize({800, 600});
    c.createCanvas(
        {{"width", 1024}, {"height", 768}, {"unit", "px"}, {"ppi", 254}, {"background", "Transparent"}});
    c.setZoom(1);
    c.viewOptions()->edit("display", "constrain", false);
}
void only(EditorViewOptions *v, const QStringList &keys) {
    for (const auto &k : {"canvas-edges", "canvas-centers", "layer-edges", "layer-centers", "guides", "grid",
                          "pixel-edges", "pixel-centers", "anchors", "baselines", "spacing", "size"})
        v->edit("snap", k, keys.contains(QLatin1String(k)));
}
class InspectableOverlay : public EditorAlignmentOverlay {
  public:
    std::unique_ptr<QSGNode> geometry() { return std::unique_ptr<QSGNode>(updatePaintNode(nullptr, nullptr)); }
};
} // namespace
class ViewOptionsTests : public QObject {
    Q_OBJECT
  private slots:
    void outlineControlsShareOneState() {
        EditorViewOptions options;
        QVERIFY(options.edit("display", "composite", "Outlines"));
        QVERIFY(options.display()["outlines"].toBool());
        QVERIFY(options.edit("display", "outlines", false));
        QCOMPARE(options.display()["composite"].toString(), QString("Composite"));
        QVERIFY(options.edit("display", "outlines", true));
        QCOMPARE(options.display()["composite"].toString(), QString("Outlines"));
        QVERIFY(options.edit("display", "composite", "Composite"));
        QVERIFY(!options.display()["outlines"].toBool());
    }
    void gridLineSnapUsesExactGeometryAndFullTolerance() {
        EditorCanvas c; prepare(c); auto *v = c.viewOptions();
        only(v, {"grid"});
        QVERIFY(v->edit("grid", "enabled", true));
        QVERIFY(v->edit("grid", "intersections", false));
        QVERIFY(v->edit("snap", "axes", "Horizontal"));
        c.setZoom(.2);
        const auto exact = c.snapViewPosition(at(c, {82, 111}));
        QVERIFY2(QLineF(exact, at(c, {80, 111})).length() < 1e-6,
                 "Adaptive display must not change the exact 16 px grid used for snapping");
        c.setZoom(1);
        QVERIFY(v->edit("grid", "subdivisions", 1));
        QVERIFY(v->edit("grid", "spacing-x", 100));
        QVERIFY(v->edit("snap", "tolerance", 50));
        const auto wide = c.snapViewPosition(at(c, {60, 221}));
        QVERIFY2(QLineF(wide, at(c, {100, 221})).length() < 1e-6,
                 "Line snapping must search the full configured screen-pixel tolerance");
    }
    void deletedGuidesDoNotRemainSnapTargets() {
        EditorCanvas c; prepare(c); auto *v = c.viewOptions();
        only(v, {"guides"}); v->edit("guides", "position", 100);
        QVERIFY(c.createViewGuides());
        QVERIFY(QLineF(c.snapViewPosition(at(c, {98, 300})), at(c, {100, 300})).length() < 1e-6);
        v->clearGuides();
        QVERIFY(QLineF(c.snapViewPosition(at(c, {97, 300})), at(c, {97, 300})).length() < 1e-6);
    }
    void targetVisibilityAndPixelRevealAreRespected() {
        EditorCanvas c; prepare(c); auto *v = c.viewOptions();
        c.configureTool("elements", {{"selector", "Rectangle"}, {"field-2", "Solid"},
                                     {"field-3", false}, {"field-16", "#FF0000"}});
        QVERIFY(c.createElement({100, 100, 40, 40})); const auto target = c.selectedLayerId();
        QVERIFY(c.createElement({300, 300, 20, 20}));
        only(v, {"layer-edges"}); v->edit("snap", "axes", "Horizontal");
        QVERIFY(QLineF(c.snapViewPosition(at(c, {98, 110})), at(c, {100, 110})).length() < 1e-6);
        QVERIFY(c.setLayerVisible(target, false));
        QVERIFY(QLineF(c.snapViewPosition(at(c, {98, 110})), at(c, {98, 110})).length() < 1e-6);
        v->edit("snap", "visible-only", false);
        QVERIFY(QLineF(c.snapViewPosition(at(c, {98, 110})), at(c, {100, 110})).length() < 1e-6);
        only(v, {"grid"}); v->edit("snap", "visible-only", true); v->edit("snap", "axes", "Both");
        v->edit("grid", "enabled", true); v->edit("grid", "type", "Pixel");
        QVERIFY(QLineF(c.snapViewPosition(at(c, {98.2, 110.2})), at(c, {98.2, 110.2})).length() < 1e-6);
        c.setZoom(4);
        QVERIFY(QLineF(c.snapViewPosition(at(c, {98.2, 110.2})), at(c, {98, 110})).length() < 1e-6);
    }
    void equalSizeHonorsAxes() {
        for (const auto &axes : {QString("Both"), QString("Horizontal"), QString("Vertical")}) {
            EditorCanvas c; prepare(c); auto *v = c.viewOptions();
            c.configureTool("elements", {{"selector", "Rectangle"}, {"field-2", "Solid"},
                                         {"field-3", false}, {"field-16", "#FF0000"}});
            QVERIFY(c.createElement({500, 500, 80, 60}));
            QVERIFY(c.createElement({10, 10, 20, 20}));
            only(v, {"size"}); v->edit("snap", "axes", axes);
            QVERIFY(c.beginElementTransform("se", at(c, {30, 30})));
            QVERIFY(c.endElementTransform(at(c, {89, 68})));
            QCOMPARE(c.elementTransform()["width"].toDouble(), axes == "Vertical" ? 79. : 80.);
            QCOMPARE(c.elementTransform()["height"].toDouble(), axes == "Horizontal" ? 58. : 60.);
        }
    }
    void explicitDimensionsAreNotChangedByPointerConstraints() {
        EditorCanvas c; prepare(c);
        c.configureTool("elements", {{"selector", "Rectangle"}, {"field-2", "Solid"},
                                     {"field-3", false}, {"field-16", "#FF0000"}});
        QVERIFY(c.createElement({20, 20, 40, 40}));
        c.viewOptions()->edit("display", "constrain", true);
        c.viewOptions()->edit("snap", "scale", true);
        c.viewOptions()->edit("snap", "scale-step", 25);
        QVERIFY(c.applyToolField("elements", "field-0", QVariantList{83, 57}));
        QCOMPARE(c.elementTransform()["width"].toDouble(), 83.);
        QCOMPARE(c.elementTransform()["height"].toDouble(), 57.);
        QVERIFY(c.viewOptions()->display()["constrain"].toBool());
        QVERIFY(c.viewOptions()->snap()["scale"].toBool());
    }
    void scaleIncrementsStartAtUnchangedSize() {
        EditorCanvas c; prepare(c);
        c.configureTool("elements", {{"selector", "Rectangle"}, {"field-2", "Solid"},
                                     {"field-3", false}, {"field-16", "#FF0000"}});
        QVERIFY(c.createElement({20, 20, 40, 20})); only(c.viewOptions(), {});
        c.viewOptions()->edit("snap", "scale", true); c.viewOptions()->edit("snap", "scale-step", 24);
        const auto before = iiSharedCanvas::encodeIisc(*c.document()).bytes;
        QVERIFY(c.beginElementTransform("se", at(c, {60, 40}))); QVERIFY(c.endElementTransform(at(c, {60, 40})));
        QCOMPARE(iiSharedCanvas::encodeIisc(*c.document()).bytes, before);
        QVERIFY(c.beginElementTransform("se", at(c, {60, 40}))); QVERIFY(c.endElementTransform(at(c, {68, 44})));
        QVERIFY(std::abs(c.elementTransform()["width"].toDouble() - 49.6) < 1e-6);
        QVERIFY(std::abs(c.elementTransform()["height"].toDouble() - 24.8) < 1e-6);
    }
    void outlinesFollowActualPathsAndVisibility() {
        EditorCanvas c; prepare(c);
        c.configureTool("elements", {{"selector", "Ellipse"}, {"field-2", "Solid"},
                                     {"field-3", false}, {"field-16", "#FF0000"}});
        QVERIFY(c.createElement({100, 100, 200, 160}));
        const auto id = c.selectedLayerId();
        c.viewOptions()->edit("display", "outlines", true);
        InspectableOverlay overlay; overlay.setCanvas(&c); overlay.setFamily("outlines"); overlay.setSize(c.size());
        auto geometry = overlay.geometry();
        int vertices = 0;
        for (auto *node = geometry->firstChild(); node; node = node->nextSibling())
            vertices += static_cast<QSGGeometryNode *>(node)->geometry()->vertexCount();
        QVERIFY2(vertices > 16, "An ellipse outline must follow its curve rather than a four-edge box");
        QVERIFY(c.setLayerVisible(id, false));
        geometry = overlay.geometry();
        vertices = 0;
        for (auto *node = geometry->firstChild(); node; node = node->nextSibling())
            vertices += static_cast<QSGGeometryNode *>(node)->geometry()->vertexCount();
        QCOMPARE(vertices, 0); // Transparent documents have no background layer.
    }
    void gridDotsAreIntersections() {
        EditorCanvas c; prepare(c); c.setPanX(0); c.setPanY(0);
        auto *v = c.viewOptions(); v->edit("grid", "enabled", true);
        v->edit("grid", "style", "Dots"); v->edit("grid", "subdivisions", 1);
        InspectableOverlay overlay; overlay.setCanvas(&c); overlay.setFamily("grid"); overlay.setSize(c.size());
        auto geometry = overlay.geometry(); int vertices = 0;
        for (auto *node = geometry->firstChild(); node; node = node->nextSibling()) {
            const auto *g = static_cast<QSGGeometryNode *>(node)->geometry();
            QCOMPARE(g->drawingMode(), QSGGeometry::DrawPoints);
            const auto *points = g->vertexDataAsPoint2D();
            for (int i = 0; i < g->vertexCount(); ++i) {
                QVERIFY(std::abs(points[i].x / 64 - std::round(points[i].x / 64)) < 1e-5);
                QVERIFY(std::abs(points[i].y / 64 - std::round(points[i].y / 64)) < 1e-5);
            }
            vertices += g->vertexCount();
        }
        QVERIFY(vertices > 10 && vertices < 200);
    }
    void rasterAndSparseBoundsParticipateInViews() {
        using namespace iiSharedCanvas;
        for (const bool sparse : {false, true}) {
            Document document; document.extent = {512, 512};
            const auto pixels = makeRasterLayer(64, 64, 0xff884422);
            if (sparse) {
                document.canvasMode = CanvasMode::Infinite; document.infiniteCanvas.chunkSize = 64;
                document.assets.emplace_back(ChunkedRasterAsset{"bitmap", {{-1, 1, pixels}}});
            } else document.assets.emplace_back(RasterAsset{"bitmap", pixels});
            LayerProperties properties; properties.id = "bitmap.layer"; properties.name = "Bitmap";
            properties.transform.translationX = 100; properties.transform.translationY = 100;
            document.layers.emplace_back(StaticBitmapLayer{properties, StaticSource{"bitmap"}});
            EditorCanvas c; c.setSize({800, 600}); QVERIFY(c.loadProjectPage(std::move(document), "Bounds")); c.setZoom(1);
            c.configureTool("elements", {{"selector", "Rectangle"}, {"field-2", "Solid"},
                                         {"field-3", false}, {"field-16", "#FF0000"}});
            QVERIFY(c.createElement({300, 300, 20, 20}));
            only(c.viewOptions(), {"layer-edges"}); c.viewOptions()->edit("snap", "axes", "Horizontal");
            const auto left = sparse ? 36. : 100., top = sparse ? 164. : 100.;
            QVERIFY(QLineF(c.snapViewPosition(at(c, {left - 2, top + 15})), at(c, {left, top + 15})).length() < 1e-6);
            QVERIFY(c.setLayerVisible(c.selectedLayerId(), false));
            c.viewOptions()->edit("display", "outlines", true);
            InspectableOverlay overlay; overlay.setCanvas(&c); overlay.setFamily("outlines"); overlay.setSize(c.size());
            auto geometry = overlay.geometry(); int vertices = 0;
            for (auto *node = geometry->firstChild(); node; node = node->nextSibling())
                vertices += static_cast<QSGGeometryNode *>(node)->geometry()->vertexCount();
            QCOMPARE(vertices, 8); // Only the bitmap remains visible.
        }
    }
    void selectionRelativeOverlayRefreshesDuringTransform() {
        EditorCanvas c; prepare(c);
        c.configureTool("elements", {{"selector", "Rectangle"}, {"field-2", "Solid"},
                                     {"field-3", false}, {"field-16", "#FF0000"}});
        QVERIFY(c.createElement({100, 100, 100, 80}));
        c.viewOptions()->edit("grid", "reference", "Selection");
        c.viewOptions()->edit("snap", "enabled", false);
        EditorAlignmentOverlay overlay; overlay.setCanvas(&c); overlay.setFamily("grid"); overlay.setSize(c.size());
        QSignalSpy refreshed(&overlay, &EditorAlignmentOverlay::labelsChanged);
        const auto start = at(c, {150, 140});
        QVERIFY(c.beginElementTransform("move", start));
        refreshed.clear();
        QVERIFY(c.updateElementTransform(start + QPointF(30, 20)));
        QVERIFY2(!refreshed.isEmpty(), "Selection-relative overlays must refresh during a transform preview");
        c.cancelElementTransform();
    }
    void defaultsValidationAndPresets() {
        EditorViewOptions v;
        QSignalSpy changed(&v, &EditorViewOptions::changed);
        QVERIFY(v.snap().value("enabled").toBool());
        QCOMPARE(v.grid().value("type").toString(), QString("Rectangular"));
        QVERIFY(!v.grid().value("enabled").toBool());
        QCOMPARE(v.guides().value("orientation").toString(), QString("Vertical"));
        QVERIFY(!v.edit("grid", "subdivisions", 0));
        QVERIFY(!v.edit("grid", "subdivisions", 1.5));
        QVERIFY(!v.edit("grid", "spacing-x", std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!v.edit("grid", "type", "Invented"));
        QVERIFY(!v.edit("guides", "color", "garbage"));
        QVERIFY(!v.edit("snap", "enabled", "false"));
        QCOMPARE(changed.size(), 0);
        QVERIFY(v.preset("Pixel"));
        QCOMPARE(v.snap().value("tolerance").toDouble(), 1.);
        QVERIFY(v.snap().value("pixel-edges").toBool());
        QVERIFY(!v.snap().value("anchors").toBool());
        QVERIFY(v.preset("Vector"));
        QVERIFY(v.snap().value("anchors").toBool());
        QVERIFY(v.snap().value("scale").toBool());
        QVERIFY(v.preset("Layout"));
        QVERIFY(v.snap().value("spacing").toBool());
        QVERIFY(v.snap().value("baselines").toBool());
        v.reset("snap");
        QCOMPARE(v.snap().value("preset").toString(), QString("Smart"));
        QCOMPARE(v.snap().value("tolerance").toDouble(), 8.);
    }
    void rulersUsePixelCoordinates() {
        EditorCanvas canvas;
        canvas.setSize({800, 600});
        QCOMPARE(canvas.viewOptions()->property("rulerUnit").toString(), QString("px"));
        for (const double ppi : {72., 254., 300.}) {
            QVERIFY(canvas.createCanvas({{"width", 1024}, {"height", 768}, {"unit", "px"},
                                         {"ppi", ppi}, {"background", "Transparent"}}));
            QVERIFY(canvas.viewOptions()->edit("display", "constrain", false));
            canvas.setZoom(2);
            canvas.panBy(37, 29);
            for (const bool horizontal : {true, false}) {
                const auto ticks = canvas.viewRulerTicks(horizontal, horizontal ? 800 : 600);
                QVERIFY(!ticks.isEmpty());
                for (const auto& entry : ticks) {
                    const auto tick = entry.toMap();
                    const double pan = horizontal ? canvas.panX() : canvas.panY();
                    const double origin = horizontal ? canvas.canvasOriginX() : canvas.canvasOriginY();
                    const auto expectedPixels = (tick["position"].toDouble() - pan) / canvas.zoom() + origin;
                    QVERIFY(qAbs(tick["label"].toString().toDouble() - expectedPixels) < 0.0001);
                }
            }
        }
    }
    void unitsReferencesAndGuideCreation() {
        EditorCanvas c;
        prepare(c);
        auto *v = c.viewOptions();
        const auto before = iiSharedCanvas::encodeIisc(*c.document()).bytes;
        QVERIFY(v->edit("grid", "units", "mm"));
        QVERIFY(v->edit("grid", "spacing-x", 10));
        auto g = v->gridGeometry({20, 40, 1000, 800}, 254);
        QCOMPARE(g.spacingX, 100.);
        QCOMPARE(g.origin.x, 20.);
        v->edit("grid", "units", "px");
        v->edit("grid", "type", "Layout");
        v->edit("grid", "origin-x", 25);
        v->edit("grid", "origin-y", 30);
        g = v->gridGeometry({20, 40, 1000, 800}, 254);
        QCOMPARE(g.reference.x, 45.);
        QCOMPARE(g.reference.y, 70.);
        QCOMPARE(g.origin.x, 45.);
        QVERIFY(v->edit("grid", "units", "%"));
        g = v->gridGeometry({20, 40, 1000, 800}, 254);
        QCOMPARE(g.spacingX, 100.);
        QVERIFY(v->edit("guides", "type", "Video safe"));
        QVERIFY(c.createViewGuides());
        QCOMPARE(v->guideCount(), 8);
        QVERIFY(std::abs(v->placedGuides()[0].line.a.x - 51.2) < 1e-8);
        QVERIFY(v->edit("guides", "locked", true));
        QVERIFY(!v->moveGuide(0, {4, 8}));
        QVERIFY(!v->removeGuide(0));
        QVERIFY(v->edit("guides", "locked", false));
        QVERIFY(v->moveGuide(0, {4, 8}));
        QVERIFY(v->removeGuide(0));
        QCOMPARE(v->guideCount(), 7);
        QVERIFY(v->edit("guides", "clear-existing", true));
        QVERIFY(v->edit("guides", "type", "Rule of thirds"));
        QVERIFY(c.createViewGuides());
        QCOMPARE(v->guideCount(), 4);
        QCOMPARE(iiSharedCanvas::encodeIisc(*c.document()).bytes,
                 before); // View geometry never changes IISC pixels or Undo.
    }
    void guidesUseActualArtboardsAndPercentageUnits() {
        iiSharedCanvas::Document document; document.extent = {1000, 500};
        document.artboards = {{"one", "One", {{100, 50}, {200, 100}}, 0, true},
                             {"two", "Two", {{500, 150}, {300, 200}}, 0, true}};
        EditorCanvas c; c.setSize({800, 600}); QVERIFY(c.loadProjectPage(std::move(document), "Boards"));
        const auto before = iiSharedCanvas::encodeIisc(*c.document()).bytes;
        auto *v = c.viewOptions(); v->edit("guides", "all-artboards", true); v->edit("guides", "type", "Rule of thirds");
        QVERIFY(c.createViewGuides()); QCOMPARE(v->guideCount(), 8);
        QCOMPARE(v->placedGuides()[0].artboard, QString("one")); QCOMPARE(v->placedGuides()[4].artboard, QString("two"));
        QVERIFY(std::abs(v->placedGuides()[0].line.a.x - (100 + 200. / 3)) < 1e-6);
        QCOMPARE(v->placedGuides()[4].line.a.x, 600.);
        v->edit("guides", "clear-existing", true); v->edit("guides", "type", "Single");
        v->edit("guides", "units", "%"); v->edit("guides", "position", 50);
        QVERIFY(c.createViewGuides()); QCOMPARE(v->guideCount(), 2);
        QCOMPARE(v->placedGuides()[0].line.a.x, 200.); QCOMPARE(v->placedGuides()[1].line.a.x, 650.);
        QCOMPARE(iiSharedCanvas::encodeIisc(*c.document()).bytes, before);
    }
    void guideLabelsUseStableNumbersAndAvoidRulers() {
        EditorCanvas c; prepare(c); c.setPanX(-200); c.setPanY(-100);
        auto *v = c.viewOptions(); v->edit("guides", "position", 100); QVERIFY(c.createViewGuides());
        v->edit("guides", "position", 320); QVERIFY(c.createViewGuides());
        v->edit("guides", "labels", true); v->edit("guides", "color", "#123456"); v->edit("guides", "opacity", 25);
        EditorAlignmentOverlay overlay; overlay.setCanvas(&c); overlay.setFamily("guides"); overlay.setSize(c.size());
        const auto labels = overlay.labels(); QCOMPARE(labels.size(), 1);
        const auto label = labels.front().toMap(); QCOMPARE(label["label"].toString(), QString("2"));
        const auto position = label["position"].toPointF(); QVERIFY(position.x() >= 26 - 1e-6 && position.y() >= 26 - 1e-6);
        const auto color = label["color"].value<QColor>(); QCOMPARE(color.name(), QString("#123456"));
        QVERIFY(std::abs(color.alphaF() - .25) < .005);
        v->edit("guides", "color", "#80123456");
        QVERIFY(std::abs(overlay.labels().front().toMap()["color"].value<QColor>().alphaF() - .125) < .005);
    }
    void gridSnapAndToggleAreReal() {
        EditorCanvas c;
        prepare(c);
        auto *v = c.viewOptions();
        only(v, {"grid"});
        v->edit("grid", "enabled", true);
        v->edit("grid", "subdivisions", 1);
        auto p = c.snapViewPosition(at(c, {61, 67}));
        QVERIFY(QLineF(p, at(c, {64, 64})).length() < 1e-6);
        v->edit("snap", "enabled", false);
        p = c.snapViewPosition(at(c, {61, 67}));
        QVERIFY(QLineF(p, at(c, {61, 67})).length() < 1e-6);
        v->edit("snap", "enabled", true);
        v->edit("grid", "enabled", false);
        p = c.snapViewPosition(at(c, {61, 67}));
        QVERIFY(QLineF(p, at(c, {61, 67})).length() < 1e-6);
    }
    void elementMoveSnapsAndKeepsSingleUndo() {
        EditorCanvas c;
        prepare(c);
        auto *v = c.viewOptions();
        only(v, {"guides"});
        v->edit("guides", "position", 100);
        QVERIFY(c.createViewGuides());
        c.configureTool(
            "elements",
            {{"selector", "Rectangle"}, {"field-2", "Solid"}, {"field-3", false}, {"field-16", "#FF0000"}});
        QVERIFY(c.createElement({10, 10, 20, 20}));
        QSignalSpy history(&c, &EditorCanvas::undoRedoChanged);
        const auto before = iiSharedCanvas::encodeIisc(*c.document()).bytes;
        QVERIFY(c.beginElementTransform("move", at(c, {20, 20})));
        QVERIFY(c.updateElementTransform(at(c, {108, 20})));
        QVERIFY(std::abs(c.elementTransform().value("x").toDouble() - 100) < 1e-6);
        QCOMPARE(history.size(), 0);
        QVERIFY(!c.alignmentFeedback().isEmpty());
        QVERIFY(c.endElementTransform(at(c, {108, 20})));
        QCOMPARE(history.size(), 1);
        QVERIFY(c.alignmentFeedback().isEmpty());
        QVERIFY(c.undo());
        QCOMPARE(iiSharedCanvas::encodeIisc(*c.document()).bytes, before);
    }
    void rotationScaleAndEqualSpacing() {
        EditorCanvas c;
        prepare(c);
        auto *v = c.viewOptions();
        only(v, {});
        c.configureTool(
            "elements",
            {{"selector", "Rectangle"}, {"field-2", "Solid"}, {"field-3", false}, {"field-16", "#FF0000"}});
        QVERIFY(c.createElement({10, 10, 20, 20}));
        const auto state = c.elementTransform();
        const auto center = state.value("center").toPointF(), start = state.value("rotate").toPointF(),
                   vector = start - center;
        const double a = 23 * std::numbers::pi / 180;
        const auto finish = center + QPointF(vector.x() * std::cos(a) - vector.y() * std::sin(a),
                                             vector.x() * std::sin(a) + vector.y() * std::cos(a));
        QVERIFY(c.beginElementTransform("rotate", start));
        QVERIFY(c.endElementTransform(finish));
        QVERIFY(std::abs(c.elementTransform().value("rotation").toDouble() - 30) < 1e-6);
        QVERIFY(c.undo());
        v->edit("snap", "scale", true);
        v->edit("snap", "scale-step", 25);
        QPointF corner;
        for (const auto &value : c.elementTransform().value("handles").toList())
            if (value.toMap().value("id") == "se")
                corner = value.toMap().value("point").toPointF();
        QVERIFY(c.beginElementTransform("se", corner));
        QVERIFY(c.endElementTransform(corner + QPointF(9.2, 9.2) * c.zoom()));
        QVERIFY(std::abs(c.elementTransform().value("width").toDouble() - 30) < 1e-6);
        c.viewOptions()->edit("snap", "scale", false);
        QVERIFY(c.createElement({20, 100, 20, 20}));
        QVERIFY(c.createElement({80, 100, 20, 20}));
        QVERIFY(c.createElement({10, 200, 20, 20}));
        only(v, {"spacing"});
        const auto begin = at(c, {20, 210});
        QVERIFY(c.beginElementTransform("move", begin));
        QVERIFY(c.endElementTransform(begin + QPointF(128, 0) * c.zoom()));
        QVERIFY(std::abs(c.elementTransform().value("x").toDouble() - 140) < 1e-6);
    }
    void guideDragAndLockThroughPointer() {
        QQuickWindow window;
        window.resize(800, 600);
        EditorCanvas c(window.contentItem());
        prepare(c);
        c.viewOptions()->edit("guides", "position", 100);
        QVERIFY(c.createViewGuides());
        EditorAlignmentOverlay overlay(window.contentItem());
        overlay.setCanvas(&c);
        overlay.setFamily("guides");
        overlay.setSize({800, 600});
        overlay.setZ(10);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto start = at(c, {100, 300}).toPoint();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&window, start + QPoint(20, 30));
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, start + QPoint(20, 30));
        QVERIFY(std::abs(c.viewOptions()->placedGuides()[0].line.a.x - 120) < 1e-6);
        QVERIFY(std::abs(c.viewOptions()->placedGuides()[0].line.a.y) < 1e-6);
        c.viewOptions()->edit("guides", "locked", true);
        const auto before = c.viewOptions()->placedGuides()[0].line.a.x;
        QTest::keyClick(&window, Qt::Key_Delete);
        QCOMPARE(c.viewOptions()->guideCount(), 1);
        QCOMPARE(c.viewOptions()->placedGuides()[0].line.a.x, before);
        c.viewOptions()->edit("guides", "locked", false);
        QTest::keyClick(&window, Qt::Key_Delete);
        QCOMPARE(c.viewOptions()->guideCount(), 0);
    }
    void invisibleGuidesDoNotCapturePointer() {
        for (const bool transparentColour : {false, true}) {
            QQuickWindow window; window.resize(800, 600);
            EditorCanvas c; prepare(c); c.viewOptions()->edit("guides", "position", 100);
            QVERIFY(c.createViewGuides());
            if (transparentColour) c.viewOptions()->edit("guides", "color", "transparent");
            else c.viewOptions()->edit("guides", "opacity", 0);
            EditorAlignmentOverlay overlay(window.contentItem()); overlay.setCanvas(&c); overlay.setFamily("guide-input"); overlay.setSize({800, 600});
            window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
            const auto start = at(c, {100, 300}).toPoint();
            QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
            QTest::mouseMove(&window, start + QPoint(20, 0));
            QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, start + QPoint(20, 0));
            QCOMPARE(c.viewOptions()->placedGuides().front().line.a.x, 100.);
            only(c.viewOptions(), {"guides"});
            QVERIFY(QLineF(c.snapViewPosition(at(c, {98, 300})), at(c, {98, 300})).length() < 1e-6);
        }
    }
    void allPanelStates_data() {
        QTest::addColumn<QString>("family");
        QTest::addColumn<QString>("state");
        QTest::addColumn<int>("width");
        for (const auto width : {384, 304})
            for (const auto &family : {QString("snap"), QString("grid"), QString("guides")}) {
                const auto states =
                    family == "snap" ? QStringList{"Smart", "Pixel", "Vector", "Layout"}
                    : family == "grid"
                        ? QStringList{"Rectangular", "Isometric", "Polar", "Perspective",
                                      "Pixel",       "Baseline",  "Layout"}
                        : QStringList{"Single",         "Layout",       "Video safe", "Print / bleed",
                                      "Rule of thirds", "Golden ratio", "Diagonals"};
                for (const auto &s : states)
                    QTest::newRow(qPrintable(family + "-" + s + "-" + QString::number(width)))
                        << family << s << width;
            }
    }
    void allPanelStates() {
        QFETCH(QString, family);
        QFETCH(QString, state);
        QFETCH(int, width);
        EditorCanvas c;
        prepare(c);
        QVERIFY(c.viewOptions()->edit(family, family == "snap" ? "preset" : "type", state));
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR) +
                                                             "/Views/Editor/EditorViewOptionsPanel.qml"));
        std::unique_ptr<QObject> object(
            component.createWithInitialProperties({{"canvas", QVariant::fromValue(&c)}, {"family", family}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *root = qobject_cast<QQuickItem *>(object.get());
        QVERIFY(root);
        QQuickWindow window;
        window.resize(width, 900);
        window.setColor(QColor("#1d1e1e"));
        root->setParentItem(window.contentItem());
        root->setWidth(width);
        root->setHeight(std::min(850., root->implicitHeight()));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(30);
        QVERIFY(root->implicitHeight() > 400 && root->implicitHeight() < 1000);
        QList<QQuickItem *> nodes;
        collect(root, nodes);
        int controls = 0;
        for (auto *item : nodes)
            if (item->isVisible() && item->objectName().startsWith("viewOption-")) {
                QVERIFY2(item->width() > 0, qPrintable(item->objectName()));
                QVERIFY2(item->x() + item->width() <= item->parentItem()->width() + 1,
                         qPrintable(item->objectName()));
                ++controls;
            }
        QVERIFY(controls >= 10);
        const auto folder = QString(DREAMSCAPES_TEST_DIRECTORY) + "/view-options-verification";
        QVERIFY(QDir().mkpath(folder));
        QVariantList geometry;
        for (auto *item : nodes)
            if (item->isVisible() && item->objectName().startsWith("viewOption-"))
                geometry.append(QVariantMap{
                    {"name", item->objectName()}, {"width", item->width()}, {"height", item->height()}});
        QFile report(folder + "/" + family + "-" + state.replace('/', '-') + "-" + QString::number(width) +
                     ".json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument::fromVariant(QVariantMap{{"family", family},
                                                            {"state", state},
                                                            {"width", width},
                                                            {"implicitHeight", root->implicitHeight()},
                                                            {"controls", geometry}})
                         .toJson());
        if (width == 384) {
            QTest::qWait(60);
            const auto image = window.grabWindow();
            QVERIFY(image.save(folder + "/" + family + "-" + state + ".png"));
            auto *preview = visual(root, "viewOptionsPreview-" + family);
            QVERIFY(preview);
            const auto bounds = preview->mapRectToScene(preview->boundingRect());
            const double scale = double(image.width()) / window.width();
            int colored = 0;
            for (int y = std::max(0, int(bounds.top() * scale));
                 y < std::min(image.height(), int(bounds.bottom() * scale)); ++y)
                for (int x = std::max(0, int(bounds.left() * scale));
                     x < std::min(image.width(), int(bounds.right() * scale)); ++x) {
                    const auto pixel = image.pixelColor(x, y);
                    if (pixel.blue() > pixel.red() + 5)
                        ++colored;
                }
            QVERIFY2(colored > 10, "The live geometry preview must render on both GPU and software backends");
        }
    }
    void topBarOpensSettingsAndEditsRealInput() {
        EditorCanvas c;
        prepare(c);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR) +
                                                             "/Views/Editor/EditorViewBar.qml"));
        std::unique_ptr<QObject> object(
            component.createWithInitialProperties({{"canvas", QVariant::fromValue(&c)}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *bar = qobject_cast<QQuickItem *>(object.get());
        QVERIFY(bar);
        QQuickWindow window;
        window.resize(1800, 1000);
        bar->setParentItem(window.contentItem());
        bar->setWidth(1800);
        bar->setHeight(40);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (const auto &name : {"grid", "guides", "snap"}) {
            auto *arrow = visual(bar, QString("editorViewOptions-") + name);
            QVERIFY(arrow);
            click(window, arrow);
            QTRY_VERIFY(bar->property("modalActive").toBool());
            auto *panel = qvariant_cast<QObject *>(bar->property("optionsPanel"));
            QVERIFY(panel);
            QCOMPARE(panel->property("family").toString(), QString(name));
            auto *done = visual(window.contentItem(), "viewOptionsDone");
            QVERIFY(done);
            click(window, done);
            QTRY_VERIFY(!bar->property("modalActive").toBool());
        }
        click(window, visual(bar, "editorViewOptions-snap"));
        auto *input = visual(window.contentItem(), "viewOption-snap-tolerance");
        QVERIFY(input);
        click(window, input);
        QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(&window, Qt::Key_1);
        QTest::keyClick(&window, Qt::Key_7);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(c.viewOptions()->snap().value("tolerance").toDouble(), 17.);
        auto *pixel = visual(window.contentItem(), "viewOption-snap-preset-Pixel");
        QVERIFY(pixel);
        click(window, pixel);
        QCOMPARE(c.viewOptions()->snap().value("preset").toString(), QString("Pixel"));
        QCOMPARE(c.viewOptions()->snap().value("tolerance").toDouble(), 1.);
        click(window, visual(window.contentItem(), "viewOptionsDone"));
        auto *toggle = visual(bar, "editorViewToggle-snap-enabled");
        QVERIFY(toggle);
        click(window, toggle);
        QVERIFY(!c.viewOptions()->snap().value("enabled").toBool());
    }
    void detailControlsCommit_data() {
        QTest::addColumn<QString>("family"); QTest::addColumn<QString>("state");
        const std::array<QStringList, 3> states{{{"Smart", "Pixel", "Vector", "Layout"},
            {"Rectangular", "Isometric", "Polar", "Perspective", "Pixel", "Baseline", "Layout"},
            {"Single", "Layout", "Video safe", "Print / bleed", "Rule of thirds", "Golden ratio", "Diagonals"}}};
        const QStringList families{"snap", "grid", "guides"};
        for (int i = 0; i < families.size(); ++i) for (const auto &state : states[i])
            QTest::newRow(qPrintable(families[i] + '-' + state)) << families[i] << state;
    }
    void ordinaryEditsPreserveControlInstances() {
        EditorCanvas c; prepare(c);
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR) + "/Views/Editor/EditorViewOptionsPanel.qml"));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"canvas", QVariant::fromValue(&c)}, {"family", "snap"}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *panel = qobject_cast<QQuickItem *>(object.get()); QVERIFY(panel); panel->setSize({384, 850});
        QPointer<QQuickItem> input(visual(panel, "viewOption-snap-tolerance")); QVERIFY(input);
        QVERIFY(c.viewOptions()->edit("snap", "tolerance", 17)); QCoreApplication::processEvents();
        QVERIFY(input); QCOMPARE(visual(panel, "viewOption-snap-tolerance"), input.data());
        QVERIFY(c.viewOptions()->preset("Pixel")); QCoreApplication::processEvents();
        QVERIFY(input); QCOMPARE(visual(panel, "viewOption-snap-tolerance"), input.data());
        QCOMPARE(input->property("text").toString(), QString("1 px"));
    }
    void detailControlsCommit() {
        QFETCH(QString, family); QFETCH(QString, state);
        EditorCanvas c; prepare(c);
        QVERIFY(c.viewOptions()->edit(family, family == "snap" ? "preset" : "type", state));
        if (family == "guides") { QVERIFY(c.createViewGuides()); QVERIFY(c.viewOptions()->guideCount() > 0); }
        QFile schemaFile(":/editor/ViewOptions.json"); QVERIFY(schemaFile.open(QIODevice::ReadOnly));
        const auto schema = QJsonDocument::fromJson(schemaFile.readAll()).toVariant().toMap()[family].toMap();
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR) + "/Views/Editor/EditorViewOptionsPanel.qml"));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"canvas", QVariant::fromValue(&c)}, {"family", family}}));
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *panel = qobject_cast<QQuickItem *>(object.get()); QVERIFY(panel);
        QQuickWindow window; window.resize(384, 900); panel->setParentItem(window.contentItem()); panel->setSize({384, 850});
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *scroll = visual(panel, "viewOptionsScroll"); QVERIFY(scroll);
        const auto expose = [&](QQuickItem *control) {
            const auto y = control->mapToItem(scroll, QPointF()).y() + scroll->property("contentY").toDouble();
            scroll->setProperty("contentY", std::clamp(y - 100., 0., std::max(0., scroll->property("contentHeight").toDouble() - scroll->height())));
            QTest::qWait(20);
        };
        auto keys = schema.keys(); keys.removeAll("type"); keys.removeAll("preset");
        keys.append(family == "snap" ? "preset" : "type");
        QStringList checked;
        for (const auto &key : keys) {
            const auto current = c.viewOptions()->property(family.toLatin1()).toMap()[key];
            const auto spec = schema[key].toMap();
            const auto name = "viewOption-" + family + '-' + key;
            auto *control = visual(panel, name);
            QVariant expected;
            if (current.metaType().id() == QMetaType::Bool && control) {
                expected = !current.toBool(); expose(control); click(window, control);
            } else if (control && control->property("dirty").isValid()) {
                expected = key == "color" ? QVariant(QString("#123456")) : QVariant(std::min(spec["max"].toDouble(), current.toDouble() + 1));
                expose(control); click(window, control);
                QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
                for (const auto character : expected.toString()) QTest::keyClick(&window, character.toLatin1());
                QTest::keyClick(&window, Qt::Key_Return);
            } else if (spec.contains("options")) {
                for (const auto &option : spec["options"].toList()) {
                    if (option == current) continue;
                    if (auto *choice = visual(panel, name + '-' + option.toString())) {
                        expected = option; expose(choice); click(window, choice); break;
                    }
                    if (control) {
                        QPointer<QQuickItem> owner(control->parentItem());
                        QVERIFY(owner && owner->property("modalActive").isValid());
                        expected = option; expose(control); click(window, control);
                        QTRY_VERIFY(menuEntry(window.contentItem(), option.toString()));
                        QPointer<QQuickItem> entry(menuEntry(window.contentItem(), option.toString()));
                        click(window, entry);
                        QTRY_VERIFY(!owner || !owner->property("modalActive").toBool()); break;
                    }
                }
            }
            if (!expected.isValid()) continue;
            QTRY_COMPARE(c.viewOptions()->property(family.toLatin1()).toMap()[key], expected);
            checked.append(key);
        }
        QVERIFY2(checked.size() >= 10, qPrintable(state));
        const auto output = QString(DREAMSCAPES_TEST_DIRECTORY) + "/view-options-details"; QVERIFY(QDir().mkpath(output));
        auto safeState = state; safeState.replace('/', '-');
        QFile report(output + "/controls-" + family + '-' + safeState + ".json"); QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument::fromVariant(QVariantMap{{"family", family}, {"state", state}, {"committedControls", checked}}).toJson());
        click(window, visual(panel, "viewOptionsReset"));
        for (auto i = schema.cbegin(); i != schema.cend(); ++i)
            QCOMPARE(c.viewOptions()->property(family.toLatin1()).toMap()[i.key()], i.value().toMap()["default"]);
        // Every guide mode exposes its reference, including safe/composition
        // modes after switching away from an unavailable Selection reference.
        if (family == "guides") {
            c.viewOptions()->edit("guides", "reference", "Canvas");
            c.viewOptions()->edit("guides", "type", "Rule of thirds");
            QVERIFY(c.createViewGuides());
            auto *clear = visual(panel, "viewOptionsClearGuides"); QVERIFY(clear && clear->isEnabled());
            click(window, clear); QCOMPARE(c.viewOptions()->guideCount(), 0);
        }
        QSignalSpy closed(panel, SIGNAL(closeRequested()));
        click(window, visual(panel, "viewOptionsClose")); QCOMPARE(closed.size(), 1);
        click(window, visual(panel, "viewOptionsDone")); QCOMPARE(closed.size(), 2);
    }
};
int main(int argc, char **argv) {
    QQuickStyle::setStyle("Basic");
    QApplication app(argc, argv);
    FontPolicy::loadBundledFonts();
    FontPolicy::installPretendardFallbacks();
    FontPolicy::enforcePretendardFallback();
    qmlRegisterType<EditorAlignmentOverlay>("Dreamscapes.Storage", 1, 0, "EditorAlignmentOverlay");
    qmlRegisterType<EditorClippingOverlay>("Dreamscapes.Storage", 1, 0, "EditorClippingOverlay");
    qmlRegisterAnonymousType<EditorViewOptions>("Dreamscapes.Storage", 1);
    ViewOptionsTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "tst_viewoptions.moc"
