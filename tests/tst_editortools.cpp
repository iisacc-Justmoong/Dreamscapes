#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorMedia.h"
#include <iiSharedCanvas.h>
#include <QImage>
#include <QTemporaryDir>
#include <QMouseEvent>
#include <QtTest>
using namespace iiSharedCanvas;

class EditorToolsTests : public QObject {
    Q_OBJECT
private:
    void create(EditorCanvas &c, int width = 64, int height = 48) {
        QVERIFY(c.createCanvas({{"width", width}, {"height", height}, {"unit", "px"}, {"background", "Transparent"}}));
    }
private slots:
    void initTestCase() { qputenv("DREAMSCAPES_EDITOR_CACHE", QByteArray(DREAMSCAPES_TEST_DIRECTORY "/editor-reference-cache")); }
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
        QCOMPARE(QImage(exported).dotsPerMeterX(), 10000);
        const auto before = encodeIisc(*c.document()).bytes;
        QVERIFY(!c.applyToolField("canvas", "field-17", "bad mm"));
        QCOMPARE(c.toolValues("canvas").value("field-17").toString(), QString("3 mm"));
        QVERIFY(c.applyToolField("color", "field-18", true));
        QVERIFY(c.toolState().value("clippingOverlay").toUrl().isEmpty()); // Transparent pixels do not clip.
        QVERIFY(c.applyToolField("background", "field-1", "#FFFFFF"));
        QVERIFY(!c.toolState().value("clippingOverlay").toUrl().isEmpty());
        const auto white = encodeIisc(*c.document()).bytes;
        QVERIFY(c.applyToolField("color", "field-18", false));
        QCOMPARE(encodeIisc(*c.document()).bytes, white);
        QVERIFY(c.toolState().value("clippingOverlay").toUrl().isEmpty());
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
