#include "App/Views/Editor/EditorCanvas.h"
#include <iiSharedCanvas.h>
#include <QImage>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace iiSharedCanvas;

class ElementsSelectionTests : public QObject {
    Q_OBJECT
private:
    static QVariantMap settings(const QString &shape = "Rectangle", const QString &fill = "Solid") {
        return {{"selector", shape}, {"field-0", QVariantList{1080, 1080}}, {"field-1", 2},
            {"field-2", fill}, {"field-3", false}, {"field-4", 360}, {"field-5", 0},
            {"field-6", "Inside"}, {"field-7", 5}, {"field-8", 0}, {"field-9", false},
            {"field-10", 1}, {"field-11", "Round"}, {"field-12", "None"}, {"field-13", "Arrow"},
            {"field-14", 100}, {"field-15", "Normal"}, {"field-16", "#FF0000"}, {"field-17", "#0000FF"}};
    }
    static QVariantMap candidate(const EditorCanvas &canvas, const QString &id) {
        for (const auto &entry : canvas.elementCandidates()) if (entry.toMap().value("id").toString() == id) return entry.toMap();
        return {};
    }
    static Document foreignDocument(bool artboards = false) {
        Document document;
        document.extent = {64, 48};
        if (artboards) document.artboards = {{"one", "One", {{0, 0}, {64, 48}}, 0}, {"two", "Two", {{80, 0}, {64, 48}}, 0}};
        VectorPath path;
        path.commands = {MoveTo{{2, 2}}, LineTo{{12, 2}}, LineTo{{12, 12}}, ClosePath{}};
        path.fill = SolidPaint{0xff00ff00};
        document.assets.emplace_back(VectorAsset{"foreign.asset", document.extent, {path}});
        LayerProperties foreign{"foreign", "Imported path"};
        if (artboards) foreign.artboardId = "one";
        document.layers.emplace_back(StaticVectorLayer{foreign, StaticSource{"foreign.asset"}});
        document.assets.emplace_back(RasterAsset{"photo.asset", makeRasterLayer(64, 48)});
        LayerProperties photo{"photo", "Bitmap photo"};
        if (artboards) photo.artboardId = "one";
        document.layers.emplace_back(StaticBitmapLayer{photo, StaticSource{"photo.asset"}});
        if (artboards) {
            LayerProperties second{"other", "Other artboard path"}; second.artboardId = "two";
            document.layers.emplace_back(StaticVectorLayer{second, StaticSource{"foreign.asset"}});
            LayerProperties background{"one:background.layer", "Background"}; background.artboardId = "one";
            document.layers.emplace_back(StaticVectorLayer{background, StaticSource{"foreign.asset"}});
        }
        return document;
    }
    static void create(EditorCanvas &canvas, const QString &background = "Transparent") {
        QVERIFY(canvas.createCanvas({{"width", 64}, {"height", 48}, {"unit", "px"}, {"background", background}}));
    }
private slots:
    void selectedDimensionsFollowTheRealLayerTransform() {
        EditorCanvas canvas; create(canvas);
        canvas.configureTool("elements", settings());
        QVERIFY(canvas.createElement({4, 5, 20, 12}));
        const auto id = canvas.selectedLayerId();
        QVERIFY(canvas.setLayerTransform(id, {{"scaleX", 2.5}, {"scaleY", 1.75}, {"x", 3}, {"y", 7}}));
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({50.0, 21.0}));
        QVERIFY(canvas.applyToolField("elements", "field-0", QVariantList{75.0, 35.0}));
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({75.0, 35.0}));
        const auto &transform = layerProperties(*findLayer(*canvas.document(), id.toStdString())).transform;
        QCOMPARE(transform.m11, 2.5); QCOMPARE(transform.m22, 1.75);
        QVERIFY(canvas.undo());
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({50.0, 21.0}));
    }
    void repeatedElementsConfigurationPreservesTheMouseCreationGesture() {
        class InputCanvas final : public EditorCanvas {
        public:
            using EditorCanvas::mousePressEvent;
            using EditorCanvas::mouseMoveEvent;
            using EditorCanvas::mouseReleaseEvent;
        };
        InputCanvas canvas; canvas.setSize({640, 480}); create(canvas);
        const auto draft = settings(); canvas.configureTool("elements", draft);
        const auto before = encodeIisc(*canvas.document()).bytes;
        const auto revision = canvas.revision();
        const auto layerCount = canvas.document()->layers.size();
        const auto point = [&](qreal x, qreal y) {
            return QPointF(canvas.panX() + x * canvas.zoom(), canvas.panY() + y * canvas.zoom());
        };
        const auto start = point(4, 5), finish = point(24, 17);
        QMouseEvent press(QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        canvas.mousePressEvent(&press); QVERIFY(press.isAccepted());
        QSignalSpy configured(&canvas, &EditorCanvas::toolStateChanged);
        canvas.configureTool("elements", draft);
        QCOMPARE(configured.count(), 0);
        QCOMPARE(canvas.revision(), revision);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        QMouseEvent move(QEvent::MouseMove, finish, finish, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        canvas.mouseMoveEvent(&move); QVERIFY(move.isAccepted());
        QMouseEvent release(QEvent::MouseButtonRelease, finish, finish, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        canvas.mouseReleaseEvent(&release); QVERIFY(release.isAccepted());
        QCOMPARE(canvas.document()->layers.size(), layerCount + 1);
        QCOMPARE(canvas.elementCandidates().size(), 1);
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({20.0, 12.0}));
        const auto rendered = renderFrame(*canvas.document(), 0); QVERIFY(rendered.ok());
        QCOMPARE(rendered.pixels.pixels[9 * 64 + 10], 0xffff0000U);
        const auto completed = encodeIisc(*canvas.document()).bytes;
        QVERIFY(completed != before);
        QVERIFY(canvas.undo()); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        QVERIFY(canvas.redo()); QCOMPARE(encodeIisc(*canvas.document()).bytes, completed);

        // A genuinely different draft or another tool still cancels creation.
        press.setAccepted(false); canvas.mousePressEvent(&press); QVERIFY(press.isAccepted());
        auto changed = canvas.toolValues("elements"); changed["field-16"] = "#00FF00";
        canvas.configureTool("elements", changed);
        canvas.mouseReleaseEvent(&release);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, completed);
        QCOMPARE(canvas.document()->layers.size(), layerCount + 1);
        canvas.mousePressEvent(&press);
        canvas.configureTool("text", {{"field-0", "Text"}, {"field-2", 12}});
        canvas.mouseReleaseEvent(&release);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, completed);
        QCOMPARE(canvas.document()->layers.size(), layerCount + 1);
    }
    void candidatesUseRealLayersAndExcludeBackground() {
        EditorCanvas canvas; create(canvas, "White");
        QVERIFY(canvas.elementCandidates().isEmpty());
        canvas.configureTool("elements", settings());
        QVERIFY(canvas.createElement({4, 5, 20, 12}));
        const auto rectangle = canvas.selectedLayerId();
        QCOMPARE(canvas.elementCandidates().size(), 1);
        QCOMPARE(candidate(canvas, rectangle).value("shape").toString(), "Rectangle");
        QVERIFY(candidate(canvas, rectangle).value("editable").toBool());
        canvas.configureTool("elements", settings("Ellipse", "Gradient"));
        QVERIFY(canvas.createElement({28, 5, 20, 12}));
        const auto gradient = canvas.selectedLayerId();
        QVERIFY(std::holds_alternative<StaticBitmapLayer>(*findLayer(*canvas.document(), gradient.toStdString())));
        QCOMPARE(candidate(canvas, gradient).value("shape").toString(), "Ellipse");
        QVERIFY(candidate(canvas, gradient).value("editable").toBool());
        QVERIFY(canvas.clearElementSelection());
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/element-fill-XXXXXX"); QVERIFY(directory.isValid());
        QImage image(2, 2, QImage::Format_ARGB32); image.fill(Qt::yellow);
        const auto path = directory.filePath("fill.png"); QVERIFY(image.save(path));
        const auto imageSettings = settings("Polygon", "Image");
        canvas.configureTool("elements", imageSettings);
        QVERIFY(canvas.importToolSource(QUrl::fromLocalFile(path), "elements", imageSettings));
        QVERIFY(canvas.createElement({4, 24, 20, 16}));
        const auto imageElement = canvas.selectedLayerId();
        QVERIFY(std::holds_alternative<StaticBitmapLayer>(*findLayer(*canvas.document(), imageElement.toStdString())));
        QCOMPARE(candidate(canvas, imageElement).value("shape").toString(), "Polygon");
        QVERIFY(candidate(canvas, imageElement).value("editable").toBool());
        QVERIFY(canvas.addPaintLayer());
        QCOMPARE(canvas.elementCandidates().size(), 3);
        canvas.configureTool("text", {{"field-0", "Text"}, {"field-2", 12}, {"textFill", "White"}});
        QVERIFY(canvas.createText({1, 1}));
        QCOMPARE(canvas.elementCandidates().size(), 3);
    }
    void multipleSelectionIsReadOnlyAndClearDoesNotDelete() {
        EditorCanvas canvas; create(canvas);
        QStringList ids;
        for (const auto &shape : {"Rectangle", "Ellipse", "Polygon"}) {
            canvas.configureTool("elements", settings(shape));
            QVERIFY(canvas.createElement({2 + ids.size() * 18.0, 4, 14, 10}));
            ids.append(canvas.selectedLayerId());
        }
        const auto bytes = encodeIisc(*canvas.document()).bytes;
        const auto revision = canvas.revision();
        const bool undo = canvas.canUndo(), redo = canvas.canRedo();
        QSignalSpy history(&canvas, &EditorCanvas::undoRedoChanged);
        QVERIFY(canvas.selectElement(ids[0])); QCOMPARE(canvas.elementSelectionCount(), 1);
        QVERIFY(canvas.setElementSelected(ids[1], true)); QCOMPARE(canvas.elementSelectionCount(), 2);
        QCOMPARE(canvas.selectedLayerId(), ids[1]);
        int active = 0;
        for (const auto &entry : canvas.elementCandidates()) active += entry.toMap().value("active").toBool();
        QCOMPARE(active, 1);
        QVERIFY(canvas.invertElementSelection()); QCOMPARE(canvas.elementSelectionCount(), 1);
        QCOMPARE(canvas.selectedLayerId(), ids[2]);
        QVERIFY(canvas.selectAllElements()); QCOMPARE(canvas.elementSelectionCount(), 3);
        QVERIFY(canvas.setElementSelected(ids[1], false)); QCOMPARE(canvas.elementSelectionCount(), 2);
        QVERIFY(canvas.setElementSelected(ids[0], true, false)); QCOMPARE(canvas.elementSelectionCount(), 1);
        QVERIFY(canvas.clearElementSelection()); QCOMPARE(canvas.elementSelectionCount(), 0);
        QVERIFY(canvas.selectedLayerId().isEmpty());
        QCOMPARE(canvas.elementCandidates().size(), 3);
        QVERIFY(canvas.selectedElementSettings().isEmpty());
        QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
        QCOMPARE(canvas.revision(), revision);
        QCOMPARE(canvas.canUndo(), undo); QCOMPARE(canvas.canRedo(), redo);
        QCOMPARE(history.count(), 0);
    }
    void selectingKnownElementRestoresItsOwnDraftAndActualSize() {
        EditorCanvas canvas; create(canvas);
        auto rectangleSettings = settings(); rectangleSettings["field-1"] = 7; rectangleSettings["field-16"] = "#12AB34";
        canvas.configureTool("elements", rectangleSettings); QVERIFY(canvas.createElement({2, 4, 24, 16}));
        const auto rectangle = canvas.selectedLayerId();
        canvas.configureTool("elements", settings("Ellipse")); QVERIFY(canvas.createElement({32, 4, 20, 18}));
        const auto ellipse = canvas.selectedLayerId();
        const auto before = encodeIisc(*canvas.document()).bytes;
        QVERIFY(canvas.selectElement(rectangle));
        auto restored = canvas.selectedElementSettings();
        QCOMPARE(restored.value("selector").toString(), "Rectangle");
        QCOMPARE(restored.value("field-1").toInt(), 7);
        QCOMPARE(restored.value("field-16").toString(), "#12AB34");
        QCOMPARE(restored.value("field-0").toList(), QVariantList({24.0, 16.0}));
        QCOMPARE(canvas.toolValues("elements"), restored);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        QVERIFY(canvas.applyToolField("elements", "field-1", 4));
        QCOMPARE(candidate(canvas, rectangle).value("shape").toString(), "Rectangle");
        QVERIFY(canvas.selectLayer(ellipse));
        QCOMPARE(canvas.elementSelectionCount(), 1);
        QCOMPARE(canvas.selectedElementSettings().value("selector").toString(), "Ellipse");
        QVERIFY(canvas.selectElement(rectangle));
        QCOMPARE(canvas.selectedElementSettings().value("field-1").toInt(), 4);
    }
    void foreignVectorReportsRealSizeWithoutInventingGenerationParameters() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(foreignDocument(), "Imported"));
        canvas.configureTool("elements", settings());
        QCOMPARE(canvas.elementCandidates().size(), 1);
        const auto foreign = candidate(canvas, "foreign");
        QCOMPARE(foreign.value("name").toString(), "Imported path");
        QCOMPARE(foreign.value("shape").toString(), "Vector");
        QVERIFY(!foreign.value("editable").toBool());
        const auto bytes = encodeIisc(*canvas.document()).bytes;
        const auto revision = canvas.revision();
        QVERIFY(canvas.selectElement("foreign")); QCOMPARE(canvas.elementSelectionCount(), 1);
        QCOMPARE(canvas.selectedElementSettings().value("selector").toString(), QString("Vector"));
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({10.0, 10.0}));
        QVERIFY(!canvas.selectedElementSettings().contains("field-1"));
        QVERIFY(!canvas.applyToolField("elements", "field-1", 40));
        QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
        QCOMPARE(canvas.revision(), revision);
        QVERIFY(!canvas.selectElement("photo"));
        QCOMPARE(canvas.selectedLayerId(), "foreign");
    }
    void polygonStarToggleChangesGeometryWithoutAnInnerRadiusControl() {
        EditorCanvas canvas; create(canvas);
        auto polygon = settings("Polygon"); polygon["field-10"] = 0;
        canvas.configureTool("elements", polygon); QVERIFY(canvas.createElement({8, 8, 40, 32}));
        const auto id = canvas.selectedLayerId();
        const auto polygonPixels = renderFrame(*canvas.document(), 0).pixels.pixels;
        const auto polygonCommands = std::get<VectorAsset>(*resolveAssetAt(*canvas.document(), *findLayer(*canvas.document(), id.toStdString()), 0)).paths.front().commands.size();
        QVERIFY(canvas.applyToolField("elements", "field-9", true));
        const auto *asset = resolveAssetAt(*canvas.document(), *findLayer(*canvas.document(), id.toStdString()), 0);
        const auto &commands = std::get<VectorAsset>(*asset).paths.front().commands;
        QVERIFY(commands.size() > polygonCommands);
        QVERIFY(renderFrame(*canvas.document(), 0).pixels.pixels != polygonPixels);
        const auto defaultInner = std::get<LineTo>(commands[1]).point;
        QVERIFY(std::abs(defaultInner.x - (28 + std::sin(std::numbers::pi / 5) * 20 * 0.5)) < 0.0001);
        QVERIFY(std::abs(defaultInner.y - (24 - std::cos(std::numbers::pi / 5) * 16 * 0.5)) < 0.0001);
        const auto defaultPixels = renderFrame(*canvas.document(), 0).pixels.pixels;
        QVERIFY(canvas.applyToolField("elements", "field-5", 25));
        const auto explicitInner = std::get<LineTo>(std::get<VectorAsset>(*resolveAssetAt(*canvas.document(), *findLayer(*canvas.document(), id.toStdString()), 0)).paths.front().commands[1]).point;
        QVERIFY(std::abs(explicitInner.x - (28 + std::sin(std::numbers::pi / 5) * 20 * 0.25)) < 0.0001);
        QVERIFY(std::abs(explicitInner.y - (24 - std::cos(std::numbers::pi / 5) * 16 * 0.25)) < 0.0001);
        QVERIFY(renderFrame(*canvas.document(), 0).pixels.pixels != defaultPixels);
        QVERIFY(canvas.applyToolField("elements", "field-9", false));
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, polygonPixels);
    }
    void presentationSynchronizationCannotRollBackAnEditOrCollapseMultipleSelection() {
        EditorCanvas canvas; create(canvas);
        canvas.configureTool("elements", settings()); QVERIFY(canvas.createElement({3, 4, 24, 16}));
        const auto rectangle = canvas.selectedLayerId();
        canvas.configureTool("elements", settings("Ellipse")); QVERIFY(canvas.createElement({32, 4, 24, 18}));
        const auto ellipse = canvas.selectedLayerId();
        QVariantMap presented;
        // Match ToolSheet's read-only draft synchronization during native signals.
        connect(&canvas, &EditorCanvas::elementSelectionChanged, &canvas, [&] {
            const auto selected = canvas.selectedElementSettings();
            if (selected.isEmpty()) return;
            presented = selected;
            canvas.configureTool("elements", selected);
        });
        QVERIFY(canvas.selectElement(rectangle));
        QVERIFY(canvas.setElementSelected(ellipse, true)); QCOMPARE(canvas.elementSelectionCount(), 2);
        const auto before = renderFrame(*canvas.document(), 0).pixels.pixels;
        QVERIFY(canvas.applyToolField("elements", "field-4", 180));
        QCOMPARE(canvas.selectedElementSettings().value("field-4").toInt(), 180);
        QCOMPARE(presented.value("field-4").toInt(), 180);
        QCOMPARE(canvas.elementSelectionCount(), 2);
        QVERIFY(renderFrame(*canvas.document(), 0).pixels.pixels != before);
        QVERIFY(canvas.applyToolField("elements", "field-2", "Gradient"));
        QVERIFY(std::holds_alternative<StaticBitmapLayer>(*findLayer(*canvas.document(), ellipse.toStdString())));
        QCOMPARE(canvas.selectedElementSettings().value("field-2").toString(), "Gradient");
        QCOMPARE(presented.value("selector").toString(), "Ellipse");
        QCOMPARE(canvas.elementSelectionCount(), 2);
        QVERIFY(canvas.selectElement(rectangle));
        QVERIFY(canvas.applyToolField("elements", "field-1", 5));
        QCOMPARE(presented.value("field-1").toInt(), 5);
        QVERIFY(canvas.applyToolField("elements", "field-0", QVariantList{36, 24}));
        QCOMPARE(presented.value("field-0").toList(), QVariantList({36.0, 24.0}));
        const auto bytes = encodeIisc(*canvas.document()).bytes;
        const auto revision = canvas.revision();
        for (const auto &invalid : {QVariantList{0, 24}, QVariantList{36, 65537}, QVariantList{36}}) {
            QVERIFY(!canvas.applyToolField("elements", "field-0", invalid));
            QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
            QCOMPARE(canvas.revision(), revision);
            QCOMPARE(presented.value("field-0").toList(), QVariantList({36.0, 24.0}));
            QCOMPARE(canvas.toolValues("elements").value("field-0").toList(), QVariantList({36.0, 24.0}));
        }
        canvas.configureTool("elements", settings("Polygon"));
        QVERIFY(canvas.createElement({3, 28, 20, 16}));
        QCOMPARE(presented.value("selector").toString(), "Polygon");
        QCOMPARE(canvas.selectedElementSettings().value("selector").toString(), "Polygon");
        QCOMPARE(canvas.elementSelectionCount(), 1);
    }
    void geometryAndBitmapHistoryRestoreTheInspectorAndPixels() {
        EditorCanvas canvas; create(canvas);
        canvas.configureTool("elements", settings()); QVERIFY(canvas.createElement({4, 4, 24, 16}));
        const auto id = canvas.selectedLayerId();
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/element-history-XXXXXX"); QVERIFY(directory.isValid());
        QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(directory.filePath("document.iisc"))));
        QVariantMap presented;
        connect(&canvas, &EditorCanvas::elementSelectionChanged, &canvas, [&] {
            const auto selected = canvas.selectedElementSettings();
            if (!selected.isEmpty()) { presented = selected; canvas.configureTool("elements", selected); }
        });
        QSignalSpy restoredSettings(&canvas, &EditorCanvas::historyRestored);
        const auto vectorBytes = encodeIisc(*canvas.document()).bytes;
        const auto vectorPixels = renderFrame(*canvas.document(), 0).pixels.pixels;
        QVERIFY(canvas.applyToolField("elements", "field-1", 7));
        const auto roundedPixels = renderFrame(*canvas.document(), 0).pixels.pixels;
        QVERIFY(roundedPixels != vectorPixels);
        QVERIFY(canvas.undo());
        QCOMPARE(canvas.selectedElementSettings().value("field-1").toInt(), 2);
        QCOMPARE(presented.value("field-1").toInt(), 2);
        QVERIFY(!restoredSettings.isEmpty());
        QCOMPARE(restoredSettings.last().first().toMap().value("elements").toMap(), canvas.selectedElementSettings());
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, vectorPixels);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, vectorBytes);
        QVERIFY(canvas.redo());
        QCOMPARE(canvas.selectedElementSettings().value("field-1").toInt(), 7);
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, roundedPixels);
        QVERIFY(canvas.applyToolField("elements", "field-0", QVariantList{36, 24}));
        const auto resizedPixels = renderFrame(*canvas.document(), 0).pixels.pixels;
        QVERIFY(canvas.undo());
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({24.0, 16.0}));
        QCOMPARE(presented.value("field-0").toList(), QVariantList({24.0, 16.0}));
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, roundedPixels);
        QVERIFY(canvas.canRedo());
        QVERIFY(!canvas.applyToolField("elements", "field-0", QVariantList{0, 24}));
        QVERIFY(canvas.canRedo());
        QVERIFY(canvas.redo());
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({36.0, 24.0}));
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, resizedPixels);
        QVERIFY(canvas.applyToolField("elements", "field-2", "Gradient"));
        const auto gradientPixels = renderFrame(*canvas.document(), 0).pixels.pixels;
        QVERIFY(std::holds_alternative<StaticBitmapLayer>(*findLayer(*canvas.document(), id.toStdString())));
        QVERIFY(canvas.undo());
        QVERIFY(std::holds_alternative<StaticVectorLayer>(*findLayer(*canvas.document(), id.toStdString())));
        QCOMPARE(canvas.selectedElementSettings().value("field-2").toString(), "Solid");
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, resizedPixels);
        QVERIFY(canvas.redo());
        QCOMPARE(canvas.selectedElementSettings().value("field-2").toString(), "Gradient");
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, gradientPixels);
        QImage fill(2, 2, QImage::Format_ARGB32); fill.fill(Qt::yellow);
        const auto path = directory.filePath("fill.png"); QVERIFY(fill.save(path));
        auto imageSettings = canvas.selectedElementSettings(); imageSettings["field-2"] = "Image";
        canvas.configureTool("elements", imageSettings);
        QVERIFY(canvas.importToolSource(QUrl::fromLocalFile(path), "elements", imageSettings));
        const auto imagePixels = renderFrame(*canvas.document(), 0).pixels.pixels;
        QVERIFY(imagePixels != gradientPixels);
        QVERIFY(canvas.undo());
        QCOMPARE(canvas.selectedElementSettings().value("field-2").toString(), "Gradient");
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, gradientPixels);
        QVERIFY(canvas.redo());
        QCOMPARE(canvas.selectedElementSettings().value("field-2").toString(), "Image");
        QCOMPARE(presented.value("field-2").toString(), "Image");
        QCOMPARE(restoredSettings.last().first().toMap().value("elements").toMap(), canvas.selectedElementSettings());
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, imagePixels);
        QVERIFY(candidate(canvas, id).value("editable").toBool());
    }
    void creationHistoryDropsStaleIdsAndBranchingRestoresKnownMetadata() {
        EditorCanvas canvas; create(canvas);
        canvas.configureTool("elements", settings()); QVERIFY(canvas.createElement({2, 4, 20, 12}));
        const auto rectangle = canvas.selectedLayerId();
        canvas.configureTool("elements", settings("Ellipse", "Gradient")); QVERIFY(canvas.createElement({30, 4, 20, 12}));
        const auto ellipse = canvas.selectedLayerId();
        const auto afterCreation = encodeIisc(*canvas.document()).bytes;
        QVERIFY(canvas.selectAllElements()); QCOMPARE(canvas.elementSelectionCount(), 2);
        QVERIFY(canvas.undo());
        QVERIFY(candidate(canvas, ellipse).isEmpty()); QCOMPARE(canvas.elementCandidates().size(), 1);
        QVERIFY(candidate(canvas, rectangle).value("editable").toBool());
        QCOMPARE(canvas.selectedElementSettings().value("selector").toString(), "Rectangle");
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({20.0, 12.0}));
        QVERIFY(canvas.redo());
        QCOMPARE(encodeIisc(*canvas.document()).bytes, afterCreation);
        QVERIFY(candidate(canvas, ellipse).value("editable").toBool());
        QVERIFY(canvas.selectElement(ellipse));
        QCOMPARE(canvas.selectedElementSettings().value("selector").toString(), "Ellipse");
        QCOMPARE(canvas.selectedElementSettings().value("field-2").toString(), "Gradient");
        QVERIFY(canvas.undo()); QVERIFY(canvas.canRedo());
        canvas.configureTool("elements", settings("Polygon")); QVERIFY(canvas.createElement({30, 24, 20, 12}));
        const auto polygon = canvas.selectedLayerId();
        QVERIFY(!canvas.canRedo()); QVERIFY(candidate(canvas, ellipse).isEmpty());
        QVERIFY(candidate(canvas, polygon).value("editable").toBool());
        QVERIFY(canvas.undo()); QVERIFY(candidate(canvas, polygon).isEmpty());
        QVERIFY(canvas.redo()); QVERIFY(candidate(canvas, polygon).value("editable").toBool());
        QVERIFY(canvas.selectElement(polygon));
        QCOMPARE(canvas.selectedElementSettings().value("selector").toString(), "Polygon");
    }
    void boundedHistoryTrimsGeometrySnapshotsAlongsideDocuments() {
        EditorCanvas canvas; create(canvas);
        QStringList ids;
        for (int i = 0; i < 35; ++i) {
            canvas.configureTool("elements", settings(i % 2 ? "Ellipse" : "Rectangle"));
            QVERIFY(canvas.createElement({double(i % 16), double(i % 10), double(10 + i % 3), 8}));
            ids.append(canvas.selectedLayerId());
        }
        const auto finalBytes = encodeIisc(*canvas.document()).bytes;
        int undos = 0;
        while (canvas.canUndo()) { QVERIFY(canvas.undo()); ++undos; }
        QCOMPARE(undos, 31);
        QCOMPARE(canvas.elementCandidates().size(), 4);
        QVERIFY(candidate(canvas, ids.last()).isEmpty());
        QVERIFY(canvas.selectElement(ids[3]));
        QCOMPARE(canvas.selectedElementSettings().value("selector").toString(), "Ellipse");
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({10.0, 8.0}));
        int redos = 0;
        while (canvas.canRedo()) { QVERIFY(canvas.redo()); ++redos; }
        QCOMPARE(redos, 31); QCOMPARE(encodeIisc(*canvas.document()).bytes, finalBytes);
        QVERIFY(canvas.selectElement(ids.last()));
        QCOMPARE(canvas.selectedElementSettings().value("selector").toString(), "Rectangle");
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({11.0, 8.0}));
    }
    void independentImageElementsRetainTheirOwnSourcesThroughEditingAndHistory() {
        EditorCanvas canvas; create(canvas);
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/element-patterns-XXXXXX"); QVERIFY(directory.isValid());
        QStringList paths;
        for (const auto color : {Qt::red, Qt::blue, Qt::yellow}) {
            QImage image(2, 2, QImage::Format_ARGB32); image.fill(color);
            const auto path = directory.filePath(QString("fill-%1.png").arg(paths.size()));
            QVERIFY(image.save(path)); paths.append(path);
        }
        auto imageSettings = settings("Rectangle", "Image"); imageSettings["field-10"] = 0;
        canvas.configureTool("elements", imageSettings);
        QVERIFY(canvas.importToolSource(QUrl::fromLocalFile(paths[0]), "elements", imageSettings));
        QVERIFY(canvas.createElement({4, 4, 18, 16})); const auto red = canvas.selectedLayerId();
        QVERIFY(canvas.clearElementSelection());
        canvas.configureTool("elements", imageSettings);
        QVERIFY(canvas.importToolSource(QUrl::fromLocalFile(paths[1]), "elements", imageSettings));
        QVERIFY(canvas.createElement({34, 4, 18, 16})); const auto blue = canvas.selectedLayerId();
        const auto colors = [&] {
            const auto result = renderFrame(*canvas.document(), 0);
            QVERIFY(result.ok());
            QCOMPARE(result.pixels.pixels[12 * 64 + 12], 0xffff0000U);
            QCOMPARE(result.pixels.pixels[12 * 64 + 42], 0xff0000ffU);
        };
        colors(); const auto original = renderFrame(*canvas.document(), 0).pixels.pixels;
        QVERIFY(canvas.selectElement(red));
        QVERIFY(canvas.applyToolField("elements", "field-0", QVariantList{22, 18}));
        QVERIFY(canvas.applyToolField("elements", "field-10", 2)); colors();
        const auto redEdited = renderFrame(*canvas.document(), 0).pixels.pixels;
        QVERIFY(canvas.undo()); QVERIFY(canvas.undo()); colors();
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, original);
        QCOMPARE(canvas.selectedElementSettings().value("field-0").toList(), QVariantList({18.0, 16.0}));
        QVERIFY(canvas.redo()); QVERIFY(canvas.redo()); colors();
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, redEdited);
        QVERIFY(canvas.selectElement(blue));
        QVERIFY(canvas.applyToolField("elements", "field-10", 3));
        QVERIFY(canvas.applyToolField("elements", "field-0", QVariantList{20, 18})); colors();
        QVERIFY(canvas.undo()); QVERIFY(canvas.undo()); colors();
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels, redEdited);
        QVERIFY(canvas.redo()); QVERIFY(canvas.redo()); colors();
        QVERIFY(canvas.selectElement(red));
        auto replacement = canvas.selectedElementSettings();
        QVERIFY(canvas.applyToolField("layers", "field-8", "Pixels"));
        const auto lockedBytes = encodeIisc(*canvas.document()).bytes;
        QVERIFY(!canvas.importToolSource(QUrl::fromLocalFile(paths[2]), "elements", replacement));
        QCOMPARE(encodeIisc(*canvas.document()).bytes, lockedBytes);
        QVERIFY(canvas.applyToolField("layers", "field-8", "None"));
        QVERIFY(canvas.applyToolField("elements", "field-1", 3)); colors();
        replacement = canvas.selectedElementSettings();
        QVERIFY(canvas.importToolSource(QUrl::fromLocalFile(paths[2]), "elements", replacement));
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels[12 * 64 + 12], 0xffffff00U);
        QVERIFY(canvas.undo()); colors();
        // An edit after Undo must use the restored red pattern and discard Redo.
        QVERIFY(canvas.applyToolField("elements", "field-1", 4)); colors(); QVERIFY(!canvas.canRedo());
        QVERIFY(canvas.selectElement(blue)); QVERIFY(canvas.applyToolField("elements", "field-1", 3)); colors();
        // A different element's loaded source is not accepted for a new Image request.
        canvas.configureTool("elements", settings()); QVERIFY(canvas.createElement({4, 28, 18, 16}));
        const auto solidBytes = encodeIisc(*canvas.document()).bytes;
        auto requested = canvas.selectedElementSettings(); requested["field-2"] = "Image";
        QVERIFY(!canvas.applyToolField("elements", "field-2", "Image"));
        QCOMPARE(encodeIisc(*canvas.document()).bytes, solidBytes);
        QCOMPARE(canvas.toolValues("elements").value("field-2").toString(), "Solid");
        QVERIFY(canvas.importToolSource(QUrl::fromLocalFile(paths[0]), "elements", requested));
        QCOMPARE(canvas.selectedElementSettings().value("field-2").toString(), "Image");
        QCOMPARE(renderFrame(*canvas.document(), 0).pixels.pixels[36 * 64 + 12], 0xffff0000U);
        create(canvas); canvas.configureTool("elements", imageSettings);
        QVERIFY(!canvas.createElement({4, 4, 18, 16}));
        QVERIFY(canvas.elementCandidates().isEmpty());
    }
    void candidatesAndSelectionsFollowArtboardsLayersAndDocumentReplacement() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(foreignDocument(true), "Boards"));
        canvas.configureTool("elements", settings());
        QCOMPARE(canvas.elementCandidates().size(), 1);
        QVERIFY(candidate(canvas, "other").isEmpty());
        QVERIFY(candidate(canvas, "one:background.layer").isEmpty());
        QVERIFY(canvas.selectElement("foreign"));
        QVERIFY(canvas.selectArtboard(1, false));
        QCOMPARE(canvas.elementCandidates().size(), 1);
        QVERIFY(candidate(canvas, "foreign").isEmpty());
        QVERIFY(!canvas.selectElement("foreign"));
        QCOMPARE(canvas.selectedLayerId(), "other");
        QVERIFY(canvas.selectArtboard(0, false));
        QVERIFY(canvas.selectElement("foreign"));
        QVERIFY(canvas.selectLayer("photo")); QCOMPARE(canvas.elementSelectionCount(), 0);
        QVERIFY(canvas.selectElement("foreign"));
        canvas.configureTool("brush", {}); QCOMPARE(canvas.elementSelectionCount(), 0);
        QVERIFY(canvas.selectedElementSettings().isEmpty());
        canvas.configureTool("elements", settings());
        QVERIFY(canvas.createElement({2, 3, 15, 10}));
        const auto known = canvas.selectedLayerId();
        QVERIFY(canvas.setElementSelected("foreign", true)); QCOMPARE(canvas.elementSelectionCount(), 2);
        QVERIFY(canvas.removeLayer(known));
        QVERIFY(candidate(canvas, known).isEmpty());
        QVERIFY(canvas.elementSelectionCount() <= 1);
        create(canvas); QVERIFY(canvas.elementCandidates().isEmpty());
        QCOMPARE(canvas.elementSelectionCount(), 0);
        QVERIFY(canvas.selectedElementSettings().isEmpty());
    }
    void geometryLockAndLiveStrokePreventWritesAndSelectionInterruption() {
        EditorCanvas canvas; create(canvas);
        canvas.configureTool("elements", settings()); QVERIFY(canvas.createElement({4, 4, 20, 12}));
        const auto id = canvas.selectedLayerId();
        for (const auto &lock : {"Pixels", "Position"}) {
            QVERIFY(canvas.applyToolField("layers", "field-8", lock));
            QVERIFY(canvas.selectElement(id));
            QVERIFY(!candidate(canvas, id).value("editable").toBool());
            const auto bytes = encodeIisc(*canvas.document()).bytes;
            const auto revision = canvas.revision();
            QVERIFY(!canvas.applyToolField("elements", "field-1", 12));
            QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
            QCOMPARE(canvas.revision(), revision);
            QCOMPARE(canvas.toolValues("elements").value("field-1").toInt(), 2);
            QVERIFY(canvas.clearElementSelection());
            QVERIFY(canvas.selectElement(id));
            QVERIFY(canvas.applyToolField("layers", "field-8", "None"));
        }
        QVERIFY(candidate(canvas, id).value("editable").toBool());
        QVERIFY(canvas.addPaintLayer());
        canvas.configureTool("brush", {{"field-4", 4}, {"field-5", 100}, {"field-8", 100}, {"field-9", 100}});
        QVERIFY(canvas.beginStrokeAt({8, 8}));
        QVERIFY(canvas.liveStrokeActive());
        const auto revision = canvas.revision();
        QVERIFY(!canvas.selectElement(id));
        QVERIFY(!canvas.selectAllElements());
        QVERIFY(!canvas.invertElementSelection());
        QVERIFY(!canvas.clearElementSelection());
        QCOMPARE(canvas.revision(), revision);
        QVERIFY(canvas.liveStrokeActive());
        QVERIFY(canvas.endStrokeAt({10, 10}));
    }
};

QTEST_MAIN(ElementsSelectionTests)
#include "tst_elementsselection.moc"
