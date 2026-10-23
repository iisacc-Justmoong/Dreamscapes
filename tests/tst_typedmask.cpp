#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorGraphicCommand.h"
#include "App/Views/Editor/EditorGraphicAdjust.h"
#include "App/Views/Editor/EditorToolUtils.h"
#include "App/Views/Editor/EditorRasterSource.h"
#include <iiGraphicAdjust/iiGraphicAdjust.h>
#include <iiFileProviderNative.h>
#include <SocietyDrive.h>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
using namespace iiSharedCanvas;
namespace GA = iiGraphicAdjust;
namespace {
Document picture() {
    Document d; d.extent = {8, 8};
    d.assets.emplace_back(RasterAsset{"pixels", makeRasterLayer(8, 8, 0xff4a6a8aU)});
    d.layers.emplace_back(StaticBitmapLayer{{"layer", "Layer"}, StaticSource{"pixels"}}); return d;
}
QVariantMap maskMap(const GA::Mask &mask) {
    QVariantList values; for (auto value : mask.coverage) values.append(double(value));
    return {{"width", qulonglong(mask.width)}, {"height", qulonglong(mask.height)}, {"coverage", values}};
}
QVariantMap adjustments(const GA::Mask &first, const GA::Mask &second) {
    return {{"adjustments", QVariantList{
        QVariantMap{{"mask", maskMap(first)}, {"options", QVariantMap{{"exposure", .7}}}},
        QVariantMap{{"mask", maskMap(second)}, {"options", QVariantMap{{"contrast", 18.0}, {"temperature", -12.0}}}}}}};
}
GA::Mask levels() {
    GA::Mask mask{16, 16, std::vector<float>(256)};
    for (int i = 0; i < 256; ++i) mask.coverage[i] = float(i / 255.0);
    return mask;
}
}
class TypedMaskTests : public QObject {
    Q_OBJECT
    QTemporaryDir storage{DREAMSCAPES_TEST_DIRECTORY "/typed-mask-documents-XXXXXX.societycontainer"};
    QByteArray oldContainer;
private slots:
    void initTestCase() {
        oldContainer = qgetenv("SOCIETY_CONTAINER_PATH"); QVERIFY(storage.isValid());
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path())); qputenv("SOCIETY_CONTAINER_PATH", storage.path().toUtf8());
    }
    void cleanupTestCase() { if (oldContainer.isNull()) qunsetenv("SOCIETY_CONTAINER_PATH"); else qputenv("SOCIETY_CONTAINER_PATH", oldContainer); }
    void explicitAdjustmentCollectionsReachTheSdk_data() {
        QTest::addColumn<bool>("selected"); QTest::addColumn<bool>("wrapped");
        QTest::newRow("explicit-masks-only") << false << false; QTest::newRow("explicit-masks-with-selection") << true << false;
        QTest::newRow("wrapped-masks-only") << false << true; QTest::newRow("wrapped-masks-with-selection") << true << true;
    }
    void explicitAdjustmentCollectionsReachTheSdk() {
        QFETCH(bool, selected); QFETCH(bool, wrapped); auto d = picture(); const auto &pixels = std::get<RasterAsset>(d.assets[0]).pixels;
        GA::Mask first{8, 8, std::vector<float>(64, .65f)}, second{8, 8, std::vector<float>(64)};
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) second.coverage[y * 8 + x] = float(x / 7.0);
        const auto native = GA::fromArgb32(8, 8, pixels.pixels); QVERIFY(native);
        GA::LocalAdjustment a{first.view(), {}}, b{second.view(), {}}; a.options.exposure = .7; b.options.contrast = 18; b.options.temperature = -12;
        const std::array<GA::LocalAdjustment, 2> items{a, b}; const auto corrected = GA::applyLocalAdjustments(native->view(), items); QVERIFY(corrected);
        const auto words = GA::toArgb32(corrected->view()); QVERIFY(words);
        graphicImage::Input input; input.snapshot = {std::make_shared<const Document>(d), 0, {}}; input.layer = "layer";
        input.operation = "applyLocalAdjustments"; input.parameters = adjustments(first, second); input.extent = d.extent;
        if (wrapped) input.parameters = {{"options", input.parameters}};
        if (selected) {
            input.selection = {{8, 8}, std::vector<std::uint8_t>(64, 0)};
            for (int y = 0; y < 8; ++y) for (int x = 0; x < 4; ++x) input.selection.alpha[y * 8 + x] = 255;
        }
        const auto result = graphicImage::calculate(input); QVERIFY2(result.ok(), qPrintable(result.error));
        RasterLayer expected{8, 8, *words};
        if (selected) { const auto blended = blendRaster(pixels, expected, input.selection); QVERIFY(blended.ok()); expected = blended.pixels; }
        QCOMPARE(result.pixels->pixels, expected.pixels); QCOMPARE(encodeIisc(*input.snapshot.document).bytes, encodeIisc(d).bytes);
    }
    void acceptedExplicitCollectionsDoNotRequireAnEditorSelection() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Collection")); QVERIFY(canvas.selectLayer("layer"));
        GA::Mask first{8, 8, std::vector<float>(64, .65f)}, second{8, 8, std::vector<float>(64, .25f)};
        const auto path = storage.filePath("Files/collection.iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        const auto file = std::filesystem::path(path.toStdU16String()); const auto saved = iiFileProvider::native::File::read(file);
        const auto before = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision(); const auto alias = canvas.fileAlias();
        EditorGraphicCommand command; command.setCanvas(&canvas);
        QVERIFY(command.request("preview", "masking", "applyLocalAdjustments", adjustments(first, second)));
        QTRY_VERIFY(command.state()["valid"].toBool()); QVERIFY(!command.state()["imageSource"].toUrl().isEmpty());
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before); QCOMPARE(canvas.fileAlias(), alias);
        QCOMPARE(iiFileProvider::native::File::read(file), saved);
        const auto expected = EditorGraphicAdjust::process(editorTools::image(*canvas.selectedRasterPixels()), "applyLocalAdjustments", adjustments(first, second)); QVERIFY(expected.ok());
        QVERIFY(command.request("apply", "masking", "applyLocalAdjustments", adjustments(first, second))); QTRY_VERIFY(command.state()["valid"].toBool());
        QCOMPARE(canvas.revision(), revision + 1); QCOMPARE(canvas.selectedRasterPixels()->pixels, editorTools::raster(expected.image).pixels);
        EditorCanvas reopened; QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path))); QVERIFY(reopened.selectLayer("layer"));
        QCOMPARE(reopened.selectedRasterPixels()->pixels, canvas.selectedRasterPixels()->pixels);
    }
    void borrowedImagesMatchPublicMasksAtEveryByteLevel_data() {
        QTest::addColumn<QString>("operation");
        for (auto op : {"maskColorOverlay", "maskColorOnBwOverlay", "maskImageOnBlack", "maskImageOnWhite", "maskWhiteOnBlack", "applyLocalAdjustments"}) QTest::newRow(op) << QString(op);
    }
    void borrowedImagesMatchPublicMasksAtEveryByteLevel() {
        QFETCH(QString, operation); auto mask = levels(); QImage source(16, 16, QImage::Format_ARGB32);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) source.setPixel(x, y, qRgba(17 * x, 17 * y, 119, 128 + x));
        QVariantMap parameters; if (operation == "applyLocalAdjustments") parameters = {{"exposure", .7}, {"contrast", 12.0}};
        auto publicParameters = parameters; publicParameters["mask"] = maskMap(mask);
        const auto legacy = EditorGraphicAdjust::process(source, operation, publicParameters); QVERIFY2(legacy.ok(), qPrintable(legacy.error));
        EditorGraphicAdjust::MaskInputs inputs; inputs.mask = mask.view();
        const auto actual = EditorGraphicAdjust::process(source, operation, parameters, inputs); QVERIFY2(actual.ok(), qPrintable(actual.error));
        QCOMPARE(actual.image, legacy.image); QCOMPARE(actual.parameters, legacy.parameters);
        // The borrowed view is not modified or retained by the adapter.
        QCOMPARE(mask.coverage, levels().coverage);
    }
    void borrowedMaskOperatorsMatchNativeSdk_data() {
        QTest::addColumn<QString>("operation"); QTest::addColumn<int>("mode");
        QTest::newRow("invert") << QString("invertMask") << 0; QTest::newRow("refine") << QString("refineMask") << 0;
        QTest::newRow("combine-add") << QString("combineMasks") << 0; QTest::newRow("combine-subtract") << QString("combineMasks") << 1;
        QTest::newRow("combine-intersect") << QString("combineMasks") << 2;
    }
    void borrowedMaskOperatorsMatchNativeSdk() {
        QFETCH(QString, operation); QFETCH(int, mode); auto first = levels(); auto second = levels(); std::reverse(second.coverage.begin(), second.coverage.end());
        QImage source(16, 16, QImage::Format_ARGB32); source.fill(0xff336699U);
        const auto native = GA::fromArgb32(16, 16, editorTools::raster(source).pixels); QVERIFY(native);
        auto expected = operation == "invertMask" ? GA::invertMask(first.view())
            : operation == "refineMask" ? GA::refineMask(native->view(), first.view(), 10)
            : GA::combineMasks(first.view(), second.view(), GA::MaskCombine(mode)); QVERIFY(expected);
        QVariantMap parameters, serialized; EditorGraphicAdjust::MaskInputs inputs;
        if (operation == "combineMasks") {
            parameters["operation"] = mode; inputs.base = first.view(); inputs.operand = second.view();
            serialized = parameters; serialized["base"] = maskMap(first); serialized["operand"] = maskMap(second);
        } else {
            if (operation == "refineMask") parameters["amount"] = 10;
            inputs.mask = first.view(); serialized = parameters; serialized["mask"] = maskMap(first);
        }
        const auto result = EditorGraphicAdjust::makeMask(source, operation, parameters, inputs); QVERIFY2(result.ok(), qPrintable(result.error));
        QCOMPARE(result.width, 16); QCOMPARE(result.height, 16); QCOMPARE(result.coverage, expected->coverage);
        const auto publicResult = EditorGraphicAdjust::makeMask(source, operation, serialized); QVERIFY(publicResult.ok()); QCOMPARE(result.coverage, publicResult.coverage);
    }
    void invalidBorrowedMasksAreRejectedWithoutFallingBack_data() {
        QTest::addColumn<QString>("invalid");
        for (auto value : {"zero-width", "zero-height", "oversized", "wrong-count", "negative", "above-one", "nan", "infinity"}) QTest::newRow(value) << QString(value);
    }
    void invalidBorrowedMasksAreRejectedWithoutFallingBack() {
        QFETCH(QString, invalid); auto mask = levels();
        if (invalid == "zero-width") mask.width = 0; else if (invalid == "zero-height") mask.height = 0;
        else if (invalid == "oversized") mask.width = 65537; else if (invalid == "wrong-count") mask.coverage.pop_back();
        else if (invalid == "negative") mask.coverage[3] = -.01f; else if (invalid == "above-one") mask.coverage[3] = 1.01f;
        else if (invalid == "nan") mask.coverage[3] = std::numeric_limits<float>::quiet_NaN(); else mask.coverage[3] = std::numeric_limits<float>::infinity();
        QImage source(16, 16, QImage::Format_ARGB32); source.fill(Qt::red);
        const QVariantMap valid{{"mask", maskMap(levels())}}; EditorGraphicAdjust::MaskInputs inputs; inputs.mask = mask.view();
        const auto diagnostic = EditorGraphicAdjust::process(source, "maskWhiteOnBlack", valid, inputs); QVERIFY(!diagnostic.ok()); QVERIFY(!diagnostic.error.isEmpty());
        const auto inversion = EditorGraphicAdjust::makeMask(source, "invertMask", valid, inputs); QVERIFY(!inversion.ok()); QVERIFY(!inversion.error.isEmpty());
    }
    void nativeInputsPreserveStrictParameterValidation() {
        auto mask = levels(); QImage source(16, 16, QImage::Format_ARGB32); source.fill(Qt::red);
        EditorGraphicAdjust::MaskInputs inputs; inputs.mask = mask.view();
        QVERIFY(!EditorGraphicAdjust::process(source, "maskWhiteOnBlack", {{"typo", 1}}, inputs).ok());
        QVERIFY(!EditorGraphicAdjust::makeMask(source, "invertMask", {{"typo", 1}}, inputs).ok());
        // A top-level mask must not override either of the two independent collection masks.
        QVERIFY(!EditorGraphicAdjust::process(source, "applyLocalAdjustments", adjustments(mask, mask), inputs).ok());
        QVERIFY(!EditorGraphicAdjust::process(source, "maskWhiteOnBlack", {{"mask", QVariantMap{{"width", 16}, {"height", 16}, {"coverage", QVariantList{0.0}}}}}).ok());
    }
    void newMaskPreviewsOmitUnusedSelectionButApplyingKeepsIt() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Mask capture")); QVERIFY(canvas.selectLayer("layer"));
        canvas.configureTool("select", {}); QVERIFY(canvas.selectArea({QPointF(0, 0), QPointF(4, 8)}));
        const auto revision = canvas.revision(); const auto before = encodeIisc(*canvas.document()).bytes;
        auto captured = std::make_shared<graphicImage::Input>();
        EditorGraphicCommand command([captured](const auto &input, std::stop_token stop) { *captured = input; return graphicImage::calculate(input, stop); }, nullptr);
        command.setCanvas(&canvas); QVERIFY(command.request("preview", "masking", "radialMask", {})); QTRY_VERIFY(command.state()["valid"].toBool());
        QVERIFY(captured->selection.alpha.empty());
        QVERIFY(command.request("mask-helper", "masking", "radialMask", {})); QTRY_VERIFY(command.state()["valid"].toBool());
        QVERIFY(!captured->selection.alpha.empty());
        const auto previous = captured->selection;
        QVERIFY(command.request("preview", "masking", "combineMasks", {{"operation", "Add"}})); QTRY_VERIFY(command.state()["valid"].toBool());
        QVERIFY(!captured->selection.alpha.empty()); QVERIFY(captured->operand); QCOMPARE(captured->operand->alpha, previous.alpha);
        QCOMPARE(canvas.revision(), revision); QCOMPARE(encodeIisc(*canvas.document()).bytes, before);
    }
    void geometryMaskApplyCapturesNoImageSnapshot_data() {
        QTest::addColumn<QString>("operation");
        for (const auto op : {"linearMask", "radialMask", "invertMask", "combineMasks"}) QTest::newRow(op) << QString(op);
    }
    void geometryMaskApplyCapturesNoImageSnapshot() {
        QFETCH(QString, operation); EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Geometry")); QVERIFY(canvas.selectLayer("layer"));
        const auto path = storage.filePath("Files/geometry-" + operation + ".iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
        canvas.configureTool("select", {}); QVERIFY(canvas.selectArea({QPointF(0, 0), QPointF(4, 8)}));
        const auto bytes = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
        const auto native = std::filesystem::path(path.toStdU16String()); const auto saved = iiFileProvider::native::File::read(native);
        QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged), revisions(&canvas, &EditorCanvas::revisionChanged);
        QVariantMap parameters;
        if (operation == "combineMasks") parameters = {{"operand", maskMap(GA::Mask{8, 8, std::vector<float>(64, .25f)})}, {"operation", "Add"}};
        auto captured = std::make_shared<graphicImage::Input>(); auto result = std::make_shared<graphicImage::Result>();
        EditorGraphicCommand command([captured, result](const auto &input, std::stop_token stop) { *captured = input; auto out = graphicImage::calculate(input, stop); *result = out; return out; }, nullptr);
        command.setCanvas(&canvas);
        QVERIFY(command.request("preview", "masking", operation, parameters)); QTRY_VERIFY(command.state()["valid"].toBool());
        QVERIFY(captured->snapshot.document); QVERIFY(result->original); QVERIFY(!result->preview["originalImageSource"].toUrl().isEmpty());
        QVERIFY(command.request("mask-helper", "masking", operation, parameters)); QTRY_VERIFY(command.state()["valid"].toBool());
        QVERIFY2(!captured->snapshot.document, "Geometry-only mask application retained a full image document snapshot.");
        QVERIFY(!captured->original); QVERIFY(!result->original); QVERIFY(result->mask);
        QVERIFY(!captured->selection.alpha.empty()); QCOMPARE(canvas.revision(), revision); QCOMPARE(revisions.count(), 0); QCOMPARE(files.count(), 0);
        QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes); QCOMPARE(iiFileProvider::native::File::read(native), saved);
    }
    void imageDependentMasksKeepTheirSource_data() {
        QTest::addColumn<QString>("operation");
        for (const auto op : {"brushMask", "luminanceRangeMask", "colorRangeMask", "refineMask"}) QTest::newRow(op) << QString(op);
    }
    void imageDependentMasksKeepTheirSource() {
        QFETCH(QString, operation); EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(picture(), "Image mask")); QVERIFY(canvas.selectLayer("layer"));
        canvas.configureTool("select", {}); QVERIFY(canvas.selectArea({QPointF(0, 0), QPointF(4, 8)}));
        QVariantMap parameters;
        if (operation == "brushMask") parameters = {{"points", QVariantList{QVariantMap{{"x", .2}, {"y", .2}}, QVariantMap{{"x", .8}, {"y", .8}}}}, {"radius", .3}};
        if (operation == "colorRangeMask") parameters = {{"samples", QVariantList{QVariantList{.3, .4, .5, 1.0}}}};
        if (operation == "refineMask") parameters["amount"] = 10;
        auto captured = std::make_shared<graphicImage::Input>(); auto result = std::make_shared<graphicImage::Result>();
        EditorGraphicCommand command([captured, result](const auto &input, std::stop_token stop) { *captured = input; auto out = graphicImage::calculate(input, stop); *result = out; return out; }, nullptr);
        command.setCanvas(&canvas); QVERIFY(command.request("mask-helper", "masking", operation, parameters)); QTRY_VERIFY(command.state()["valid"].toBool());
        QVERIFY(captured->snapshot.document); QVERIFY(result->original); QVERIFY(result->mask);
    }
    void dimensionMaskAdaptersPreserveResultsAndRejectPixelOnlyWork() {
        QImage source(16, 16, QImage::Format_ARGB32); source.fill(Qt::red); auto mask = levels();
        EditorGraphicAdjust::MaskInputs inputs; inputs.mask = mask.view(); inputs.base = mask.view(); inputs.operand = mask.view();
        for (const auto op : {"linearMask", "radialMask", "invertMask", "combineMasks"}) {
            EditorGraphicAdjust::MaskInputs use;
            if (QString(op) == "invertMask") use.mask = inputs.mask;
            if (QString(op) == "combineMasks") { use.base = inputs.base; use.operand = inputs.operand; }
            const auto pixels = EditorGraphicAdjust::makeMask(source, op, {}, use);
            const auto geometry = EditorGraphicAdjust::makeMask(QSize(16, 16), op, {}, use);
            QVERIFY2(geometry.ok(), qPrintable(geometry.error)); QVERIFY(pixels.ok()); QCOMPARE(geometry.coverage, pixels.coverage);
        }
        for (const auto op : {"brushMask", "luminanceRangeMask", "colorRangeMask", "refineMask"}) {
            const auto result = EditorGraphicAdjust::makeMask(QSize(16, 16), op, {}, {}); QVERIFY(!result.ok()); QVERIFY(result.error.contains("pixels"));
        }
        for (const auto size : {QSize(), QSize(0, 16), QSize(16, -1)}) QVERIFY(!EditorGraphicAdjust::makeMask(size, "linearMask", {}).ok());
        QVERIFY(!EditorGraphicAdjust::makeMask(QSize(16, 16), "linearMask", {{"typo", 1}}).ok());
        QVERIFY(!EditorGraphicAdjust::makeMask(QSize(16, 16), "missingMaskOperation", {}).ok());
        graphicImage::Input invalid; invalid.sourceGeometry = graphicImage::SourceGeometry{{8, 8}, {}}; invalid.operation = "refineMask";
        QVERIFY(!graphicImage::calculate(invalid).ok()); invalid.operation = "linearMask"; invalid.preview = true; QVERIFY(!graphicImage::calculate(invalid).ok());
        invalid.preview = false; invalid.sourceGeometry->extent.width = 0; QVERIFY(!graphicImage::calculate(invalid).ok());
    }
    void masksUseTheSampledFrameTransform_data() {
        QTest::addColumn<bool>("command"); QTest::addColumn<bool>("artboard");
        QTest::newRow("native-calculation") << false << false; QTest::newRow("accepted-command") << true << false;
        QTest::newRow("artboard-native-calculation") << false << true; QTest::newRow("artboard-accepted-command") << true << true;
    }
    void masksUseTheSampledFrameTransform() {
        QFETCH(bool, command); QFETCH(bool, artboard); auto document = picture(); document.extent = {16, 8}; document.timeline.frameCount = 3;
        if (artboard) { document.artboards = {{"board", "Board", {{72, 24}, {16, 8}}, 0}}; layerProperties(document.layers[0]).artboardId = "board"; }
        MotionValue end; end.position.x = 4;
        layerProperties(document.layers[0]).motion = {{0, {}}, {2, end}};
        graphicImage::Input input; input.snapshot = {std::make_shared<const Document>(document), 2, {}}; input.layer = "layer"; input.extent = document.extent; input.operation = "linearMask";
        if (artboard) { input.snapshot.artboardId = "board"; input.origin = {72, 24}; }
        graphicImage::Result result;
        if (command) {
            EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(document, "Animated mask")); QVERIFY(canvas.selectLayer("layer")); canvas.setFrame(2);
            auto output = std::make_shared<graphicImage::Result>(); EditorGraphicCommand task([output](const auto &value, std::stop_token stop) { auto r = graphicImage::calculate(value, stop); *output = r; return r; }, nullptr);
            task.setCanvas(&canvas); QVERIFY(task.request("mask-helper", "masking", "linearMask", {})); QTRY_VERIFY(task.state()["valid"].toBool()); result = *output;
        } else result = graphicImage::calculate(input);
        QVERIFY2(result.ok(), qPrintable(result.error)); const auto native = GA::linearMask(8, 8); QVERIFY(native);
        std::vector<std::uint8_t> expected(16 * 8, 0);
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) expected[y * 16 + 4 + x] = std::uint8_t(qRound(native->coverage[y * 8 + x] * 255));
        QCOMPARE(result.selection.alpha, expected);
    }
    void transformedSelectionAndCachedOperandKeepLayerCoordinates() {
        auto document = picture(); document.extent = {12, 12}; layerProperties(document.layers[0]).transform.translationX = 2; layerProperties(document.layers[0]).transform.translationY = 3;
        graphicImage::Input input; input.snapshot = {std::make_shared<const Document>(document), 0, {}}; input.layer = "layer"; input.extent = document.extent;
        input.selection = {{12, 12}, std::vector<std::uint8_t>(144, 0)};
        GA::Mask local{8, 8, std::vector<float>(64)};
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
            auto alpha = std::uint8_t((x + y * 8) * 4); input.selection.alpha[(y + 3) * 12 + x + 2] = alpha; local.coverage[y * 8 + x] = float(alpha / 255.0);
        }
        auto operand = std::make_shared<RasterMask>(RasterMask{{8, 8}, std::vector<std::uint8_t>(64, 127)}); input.operand = operand;
        GA::Mask nativeOperand{8, 8, std::vector<float>(64, float(127 / 255.0))}; const auto expected = GA::combineMasks(local.view(), nativeOperand.view(), GA::MaskCombine::Subtract); QVERIFY(expected);
        input.operation = "combineMasks"; input.parameters = {{"operation", "Subtract"}}; input.preview = true;
        const auto result = graphicImage::calculate(input); QVERIFY2(result.ok(), qPrintable(result.error));
        const auto url = result.preview["imageSource"].toUrl().toString(); const auto image = QImage::fromData(QByteArray::fromBase64(url.mid(url.indexOf(',') + 1).toLatin1()));
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) QCOMPARE(qGray(image.pixel(x, y)), qRound(expected->coverage[y * 8 + x] * 255));
        QCOMPARE(operand->alpha, std::vector<std::uint8_t>(64, 127));
    }
    void chunkedMaskCoordinatesMatchTheRawViewport_data() {
        QTest::addColumn<int>("originX"); QTest::addColumn<int>("originY"); QTest::addColumn<QString>("transform"); QTest::addColumn<bool>("command");
        for (bool command : {false, true}) {
            const auto suffix = command ? "-command" : "-direct";
            QTest::newRow(qPrintable(QString("negative") + suffix)) << -1 << -1 << QString("identity") << command;
            QTest::newRow(qPrintable(QString("positive") + suffix)) << 2 << 3 << QString("identity") << command;
            QTest::newRow(qPrintable(QString("translated") + suffix)) << -3 << -2 << QString("translation") << command;
            QTest::newRow(qPrintable(QString("rotated") + suffix)) << -2 << 3 << QString("rotation") << command;
            QTest::newRow(qPrintable(QString("motion") + suffix)) << 2 << -3 << QString("motion") << command;
        }
    }
    void chunkedMaskCoordinatesMatchTheRawViewport() {
        QFETCH(int, originX); QFETCH(int, originY); QFETCH(QString, transform); QFETCH(bool, command);
        Document d; d.canvasMode = CanvasMode::Infinite; d.extent = {8, 8}; d.infiniteCanvas.origin = {originX, originY}; d.infiniteCanvas.chunkSize = 32; d.timeline.frameCount = 3;
        ChunkedRasterAsset asset{"chunks", {}};
        for (int y : {-1, 0}) for (int x : {-1, 0}) asset.chunks.push_back({x, y, makeRasterLayer(32, 32, 0xff336699U)});
        d.assets.emplace_back(std::move(asset)); LayerProperties properties{"layer", "Chunks"};
        if (transform == "translation") { properties.transform.translationX = 2; properties.transform.translationY = 1; }
        if (transform == "rotation") { properties.transform.m11 = properties.transform.m22 = 0; properties.transform.m12 = 1; properties.transform.m21 = -1; properties.transform.translationX = originX + 8 + originY; properties.transform.translationY = originY - originX; }
        if (transform == "motion") { MotionValue end; end.position.x = 4; properties.motion = {{0, {}}, {2, end}}; }
        d.layers.emplace_back(StaticBitmapLayer{properties, StaticSource{"chunks"}}); QVERIFY(validate(d).ok());
        graphicImage::Input input; input.snapshot = {std::make_shared<const Document>(d), transform == "motion" ? 2U : 0U, {}}; input.layer = "layer"; input.extent = d.extent; input.origin = {qreal(originX), qreal(originY)}; input.operation = "linearMask";
        auto output = std::make_shared<graphicImage::Result>(); auto captured = std::make_shared<graphicImage::Input>();
        if (command) {
            EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(d, "Chunk mask")); QVERIFY(canvas.selectLayer("layer")); canvas.setFrame(input.snapshot.frame);
            const auto path = storage.filePath("Files/chunk-" + QString(QTest::currentDataTag()) + ".iisc"); QVERIFY(canvas.saveDocumentAs(QUrl::fromLocalFile(path)));
            const auto native = std::filesystem::path(path.toStdU16String()); const auto saved = iiFileProvider::native::File::read(native); const auto bytes = encodeIisc(*canvas.document()).bytes; const auto revision = canvas.revision();
            QSignalSpy files(&canvas, &EditorCanvas::fileInformationChanged); EditorGraphicCommand task([output, captured](const auto &value, std::stop_token stop) { *captured = value; auto r = graphicImage::calculate(value, stop); *output = r; return r; }, nullptr); task.setCanvas(&canvas);
            QVERIFY(task.request("mask-helper", "masking", "linearMask", {})); QTRY_VERIFY(task.state()["valid"].toBool());
            QVERIFY(!captured->snapshot.document); QVERIFY(!captured->original); QCOMPARE(canvas.revision(), revision); QCOMPARE(files.count(), 0); QCOMPARE(encodeIisc(*canvas.document()).bytes, bytes); QCOMPARE(iiFileProvider::native::File::read(native), saved);
        } else *output = graphicImage::calculate(input);
        QVERIFY2(output->ok(), qPrintable(output->error)); const auto mask = GA::linearMask(8, 8); QVERIFY(mask); std::vector<std::uint8_t> expected(64, 0);
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
            int sx = x, sy = y;
            if (transform == "translation") { sx -= 2; sy -= 1; }
            if (transform == "motion") sx -= 4;
            if (transform == "rotation") { sx = y; sy = 7 - x; }
            if (sx >= 0 && sy >= 0 && sx < 8 && sy < 8) expected[y * 8 + x] = std::uint8_t(qRound(mask->coverage[sy * 8 + sx] * 255));
        }
        QCOMPARE(output->selection.alpha, expected);
    }
    void chunkedPixelDependentMasksUseTheSameOrigin_data() {
        QTest::addColumn<QString>("operation"); QTest::newRow("refine") << QString("refineMask"); QTest::newRow("diagnostic") << QString("maskWhiteOnBlack");
    }
    void chunkedPixelDependentMasksUseTheSameOrigin() {
        Document d; d.canvasMode = CanvasMode::Infinite; d.extent = {8, 8}; d.infiniteCanvas.origin = {-1, -1}; d.infiniteCanvas.chunkSize = 32;
        d.assets.emplace_back(ChunkedRasterAsset{"chunks", {{-1, -1, makeRasterLayer(32, 32, 0xff336699U)}, {0, 0, makeRasterLayer(32, 32, 0xff336699U)}}}); d.layers.emplace_back(StaticBitmapLayer{{"layer", "Chunks"}, StaticSource{"chunks"}});
        QFETCH(QString, operation); graphicImage::Input input; input.snapshot = {std::make_shared<const Document>(d), 0, {}}; input.layer = "layer"; input.extent = d.extent; input.origin = {-1, -1}; input.operation = operation;
        input.selection = {{8, 8}, std::vector<std::uint8_t>(64, 0)}; for (int y = 0; y < 8; ++y) for (int x = 0; x < 4; ++x) input.selection.alpha[y * 8 + x] = 255;
        if (operation == "refineMask") input.parameters["amount"] = 0; else input.preview = true;
        const auto output = graphicImage::calculate(input); QVERIFY2(output.ok(), qPrintable(output.error)); QVERIFY(output.original);
        if (operation == "refineMask") QCOMPARE(output.selection.alpha, input.selection.alpha);
        else {
            QVERIFY(output.pixels); const auto raw = editorRaster::read(input.snapshot, input.layer); QVERIFY(raw);
            const auto native = GA::fromArgb32(8, 8, (*raw)->pixels); QVERIFY(native);
            GA::Mask mask{8, 8, {}}; for (auto alpha : input.selection.alpha) mask.coverage.push_back(float(alpha / 255.0));
            const auto expected = GA::maskWhiteOnBlack(native->view(), mask.view()); QVERIFY(expected); const auto words = GA::toArgb32(expected->view()); QVERIFY(words);
            QCOMPARE(output.pixels->pixels, *words);
        }
    }
};
QTEST_MAIN(TypedMaskTests)
#include "tst_typedmask.moc"
