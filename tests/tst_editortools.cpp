#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorMedia.h"
#include "App/Views/Editor/EditorClippingOverlay.h"
#include "App/Views/Editor/EditorLayerHistogram.h"
#include <iiSharedCanvas.h>
#include <iiFileProviderNative.h>
#include <SocietyDrive.h>
#include <QImage>
#include <QTemporaryDir>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>
#include <algorithm>
#include <cmath>
using namespace iiSharedCanvas;

class EditorToolsTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/tool-documents-XXXXXX.societycontainer"};
private:
    void create(EditorCanvas &c, int width = 64, int height = 48) {
        QVERIFY(c.createCanvas({{"width", width}, {"height", height}, {"unit", "px"}, {"background", "Transparent"}}));
    }
    QImage graphicPreviewImage(const QVariantMap &preview) {
        const auto source = preview.value("imageSource").toUrl().toString();
        return QImage::fromData(QByteArray::fromBase64(source.mid(source.indexOf(',') + 1).toLatin1()));
    }
    QImage clippingPixels(EditorCanvas &canvas) {
        const auto settings = canvas.clippingState();
        if (!settings.value("enabled").toBool()) return {};
        const auto result = EditorClippingOverlay::render({canvas.editingFrameSnapshot(),
            settings.value("white").toInt(), settings.value("black").toInt(),
            settings.value("highlights").toBool(), settings.value("shadows").toBool()});
        if (!result.error.isEmpty()) QTest::qFail(qPrintable(result.error), __FILE__, __LINE__);
        return result.image;
    }
    int exposedChannel(int input, double stops) {
        const double encoded = input / 255.0;
        const double linear = encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
        const double exposed = std::clamp(linear * std::exp2(stops), 0.0, 1.0);
        return qRound(255 * (exposed <= 0.0031308 ? exposed * 12.92 : 1.055 * std::pow(exposed, 1 / 2.4) - 0.055));
    }
private slots:
    void colorSampleDispatchDoesNotRenderTheWholeFrame() {
        Document document;document.extent={2048,2048};
        document.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(2048,2048,0xff336699U)});
        for(int i=0;i<32;++i) {
            LayerProperties properties{"sample."+std::to_string(i),"Color sample fixture"};properties.opacity=.5;
            document.layers.emplace_back(StaticBitmapLayer{properties,StaticSource{"pixels"}});
        }
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(std::move(document),"Color sample dispatch"));
        const auto bytes=encodeIisc(*canvas.document()).bytes;const auto revision=canvas.revision();
        const auto sequence=canvas.layerColorSample()["sequence"].toInt();
        QSignalSpy files(&canvas,&EditorCanvas::fileStateChanged),info(&canvas,&EditorCanvas::documentInfoChanged);
        QVERIFY(canvas.beginLayerColorSample());bool heartbeat=false;QTimer::singleShot(0,&canvas,[&]{heartbeat=true;});
        QElapsedTimer elapsed;elapsed.start();const bool accepted=canvas.sampleLayerColor({1024,1024});
        const auto microseconds=elapsed.nsecsElapsed()/1000;
        qInfo()<<"color_sample_dispatch_us"<<microseconds<<"heartbeat_before_return"<<heartbeat
               <<"sequence_before"<<sequence<<"sequence_after"<<canvas.layerColorSample()["sequence"];
        QVERIFY(accepted);QVERIFY2(microseconds<250000,"A color sample must schedule the native read without rendering a complete image on the GUI thread");
        QCOMPARE(canvas.layerColorSample()["sequence"].toInt(),sequence);QVERIFY(canvas.layerColorSample()["pending"].toBool());
        QTRY_VERIFY(heartbeat);QTRY_COMPARE(canvas.layerColorSample()["sequence"].toInt(),sequence+1);
        QVERIFY(!canvas.layerColorSample()["pending"].toBool());QCOMPARE(canvas.layerColorSample()["hex"].toString(),QString("#336699"));
        QCOMPARE(canvas.revision(),revision);QCOMPARE(files.count(),0);QCOMPARE(info.count(),0);QCOMPARE(encodeIisc(*canvas.document()).bytes,bytes);
    }
    void unchangedLayerGroupVisibilityDoesNotReviseOrPersist() {
        Document document; document.extent = {8, 8};
        document.artboards = {{"group", "Group", {{0, 0}, {8, 8}}, 0}};
        document.assets.emplace_back(RasterAsset{"pixels", makeRasterLayer(8, 8, 0xff336699U)});
        LayerProperties properties{"group.layer", "Layer"}; properties.artboardId = "group";
        document.layers.emplace_back(StaticBitmapLayer{properties, StaticSource{"pixels"}});
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(std::move(document), "No-op group"));
        const auto path = m_storage.filePath("Files/noop-group.iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        const auto nativePath = std::filesystem::path(path.toStdU16String());
        const auto bytes = iiFileProvider::native::File::read(nativePath);
        const auto revision = canvas.revision(); const auto version = canvas.fileAlias()["version"];
        const auto documentBytes = encodeIisc(*canvas.document()).bytes;
        QSignalSpy files(&canvas, &EditorCanvas::fileStateChanged), info(&canvas, &EditorCanvas::documentInfoChanged);
        QVERIFY(canvas.setLayerGroupVisible("group", true));
        QCOMPARE(canvas.revision(), revision);
        for (int i = 0; i < 200; ++i) QVERIFY(canvas.setLayerGroupVisible("group", true));
        QCOMPARE(files.count(), 0); QCOMPARE(info.count(), 0);
        QCOMPARE(canvas.fileAlias()["version"], version); QCOMPARE(iiFileProvider::native::File::read(nativePath), bytes);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, documentBytes);
        canvas.configureTool("brush", {{"field-4", 2}, {"field-5", 100}, {"field-8", 100}, {"field-9", 100}});
        QVERIFY(canvas.beginStrokeAt({1, 1}));
        const auto liveRevision = canvas.revision();
        QVERIFY(canvas.setLayerGroupVisible("group", true)); QCOMPARE(canvas.revision(), liveRevision);
        QVERIFY(canvas.liveStrokeActive()); QVERIFY(!canvas.setLayerGroupVisible("group", false)); QVERIFY(canvas.liveStrokeActive());
        QCOMPARE(iiFileProvider::native::File::read(nativePath), bytes);
        canvas.configureTool("select", {}); QVERIFY(!canvas.liveStrokeActive());
        QVERIFY(canvas.setLayerGroupVisible("group", false));
        QCOMPARE(canvas.document()->artboards.front().visible, false); QCOMPARE(files.count(), 1);
        DocumentFile observer; QVERIFY(observer.open(nativePath).ok()); QCOMPARE(observer.document()->artboards.front().visible, false);
    }
    void histogramPresentationDoesNotExposeBlockingWork() {
        EditorCanvas canvas;
        const auto method = canvas.metaObject()->indexOfMethod("layerHistogram(bool,QString)");
        if (method >= 0) {
            Document document; document.extent = {2048, 2048};
            document.assets.emplace_back(RasterAsset{"pixels", makeRasterLayer(2048, 2048, 0xff336699U)});
            for (int i = 0; i < 32; ++i) {
                LayerProperties properties{"histogram." + std::to_string(i), "Histogram fixture"};
                properties.opacity = .5;
                document.layers.emplace_back(StaticBitmapLayer{properties, StaticSource{"pixels"}});
            }
            QVERIFY(canvas.loadProjectPage(std::move(document), "Histogram dispatch"));
            bool heartbeat = false;
            QTimer::singleShot(0, &canvas, [&] { heartbeat = true; });
            QElapsedTimer elapsed; elapsed.start();
            QVariantMap result;
            QVERIFY(QMetaObject::invokeMethod(&canvas, "layerHistogram", Q_RETURN_ARG(QVariantMap, result),
                Q_ARG(bool, true), Q_ARG(QString, QStringLiteral("RGB"))));
            qInfo() << "synchronous histogram microseconds" << elapsed.nsecsElapsed() / 1000
                    << "heartbeat before return" << heartbeat;
            QCOMPARE(result["pixelCount"].toLongLong(), 2048LL * 2048);
        }
        QCOMPARE(method, -1); // QML must use a view-owned asynchronous analysis.
    }
    void selectionStateReadsDoNotPerformDocumentWork() {
        EditorCanvas canvas;
        Document document; document.extent = {2048, 2048};
        QVERIFY(canvas.loadProjectPage(std::move(document), "Selection state"));
        const QVariantMap rectangle{{"selector", "Rectangle"}, {"field-0", "New"}, {"field-2", 0}, {"field-5", false}};
        canvas.configureTool("select", rectangle);
        QVERIFY(canvas.selectArea({QPointF(0, 0), QPointF(2048, 2048)}));
        constexpr qlonglong selected = 2048LL * 2048;
        QCOMPARE(canvas.toolState().value("selectedPixels").toLongLong(), selected);
        const auto before = encodeIisc(*canvas.document()).bytes;
        const auto revision = canvas.revision();
        const auto overlay = canvas.selectionOverlay();
        QSignalSpy state(&canvas, &EditorCanvas::toolStateChanged);
        QSignalSpy file(&canvas, &EditorCanvas::fileStateChanged);
        QElapsedTimer elapsed; elapsed.start();
        for (int i = 0; i < 200; ++i) {
            const auto snapshot = canvas.toolState();
            QCOMPARE(snapshot.value("selectedPixels").toLongLong(), selected);
            QVERIFY(snapshot.value("selectionActive").toBool());
        }
        qInfo() << "selection_state: pixels=" << selected << "reads=200 us=" << elapsed.nsecsElapsed() / 1000;
        QCOMPARE(state.count(), 0); QCOMPARE(file.count(), 0);
        QCOMPARE(canvas.revision(), revision);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
        QCOMPARE(canvas.selectionOverlay(), overlay);
        canvas.clearAreaSelection();
        QCOMPARE(canvas.toolState().value("selectedPixels").toLongLong(), 0);
        QVERIFY(!canvas.toolState().value("selectionActive").toBool());
        QVERIFY(canvas.selectArea({QPointF(2, 3), QPointF(14, 9)}));
        QCOMPARE(canvas.toolState().value("selectedPixels").toLongLong(), 72);
        auto subtract = rectangle; subtract["field-0"] = "Subtract";
        canvas.configureTool("select", subtract);
        QVERIFY(canvas.selectArea({QPointF(2, 3), QPointF(8, 9)}));
        QCOMPARE(canvas.toolState().value("selectedPixels").toLongLong(), 36);
        Document replacement; replacement.extent = {32, 32};
        QVERIFY(canvas.loadProjectPage(std::move(replacement), "Replacement"));
        QCOMPARE(canvas.toolState().value("selectedPixels").toLongLong(), 0);
    }
    void compactRepairPreviewDoesNotCommitUntilApply() {
        EditorCanvas canvas; create(canvas, 32, 32);
        QImage source(32, 32, QImage::Format_ARGB32); source.fill(Qt::blue);
        for(int y=12;y<20;++y) for(int x=12;x<20;++x) source.setPixel(x,y,0xffff0000);
        QVERIFY(canvas.insertPixels(source, "Repair source"));
        canvas.configureTool("select", {{"selector", "Rectangle"}, {"field-2", 0}, {"field-0", "New"}});
        QVERIFY(canvas.selectArea({QPointF(10, 10), QPointF(22, 22)}));
        const QVariantMap values{{"selector", "Content-aware"}};
        canvas.configureTool("retouch", values);
        const auto bytes = encodeIisc(*canvas.document()).bytes;
        const auto revision = canvas.revision(); const auto overlay = canvas.selectionOverlay();
        const auto preview = canvas.toolPreview("retouch", "field-18", values);
        const auto rendered = graphicPreviewImage(preview); QVERIFY2(!rendered.isNull(), qPrintable(preview["description"].toString()));
        QCOMPARE(preview["items"].toList().first().toMap()["action"].toString(), QString("applyRepair"));
        QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes); QCOMPARE(canvas.revision(), revision);
        QCOMPARE(canvas.selectionOverlay(), overlay);
        QVERIFY(canvas.executeToolAction("retouch", "field-18", values));
        const auto applied = renderFrame(*canvas.document(), canvas.frame()); QVERIFY(applied.ok());
        const auto previewCenter = rendered.pixel(rendered.width()/2, rendered.height()/2);
        QCOMPARE(applied.pixels.pixels[16*32+16], previewCenter);
        QVERIFY(canvas.undo()); QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes);
    }
    void unavailableFaceViewDoesNotDrawRepairMask() {
        EditorCanvas canvas; create(canvas, 32, 32); canvas.setSize({320, 320});
        QImage source(32, 32, QImage::Format_ARGB32); source.fill(Qt::blue);
        QVERIFY(canvas.insertPixels(source, "Face source")); canvas.fitToView();
        canvas.configureTool("retouch", {{"selector", "Face Refine"}, {"field-0", 12}});
        const auto before = encodeIisc(*canvas.document()).bytes;
        const auto revision = canvas.revision();
        const auto selection = canvas.selectionOverlay();
        for (const int field : {19, 20, 21, 22}) {
            const auto state = canvas.toolControlState("retouch", QString("field-%1").arg(field), {});
            QVERIFY(!state["supported"].toBool()); QVERIFY(!state["enabled"].toBool());
        }
        const QPointF point(canvas.panX() + 16 * canvas.zoom(), canvas.panY() + 16 * canvas.zoom());
        QMouseEvent press(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&canvas, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, point, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&canvas, &release);
        QCOMPARE(canvas.selectionOverlay(), selection);
        QCOMPARE(canvas.revision(), revision);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
    }
    void graphicPreviewApplyAndUndoUseOriginalPixels() {
        EditorCanvas c; create(c, 8, 8);
        QImage source(8, 8, QImage::Format_ARGB32); source.fill(qRgba(80, 100, 120, 127));
        QVERIFY(c.insertPixels(source, "Graphic source"));
        const auto original = c.selectedRasterPixels()->pixels;
        const auto before = encodeIisc(*c.document()).bytes;
        const auto revision = c.revision();
        const auto preview = graphicPreviewImage(c.previewGraphicOperation("color", "developLight", {{"exposure", 1.0}}));
        QVERIFY(!preview.isNull());
        QCOMPARE(c.revision(), revision);
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
        QVERIFY(std::abs(qRed(preview.pixel(0, 0)) - exposedChannel(80, 1)) <= 1);
        QCOMPARE(qAlpha(preview.pixel(0, 0)), 127);
        QVERIFY(c.applyGraphicOperation("color", "developLight", {{"exposure", 1.0}}));
        const auto oneStop = c.selectedRasterPixels()->pixels;
        QVERIFY(oneStop != original);
        QCOMPARE(oneStop.front(), preview.pixel(0, 0));
        QVERIFY(c.applyGraphicOperation("color", "developLight", {{"exposure", 2.0}}));
        QVERIFY(std::abs(qRed(c.selectedRasterPixels()->pixels.front()) - exposedChannel(80, 2)) <= 1);
        QVERIFY(c.applyGraphicOperation("color", "developLight", {{"exposure", 1.0}}));
        QCOMPARE(c.selectedRasterPixels()->pixels, oneStop);
        QVERIFY(c.undo());
        QVERIFY(std::abs(qRed(c.selectedRasterPixels()->pixels.front()) - exposedChannel(80, 2)) <= 1);
        QVERIFY(c.undo()); QCOMPARE(c.selectedRasterPixels()->pixels, oneStop);
        QVERIFY(c.undo()); QCOMPARE(c.selectedRasterPixels()->pixels, original);
    }
    void graphicFailuresAndPixelLockLeaveDocumentUnchanged() {
        EditorCanvas c; create(c, 8, 8);
        QImage source(8, 8, QImage::Format_ARGB32); source.fill(qRgba(80, 100, 120, 127));
        QVERIFY(c.insertPixels(source, "Graphic source"));
        const auto before = encodeIisc(*c.document()).bytes;
        QVERIFY(!c.graphicOperationState("unavailableGraphicFilter").value("supported").toBool());
        QVERIFY(!c.applyGraphicOperation("color", "developLight", {{"unknown", true}}));
        QVERIFY(c.previewGraphicOperation("color", "developLight", {{"exposure", "invalid"}}).isEmpty());
        QVERIFY(!c.applyGraphicOperation("masking", "applyLocalAdjustments", {{"exposure", 1.0}}));
        QVERIFY(c.previewGraphicOperation("masking", "invertMask", {}).isEmpty());
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
        QVERIFY(c.applyToolField("layers", "field-8", "Pixels"));
        const auto locked = encodeIisc(*c.document()).bytes;
        const auto capability = c.graphicOperationState("developLight");
        QVERIFY(capability.value("supported").toBool());
        QVERIFY(!capability.value("enabled").toBool());
        QVERIFY(!capability.value("reason").toString().isEmpty());
        QVERIFY(!c.applyGraphicOperation("color", "developLight", {{"exposure", 1.0}}));
        QVERIFY(c.previewGraphicOperation("color", "developLight", {{"exposure", 1.0}}).isEmpty());
        QCOMPARE(encodeIisc(*c.document()).bytes, locked);
        QVERIFY(c.applyToolField("layers", "field-8", "None"));
        QVERIFY(c.setLayerVisible(c.selectedLayerId(), false));
        QVERIFY(c.selectedRasterPixels());
        const auto hidden = encodeIisc(*c.document()).bytes;
        const auto hiddenState = c.graphicOperationState("developLight");
        QVERIFY(hiddenState.value("supported").toBool());
        QVERIFY(!hiddenState.value("enabled").toBool());
        QVERIFY(!c.applyGraphicOperation("color", "developLight", {{"exposure", 1.0}}));
        QVERIFY(c.previewGraphicOperation("color", "developLight", {{"exposure", 1.0}}).isEmpty());
        QCOMPARE(encodeIisc(*c.document()).bytes, hidden);
        QVERIFY(c.setLayerVisible(c.selectedLayerId(), true));
        QVERIFY(c.graphicOperationState("developLight").value("enabled").toBool());
    }
    void graphicSoftMaskIsAppliedOnceAndDiagnosticsDoNotCommit() {
        EditorCanvas c; create(c, 8, 8);
        QImage source(8, 8, QImage::Format_ARGB32); source.fill(qRgba(80, 100, 120, 127));
        QVERIFY(c.insertPixels(source, "Local source"));
        const auto original = c.selectedRasterPixels()->pixels;
        const auto before = encodeIisc(*c.document()).bytes;
        const QVariantMap gradient{{"start", QVariantMap{{"x", 0.0}, {"y", 0.0}}}, {"end", QVariantMap{{"x", 1.0}, {"y", 0.0}}}};
        QVERIFY(!graphicPreviewImage(c.previewGraphicOperation("masking", "linearMask", gradient)).isNull());
        QVERIFY(!c.toolState().value("selectionActive").toBool());
        QVERIFY(c.applyGraphicOperation("masking", "linearMask", gradient));
        QVERIFY(c.toolState().value("selectionActive").toBool());
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
        const auto maskOverlay = c.selectionOverlay();
        QVERIFY(!graphicPreviewImage(c.previewGraphicOperation("masking", "maskColorOverlay", {})).isNull());
        QCOMPARE(c.selectionOverlay(), maskOverlay);
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
        QVERIFY(!c.applyGraphicOperation("masking", "maskColorOverlay", {}));
        const auto preview = graphicPreviewImage(c.previewGraphicOperation("masking", "applyLocalAdjustments", {{"exposure", 1.0}}));
        QVERIFY(!preview.isNull());
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
        QVERIFY(c.applyGraphicOperation("masking", "applyLocalAdjustments", {{"exposure", 1.0}}));
        for (int x = 0; x < 8; ++x) {
            // SDK linear masks use smoothstep at the pixel center. Coverage is
            // quantized once by the editor selection; local exposure scales EV.
            const double t = (x + 0.5) / 8;
            const double coverage = qRound((1 - t * t * (3 - 2 * t)) * 255) / 255.0;
            const auto pixel = c.selectedRasterPixels()->pixels[4 * 8 + x];
            QVERIFY(std::abs(qRed(pixel) - exposedChannel(80, coverage)) <= 1);
            QCOMPARE(qAlpha(pixel), 127);
            QCOMPARE(pixel, preview.pixel(x, 4));
        }
        QVERIFY(c.undo()); QCOMPARE(c.selectedRasterPixels()->pixels, original);
        QVERIFY(c.applyGraphicOperation("masking", "invertMask", {}));
        QVERIFY(c.selectionOverlay() != maskOverlay);
        QVERIFY(c.applyGraphicOperation("masking", "invertMask", {}));
        QCOMPARE(c.selectionOverlay(), maskOverlay);
        QVERIFY(c.applyGraphicOperation("masking", "refineMask", {{"amount", 0.0}}));
        QCOMPARE(c.selectionOverlay(), maskOverlay);
        QVariantList coverage; for (int i = 0; i < 64; ++i) coverage.append(1.0);
        const QVariantMap operand{{"width", 8}, {"height", 8}, {"coverage", coverage}};
        QVERIFY(c.applyGraphicOperation("masking", "combineMasks", {{"operand", operand}, {"operation", "Intersect"}}));
        QCOMPARE(c.selectionOverlay(), maskOverlay);
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
    }
    void graphicCombineRetainsThePreviousMaskForItsLayer() {
        EditorCanvas c; create(c, 8, 8);
        QImage source(8, 8, QImage::Format_ARGB32); source.fill(qRgba(80, 100, 120, 127));
        QVERIFY(c.insertPixels(source, "Mask source"));
        const auto sourceLayer = c.selectedLayerId();
        const auto original = c.selectedRasterPixels()->pixels;
        const auto before = encodeIisc(*c.document()).bytes;
        const QVariantMap left{{"start", QVariantMap{{"x", 0.0}, {"y", 0.0}}}, {"end", QVariantMap{{"x", 1.0}, {"y", 0.0}}}};
        const QVariantMap right{{"start", QVariantMap{{"x", 1.0}, {"y", 0.0}}}, {"end", QVariantMap{{"x", 0.0}, {"y", 0.0}}}};
        QVERIFY(c.applyGraphicOperation("masking", "linearMask", left));
        QVERIFY(!c.applyGraphicOperation("masking", "combineMasks", {{"operation", "Add"}}));
        QVERIFY(c.error().contains("second mask"));
        QVERIFY(c.applyGraphicOperation("masking", "linearMask", right));
        const auto rightOverlay = c.selectionOverlay();
        const auto preview = graphicPreviewImage(c.previewGraphicOperation("masking", "combineMasks", {{"operation", "Add"}}));
        QVERIFY(!preview.isNull());
        const auto combinedCoverage = [](int x) {
            const double t = (x + 0.5) / 8;
            const double left = 1 - t * t * (3 - 2 * t);
            // SDK Add is the union max, with each editor operand quantized once.
            return std::max(qRound(left * 255), qRound((1 - left) * 255)) / 255.0;
        };
        QCOMPARE(qRed(preview.pixel(4, 4)), qRound(combinedCoverage(4) * 255));
        QCOMPARE(c.selectionOverlay(), rightOverlay);
        QVERIFY(c.applyGraphicOperation("masking", "combineMasks", {{"operation", "Add"}}));
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
        QVERIFY(c.applyGraphicOperation("masking", "applyLocalAdjustments", {{"exposure", 1.0}}));
        for (std::size_t index = 0; index < c.selectedRasterPixels()->pixels.size(); ++index) {
            const auto pixel = c.selectedRasterPixels()->pixels[index];
            QVERIFY(std::abs(qRed(pixel) - exposedChannel(80, combinedCoverage(int(index % 8)))) <= 1);
            QCOMPARE(qAlpha(pixel), 127);
        }
        QVERIFY(c.undo()); QCOMPARE(c.selectedRasterPixels()->pixels, original);
        QVERIFY(c.insertPixels(source, "Different layer"));
        QVERIFY(c.selectedLayerId() != sourceLayer);
        const auto otherLayer = encodeIisc(*c.document()).bytes;
        QVERIFY(!c.applyGraphicOperation("masking", "combineMasks", {{"operation", "Add"}}));
        QVERIFY(c.error().contains("second mask"));
        QCOMPARE(encodeIisc(*c.document()).bytes, otherLayer);
        QVERIFY(c.selectLayer(sourceLayer));
        QVERIFY(c.applyGraphicOperation("masking", "combineMasks", {{"operation", "Intersect"}}));
        create(c, 8, 8);
        QVERIFY(c.insertPixels(source, "New document source"));
        QVERIFY(c.applyGraphicOperation("masking", "linearMask", left));
        QVERIFY(!c.applyGraphicOperation("masking", "combineMasks", {{"operation", "Add"}}));
        QVERIFY(c.error().contains("second mask"));
    }
    void graphicMasksUseTheActiveArtboardOrigin() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/graphic-artboards-XXXXXX"); QVERIFY(directory.isValid());
        Document d; d.extent = {8, 8};
        d.artboards = {{"first", "First", {{0, 0}, {8, 8}}, 0}, {"second", "Second", {{72, 24}, {8, 8}}, 0}};
        RasterLayer pixels{8, 8, std::vector<std::uint32_t>(64, qRgba(80, 100, 120, 127))};
        for (const auto &board : d.artboards) {
            const auto asset = board.id + ".pixels";
            d.assets.emplace_back(RasterAsset{asset, pixels});
            LayerProperties properties{board.id + ".layer", board.name};
            properties.artboardId = board.id;
            // Membership makes the base transform local to the artboard.
            properties.transform.translationX = 0;
            properties.transform.translationY = 0;
            d.layers.emplace_back(StaticBitmapLayer{properties, StaticSource{asset}});
        }
        const auto path = directory.filePath("artboards.iisc"); DocumentFile file;
        QVERIFY(file.create(path.toStdString(), d).ok()); file.close();
        EditorCanvas c; QVERIFY(c.openDocumentSource(QUrl::fromLocalFile(path)));
        QVERIFY(c.selectArtboard(1, false)); QVERIFY(c.selectLayer("second.layer"));
        QCOMPARE(c.editingOrigin(), QPointF(72, 24));
        const auto visible = renderArtboard(*c.document(), c.frame(), "second"); QVERIFY(visible.ok());
        const auto center = visible.pixels.pixels[4 * 8 + 4]; QCOMPARE(qAlpha(center), 127);
        QVERIFY(std::abs(qRed(center) - 80) <= 1); QVERIFY(std::abs(qGreen(center) - 100) <= 1); QVERIFY(std::abs(qBlue(center) - 120) <= 1);
        const QVariantMap gradient{{"start", QVariantMap{{"x", 0.0}, {"y", 0.0}}}, {"end", QVariantMap{{"x", 1.0}, {"y", 0.0}}}};
        QVERIFY(c.applyGraphicOperation("masking", "linearMask", gradient));
        QVERIFY(c.toolState().value("selectionActive").toBool());
        QVERIFY(c.applyGraphicOperation("masking", "applyLocalAdjustments", {{"exposure", 1.0}}));
        const double t = 4.5 / 8;
        const double coverage = qRound((1 - t * t * (3 - 2 * t)) * 255) / 255.0;
        QVERIFY(std::abs(qRed(c.selectedRasterPixels()->pixels[4 * 8 + 4]) - exposedChannel(80, coverage)) <= 1);
        QVERIFY(c.undo()); QCOMPARE(c.selectedRasterPixels()->pixels, pixels.pixels);
        c.clearAreaSelection();
        c.configureTool("select", {{"selector", "Rectangle"}, {"field-2", 0}, {"field-5", false}});
        QVERIFY(c.selectArea({QPointF(0, 0), QPointF(4, 8)}));
        QVERIFY(c.applyGraphicOperation("color", "developLight", {{"exposure", 1.0}}));
        QVERIFY(qRed(c.selectedRasterPixels()->pixels[4 * 8 + 1]) > 80);
        QCOMPARE(c.selectedRasterPixels()->pixels[4 * 8 + 6], pixels.pixels[4 * 8 + 6]);
        QVERIFY(c.selectArtboard(0, false)); QVERIFY(c.selectLayer("first.layer"));
        QCOMPARE(c.selectedRasterPixels()->pixels, pixels.pixels);
        QVERIFY(c.setLayerGroupVisible("first", false));
        QVERIFY(!c.graphicOperationState("developLight").value("enabled").toBool());
        const auto hiddenBoard = encodeIisc(*c.document()).bytes;
        QVERIFY(!c.applyGraphicOperation("color", "developLight", {{"exposure", 1.0}}));
        QCOMPARE(encodeIisc(*c.document()).bytes, hiddenBoard);
        QVERIFY(c.setLayerGroupVisible("first", true));
        QVERIFY(c.graphicOperationState("developLight").value("enabled").toBool());
    }
    void layerPanelReadsActualPixelsWithoutEditing() {
        EditorCanvas c; create(c, 2, 2);
        QImage pixels(2, 2, QImage::Format_ARGB32); pixels.fill(Qt::transparent);
        pixels.setPixel(0, 0, qRgb(255, 0, 0)); pixels.setPixel(1, 0, qRgb(0, 255, 0));
        pixels.setPixel(0, 1, qRgb(0, 0, 255));
        QVERIFY(c.insertPixels(pixels, "RGB sample"));
        const auto before = encodeIisc(*c.document()).bytes;
        EditorLayerHistogram analysis; analysis.setCanvas(&c); analysis.setComposite(false); analysis.setChannel("Red"); analysis.setActive(true);
        QTRY_VERIFY(!analysis.histogram()["pending"].toBool());
        const auto histogram = analysis.histogram();
        QCOMPARE(histogram["pixelCount"].toLongLong(), 3);
        const auto red = histogram["red"].toList(); QCOMPARE(red.size(), 256);
        QCOMPARE(red[0].toInt(), 2); QCOMPARE(red[255].toInt(), 1);
        QCOMPARE(histogram["mean"].toDouble(), 85.0);
        const auto details = c.layerPanelInfo();
        QCOMPARE(details["dimensions"].toString(), QString("2 × 2 px"));
        QCOMPARE(details["profile"].toString(), QString("Untagged RGB"));
        QCOMPARE(details["bitDepth"].toString(), QString("8 bit / channel"));
        const auto sequence = c.layerColorSample()["sequence"].toInt();
        QVERIFY(c.sampleLayerColor({0, 0}));
        QTRY_COMPARE(c.layerColorSample()["sequence"].toInt(), sequence + 1);
        QCOMPARE(c.layerColorSample()["hex"].toString(), QString("#FF0000"));
        QVERIFY(c.beginLayerColorSample());
        QVERIFY(c.layerColorSample()["picking"].toBool());
        QCOMPARE(c.layerColorSample()["sequence"].toInt(), sequence + 1);
        c.cancelLayerColorSample();
        QVERIFY(!c.layerColorSample()["picking"].toBool());
        QVERIFY(c.layerColorSample()["valid"].toBool());
        QCOMPARE(c.layerColorSample()["sequence"].toInt(), sequence + 1);
        QCOMPARE(c.layerColorSample()["hex"].toString(), QString("#FF0000"));
        QVERIFY(c.beginLayerColorSample());
        QVERIFY(c.sampleLayerColor({1, 0}));
        QVERIFY(!c.layerColorSample()["picking"].toBool());
        QTRY_COMPARE(c.layerColorSample()["sequence"].toInt(), sequence + 2);
        QCOMPARE(c.layerColorSample()["hex"].toString(), QString("#00FF00"));
        c.setLayerHistogramClipping(true, false);
        QVERIFY(!clippingPixels(c).isNull());
        c.setLayerHistogramClipping(false, false);
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
        QVERIFY(!c.sampleLayerColor({-1, 0}));
        QCOMPARE(c.layerColorSample()["sequence"].toInt(), sequence + 2);
    }
    void layerPanelDuplicateGroupMergeAndUndoPersist() {
        EditorCanvas c; create(c, 8, 8);
        QImage red(8, 8, QImage::Format_ARGB32); red.fill(Qt::red);
        QVERIFY(c.insertPixels(red, "Subject")); const auto original = c.selectedLayerId();
        QCOMPARE(c.document()->timeline.frameCount,24U);
        QCOMPARE(layerProperties(*findLayer(*c.document(),original.toStdString())).frameRange->lastFrame,23U);
        const auto layerCount = c.document()->layers.size();
        QVERIFY(c.duplicateSelectedLayer()); const auto copy = c.selectedLayerId();
        QVERIFY(copy != original); QCOMPARE(c.document()->layers.size(), layerCount + 1);
        auto blue = *c.selectedRasterPixels(); std::fill(blue.pixels.begin(), blue.pixels.end(), 0xff0000ff);
        QVERIFY(c.replaceSelectedPixels(blue)); QVERIFY(c.selectLayer(original));
        QCOMPARE(c.selectedRasterPixels()->pixels.front(), 0xffff0000u);
        QVERIFY(c.selectLayer(copy)); QVERIFY(c.layerPanelInfo()["canMerge"].toBool()); QVERIFY(c.mergeSelectedDown());
        QCOMPARE(c.document()->layers.size(), layerCount);
        QCOMPARE(c.document()->timeline.frameCount,24U);
        QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels.front(), 0xff0000ffu);
        QVERIFY(c.undo()); QCOMPARE(c.document()->layers.size(), layerCount + 1);
        QVERIFY(c.groupSelectedLayer()); QCOMPARE(c.document()->artboards.size(), 1u);
        QVERIFY(c.layerHierarchy().first().toMap()["group"].toBool());
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/layer-panel-XXXXXX"); QVERIFY(directory.isValid());
        const auto path = QUrl::fromLocalFile(directory.filePath("grouped.iisc"));
        QVERIFY(c.saveDocumentAs(path)); EditorCanvas reopened;
        QVERIFY(reopened.openDocumentSource(path)); QCOMPARE(reopened.document()->artboards.size(), 1u);
        QVERIFY(c.undo()); QVERIFY(c.document()->artboards.empty());
    }
    void initTestCase() {
        qputenv("DREAMSCAPES_EDITOR_CACHE", QByteArray(DREAMSCAPES_TEST_DIRECTORY "/editor-reference-cache"));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH", m_storage.path().toUtf8());
    }
    void capabilityDistinguishesUnsupportedControlsFromMissingInputs() {
        EditorCanvas c; create(c);
        const auto raw = c.toolControlState("camera-photo", "field-15", {});
        QVERIFY(raw.contains("supported")); QVERIFY(!raw.value("supported").toBool());
        const auto mask = c.toolControlState("layers", "field-23", {});
        QVERIFY(mask.value("supported").toBool()); QVERIFY(!mask.value("enabled").toBool());
        const auto before = encodeIisc(*c.document()).bytes;
        QVERIFY(!c.applyToolField("asset", "field-6", true));
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
        QVERIFY(c.toolControlState("effects", "field-45", {}).value("supported").toBool());
    }
    void manualLensControlsChangePixelsAndRestoreNeutral() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-lens-XXXXXX"); QVERIFY(dir.isValid());
        QImage source(32, 32, QImage::Format_ARGB32);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) source.setPixel(x, y, qRgb(x * 8, y * 8, (x ^ y) * 8));
        const auto path = dir.filePath("grid.png"); QVERIFY(source.save(path));
        EditorCanvas c; QVERIFY(c.openImages({QUrl::fromLocalFile(path)}));
        const auto original = c.selectedRasterPixels()->pixels;
        c.configureTool("effects", {{"field-45", 0}, {"field-46", false}});
        QVERIFY(c.applyToolField("effects", "field-45", 50));
        QVERIFY(c.selectedRasterPixels()->pixels != original);
        QVERIFY(c.applyToolField("effects", "field-45", 0));
        QCOMPARE(c.selectedRasterPixels()->pixels, original);
        QVERIFY(c.applyToolField("effects", "field-46", true));
        QVERIFY(c.selectedRasterPixels()->pixels != original);
        QVERIFY(c.applyToolField("effects", "field-46", false));
        QCOMPARE(c.selectedRasterPixels()->pixels, original);
    }
    void cloneSamplesActualCompositeWhenRequested() {
        EditorCanvas c; create(c, 32, 32); c.setSize({320, 320});
        QImage base(32, 32, QImage::Format_ARGB32); base.fill(Qt::blue);
        QVERIFY(c.insertPixels(base, "Target")); const auto target = c.selectedLayerId();
        QImage top(32, 32, QImage::Format_ARGB32); top.fill(Qt::transparent);
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 16; ++x) top.setPixel(x, y, 0xffff0000);
        QVERIFY(c.insertPixels(top, "Source overlay")); QVERIFY(c.selectLayer(target)); c.fitToView();
        const auto click = [&](QPointF documentPoint) {
            const QPointF point(c.panX() + documentPoint.x() * c.zoom(), c.panY() + documentPoint.y() * c.zoom());
            QMouseEvent press(QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&c, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, point, point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&c, &release);
        };
        QVariantMap values{{"selector", "Clone"}, {"field-0", 4}, {"field-8", 0}, {"field-10", false}, {"field-12", 100}, {"field-14", true}};
        c.configureTool("retouch", values); QVERIFY(c.executeToolAction("retouch", "field-11", values)); click({4, 8}); click({24, 8});
        QCOMPARE(c.selectedRasterPixels()->pixels[8 * 32 + 24], 0xff0000ffu);
        values["field-10"] = true; c.configureTool("retouch", values); click({24, 8});
        QCOMPARE(c.selectedRasterPixels()->pixels[8 * 32 + 24], 0xffff0000u);
        QVERIFY(!c.toolPreview("retouch", "field-15", values).value("imageSource").toUrl().isEmpty());
        QVERIFY(c.undo()); QCOMPARE(c.selectedRasterPixels()->pixels[8 * 32 + 24], 0xff0000ffu);
    }
    void generativeFillMarksRegionWithoutPaintingPlaceholderColor() {
        EditorCanvas c; create(c, 32, 32); QVERIFY(c.applyToolField("background", "field-1", "#FF0000"));
        QVERIFY(c.selectLayer("canvas.background.layer"));
        c.configureTool("fill", {{"selector", "Generative"}, {"field-1", "#0000FF"}});
        const auto before = encodeIisc(*c.document()).bytes;
        QVERIFY(c.fillAt({8, 8})); QVERIFY(c.toolState().value("selectionActive").toBool());
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
        QVERIFY(c.toolHint().contains("Generate fill"));
    }
    void timelineAudioMixHonorsCurrentFrameClipOffsetAndMute() {
        Document d; d.timeline.frameRate = {24, 1}; d.timeline.frameCount = 48;
        d.audioAssets.push_back({"mono", 24000, 1, std::vector<std::int16_t>(24000, 10000)});
        d.audioAssets.push_back({"stereo", 48000, 2, std::vector<std::int16_t>(96000, 2000)});
        AudioTrackLayer first{"first", "First"}; first.clips.push_back({"clip1", "Clip", "mono", 24, 24, 0, -6.020599913279624});
        AudioTrackLayer second{"second", "Second"}; second.clips.push_back({"clip2", "Clip", "stereo", 24, 24});
        AudioTrackLayer muted = second; muted.id = "muted"; muted.muted = true;
        d.audioTracks = {first, second, muted};
        const auto all = EditorMedia::mixTimelineAudio(d, 0); QVERIFY(all.ok());
        QCOMPARE(all.asset.samples[0], std::int16_t(0)); QCOMPARE(all.asset.samples[48000 * 2], std::int16_t(7000));
        const auto resumed = EditorMedia::mixTimelineAudio(d, 24); QVERIFY(resumed.ok());
        QCOMPARE(resumed.asset.samples.size(), std::size_t(96000));
        QCOMPARE(resumed.asset.samples[0], std::int16_t(7000)); QCOMPARE(resumed.asset.samples[1], std::int16_t(7000));
        d.audioTracks.front().clips.front().enabled = false;
        QCOMPARE(EditorMedia::mixTimelineAudio(d, 24).asset.samples[0], std::int16_t(2000));
        QVERIFY(!EditorMedia::mixTimelineAudio(d, 48).ok());
    }
    void timelineAudioMixPreservesEverySampleAcrossBlocks() {
        Document d; d.timeline.frameRate = {30000, 1001}; d.timeline.frameCount = 47;
        AudioAsset mono{"mono", 44100, 1, std::vector<std::int16_t>(90000)};
        AudioAsset stereo{"stereo", 48000, 2, std::vector<std::int16_t>(180000)};
        for (std::size_t i = 0; i < mono.samples.size(); ++i) mono.samples[i] = std::int16_t(int(i % 50001) - 25000);
        for (std::size_t i = 0; i < stereo.samples.size(); ++i) stereo.samples[i] = std::int16_t(int(i % 60001) - 30000);
        d.audioAssets = {mono, stereo};
        AudioTrackLayer first{"first", "First"}, second{"second", "Second"};
        first.clips.push_back({"mono-clip", "Mono", "mono", 3, 41, 137, 3.0});
        second.gainDb = -1.5;
        second.clips.push_back({"stereo-clip", "Stereo", "stereo", 5, 42, 41, 1.0});
        d.audioTracks = {first, second};
        constexpr FrameIndex firstFrame = 7;
        const auto actual = EditorMedia::mixTimelineAudio(d, firstFrame); QVERIFY(actual.ok());
        const double fps = double(d.timeline.frameRate.numerator) / d.timeline.frameRate.denominator;
        std::vector<double> mixed(actual.asset.samples.size());
        for (const auto &track : d.audioTracks) for (const auto &clip : track.clips) {
            const auto *source = findAudioAsset(d, clip.assetId); QVERIFY(source);
            const auto start = std::max<std::int64_t>(0, std::int64_t(std::ceil((clip.startFrame / fps - firstFrame / fps) * 48000)));
            const auto end = std::min<std::int64_t>(mixed.size() / 2, std::int64_t(std::ceil(((clip.startFrame + clip.durationFrames) / fps - firstFrame / fps) * 48000)));
            const auto sourceCount = source->samples.size() / source->channelCount;
            const double gain = std::pow(10.0, (track.gainDb + clip.gainDb) / 20);
            for (auto i = start; i < end; ++i) {
                const double position = ((firstFrame / fps + double(i) / 48000) - clip.startFrame / fps) * source->sampleRate + clip.sourceOffsetSamples;
                if (position < 0 || position >= sourceCount) continue;
                const auto left = std::size_t(position), right = std::min(left + 1, sourceCount - 1);
                const double fraction = position - left;
                for (int channel = 0; channel < 2; ++channel) {
                    const auto c = std::min<int>(channel, source->channelCount - 1);
                    mixed[i * 2 + channel] += (source->samples[left * source->channelCount + c] * (1 - fraction)
                        + source->samples[right * source->channelCount + c] * fraction) * gain;
                }
            }
        }
        for (std::size_t i = 0; i < mixed.size(); ++i)
            QCOMPARE(actual.asset.samples[i], std::int16_t(std::clamp(std::lround(mixed[i]), -32768L, 32767L)));
    }
    void timelineAudioMixLargeTimeline() {
        Document d; d.timeline.frameRate = {24, 1}; d.timeline.frameCount = 8000;
        d.audioAssets.push_back({"mono", 48000, 1, {1000}});
        AudioTrackLayer track{"track", "Track"};
        track.clips.push_back({"clip", "Clip", "mono", 0, 8000}); d.audioTracks.push_back(track);
        const auto mixed = EditorMedia::mixTimelineAudio(d, 0); QVERIFY(mixed.ok());
        QCOMPARE(mixed.asset.samples.size(), std::size_t(32000000));
        QCOMPARE(mixed.asset.samples[0], std::int16_t(1000));
        QCOMPARE(mixed.asset.samples[1], std::int16_t(1000));
        QCOMPARE(mixed.asset.samples[2], std::int16_t(0));
        QCOMPARE(mixed.asset.samples.back(), std::int16_t(0));
    }
    void undoHistoryIncludesAudioInItsMemoryBudget() {
        EditorCanvas c; create(c, 16, 16); QVERIFY(c.addPaintLayer());
        auto document = *c.document();
        document.audioAssets.push_back({"large-audio", 48000, 1, std::vector<std::int16_t>(10 * 1024 * 1024)});
        QVERIFY(c.loadProjectPage(std::move(document), "Audio history"));
        const auto id = c.selectedLayerId();
        for (int i = 0; i < 6; ++i) QVERIFY(c.setLayerName(id, QString::number(i)));
        int undos = 0;
        while (c.canUndo() && undos < 10) { QVERIFY(c.undo()); ++undos; }
        QCOMPARE(undos, 5); // Six 20-MiB states fit the existing 128-MiB budget.
        for (int i = 0; i < undos; ++i) QVERIFY(c.redo());
        QCOMPARE(c.selectedLayer().value("name").toString(), QString("5"));
        QVERIFY(c.undo()); QVERIFY(c.setLayerName(id, "Branch")); QVERIFY(!c.canRedo());
    }
    void selectedLayerMatchesThePublicLayerRow() {
        EditorCanvas c; create(c, 16, 16);
        auto document = *c.document();
        document.assets.emplace_back(RasterAsset{"pixel", {1, 1, {0xff123456}}});
        for (int i = 0; i < 256; ++i) document.layers.emplace_back(StaticBitmapLayer{
            {"layer-" + std::to_string(i), "Layer " + std::to_string(i)}, StaticSource{"pixel"}});
        QVERIFY(c.loadProjectPage(std::move(document), "Selection"));
        for (const auto &id : {QString("layer-0"), QString("layer-127"), QString("layer-255")}) {
            QVERIFY(c.selectLayer(id));
            QVariantMap expected;
            for (const auto &row : c.layers()) if (row.toMap().value("selected").toBool()) expected = row.toMap();
            expected["lock"] = "None"; expected["preserveAlpha"] = false;
            QCOMPARE(c.selectedLayer(), expected);
        }
        const auto revision = c.revision(); QElapsedTimer timer; timer.start();
        for (int i = 0; i < 1000; ++i) QCOMPARE(c.selectedLayer().value("id").toString(), QString("layer-255"));
        qInfo() << "1000 selected-layer queries, 256 layers:" << timer.nsecsElapsed() / 1000000.0 << "ms";
        QCOMPARE(c.revision(), revision);
        c.clearSelection(); QVERIFY(c.selectedLayer().isEmpty());
    }
    void toolStateDoesNotMaterializeClippingPixels() {
        EditorCanvas c; create(c, 1024, 1024);
        QVERIFY(c.applyToolField("background", "field-1", "#FFFFFF"));
        c.setLayerHistogramClipping(false, true);
        const auto before = encodeIisc(*c.document()).bytes;
        const auto revision = c.revision();
        QElapsedTimer clock; clock.start();
        const auto state = c.toolState();
        qInfo() << "clipping tool-state first read (us):" << clock.nsecsElapsed() / 1000;
        QVERIFY2(!state.contains("clippingOverlay"), "A metadata getter must not render or encode a clipping image.");
        const auto clipping = state.value("clipping").toMap();
        QVERIFY(clipping.value("enabled").toBool());
        QVERIFY(clipping.value("highlights").toBool());
        QVERIFY(!clipping.value("shadows").toBool());
        QCOMPARE(clipping.value("white").toInt(), 255);
        for (int i = 0; i < 100; ++i) QCOMPARE(c.toolState().value("clipping").toMap(), clipping);
        QCOMPARE(c.revision(), revision);
        QCOMPARE(encodeIisc(*c.document()).bytes, before);
    }
    void canvasGuidesAndClippingUseActualDocument() {
        EditorCanvas c; create(c, 64, 48);
        QVERIFY(c.applyToolField("canvas", "field-1", "254 ppi"));
        QVERIFY(c.applyToolField("canvas", "field-17", "3 mm"));
        QVERIFY(c.applyToolField("canvas", "field-18", "10%"));
        QVERIFY(c.applyToolField("canvas", "field-16", "Bleed"));
        auto guide = c.toolState().value("canvasGuide").toMap();
        QCOMPARE(guide.value("bleed").toDouble(), 30.0);
        QCOMPARE(guide.value("safeX").toDouble(), 6.4);
        QTemporaryDir exportDir(DREAMSCAPES_TEST_DIRECTORY "/editor-density-XXXXXX"); QVERIFY(exportDir.isValid());
        const auto exported = exportDir.filePath("density.png"); QVERIFY(c.exportImage(QUrl::fromLocalFile(exported), "PNG"));
        QTRY_VERIFY(!c.frameExporter()->property("busy").toBool());QVERIFY(c.frameExporter()->property("error").toString().isEmpty());
        QCOMPARE(QImage(exported).dotsPerMeterX(), 10000);
        const auto before = encodeIisc(*c.document()).bytes;
        QVERIFY(!c.applyToolField("canvas", "field-17", "bad mm"));
        QCOMPARE(c.toolValues("canvas").value("field-17").toString(), QString("3 mm"));
        QVERIFY(c.applyToolField("color", "field-18", true));
        QVERIFY(clippingPixels(c).isNull()); // Transparent pixels do not clip.
        QVERIFY(c.applyToolField("background", "field-1", "#FFFFFF"));
        QVERIFY(!clippingPixels(c).isNull());
        const auto white = encodeIisc(*c.document()).bytes;
        QVERIFY(c.applyToolField("color", "field-18", false));
        QCOMPARE(encodeIisc(*c.document()).bytes, white);
        QVERIFY(clippingPixels(c).isNull());
        QVERIFY(before != white);
    }
    void manualVignetteIsNonaccumulating() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-vignette-XXXXXX"); QVERIFY(dir.isValid());
        QImage source(32, 32, QImage::Format_ARGB32); source.fill(QColor(128, 128, 128));
        const auto path = dir.filePath("source.png"); QVERIFY(source.save(path));
        EditorCanvas c; QVERIFY(c.openImages({QUrl::fromLocalFile(path)}));
        const auto original = c.selectedRasterPixels()->pixels;
        c.configureTool("effects", {{"field-47", 0}});
        QVERIFY(c.applyToolField("effects", "field-47", -50));
        QVERIFY(c.selectedRasterPixels()->pixels != original);
        QVERIFY(c.applyToolField("effects", "field-47", 0));
        QCOMPARE(c.selectedRasterPixels()->pixels, original);
    }
    void brushStabilizerCatchUpChangesCommittedStroke() {
        auto stroke = [&](EditorCanvas &c, bool catchUp) {
            create(c, 64, 48); c.setBrushColor(QColor("#FF0000"));
            c.configureTool("brush", {{"field-4", 4}, {"field-5", 100}, {"field-6", false}, {"field-8", 100}, {"field-9", 100}, {"field-12", 0}, {"field-13", 0}, {"field-17", 0}, {"field-18", 0}, {"field-19", catchUp}, {"field-23", "Pulled"}, {"field-24", 100}, {"field-25", 1000}, {"field-26", false}});
            QVERIFY(c.beginStrokeAt({8, 24})); QVERIFY(c.continueStrokeAt({52, 24})); QVERIFY(c.endStrokeAt({52, 24}));
        };
        EditorCanvas first, second; stroke(first, false); stroke(second, true);
        const auto pulled = renderFrame(*first.document(), 0).pixels, caught = renderFrame(*second.document(), 0).pixels;
        QVERIFY(pulled.pixels != caught.pixels);
        QCOMPARE(pulled.pixels[24 * 64 + 50], 0u);
        QVERIFY(caught.pixels[24 * 64 + 50] >> 24);
    }
    void vectorBooleanEditsRealPathsAndUndoes() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-boolean-XXXXXX"); QVERIFY(dir.isValid());
        Document d; d.extent = {32, 32};
        auto box = [](double x) { VectorPath p; p.commands = {MoveTo{{x, 4}}, LineTo{{x + 16, 4}}, LineTo{{x + 16, 20}}, LineTo{{x, 20}}, ClosePath{}}; p.fill = SolidPaint{0xffff0000}; return p; };
        d.assets.emplace_back(VectorAsset{"boxes", d.extent, {box(4), box(12)}});
        d.layers.emplace_back(StaticVectorLayer{{"boxes.layer", "Boxes"}, StaticSource{"boxes"}});
        const auto filePath = dir.filePath("boxes.iisc"); DocumentFile file; QVERIFY(file.create(filePath.toStdString(), d).ok()); file.close();
        EditorCanvas c; QVERIFY(c.openDocumentSource(QUrl::fromLocalFile(filePath))); QVERIFY(c.selectLayer("boxes.layer"));
        const auto before = renderFrame(*c.document(), 0).pixels.pixels;
        QVERIFY(c.applyToolField("layers", "field-13", "Intersect"));
        const auto after = renderFrame(*c.document(), 0).pixels.pixels;
        QCOMPARE(after[8 * 32 + 8], 0u); QCOMPARE(after[8 * 32 + 16], 0xffff0000u);
        QVERIFY(c.undo()); QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels, before);
    }
    void objectEraserExpansionChangesPixelsOutsideOriginalMask() {
        EditorCanvas c; create(c, 32, 32);
        QVERIFY(c.applyToolField("background", "field-1", "#FF0000")); QVERIFY(c.selectLayer("canvas.background.layer"));
        c.configureTool("select", {{"selector", "Rectangle"}, {"field-2", 0}, {"field-5", false}});
        QVERIFY(c.selectArea({QPointF(12, 12), QPointF(20, 20)}));
        QVERIFY(c.executeToolAction("eraser", "field-7", {{"selector", "Object"}, {"field-5", 2}, {"field-6", "Transparent"}}));
        const auto pixels = c.selectedRasterPixels()->pixels;
        QCOMPARE(pixels[16 * 32 + 11], 0u); QCOMPARE(pixels[16 * 32 + 8], 0xffff0000u);
        QVERIFY(c.undo()); QCOMPARE(c.selectedRasterPixels()->pixels[16 * 32 + 11], 0xffff0000u);
    }
    void alphaProtectionBelongsToTheSelectedLayer() {
        EditorCanvas c; create(c, 32, 32); QVERIFY(c.addPaintLayer());
        const auto protectedId = c.selectedLayerId();
        QVERIFY(c.applyToolField("layers", "field-9", true));
        QVERIFY(c.selectedLayer().value("preserveAlpha").toBool());
        QVERIFY(c.addPaintLayer()); const auto freshId = c.selectedLayerId();
        QVERIFY(!c.selectedLayer().value("preserveAlpha").toBool());
        c.configureTool("brush", {{"field-4", 4}, {"field-5", 100}, {"field-6", false}, {"field-8", 100}, {"field-9", 100}, {"field-12", 0}, {"field-13", 0}});
        QVERIFY(c.beginStrokeAt({8, 8})); QVERIFY(c.endStrokeAt({20, 8}));
        QVERIFY(std::ranges::any_of(c.selectedRasterPixels()->pixels, [](auto p) { return p >> 24; }));
        QVERIFY(c.selectLayer(protectedId));
        const auto protectedPixels = c.selectedRasterPixels()->pixels;
        QVERIFY(c.beginStrokeAt({8, 8})); QVERIFY(c.endStrokeAt({20, 8}));
        QCOMPARE(c.selectedRasterPixels()->pixels, protectedPixels);
        QVERIFY(c.selectLayer(freshId)); QVERIFY(!c.selectedLayer().value("preserveAlpha").toBool());
    }
    void previewMirrorChangesViewWithoutRewritingDocument() {
        EditorCanvas c; create(c);
        const auto before = encodeIisc(*c.document()); QVERIFY(before.ok());
        QVERIFY(c.executeToolAction("canvas", "field-15", {{"field-12", "Horizontal"}, {"field-13", true}}));
        QVERIFY(c.previewMirrorX()); QVERIFY(!c.previewMirrorY());
        QCOMPARE(encodeIisc(*c.document()).bytes, before.bytes);
        QVERIFY(c.executeToolAction("canvas", "field-15", {{"field-12", "Horizontal"}, {"field-13", true}}));
        QVERIFY(!c.previewMirrorX());
    }
    void localMediaFiltersUseRealFiles() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-library-XXXXXX"); QVERIFY(dir.isValid());
        QImage p(4, 4, QImage::Format_ARGB32); p.fill(Qt::red); QVERIFY(p.save(dir.filePath("red.png")));
        QFile svg(dir.filePath("shape.svg")); QVERIFY(svg.open(QIODevice::WriteOnly)); svg.write("<svg/>"); svg.close();
        EditorMedia media;
        QCOMPARE(media.library(dir.path(), {{"field-1", "Image"}}).size(), 1);
        QCOMPARE(media.library(dir.path(), {{"field-1", "Vector"}}).size(), 1);
        QCOMPARE(media.library(dir.path(), {{"field-0", "missing"}}).size(), 0);
        EditorCanvas c; create(c);
        QCOMPARE(c.toolPreview("asset", "field-3", {{"field-8", dir.path()}}).value("items").toList().size(), 2);
    }
    void flattenedExportDoesNotRebindWorkingCanvas() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-export-XXXXXX"); QVERIFY(dir.isValid());
        EditorCanvas c; create(c); QVERIFY(c.applyToolField("background", "field-1", "#FF0000"));
        const auto before = *c.document();
        const auto path = dir.filePath("flattened.iisc"); QVERIFY(c.exportImage(QUrl::fromLocalFile(path), "IISC"));
        QTRY_VERIFY(!c.frameExporter()->property("busy").toBool());QVERIFY(c.frameExporter()->property("error").toString().isEmpty());
        QVERIFY(c.filePath().isEmpty()); QCOMPARE(c.document()->layers.size(), before.layers.size());
        EditorCanvas opened; QVERIFY(opened.openDocumentSource(QUrl::fromLocalFile(path)));
        QCOMPARE(opened.document()->layers.size(), 1u);
        QCOMPARE(renderFrame(*opened.document(), 0).pixels.pixels, renderFrame(before, 0).pixels.pixels);
    }
    void imageExportDoesNotOfferEditableLayerPreservation() {
        EditorCanvas c; create(c); QVERIFY(c.addPaintLayer());
        c.configureTool("file", {{"field-1", "PNG"}, {"field-11", false}});
        const auto before = encodeIisc(*c.document()); QVERIFY(before.ok());
        QVERIFY(!c.applyToolField("file", "field-11", true));
        QCOMPARE(encodeIisc(*c.document()).bytes, before.bytes);
        QVERIFY(c.error().contains("flattened"));
        QVERIFY(c.applyToolField("file", "field-1", "IISC"));
        QVERIFY(c.applyToolField("file", "field-11", true));
    }
    void cancelledStrokeDoesNotSuspendLaterHistory() {
        EditorCanvas c; create(c); c.configureTool("brush", {{"field-4", 6}, {"field-8", 100}, {"field-9", 100}});
        QVERIFY(c.beginStrokeAt({4, 4}));
        c.configureTool("background", {});
        QVERIFY(c.applyToolField("background", "field-1", "#00FF00"));
        QVERIFY(c.undo()); QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels.front(), 0u);
    }
    void pixelLockBlocksFillAndRepairWithoutChangingDocument() {
        EditorCanvas c; create(c, 32, 32); QVERIFY(c.addPaintLayer());
        c.configureTool("select", {{"selector", "Rectangle"}, {"field-2", 0}, {"field-5", false}});
        QVERIFY(c.selectArea({QPointF(4, 4), QPointF(12, 12)}));
        QVERIFY(c.applyToolField("layers", "field-8", "Pixels"));
        const auto before = encodeIisc(*c.document()); QVERIFY(before.ok());
        c.configureTool("fill", {{"selector", "Solid"}, {"field-1", "#FF0000"}, {"field-2", 100}});
        QVERIFY(!c.fillAt({8, 8})); QVERIFY(!c.executeToolAction("layers", "field-23", {})); QVERIFY(!c.executeToolAction("retouch", "field-3", {}));
        QCOMPARE(encodeIisc(*c.document()).bytes, before.bytes);
        QVERIFY(c.applyToolField("layers", "field-8", "None")); QVERIFY(c.fillAt({8, 8}));
        QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels[8 * 32 + 8], 0xffff0000U);
    }
    void layerResetChangesAuthoritativeProperties() {
        EditorCanvas c; create(c); QVERIFY(c.addPaintLayer()); const auto id = c.selectedLayerId();
        QVERIFY(c.setLayerOpacity(id, 0.4)); QVERIFY(c.setLayerBlend(id, "Multiply"));
        QVERIFY(c.applyToolField("layers", "field-8", "Pixels"));
        QVERIFY(c.resetTool("layers", {{"field-10", 100}, {"field-20", "Normal"}}));
        QCOMPARE(c.selectedLayer().value("opacity").toDouble(), 1.0);
        QCOMPARE(c.selectedLayer().value("blend").toString(), QString("Normal"));
        QCOMPARE(c.selectedLayer().value("lock").toString(), QString("None"));
        QVERIFY(c.undo()); QCOMPARE(c.selectedLayer().value("opacity").toDouble(), 0.4);
    }
    void selectionRotationAndRoundedTriangleChangeCoverage() {
        EditorCanvas c; create(c, 64, 64);
        QVariantMap settings{{"selector", "Rectangle"}, {"field-2", 0}, {"field-5", false}, {"field-6", 0}};
        c.configureTool("select", settings); QVERIFY(c.selectArea({QPointF(8, 8), QPointF(40, 24)}));
        const auto rectangle = c.selectionOverlay();
        settings["field-6"] = 45; c.configureTool("select", settings); QVERIFY(c.selectArea({QPointF(8, 8), QPointF(40, 24)}));
        QVERIFY(c.selectionOverlay() != rectangle);
        settings["selector"] = "Triangle"; settings["field-6"] = 0; settings["field-8"] = 0;
        c.configureTool("select", settings); QVERIFY(c.selectArea({QPointF(8, 8), QPointF(40, 40)}));
        const auto sharp = c.toolState().value("selectedPixels").toLongLong();
        settings["field-8"] = 100; c.configureTool("select", settings); QVERIFY(c.selectArea({QPointF(8, 8), QPointF(40, 40)}));
        QVERIFY(c.toolState().value("selectedPixels").toLongLong() < sharp);
    }
    void vectorEraserRemovesIntersectedOpenStroke() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-vector-XXXXXX"); QVERIFY(dir.isValid());
        Document document; document.extent = {32, 32};
        VectorPath path; path.commands = {MoveTo{{2, 16}}, LineTo{{30, 16}}}; path.stroke = StrokeStyle{SolidPaint{0xffff0000}, 4};
        document.assets.emplace_back(VectorAsset{"line.asset", document.extent, {path}});
        document.layers.emplace_back(StaticVectorLayer{{"line.layer", "Line"}, StaticSource{"line.asset"}});
        DocumentFile file; const auto source = dir.filePath("line.iisc"); QVERIFY(file.create(source.toStdString(), document).ok()); file.close();
        EditorCanvas c; QVERIFY(c.openDocumentSource(QUrl::fromLocalFile(source))); QVERIFY(c.selectLayer("line.layer"));
        c.configureTool("select", {{"selector", "Rectangle"}, {"field-2", 0}, {"field-5", false}});
        QVERIFY(c.selectArea({QPointF(12, 12), QPointF(20, 20)}));
        const auto before = renderFrame(*c.document(), 0).pixels;
        QVERIFY(before.pixels[16 * 32 + 16] >> 24);
        QVERIFY(c.executeToolAction("eraser", "field-10", {{"field-8", "Erase stroke"}}));
        QVERIFY(std::ranges::none_of(renderFrame(*c.document(), 0).pixels.pixels, [](auto p) { return p >> 24; }));
        QVERIFY(c.undo()); QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels, before.pixels);
        QVERIFY(c.executeToolAction("eraser", "field-10", {{"field-8", "Trim"}}));
        const auto trimmed = renderFrame(*c.document(), 0).pixels;
        QCOMPARE(trimmed.pixels[16 * 32 + 16], 0u); QVERIFY(trimmed.pixels[16 * 32 + 4] >> 24);
    }
    void outpaintPreservesExistingComposition() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-outpaint-XXXXXX"); QVERIFY(dir.isValid());
        EditorCanvas c; create(c, 16, 16); QVERIFY(c.applyToolField("background", "field-1", "#FF0000"));
        const auto parameters = c.generationParameters("Outpaint", {{"field-12", "4 px"}, {"field-11", "All"}}); QVERIFY(!parameters.isEmpty());
        QImage generated(24, 24, QImage::Format_ARGB32); generated.fill(Qt::blue);
        const auto path = dir.filePath("result.png"); QVERIFY(generated.save(path));
        QVERIFY(c.placeGenerated(QUrl::fromLocalFile(path), "Outpaint"));
        const auto pixels = renderFrame(*c.document(), 0).pixels;
        QCOMPARE(pixels.width, 24); QCOMPARE(pixels.pixels[0], 0xff0000ffU); QCOMPARE(pixels.pixels[12 * 24 + 12], 0xffff0000U);
        QVERIFY(c.undo()); QCOMPARE(c.canvasWidth(), 16);
    }
    void generatedBackgroundReplacesTheExistingBackground() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-bg-XXXXXX"); QVERIFY(dir.isValid());
        EditorCanvas c; create(c); QVERIFY(c.applyToolField("background", "field-1", "#FF0000"));
        QImage generated(64, 48, QImage::Format_ARGB32); generated.fill(Qt::blue);
        const auto path = dir.filePath("result.png"); QVERIFY(generated.save(path));
        QVERIFY(c.placeGenerated(QUrl::fromLocalFile(path), "Background"));
        QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels.front(), 0xff0000ffU);
        QCOMPARE(c.document()->layers.size(), 1u);
    }
    void maskedGenerationUsesTargetLayerTransform() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-inpaint-XXXXXX"); QVERIFY(dir.isValid());
        Document document; document.extent = {16, 16}; RasterLayer source = makeRasterLayer(4, 4); std::fill(source.pixels.begin(), source.pixels.end(), 0xff00ff00);
        document.assets.emplace_back(RasterAsset{"target.asset", source});
        LayerProperties target{"target.layer", "Target"}; target.transform.translationX = target.transform.translationY = 8;
        document.layers.emplace_back(StaticBitmapLayer{target, StaticSource{"target.asset"}});
        const auto path = dir.filePath("target.iisc"); DocumentFile file; QVERIFY(file.create(path.toStdString(), document).ok()); file.close();
        EditorCanvas c; QVERIFY(c.openDocumentSource(QUrl::fromLocalFile(path))); QVERIFY(c.selectLayer("target.layer"));
        c.configureTool("select", {{"selector", "Rectangle"}, {"field-2", 0}, {"field-5", false}});
        QVERIFY(c.selectArea({QPointF(8, 8), QPointF(12, 12)}));
        QVERIFY(!c.generationParameters("Inpaint", {}).isEmpty());
        QImage generated(16, 16, QImage::Format_ARGB32); generated.fill(Qt::blue);
        for (int y = 8; y < 12; ++y) for (int x = 8; x < 12; ++x) generated.setPixel(x, y, 0xffff0000);
        const auto result = dir.filePath("result.png"); QVERIFY(generated.save(result));
        QVERIFY(c.placeGenerated(QUrl::fromLocalFile(result), "Inpaint"));
        QCOMPARE(c.selectedRasterPixels()->pixels.front(), 0xffff0000u);
        QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels[8 * 16 + 8], 0xffff0000u);
    }
    void textCreatesEditableNativeGeometry() {
        EditorCanvas c; create(c);
        c.configureTool("text", {{"selector", "Free Text"}, {"field-0", "Hello"}, {"field-2", 14}});
        QVERIFY(c.createText({2, 4}));
        QVERIFY(std::holds_alternative<StaticVectorLayer>(c.document()->layers.back()));
        const auto id = c.selectedLayerId();
        const auto rendered = renderFrame(*c.document(), 0).pixels.pixels;
        QVERIFY(std::ranges::any_of(rendered, [](auto p) { return (p >> 24) != 0; }));
        QVERIFY(c.applyToolField("text", "field-0", "Changed"));
        QCOMPARE(c.selectedLayerId(), id);
        QVERIFY(renderFrame(*c.document(), 0).pixels.pixels != rendered);
        QVERIFY(c.undo());
        QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels, rendered);
        QCOMPARE(c.toolValues("text").value("field-0").toString(), QString("Hello"));
    }
    void textTypographyLayoutAndAppearanceAffectExport() {
        EditorCanvas c; create(c, 256, 144);
        QVariantMap values{{"selector", "Callout"}, {"field-0", "Hello\n한글"}, {"field-2", 18},
            {"textWeight", "Medium"}, {"textLineHeight", 22}, {"textFill", "White"},
            {"field-11", "None"}, {"textLayoutEnabled", true}, {"field-5", "Center"}};
        c.configureTool("text", values); QVERIFY2(c.createText({20, 20}), qPrintable(c.error()));
        const auto initial = renderFrame(*c.document(), 0).pixels.pixels;
        QVERIFY(c.applyToolField("text", "textWeight", "Bold"));
        QVERIFY(renderFrame(*c.document(), 0).pixels.pixels != initial);
        const auto bold = renderFrame(*c.document(), 0).pixels.pixels;
        QVERIFY(c.applyToolField("text", "field-5", "Top left"));
        QVERIFY(renderFrame(*c.document(), 0).pixels.pixels != bold);
        QVERIFY(c.applyToolField("text", "textFill", "Accent"));
        QVERIFY(c.applyToolField("text", "textStrokeWidth", 2));
        QVERIFY(c.applyToolField("text", "textShadow", true));
        QVERIFY(std::holds_alternative<StaticBitmapLayer>(c.document()->layers.back()));
        const auto shadow = renderFrame(*c.document(), 0).pixels.pixels;
        QVERIFY(c.applyToolField("text", "textNoise", 20));
        QVERIFY(c.applyToolField("text", "textNoiseSeed", 42));
        const auto noisy = renderFrame(*c.document(), 0).pixels.pixels;
        QVERIFY(noisy != shadow);
        QVERIFY(c.applyToolField("text", "textNoiseSeed", 42));
        QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels, noisy);
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/text-export-XXXXXX"); QVERIFY(directory.isValid());
        const auto path = directory.filePath("text.png"); QVERIFY(c.exportImage(QUrl::fromLocalFile(path), "PNG"));
        QTRY_VERIFY(!c.frameExporter()->property("busy").toBool());QVERIFY(c.frameExporter()->property("error").toString().isEmpty());
        const QImage exported(path); QCOMPARE(exported.size(), QSize(256, 144));
        for (int y = 0; y < 144; ++y) for (int x = 0; x < 256; ++x) QCOMPARE(exported.pixel(x, y), noisy[y * 256 + x]);
        QVERIFY(c.applyToolField("text", "textShadow", false));
        QVERIFY(c.applyToolField("text", "textNoise", 0));
        QVERIFY(std::holds_alternative<StaticVectorLayer>(c.document()->layers.back()));
        QVERIFY(c.applyToolField("text", "field-0", ""));
        QVERIFY(c.applyToolField("text", "field-0", "Still editable"));
    }
    void allElementStylesProducePixels() {
        for (const auto &shape : {"Rectangle", "Ellipse", "Polygon", "Line / Arrow"}) {
            EditorCanvas c; create(c);
            c.configureTool("elements", {{"selector", shape}, {"field-2", "Gradient"}, {"field-7", 5},
                {"field-4", 180}, {"field-5", 40}, {"field-9", true}, {"field-10", 3},
                {"field-12", "Dot"}, {"field-13", "Arrow"}, {"field-16", "#FF0000"}, {"field-17", "#0000FF"}});
            QVERIFY2(c.createElement({8, 8, 32, 24}), qPrintable(c.error()));
            const auto result = renderFrame(*c.document(), 0); QVERIFY(result.ok());
            QVERIFY(std::ranges::any_of(result.pixels.pixels, [](auto p) { return (p >> 24) != 0; }));
            QVERIFY(c.applyToolField("elements", "field-14", 50));
            QCOMPARE(layerProperties(c.document()->layers.back()).opacity, 0.5);
        }
    }
    void selectionConstrainsFillAndEdits() {
        EditorCanvas c; create(c, 32, 32); QVERIFY(c.addPaintLayer());
        c.configureTool("select", {{"selector", "Rectangle"}, {"field-2", 0}, {"field-5", false}});
        QVERIFY(c.selectArea({QPointF(4, 4), QPointF(12, 12)}));
        c.configureTool("fill", {{"selector", "Solid"}, {"field-1", "#FF0000"}, {"field-2", 100}});
        QVERIFY(c.fillAt({8, 8}));
        auto pixels = renderFrame(*c.document(), 0).pixels;
        QCOMPARE(pixels.pixels[8 * 32 + 8], 0xffff0000U);
        QCOMPARE(pixels.pixels[2 * 32 + 2], 0U);
        QVERIFY(c.undo()); QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels[8 * 32 + 8], 0U);
        QVERIFY(c.redo()); QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels[8 * 32 + 8], 0xffff0000U);
    }
    void adjustmentsDoNotAccumulateAndReopen() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-tools-XXXXXX");
        QImage image(16, 16, QImage::Format_ARGB32); image.fill(QColor(48, 64, 80, 128));
        const auto path = dir.filePath("source.png"); QVERIFY(image.save(path));
        EditorCanvas c; QVERIFY(c.openImages({QUrl::fromLocalFile(path)}));
        const auto original = c.selectedRasterPixels()->pixels;
        c.configureTool("color", {{"field-1", 0}});
        QVERIFY(c.applyToolField("color", "field-1", 1.0));
        const auto one = c.selectedRasterPixels()->pixels;
        QVERIFY(one != original);
        QVERIFY(c.applyToolField("color", "field-1", 2.0));
        QVERIFY(c.applyToolField("color", "field-1", 1.0));
        QCOMPARE(c.selectedRasterPixels()->pixels, one);
        for (auto p : one) QCOMPARE(p >> 24, 128U);
        const auto saved = dir.filePath("edited.iisc"); QVERIFY(c.saveDocumentAs(QUrl::fromLocalFile(saved)));
        EditorCanvas reopened; QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(saved)));
        QCOMPARE(reopened.selectedRasterPixels()->pixels, one);
        QCOMPARE(QImage(path).pixel(0, 0), image.pixel(0, 0));
    }
    void backgroundAndCanvasActionsChangeDocument() {
        EditorCanvas c; create(c, 32, 16);
        QVERIFY(c.applyToolField("background", "field-1", "#00FF00"));
        QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels.front(), 0xff00ff00U);
        QVERIFY(c.executeToolAction("canvas", "field-11", {{"field-8", "90° R"}, {"field-9", 0}, {"field-10", true}}));
        QCOMPARE(c.canvasWidth(), 16); QCOMPARE(c.canvasHeight(), 32);
        QVERIFY(c.executeToolAction("canvas", "field-15", {{"field-12", "Horizontal"}, {"field-13", false}}));
        QVERIFY(c.undo());
    }
    void effectsAndMaskAreActualEdits() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-effect-XXXXXX");
        QImage image(16, 16, QImage::Format_ARGB32);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) image.setPixel(x, y, (x + y) % 2 ? 0xffffffffU : 0xff000000U);
        const auto path = dir.filePath("checker.png"); QVERIFY(image.save(path));
        EditorCanvas c; QVERIFY(c.openImages({QUrl::fromLocalFile(path)})); const auto before = c.selectedRasterPixels()->pixels;
        c.configureTool("effects", {{"field-4", "Gaussian"}, {"field-5", 0}});
        QVERIFY(c.applyToolField("effects", "field-5", 3));
        QVERIFY(c.selectedRasterPixels()->pixels != before);
        QVERIFY(!c.toolPreview("color", "field-0", {}).value("imageSource").toString().isEmpty());
        c.configureTool("masking", {{"selector", "Radial"}, {"field-6", false}, {"field-9", 0}});
        QVERIFY(c.selectArea({QPointF(2, 2), QPointF(12, 12)}));
        QVERIFY(c.executeToolAction("layers", "field-23", {{"field-25", 100}, {"field-26", 0}}));
        QCOMPARE(renderFrame(*c.document(), 0).pixels.pixels.front() >> 24, 0U);
    }
    void audioImportTrimAndGainArePersisted() {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/editor-audio-XXXXXX");
        AudioAsset source; source.id = "test"; source.sampleRate = 8000; source.channelCount = 1; source.samples.assign(16000, 1000);
        const auto wav = dir.filePath("track.wav"); QVERIFY(exportAudioWav(source, wav.toStdString()).ok());
        EditorCanvas c; create(c);
        QVERIFY(c.importToolSource(QUrl::fromLocalFile(wav), "audio-track", {{"field-1", "Stereo"}, {"field-2", "48k"}, {"field-5", "00:00 — 00:01"}, {"field-8", -6}, {"field-6", 0}, {"field-7", 0}}));
        QCOMPARE(c.document()->audioTracks.size(), 1u);
        QCOMPARE(c.document()->audioAssets.back().channelCount, 2);
        QCOMPARE(c.document()->audioAssets.back().sampleRate, 48000U);
        const auto save = dir.filePath("audio.iisc"); QVERIFY(c.saveDocumentAs(QUrl::fromLocalFile(save)));
        EditorCanvas reopened; QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(save)));
        QCOMPARE(reopened.document()->audioTracks.size(), 1u);
        QCOMPARE(reopened.document()->audioAssets.back().samples.size(), c.document()->audioAssets.back().samples.size());
    }
};
QTEST_MAIN(EditorToolsTests)
#include "tst_editortools.moc"
