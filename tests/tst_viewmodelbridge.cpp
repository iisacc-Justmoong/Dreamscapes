#include "App/ViewModels/GenerationProjection.h"
#include "App/ViewModels/VideoTimelineModel.h"
#include "App/ViewModels/ViewData.h"
#include <QFile>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QtTest>
#include <limits>

using namespace dreamscapes;
class NativeViewModelBridgeTests : public QObject {
    Q_OBJECT
    QJSEngine scripts;
    QJSValue legacy(QString file, QString exports) {
        QFile source(QStringLiteral(DREAMSCAPES_TEST_SOURCE_DIR "/tests/fixtures/legacy-viewmodels/") + file);
        if (!source.open(QIODevice::ReadOnly))
            return {};
        QString text = QString::fromUtf8(source.readAll());
        text.remove(QRegularExpression("^\\.(?:pragma|import)[^\\n]*", QRegularExpression::MultilineOption));
        return scripts.evaluate("(function(){" + text + ";return {" + exports + "};})()", file);
    }
    static QByteArray json(const QVariant &value) {
        return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
    }
  private slots:
    void initTestCase() {
        registerNativeViewModelTypes();
    }
    void nativeSingletonBindings() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQml\nimport Dreamscapes.Storage 1.0\nQtObject { property var result: "
                          "NativeData.tools.defaults(NativeData.tools.tool(\"elements\")); property var graphic: "
                          "NativeData.graphics.operationDefaults(\"developColor\") }",
                          {});
        QScopedPointer<QObject> item(component.create());
        QVERIFY2(item, qPrintable(component.errorString()));
        QCOMPARE(item->property("result").value<QJSValue>().toVariant().toMap().value("selector").toString(),
                 QStringLiteral("Rectangle"));
        QVERIFY(!item->property("graphic").value<QJSValue>().toVariant().toMap().isEmpty());
    }
    void nativeCommandsAreCallableFromQml() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQml\nimport Dreamscapes.Storage 1.0\nNativeGenerationProjection { jobs: [{id: "
                          "\"one\", state: \"queued\"}]; property int queuedCount: countStates([\"queued\"]) }",
                          {});
        QScopedPointer<QObject> item(component.create());
        QVERIFY2(item, qPrintable(component.errorString()));
        QCOMPARE(item->property("queuedCount").toInt(), 1);
    }
    void nativeProjectionPreservesOpaquePayloadTypes() {
        const QVariantMap result{{"id", "result"},
                                 {"imageSource", QUrl("file:///original.png")},
                                 {"seed", 9876},
                                 {"opaque", QVariantMap{{"url", QUrl("file:///reference.png")}, {"count", 42}}}};
        GenerationProjection projection;
        projection.setResults({result});
        projection.setRequestIds({"result"});
        QCOMPARE(projection.submissionResults(), QVariantList{result});
        QCOMPARE(projection.reviewResults(), QVariantList{result});
        QCOMPARE(projection.selectedResult(), result);
        projection.setPreviewStep(1);
        QCOMPARE(projection.reviewResults(), QVariantList{result});
        ViewData data;
        QCOMPARE(data.elements()->shapes().size(), 4);
    }
    void invalidTimelineCommandsRetainTheNativeValidationContract() {
        VideoTimelineModel model;
        model.reset(120, 24);
        const auto before = model.snapshot();
        QVERIFY(model.addTrack("Camera", "Unsupported").isEmpty());
        QVERIFY(!model.resizeComposition(24, std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!model.resizeComposition(24, std::numeric_limits<double>::infinity()));
        QVERIFY(!model.resizeComposition(24, -2));
        QCOMPARE(model.snapshot(), before);
        QCOMPARE(model.undoDepth(), std::size_t(0));
        QCOMPARE(model.redoDepth(), std::size_t(0));
        QVERIFY(!model.error().isEmpty());
        QVERIFY(model.resizeComposition(240));
        QCOMPARE(model.fps(), 24.);
        QCOMPARE(model.totalFrames(), 240);
    }
    void homeProjectionsPreserveIdentityAndOpaqueMetadata() {
        ViewData data;
        const QVariantMap first{
            {"key", "one"}, {"name", "one.png"}, {"isImage", true}, {"source", QUrl("file:///one.png")}, {"seed", 42}};
        const QVariantMap second{
            {"key", "two"}, {"name", "two.mp4"}, {"mediaType", "Video"}, {"source", QUrl("file:///two.mp4")}};
        const QVariantList files{first, second};
        QCOMPARE(data.facts()->filesOfKind(files, "All types"), files);
        QCOMPARE(data.facts()->filesOfKind(files, "Image"), QVariantList{first});
        QCOMPARE(data.facts()->fileForKey(files, "two"), second);
        const QVariantMap keyed{{"id", "identity"}, {"key", "download-key"}, {"source", QUrl("file:///keyed.png")}};
        QCOMPARE(data.facts()->fileForKey({keyed}, "identity"), keyed);
        QCOMPARE(data.facts()->fileForKey({keyed}, "download-key", "key"), keyed);
        QCOMPARE(data.facts()->fileIndex(files, "missing"), -1);
        QCOMPARE(data.facts()->reconciledFileKey(files, "two"), QString("two"));
        QCOMPARE(data.facts()->reconciledFileKey(files, "missing"), QString("one"));
        QCOMPARE(data.facts()->reconciledFileKey({}, "one"), QString{});
        const QVariantMap patch{{"source", QUrl("file:///patched.png")}, {"seed", 7}};
        auto expected = first;
        for (auto it = patch.begin(); it != patch.end(); ++it)
            expected[it.key()] = it.value();
        QCOMPARE(data.facts()->merged(first, patch), expected);
        const QVariantList jobs{QVariantMap{{"state", "running"}}, QVariantMap{{"state", "queued"}},
                                QVariantMap{{"state", "failed"}, {"error", "original error"}}};
        QCOMPARE(data.facts()->stateCount(jobs, {"queued", "running"}), 2);
        QCOMPARE(data.facts()->firstState(jobs, "failed").value("error").toString(), QString("original error"));
        QVERIFY(data.facts()->firstState(jobs, "completed").isEmpty());
        QVERIFY(data.file()->containsView("information"));
        QVERIFY(!data.file()->containsView("missing"));
        QCOMPARE(data.file()->sectionRowCount(data.file()->view("information")), 108);
    }
    void allCatalogParametersPreserved() {
        scripts.globalObject().setProperty("TextDefinitions",
                                           legacy("EditorTextDefinitions.js", "definition:definition"));
        scripts.globalObject().setProperty("PhotoDefinitions",
                                           legacy("EditorCameraPhotoDefinitions.js", "definition:definition"));
        scripts.globalObject().setProperty("FileDefinitions",
                                           legacy("EditorFileDefinitions.js", "definition:definition"));
        auto tools = legacy("EditorToolDefinitions.js",
                            "tools:tools,defaults:defaults,runtimeDefinition:runtimeDefinition,rows:rows");
        QVERIFY2(!tools.isError(), qPrintable(tools.toString()));
        ViewData data;
        const auto entries = tools.property("tools");
        const auto count = entries.property("length").toInt();
        QVERIFY(count >= 18);
        for (int i = 0; i < count; ++i) {
            const auto definition = entries.property(i);
            const auto key = definition.property("key").toString();
            const auto native = data.tools()->tool(key);
            QCOMPARE(json(native), json(definition.toVariant()));
            QCOMPARE(json(data.tools()->defaults(native)),
                     json(tools.property("defaults").call({definition}).toVariant()));
            QCOMPARE(json(data.tools()->runtimeDefinition(native)),
                     json(tools.property("runtimeDefinition").call({definition}).toVariant()));
            for (const bool compact : {true, false})
                QCOMPARE(json(data.tools()->rows(native, compact)),
                         json(tools.property("rows").call({definition, QJSValue(compact)}).toVariant()));
        }
        auto graphics = legacy("EditorGraphicDefinitions.js",
                               "catalog:catalog,defaultsFor:defaultsFor,operationDefaults:operationDefaults");
        QVERIFY2(!graphics.isError(), qPrintable(graphics.toString()));
        const auto catalog = graphics.property("catalog").call();
        QCOMPARE(json(data.graphics()->catalog()), json(catalog.toVariant()));
        const auto panels = catalog.property("panels");
        QCOMPARE(panels.property("length").toInt(), 381);
        for (int i = 0; i < 381; ++i) {
            const auto panel = panels.property(i), id = panel.property("id"), operation = panel.property("operation");
            QCOMPARE(json(data.graphics()->defaultsFor(id.toString())),
                     json(graphics.property("defaultsFor").call({id}).toVariant()));
            QCOMPARE(json(data.graphics()->operationDefaults(operation.toString())),
                     json(graphics.property("operationDefaults").call({operation}).toVariant()));
        }
    }
    void graphicPathsAndNumericInputParity() {
        ViewData data;
        auto old = legacy(
            "EditorGraphicState.js",
            "nested:nested,flatten:flatten,parseNumber:parseNumber,numberText:numberText,resolvedPath:resolvedPath");
        const QVariantMap defaults{{"curves", QVariantList{QVariantMap{{"x", 0}, {"y", 1}}}}, {"amount", 1}};
        const QVariantMap patch{{"curves[0].y", .5}, {"color.primary[1]", 32}};
        QCOMPARE(json(data.graphicState()->nested(defaults, patch)),
                 json(old.property("nested")
                          .call({scripts.toScriptValue(defaults), scripts.toScriptValue(patch)})
                          .toVariant()));
        const QVariantMap row{{"min", -100}, {"max", 100}, {"unit", "%"}, {"step", .1}};
        for (auto input : {"0%", "100%", "-100%", "1.25%", "1e1%", "-101%", "1x", "", "NaN", "infinity", "5 2"}) {
            const auto expected = old.property("parseNumber").call({scripts.toScriptValue(row), QJSValue(input)});
            const auto actual = data.graphicState()->parseNumber(row, input);
            QCOMPARE(actual.isValid(), !expected.isUndefined());
            if (actual.isValid())
                QCOMPARE(actual.toDouble(), expected.toNumber());
        }
    }
    void nativeTimelineMatchesLegacyCommands() {
        QQmlEngine engine;
        QFile file(
            QStringLiteral(DREAMSCAPES_TEST_SOURCE_DIR "/tests/fixtures/legacy-viewmodels/VideoTimelineState.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QQmlComponent component(&engine);
        component.setData(file.readAll(), {});
        QScopedPointer<QObject> item(component.create());
        QVERIFY2(item, qPrintable(component.errorString()));
        auto old = engine.newQObject(item.data());
        VideoTimelineModel model;
        auto native = engine.newQObject(&model);
        const auto compare = [&] {
            QCOMPARE(json(model.snapshot()), json(old.property("snapshot").call().toVariant()));
        };
        auto command = [&](QString method, QJSValueList args = {}) {
            auto a = old.property(method).call(args), b = native.property(method).call(args);
            QVERIFY2(!a.isError(), qPrintable(a.toString()));
            QVERIFY2(!b.isError(), qPrintable(b.toString()));
            compare();
        };
        command("reset", {120, 24});
        command("seek", {55});
        command("addTrack", {"Audio", "Music", -1});
        command("addTrack", {"Shots", "Scenes", -1});
        command("addShot");
        command("seek", {60});
        command("split");
        command("changeShot", {"shot-2", "prompt", "test"});
        command("duplicateShot");
        command("nudgeShot", {2});
        command("addKey", {"file:///a.iisc", 5});
        command("addKey", {"file:///b.iisc", 6});
        command("resizeComposition", {4, 24});
        command("undo");
        command("redo");
        command("copyKey");
        command("seek", {20});
        command("pasteKey");
        command("selectShot", {"shot-1", false});
        command("toggleExclude");
        command("rippleDelete");
        command("undo");
        command("reset", {240, 30});
        QVERIFY(!model.canUndo());
        QVERIFY(model.tracks().isEmpty());
    }
};
QTEST_MAIN(NativeViewModelBridgeTests)
#include "tst_viewmodelbridge.moc"
