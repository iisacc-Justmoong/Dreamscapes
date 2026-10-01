#include "App/Generation/AdvancedImageParameters.h"
#include "App/Generation/AdvancedImageOutput.h"
#include "App/Generation/AdvancedImageInputs.h"
#include <QJsonArray>
#include <QBuffer>
#include <QColorSpace>
#include <QImage>
#include <QJsonObject>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
#include <future>

class AdvancedParametersTests : public QObject {
    Q_OBJECT
private slots:
    void editableFigmaOptionsRemainDistinctFromEngineCapabilities()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/advanced-contract-XXXXXX"); QVERIFY(dir.isValid());
        AdvancedImageParameters state(dir.filePath("presets.json"));
        QVERIFY(state.updateParameters({{"prompt", "coastal house"}, {"transparentBackground", true},
            {"faceRestore", true}, {"watermark", true}, {"safetyFilter", "standard"}}));
        const auto first = state.addControlNet("Canny");
        const auto second = state.addControlNet("Pose");
        QVERIFY(!first.isEmpty()); QVERIFY(!second.isEmpty());
        QVERIFY(state.updateControlNet(first, {{"imageSource", "reference.png"}, {"model", "canny.safetensors"},
            {"ipAdapter", true}, {"weight", 0.3}}));
        QVERIFY(state.updateControlNet(second, {{"imageSource", "pose.png"}, {"model", "pose.safetensors"},
            {"regionalMask", true}, {"maskSource", "mask.png"}, {"weight", 0.7}}));
        QVERIFY(state.applyControlNet(first)); QVERIFY(state.applyControlNet(second));
        const auto expected = state.parameters();
        const auto entries = expected.value("controlNets").toList();
        QVERIFY(entries[0].toMap().value("ipAdapter").toBool());
        QVERIFY(!entries[0].toMap().value("regionalMask").toBool());
        QVERIFY(!entries[1].toMap().value("ipAdapter").toBool());
        QVERIFY(entries[1].toMap().value("regionalMask").toBool());
        QStringList rejected;
        for (const auto &issue : state.submissionIssues(false))
            rejected.append(issue.toMap().value("field").toString());
        QVERIFY(!rejected.contains("watermark"));
        for (const auto &field : QStringList{"transparentBackground", "faceRestore",
                "safetyFilter", "controlNets." + first + ".ipAdapterModel", "controlNets." + second + ".poseDetector"})
            QVERIFY2(rejected.contains(field), qPrintable(field));
        QCOMPARE(state.parameters(), expected); // Capability inspection must not change the editable draft.
        QVERIFY(state.savePreset("Full Figma draft"));
        state.reset();
        QVERIFY(state.loadPreset("Full Figma draft"));
        QCOMPARE(state.parameters(), expected); // Unsupported effects are saved, never silently dropped.
    }
    void referenceDecodingIsOrderedBoundedAndAtomic()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/advanced-inputs-XXXXXX"); QVERIFY(dir.isValid());
        QImage first(3, 2, QImage::Format_RGB888); first.fill(QColor(11, 12, 13));
        first.setPixelColor(2, 1, QColor(21, 22, 23));
        QImage second(4096, 16, QImage::Format_RGB32); second.fill(QColor(31, 32, 33));
        QVERIFY(first.save(dir.filePath("first.png"))); QVERIFY(second.save(dir.filePath("second.png")));
        const auto firstPath = QFileInfo(dir.filePath("first.png")).canonicalFilePath();
        const auto secondPath = QFileInfo(dir.filePath("second.png")).canonicalFilePath();
        iiLocalDiffusion::NativeAdvancedControls controls;
        std::atomic_bool cancelled{false}; QString error;
        QVERIFY2(dreamscapes::decodeAdvancedReferences({firstPath, secondPath}, &controls, &error, cancelled), qPrintable(error));
        QCOMPARE(controls.references.size(), size_t(2));
        QCOMPARE(controls.references[0].width, 3); QCOMPARE(controls.references[0].height, 2);
        QCOMPARE(controls.references[0].rgb.size(), size_t(18));
        QCOMPARE(controls.references[0].rgb[0], uint8_t(11));
        QCOMPARE(controls.references[0].rgb[15], uint8_t(21));
        QCOMPARE(controls.references[1].width, 2048); QCOMPARE(controls.references[1].height, 8);
        QCOMPARE(controls.references[1].rgb[0], uint8_t(31));
        for (const QString &invalid : {dir.filePath("missing.png"), QString("https://example.invalid/image.png")}) {
            QVERIFY(!dreamscapes::decodeAdvancedReferences({firstPath, invalid}, &controls, &error, cancelled));
            QCOMPARE(controls.references.size(), size_t(2)); // No partially decoded replacement.
            QCOMPARE(controls.references[1].rgb[0], uint8_t(31));
        }
        cancelled = true;
        QVERIFY(!dreamscapes::decodeAdvancedReferences({firstPath}, &controls, &error, cancelled));
        QCOMPARE(controls.references.size(), size_t(2));
        cancelled = false;
        QVERIFY(dreamscapes::decodeAdvancedReferences({}, &controls, &error, cancelled));
        QVERIFY(controls.references.empty());
    }
    void referenceLoadingParksAndCancelsWithTheRuntime()
    {
        auto control = std::make_shared<iiLocalDiffusion::NativeExecutionControl>();
        control->setPaused(true);
        std::atomic_bool cancelled{false};
        auto pending = std::async(std::launch::async, [&] {
            iiLocalDiffusion::NativeAdvancedControls controls; QString error;
            return dreamscapes::decodeAdvancedReferences({}, &controls, &error, cancelled, control);
        });
        // Always release the worker before asserting, including on a slow runner.
        QElapsedTimer timer; timer.start();
        while (!control->isWaiting() && timer.elapsed() < 3000) QTest::qWait(5);
        const bool parked = control->isWaiting();
        cancelled = true;
        const bool succeeded = pending.get();
        QVERIFY(parked); QVERIFY(!succeeded);
        cancelled = false; control->setPaused(false);
        iiLocalDiffusion::NativeAdvancedControls controls; QString error;
        QVERIFY(dreamscapes::decodeAdvancedReferences({}, &controls, &error, cancelled, control));
    }
    void controlImageDecodingHonorsApplyAndCommitsAtomically()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/control-inputs-XXXXXX"); QVERIFY(dir.isValid());
        QImage image(3, 2, QImage::Format_RGB888); image.fill(QColor(21, 22, 23));
        QVERIFY(image.save(dir.filePath("control.png")));
        const auto path = QFileInfo(dir.filePath("control.png")).canonicalFilePath();
        const QJsonObject applied{{"imageSource", path}, {"model", "/local/control.safetensors"},
            {"process", "Tile"}, {"weight", 1.25}, {"applied", true}};
        const QJsonObject draft{{"imageSource", "missing.png"}, {"applied", false}};
        iiLocalDiffusion::NativeAdvancedControls controls;
        std::atomic_bool cancelled{false}; QString error;
        QVERIFY(dreamscapes::decodeAdvancedControlImages({draft, applied}, &controls, &error, cancelled));
        QCOMPARE(controls.controls.size(), size_t(1));
        QCOMPARE(controls.controls.front().weight, 1.25f);
        QCOMPARE(controls.controls.front().process, std::string("Tile"));
        QCOMPARE(controls.controls.front().image.rgb.size(), size_t(18));
        QCOMPARE(controls.controls.front().image.rgb.front(), uint8_t(21));
        QImage mask(2, 1, QImage::Format_RGBA8888);
        mask.setPixelColor(0, 0, QColor(255, 255, 255, 0));
        mask.setPixelColor(1, 0, QColor(255, 255, 255, 128));
        QVERIFY(mask.save(dir.filePath("mask.png")));
        auto regional = applied;
        regional["regionalMask"] = true;
        regional["maskSource"] = QFileInfo(dir.filePath("mask.png")).canonicalFilePath();
        QVERIFY(dreamscapes::decodeAdvancedControlImages({regional}, &controls, &error, cancelled));
        QCOMPARE(controls.controls.front().mask.width, 2);
        QCOMPARE(controls.controls.front().mask.rgb, std::vector<uint8_t>({0, 0, 0, 128, 128, 128}));
        regional["maskSource"] = dir.filePath("missing-mask.png");
        QVERIFY(!dreamscapes::decodeAdvancedControlImages({regional}, &controls, &error, cancelled));
        QCOMPARE(controls.controls.front().mask.rgb, std::vector<uint8_t>({0, 0, 0, 128, 128, 128}));
        regional["regionalMask"] = false;
        QVERIFY(dreamscapes::decodeAdvancedControlImages({regional}, &controls, &error, cancelled));
        QVERIFY(controls.controls.front().mask.rgb.empty());
        regional["regionalMask"] = true;
        regional["maskSource"] = QFileInfo(dir.filePath("mask.png")).canonicalFilePath();
        regional["weight"] = 0.5;
        regional["process"] = "Canny";
        regional["model"] = "/local/second-control.safetensors";
        QVERIFY(dreamscapes::decodeAdvancedControlImages({applied, draft, regional}, &controls, &error, cancelled));
        QCOMPARE(controls.controls.size(), size_t(2));
        QVERIFY(controls.controls.front().mask.rgb.empty());
        QCOMPARE(controls.controls.back().mask.rgb, std::vector<uint8_t>({0, 0, 0, 128, 128, 128}));
        QCOMPARE(controls.controls.back().weight, 0.5f);
        QCOMPARE(controls.controls.back().process, std::string("Canny"));
        QCOMPARE(controls.controls.back().model.string(), std::string("/local/second-control.safetensors"));
        auto missing = applied; missing["imageSource"] = "missing.png";
        QVERIFY(!dreamscapes::decodeAdvancedControlImages({applied, missing}, &controls, &error, cancelled));
        QCOMPARE(controls.controls.size(), size_t(2));
        QCOMPARE(controls.controls.front().image.rgb.front(), uint8_t(21));
        regional["maskSource"] = "missing-mask.png";
        QVERIFY(!dreamscapes::decodeAdvancedControlImages({applied, regional}, &controls, &error, cancelled));
        QCOMPARE(controls.controls.size(), size_t(2));
        QCOMPARE(controls.controls.back().mask.rgb, std::vector<uint8_t>({0, 0, 0, 128, 128, 128}));
        QJsonArray excess;
        for (int i = 0; i < 65; ++i) excess.append(applied);
        QVERIFY(!dreamscapes::decodeAdvancedControlImages(excess, &controls, &error, cancelled));
        QCOMPARE(controls.controls.size(), size_t(2));
        cancelled = true;
        QVERIFY(!dreamscapes::decodeAdvancedControlImages({applied}, &controls, &error, cancelled));
        QCOMPARE(controls.controls.size(), size_t(2));
        cancelled = false;
        QVERIFY(dreamscapes::decodeAdvancedControlImages({draft}, &controls, &error, cancelled));
        QVERIFY(controls.controls.empty());
    }
    void ipAdapterDecodingIsIndependentAndAtomic()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/ip-inputs-XXXXXX"); QVERIFY(dir.isValid());
        QImage image(2, 1, QImage::Format_RGB888); image.fill(QColor(12, 34, 56));
        QVERIFY(image.save(dir.filePath("reference.png")));
        const auto path = QFileInfo(dir.filePath("reference.png")).canonicalFilePath();
        QJsonObject both{{"imageSource", path}, {"model", "/local/control.safetensors"}, {"process", "Canny"},
            {"applied", true}, {"ipAdapter", true}, {"ipAdapterModel", "/local/adapter.safetensors"},
            {"ipAdapterVision", "/local/vision.safetensors"}, {"weight", 0.3}, {"regionalMask", true}, {"maskSource", path}};
        auto ipOnly = both; ipOnly["process"] = "IP-Adapter"; ipOnly["model"] = "unused-missing-control";
        ipOnly["ipAdapterModel"] = "/local/second.safetensors"; ipOnly["weight"] = 0.7;
        ipOnly["regionalMask"] = false; ipOnly["maskSource"] = "unused-missing-mask";
        iiLocalDiffusion::NativeAdvancedControls controls;
        std::atomic_bool cancelled{false}; QString error;
        QVERIFY2(dreamscapes::decodeAdvancedControlImages({both, ipOnly}, &controls, &error, cancelled), qPrintable(error));
        QCOMPARE(controls.controls.size(), size_t(1)); QCOMPARE(controls.ipAdapters.size(), size_t(2));
        QCOMPARE(controls.ipAdapters.front().image.rgb, controls.controls.front().image.rgb);
        QCOMPARE(controls.ipAdapters.front().image.rgb.front(), uint8_t(12)); // No Canny image substitution.
        QCOMPARE(controls.ipAdapters.front().mask.rgb, controls.controls.front().mask.rgb);
        QCOMPARE(controls.ipAdapters.back().model.string(), std::string("/local/second.safetensors"));
        QCOMPARE(controls.ipAdapters.back().vision.string(), std::string("/local/vision.safetensors"));
        QCOMPARE(controls.ipAdapters.back().weight, .7f); QVERIFY(controls.ipAdapters.back().mask.rgb.empty());
        auto broken = ipOnly; broken["imageSource"] = "missing.png";
        QVERIFY(!dreamscapes::decodeAdvancedControlImages({both, broken}, &controls, &error, cancelled));
        QCOMPARE(controls.ipAdapters.size(), size_t(2)); QCOMPARE(controls.controls.size(), size_t(1));
        cancelled = true;
        QVERIFY(!dreamscapes::decodeAdvancedControlImages({}, &controls, &error, cancelled));
        QCOMPARE(controls.ipAdapters.size(), size_t(2));
        cancelled = false; both["ipAdapter"] = false;
        QVERIFY(dreamscapes::decodeAdvancedControlImages({both}, &controls, &error, cancelled));
        QVERIFY(controls.ipAdapters.empty()); QCOMPARE(controls.controls.size(), size_t(1));
        QVERIFY(dreamscapes::decodeAdvancedControlImages({}, &controls, &error, cancelled));
        QVERIFY(controls.controls.empty());
    }
    void watermarkChangesOnlyFinalPixelsAndRetainsOutputContracts()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/watermark-output-XXXXXX"); QVERIFY(dir.isValid());
        QImage original(256, 128, QImage::Format_RGBA8888); original.fill(QColor(60, 80, 100, 255));
        original.setColorSpace(QColorSpace::SRgb); original.setText("Original", "private metadata");
        const auto path = dir.filePath("source.png"); QVERIFY(original.save(path));
        QFile sourceFile(path); QVERIFY(sourceFile.open(QIODevice::ReadOnly));
        const auto sourceBytes = sourceFile.readAll(); sourceFile.close();
        auto render = [&](bool enabled, bool metadata, const QString &profile) {
            QByteArray bytes; QBuffer out(&bytes); out.open(QIODevice::WriteOnly); QString error;
            const bool ok = dreamscapes::writeAdvancedImage(path, &out,
                {{"watermark", enabled}, {"preserveMetadata", metadata}, {"colorProfile", profile}}, 42, &error);
            if (!ok) QTest::qFail(qPrintable(error), __FILE__, __LINE__);
            return QImage::fromData(bytes, "PNG");
        };
        const auto plain = render(false, false, "sRGB");
        const auto marked = render(true, false, "sRGB");
        QVERIFY(!marked.isNull()); QCOMPARE(marked.size(), original.size());
        QCOMPARE(marked.colorSpace(), QColorSpace(QColorSpace::SRgb));
        QVERIFY(marked.textKeys().isEmpty());
        int changed = 0;
        const QRect region(246, 118, 8, 8); // short side / 16, short side / 64 margin.
        for (int y = 0; y < marked.height(); ++y) for (int x = 0; x < marked.width(); ++x) {
            QCOMPARE(plain.pixelColor(x, y), original.pixelColor(x, y));
            if (marked.pixelColor(x, y) != plain.pixelColor(x, y)) { QVERIFY(region.contains(x, y)); ++changed; }
        }
        QCOMPARE(changed, 64);
        const auto p3 = render(true, true, "Display P3");
        QCOMPARE(p3.colorSpace(), QColorSpace(QColorSpace::DisplayP3));
        QCOMPARE(p3.text("Original"), QString("private metadata"));
        const auto recipe = QJsonDocument::fromJson(p3.text("Dreamscapes.Parameters").toUtf8()).object();
        QVERIFY(recipe.value("watermark").toBool()); QCOMPARE(recipe.value("resolvedSeed").toInt(), 42);
        QVERIFY(sourceFile.open(QIODevice::ReadOnly));
        QCOMPARE(sourceFile.readAll(), sourceBytes); // Publication never rewrites engine output.
        const auto capture = qEnvironmentVariable("DREAMSCAPES_WATERMARK_CAPTURE");
        if (!capture.isEmpty()) QVERIFY(marked.save(capture));
    }
    void outputMetadataAndColorProfile()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/advanced-XXXXXX"); QVERIFY(dir.isValid());
        QImage input(16, 16, QImage::Format_RGB32); input.fill(Qt::red);
        input.setText("Original", "retained when requested");
        const auto source = dir.filePath("source.png"); QVERIFY(input.save(source));
        QJsonObject parameters{{"colorProfile", "Display P3"}, {"preserveMetadata", true}, {"prompt", "coast"}, {"seed", -1}};
        QByteArray bytes; QBuffer output(&bytes); QVERIFY(output.open(QIODevice::WriteOnly)); QString error;
        QVERIFY2(dreamscapes::writeAdvancedImage(source, &output, parameters, 123, &error), qPrintable(error));
        auto restored = QImage::fromData(bytes, "PNG"); QVERIFY(!restored.isNull());
        QCOMPARE(restored.size(), input.size()); QCOMPARE(restored.colorSpace(), QColorSpace(QColorSpace::DisplayP3));
        QCOMPARE(restored.text("Original"), QString("retained when requested"));
        const auto recipe = QJsonDocument::fromJson(restored.text("Dreamscapes.Parameters").toUtf8()).object();
        QCOMPARE(recipe.value("resolvedSeed").toInteger(), qint64(123));
        parameters["preserveMetadata"] = false; parameters["colorProfile"] = "sRGB";
        bytes.clear(); output.seek(0);
        QVERIFY(dreamscapes::writeAdvancedImage(source, &output, parameters, 123, &error));
        restored = QImage::fromData(bytes, "PNG");
        QVERIFY(restored.textKeys().isEmpty()); QCOMPARE(restored.colorSpace(), QColorSpace(QColorSpace::SRgb));
    }
    void atomicEditingAndReset()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/advanced-XXXXXX"); QVERIFY(dir.isValid());
        AdvancedImageParameters state(dir.filePath("presets.json"));
        const auto initial = state.parameters();
        QSignalSpy changes(&state, &AdvancedImageParameters::parametersChanged);
        QVERIFY(state.updateParameters({{"prompt", "coastal house"}, {"width", 1536}, {"negativePrompt", "noise"}}));
        QCOMPARE(changes.size(), 1);
        const auto edited = state.parameters();
        QVERIFY(!state.updateParameters({{"height", 1001}, {"prompt", "must not leak"}}));
        QCOMPARE(state.parameters(), edited); QCOMPARE(changes.size(), 1);
        QVERIFY(!state.updateParameters({{"steps", true}}));
        QVERIFY(!state.updateParameters({{"steps", "30"}}));
        QVERIFY(!state.updateParameters({{"seed", 4294967295.0}}));
        QVERIFY(!state.updateParameters({{"unknown", false}}));
        QVERIFY(state.updateParameters({{"seed", 4294967295.0}, {"outputCount", 1}}));
        state.reset(); QCOMPARE(state.parameters(), initial);
        QVERIFY(!state.submissionIssues().isEmpty());
        QVERIFY(state.updateParameters({{"prompt", "coast"}}));
        QVERIFY(state.submissionIssues().isEmpty());
        QVERIFY(state.updateParameters({{"freeU", true}}));
        QVERIFY(!state.submissionIssues().isEmpty());
        QCOMPARE(state.schema().size(), int(iiLocalDiffusion::imageParameterSpecs().size()));
    }
    void collectionsAndDraftApply()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/advanced-XXXXXX"); QVERIFY(dir.isValid());
        AdvancedImageParameters state(dir.filePath("presets.json"));
        for (int i = 0; i < 20; ++i) QVERIFY(state.addReferenceImage(QString("image-%1.png").arg(i)));
        QVERIFY(!state.addReferenceImage("overflow.png"));
        QVERIFY(!state.removeReferenceImage(20)); QVERIFY(state.removeReferenceImage(0));
        const auto id = state.addControlNet("Pose"); QVERIFY(!id.isEmpty());
        QVERIFY(!state.applyControlNet(id));
        QVERIFY(state.updateControlNet(id, {{"imageSource", "pose.png"}, {"model", "pose.safetensors"}, {"weight", 0.6}}));
        QVERIFY(state.applyControlNet(id));
        QVERIFY(state.parameters().value("controlNets").toList()[0].toMap().value("applied").toBool());
        QVERIFY(state.updateControlNet(id, {{"weight", 0.7}}));
        QVERIFY(!state.parameters().value("controlNets").toList()[0].toMap().value("applied").toBool());
        QVERIFY(!state.updateControlNet(id, {{"id", "changed"}}));
        QVERIFY(state.resetControlNet(id)); QVERIFY(state.removeControlNet(id));
        QVERIFY(!state.removeControlNet(id));
        const auto lora = state.addLora("style.safetensors", "Style"); QVERIFY(!lora.isEmpty());
        QVERIFY(state.updateLora(lora, {{"weight", -0.5}}));
        QVERIFY(!state.updateLora(lora, {{"weight", std::numeric_limits<double>::infinity()}}));
        QVERIFY(state.removeLora(lora));
    }
    void unchangedControlEditsPreserveApplyAndSignals()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/advanced-noop-XXXXXX"); QVERIFY(dir.isValid());
        AdvancedImageParameters state(dir.filePath("presets.json"));
        const auto id = state.addControlNet("Canny"); QVERIFY(!id.isEmpty());
        QVERIFY(state.updateControlNet(id, {{"imageSource", "hint.png"}, {"model", "canny.safetensors"}}));
        QVERIFY(state.applyControlNet(id));
        const auto applied = state.parameters();
        QSignalSpy changes(&state, &AdvancedImageParameters::parametersChanged);
        QVERIFY(state.updateControlNet(id, {}));
        QVERIFY(state.updateControlNet(id, {{"weight", 1}, {"process", "Canny"}, {"regionalMask", false}}));
        QCOMPARE(state.parameters(), applied);
        QCOMPARE(changes.size(), 0);
        QVERIFY(!state.updateControlNet(id, {{"weight", 0.5}, {"unknown", true}}));
        QVERIFY(!state.updateControlNet(id, {{"applied", false}}));
        QVERIFY(!state.updateControlNet(id, {{"weight", true}}));
        QVERIFY(!state.updateControlNet(id, {{"regionalMask", 0}}));
        QCOMPARE(state.parameters(), applied);
        QCOMPARE(changes.size(), 0);
        QVERIFY(!state.errorString().isEmpty());
        QVERIFY(state.updateControlNet(id, {}));
        QVERIFY(state.errorString().isEmpty());
        QCOMPARE(changes.size(), 0);
        QVERIFY(state.updateControlNet(id, {{"weight", 0.5}}));
        QCOMPARE(changes.size(), 1);
        const auto draft = state.parameters().value("controlNets").toList().front().toMap();
        QCOMPARE(draft.value("weight").toDouble(), 0.5);
        QVERIFY(!draft.value("applied").toBool());
        QVERIFY(!state.updateControlNet("missing", {}));
        QCOMPARE(changes.size(), 1);
        QVERIFY(state.applyControlNet(id));
        QVERIFY(state.updateControlNet(id, {{"imageSource", ""}}));
        const auto emptySource = state.parameters().value("controlNets").toList().front().toMap();
        QVERIFY(emptySource.value("imageSource").toString().isEmpty());
        QVERIFY(!emptySource.value("applied").toBool());
        QVERIFY(!state.applyControlNet(id));
    }
    void presetsRoundTripAndConcurrentWriters()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/advanced-XXXXXX"); QVERIFY(dir.isValid());
        const auto file = dir.filePath("nested/presets.json");
        AdvancedImageParameters first(file), second(file);
        QVariantMap allFields;
        for (const auto &value : first.schema()) {
            const auto spec = value.toMap(); const auto key = spec.value("key").toString();
            const auto type = spec.value("type").toString(); const auto initial = spec.value("defaultValue");
            if (type == "bool") allFields[key] = !initial.toBool();
            else if (type == "integer") allFields[key] = initial.toLongLong() + ((key == "width" || key == "height") ? 8 : 1);
            else if (type == "number") allFields[key] = initial.toDouble() + 0.01;
            else {
                const auto choices = spec.value("choices").toStringList();
                allFields[key] = choices.isEmpty() ? QString("edited-%1").arg(key) : choices.last();
            }
        }
        QVERIFY2(first.updateParameters(allFields), qPrintable(first.errorString()));
        QVERIFY(first.updateParameters({{"prompt", "house"}, {"cfgScale", 5.5}, {"preserveMetadata", false}}));
        QVERIFY(!first.addLora("style.safetensors").isEmpty());
        QVERIFY(first.addReferenceImage("file:///image.png"));
        const auto control = first.addControlNet("Canny"); QVERIFY(!control.isEmpty());
        QVERIFY(first.updateControlNet(control, {{"model", "canny.safetensors"}, {"imageSource", "canny.png"},
            {"regionalMask", true}, {"maskSource", "mask.png"}, {"ipAdapter", true}, {"weight", 0.6},
            {"ipAdapterModel", "ip.safetensors"}, {"ipAdapterVision", "vision.safetensors"},
            {"poseDetector", "detector.onnx"}, {"poseModel", "pose.onnx"}}));
        QVERIFY(first.applyControlNet(control));
        const auto expected = first.parameters();
        QVERIFY(first.savePreset(" Portrait "));
        QVERIFY(second.savePreset("Other"));
        AdvancedImageParameters loaded(file);
        QCOMPARE(loaded.presets(), QStringList({"Other", "Portrait"}));
        QVERIFY(loaded.loadPreset("Portrait")); QCOMPARE(loaded.parameters(), expected);
        QCOMPARE(loaded.parameters().value("upscalerModel").toString(), QString("edited-upscalerModel"));
        QCOMPARE(loaded.parameters().value("detailerModel").toString(), QString("edited-detailerModel"));
        QCOMPARE(loaded.parameters().value("refinerModel").toString(), QString("edited-refinerModel"));
        QVERIFY(loaded.removePreset("Other"));
        QVERIFY(!loaded.loadPreset("missing")); QCOMPARE(loaded.parameters(), expected);
        QFile corrupt(file); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("bad json"); corrupt.close();
        QVERIFY(!loaded.savePreset("Must not overwrite corruption"));
        QVERIFY(!loaded.loadPreset("Portrait")); QCOMPARE(loaded.parameters(), expected);
        QVERIFY(corrupt.open(QIODevice::ReadOnly)); QCOMPARE(corrupt.readAll(), QByteArray("bad json"));
    }
    void presetWriteFailureDoesNotPublish()
    {
        QTemporaryDir dir(DREAMSCAPES_TEST_DIRECTORY "/advanced-XXXXXX"); QVERIFY(dir.isValid());
        QFile blocker(dir.filePath("not-a-directory")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        AdvancedImageParameters state(blocker.fileName() + "/presets.json");
        QVERIFY(!state.savePreset("Test")); QVERIFY(state.presets().isEmpty());
    }
};
QTEST_GUILESS_MAIN(AdvancedParametersTests)
#include "tst_advancedparameters.moc"
