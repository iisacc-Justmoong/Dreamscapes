#include "App/Generation/GenerationComposerModel.h"
#include "App/Generation/AdvancedImageParameters.h"
#include "App/Generation/GenerationSubmissionModel.h"
#include "App/Generation/VideoDraftModel.h"
#include "App/Generation/SocietyGenerationStorage.h"
#include "App/Publishing/PublishDraft.h"
#include "App/Publishing/SocialPublishingController.h"
#include "App/StorageViewTypes.h"
#include "App/ViewModels/ViewData.h"
#include "App/ViewModels/WorkspaceSession.h"
#include "App/ViewModels/TimelinePlaybackModel.h"
#include "App/Views/Editor/EditorBackgroundModel.h"
#include "App/Views/Editor/EditorTimelineModel.h"
#include "App/Views/ColorManagerModel.h"
#include "App/Views/Editor/EditorToolStateModel.h"
#include "App/Views/Editor/EditorGraphicDraftModel.h"
#include "App/Views/Home/NewCanvasModel.h"
#include "App/Views/Home/ImageSelectionModel.h"
#include "App/Views/Home/StorageBrowserModel.h"
#include <QDateTime>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QSignalSpy>
#include <QtTest>
#include <limits>
#include <memory>

class NativeViewModelAdapterTests : public QObject {
    Q_OBJECT
  private slots:
    void nestedVariantTransportPreservesValues() {
        const auto wrap = [](const QVariant &value) {
            return QVariant(QMetaType::fromType<QVariant>(), &value);
        };
        const QVariantList inputs{QVariant{}, true, 17, 2.5, QString("retained"),
            QStringList{"first", "second"}, QVariantList{1, QString("two")},
            QVariantMap{{"nested", wrap(QString("payload"))}}};
        for (const auto &input : inputs) {
            const auto wrapped = wrap(wrap(input));
            QCOMPARE(wrapped.metaType().id(), QMetaType::QVariant);
            QVERIFY(dreamscapes::nativeValue(wrapped) == dreamscapes::nativeValue(input));
        }
    }
    void nativeTextEditsRejectWithoutMutatingTheDocument() {
        EditorCanvas canvas;
        QVERIFY(canvas.createCanvas({{"width",16},{"height",16},{"unit","px"},{"background","White"}}));
        EditorTimelineModel timeline;
        timeline.setCanvas(&canvas);
        const auto initial = canvas.revision();
        QVERIFY(!timeline.editFrameText(EditorTimelineModel::Seek,{},"12junk"));
        QCOMPARE(canvas.revision(),initial);
        QVERIFY(timeline.editFrameText(EditorTimelineModel::Seek,{},"F005"));
        QCOMPARE(canvas.revision(),initial);
        QVERIFY(!timeline.setRangeValues(canvas.selectedLayerId(),0,std::numeric_limits<double>::quiet_NaN()));
        QCOMPARE(canvas.revision(),initial);
        dreamscapes::ViewData data;
        EditorToolStateModel state;
        state.setCatalog(&data); state.setEngine(&canvas); state.setToolId("layers");
        const auto beforeOpacity = canvas.revision();
        QVERIFY(!state.editLayerOpacity("101%"));
        QVERIFY(!state.editLayerOpacity("wrong"));
        QCOMPARE(canvas.revision(),beforeOpacity);
        QVERIFY(state.editLayerOpacity("50%"));
        QCOMPARE(canvas.selectedLayer().value("opacity").toDouble(),.5);
        const auto accepted = canvas.revision();
        QVERIFY(state.editLayerOpacity("50%"));
        QCOMPARE(canvas.revision(),accepted);
    }
    void nativeWeightsKeepDraftsOnBadText() {
        AdvancedImageParameters parameters;
        const auto control = parameters.addControlNet("Canny"); QVERIFY(!control.isEmpty());
        const auto lora = parameters.addLora("style.safetensors","Style"); QVERIFY(!lora.isEmpty());
        const auto before = parameters.parameters();
        QVERIFY(!parameters.editControlWeight(control,""));
        QVERIFY(!parameters.editLoraWeight(lora,"NaN"));
        QCOMPARE(parameters.parameters(),before);
        QVERIFY(parameters.hasInvalidInputs());
        QVERIFY(parameters.editControlWeight(control,"0.75"));
        QVERIFY(parameters.editLoraWeight(lora,"-0.5"));
        QVERIFY(!parameters.hasInvalidInputs());
        QCOMPARE(parameters.parameters().value("controlNets").toList().front().toMap().value("weight").toDouble(),.75);
        QCOMPARE(parameters.parameters().value("loras").toList().front().toMap().value("weight").toDouble(),-.5);
    }
    void playbackStopsOnViewAndOwnerTeardown() {
        auto timeline = std::make_unique<dreamscapes::VideoTimelineModel>();
        timeline->reset(120,24);
        dreamscapes::TimelinePlaybackModel playback;
        playback.setTimeline(timeline.get());
        playback.setPlaying(true);
        QVERIFY(playback.playing());
        QTRY_VERIFY_WITH_TIMEOUT(timeline->playhead() > 0, 2000);
        QVERIFY(!timeline->canUndo());
        playback.setActive(false);
        QVERIFY(!playback.playing());
        const auto frame = timeline->playhead();
        QTest::qWait(60);
        QCOMPARE(timeline->playhead(), frame);
        playback.setPlaying(true);
        QVERIFY(!playback.playing());
        playback.setActive(true);
        playback.seekFrame(17,5);
        QCOMPARE(timeline->playhead(),15);
        playback.setPlaying(true);
        timeline.reset();
        QVERIFY(!playback.playing());
        QVERIFY(!playback.timeline());
    }
    void savedGradientAndGroupStateDoNotLoseSettings() {
        dreamscapes::ViewData data;
        EditorToolStateModel state;
        state.setCatalog(&data);
        state.setToolId("background");
        EditorBackgroundModel palette;
        palette.setToolState(&state);
        QCOMPARE(palette.gradientPresets().size(),6);
        palette.selectGradient(QVariantMap{{"name","Single"},{"colors",QStringList{"#123456"}}});
        palette.saveGradient();
        QCOMPARE(palette.gradientPresets().size(),7);
        QCOMPARE(state.values().value("backgroundGradientStops").toList().front().toMap().value("position").toDouble(),0.);
        const auto settings = state.settingsByTool();
        QSignalSpy edits(&state,&EditorToolStateModel::valueEdited);
        QSignalSpy broad(&state,&EditorToolStateModel::revisionChanged);
        state.expandLayerGroup("group-a",false);
        QCOMPARE(state.collapsedLayerGroups().value("group-a").toBool(),true);
        QCOMPARE(state.settingsByTool(),settings);
        QCOMPARE(edits.size(),0);
        QCOMPARE(broad.size(),0);
        state.expandLayerGroup("group-a",true);
        QVERIFY(state.collapsedLayerGroups().isEmpty());
    }
    void nativeProjectionsKeepPayloadsAndReevaluate() {
        dreamscapes::ViewData data;
        auto *projection = data.presentation();
        const QDateTime stamp = QDateTime::fromString("2026-10-26T01:00:00Z", Qt::ISODate);
        const QVariantMap first{{"id","a"},{"name","First"},{"source",QUrl("file:///a.iisc")},
                                 {"date",stamp},{"opaque",QVariantMap{{"keep",42}}}};
        const QVariantMap second{{"id","b"},{"name","Second"},{"source",QUrl("file:///b.iisc")}};
        const QVariantList rows{first,second};
        QCOMPARE(projection->range(rows, -2, -1), QVariantList{first});
        QCOMPARE(projection->matching(rows, "a", "id"), QVariantList{first});
        QCOMPARE(projection->find(rows, "a", "id"), first);
        QCOMPARE(projection->prepend(second, {first}), (QVariantList{second,first}));
        const auto projected = projection->range(rows).front().toMap();
        QCOMPARE(projected.value("source").metaType(), QMetaType::fromType<QUrl>());
        QCOMPARE(projected.value("date").metaType(), QMetaType::fromType<QDateTime>());
        QCOMPARE(projection->indexOf({1,2}, 1.0), 0);
        QCOMPARE(projection->labels(rows,"name"), (QStringList{"First","Second"}));
        QCOMPARE(projection->labels({second},"name"), QStringList{"Second"});
        QCOMPARE(projection->labels({"one","two"},"","",1), (QStringList{"one","✓ two"}));
        QCOMPARE(projection->videoResultLabels({QVariantMap{{"generation",QVariantMap{{"video",QVariantMap{{"frame_count",72}}}}}}}),
                 QStringList{"Video 1 · 72 frames"});
        const auto snapshot = rows;
        projection->timelineLayout({QVariantMap{{"id","a"},{"type","Shots"},{"date",stamp}}},"Shots");
        QCOMPARE(rows, snapshot);
        QCOMPARE(projection->notifications({}), QStringList{"No generation notifications"});
    }
    void qmlStringSequencesKeepLabelsAndSelection() {
        registerStorageViewTypes();
        ColorManagerModel source;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty("modeSource", &source);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import Dreamscapes.Storage 1.0
            QtObject {
                property string selectedName: modeSource.modeNames[4]
                readonly property int selectedIndex: NativeData.presentation.indexOf(modeSource.modeNames, selectedName)
                readonly property var labels: NativeData.presentation.labels(modeSource.modeNames, "", "", selectedIndex)
                readonly property string joinedLabels: labels.join("|")
            }
        )", QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto expected = source.modeNames();
        expected[4].prepend(QString::fromUtf8("✓ "));
        QCOMPARE(object->property("selectedIndex").toInt(), 4);
        QCOMPARE(object->property("joinedLabels").toString(), expected.join('|'));
        QVERIFY(object->setProperty("selectedName", source.modeNames().front()));
        expected = source.modeNames();
        expected[0].prepend(QString::fromUtf8("✓ "));
        QCOMPARE(object->property("selectedIndex").toInt(), 0);
        QCOMPARE(object->property("joinedLabels").toString(), expected.join('|'));
        QVERIFY(object->setProperty("selectedName", QString("missing")));
        QCOMPARE(object->property("selectedIndex").toInt(), -1);
        QCOMPARE(object->property("joinedLabels").toString(), source.modeNames().join('|'));
    }
    void initializationOrderPreservesDraftInputs() {
        dreamscapes::ViewData data;
        EditorToolStateModel model;
        model.setToolId("background");
        model.setSettingsByTool({{"background", QVariantMap{{"replacePrompt", "user prompt"}}}});
        model.setGenerationModels({QVariantMap{{"id", "model"}, {"name", "Selected model"}}});
        model.setCatalog(&data);
        QCOMPARE(model.toolId(), QString("background"));
        QCOMPARE(model.values().value("replacePrompt").toString(), QString("user prompt"));
        QCOMPARE(model.generationModels().size(), 1);
        const auto before = model.settingsByTool();
        model.setNotice("view message");
        QCOMPARE(model.settingsByTool(), before);
    }
    void undoKeepsIndependentGraphicAndAcquisitionDrafts() {
        dreamscapes::ViewData data;
        EditorToolStateModel model;
        model.setCatalog(&data);
        model.setSettingsByTool(
            {{"color", QVariantMap{{"graphicDrafts", QVariantMap{{"developLight", QVariantMap{{"exposure", 2}}}}},
                                   {"graphicOperation", "developLight"}}},
             {"camera-photo", QVariantMap{{"selector", "Gallery"}, {"gallerySearch", "flowers"}}}});
        model.restoreSettings({{"color", QVariantMap{{"field-1", 5}}}, {"text", QVariantMap{{"textSize", 32}}}});
        QCOMPARE(model.settingsByTool().value("camera-photo").toMap().value("gallerySearch").toString(),
                 QString("flowers"));
        QCOMPARE(model.settingsByTool().value("color").toMap().value("field-1").toInt(), 5);
        QVERIFY(model.settingsByTool().value("color").toMap().contains("graphicDrafts"));
        QCOMPARE(model.settingsByTool().value("text").toMap().value("textSize").toInt(), 32);
    }
    void canvasValidationAndCreationRemainNative() {
        NewCanvasModel model;
        QSignalSpy created(&model, &NewCanvasModel::canvasRequested);
        model.begin();
        QVERIFY(model.resultCount() > 0);
        QVERIFY(model.specification().value("valid").toBool());
        model.customSize();
        model.setDraftWidth("2048");
        model.setDraftHeight("1024");
        QCOMPARE(model.aspectRatio(), QString("2:1"));
        model.create();
        QCOMPARE(created.size(), 1);
        QCOMPARE(created.front().front().toMap().value("pixelWidth").toInt(), 2048);
        model.setDraftWidth("bad");
        model.create();
        QCOMPARE(created.size(), 1);
        QVERIFY(!model.specification().value("valid").toBool());
    }
    void graphicSourceSelectionUpdatesOnlyItsRequestedParameter() {
        dreamscapes::ViewData data;
        EditorToolStateModel state;
        state.setCatalog(&data);
        state.setToolId("effects");
        EditorGraphicDraftModel draft;
        draft.setCatalog(&data);
        draft.setToolState(&state);
        QVERIFY(draft.selectView("718:59027"));
        QVariantMap sourceRow;
        for (const auto &row : draft.rowsOf(draft.selectedView().toMap().value("rows"))) {
            if (row.toMap().value("id").toString() == "718:59219")
                sourceRow = row.toMap();
        }
        QVERIFY(!sourceRow.isEmpty());
        const auto options = sourceRow.value("options").toList();
        QCOMPARE(options.size(), 1);
        const auto before = state.settingsByTool();
        auto expected = draft.parameters("displace");
        QSignalSpy requested(&draft, &EditorGraphicDraftModel::sourceRequested);
        draft.activate(options.front(), sourceRow);
        QCOMPARE(requested.size(), 1);
        QCOMPARE(state.settingsByTool(), before);
        const QUrl source("file:///source%20map.png");
        draft.acceptSource(source);
        expected.insert("map", source.toString());
        QCOMPARE(draft.parameters("displace"), expected);
        QCOMPARE(draft.value(sourceRow).toString(), source.toString());
    }
    void composerOwnershipIsAcyclicAndPromptsArePreserved() {
        GenerationComposerModel owner, child;
        child.setSharedModel(&owner);
        owner.setSharedModel(&child);
        QVERIFY(!owner.sharedModel());
        owner.setPrompt("user prompt");
        child.setMediaType("Video");
        QCOMPARE(owner.mediaType(), QString("Video"));
        QCOMPARE(child.prompt(), QString("user prompt"));
    }
    void correctedInputsClearOnlyValidationErrors() {
        AdvancedImageParameters previous, current;
        GenerationSubmissionModel submission;
        submission.setImageParameters(&previous);
        submission.setJobIds({"accepted-job"});
        submission.setSelectedResultId("retained-result");
        submission.setSelectedImageSource("file:///retained-result.png");
        previous.recordValidity("width", false);
        QVERIFY(!submission.submit()); QVERIFY(submission.invalidInputSubmission());
        QSignalSpy changed(&submission, &GenerationSubmissionModel::changed);
        QSignalSpy reset(&submission, &GenerationSubmissionModel::selectionResetRequested);
        previous.recordValidity("width", true);
        QVERIFY(!submission.invalidInputSubmission()); QVERIFY(submission.requestError().isEmpty());
        QCOMPARE(changed.size(), 1); QCOMPARE(reset.size(), 0);
        QCOMPARE(submission.jobIds(), QStringList{"accepted-job"});
        QCOMPARE(submission.selectedResultId(), QString("retained-result"));
        QCOMPARE(submission.selectedImageSource(), QString("file:///retained-result.png"));
        previous.recordValidity("width", true); QCOMPARE(changed.size(), 1);
        submission.setRequestError("Runtime error"); changed.clear();
        previous.recordValidity("width", false); previous.recordValidity("width", true);
        QCOMPARE(submission.requestError(), QString("Runtime error")); QCOMPARE(changed.size(), 0);
        current.recordValidity("width", false); submission.setImageParameters(&current);
        QVERIFY(!submission.submit());
        previous.recordValidity("width", false); previous.recordValidity("width", true);
        QVERIFY(submission.invalidInputSubmission());
        current.recordValidity("width", true);
        QVERIFY(!submission.invalidInputSubmission()); QVERIFY(submission.requestError().isEmpty());

        VideoDraftModel video; submission.setVideoParameters(&video); submission.setMediaType("Video");
        QVERIFY(!video.editNumber("fps", "bad"));
        QVERIFY(!submission.submit()); QVERIFY(submission.invalidInputSubmission());
        QVERIFY(video.editNumber("fps", "24"));
        QVERIFY(!submission.invalidInputSubmission()); QVERIFY(submission.requestError().isEmpty());
        QCOMPARE(submission.jobIds(), QStringList{"accepted-job"}); QCOMPARE(reset.size(), 0);
    }
    void inputBindingObserversMayCloseTheSubmission() {
        AdvancedImageParameters parameters;
        auto submission = std::make_unique<GenerationSubmissionModel>();
        connect(submission.get(), &GenerationSubmissionModel::bindingsChanged, this, [&] { submission.reset(); });
        submission->setImageParameters(&parameters);
        QVERIFY(!submission);
        AdvancedImageParameters invalid;
        invalid.recordValidity("width", false);
        submission = std::make_unique<GenerationSubmissionModel>();
        submission->setImageParameters(&invalid);
        QVERIFY(!submission->submit());
        connect(submission.get(), &GenerationSubmissionModel::changed, this, [&] { submission.reset(); });
        submission->setImageParameters(&parameters);
        QVERIFY(!submission);
    }
    void missingRuntimeRejectsWithoutInventingJobsOrOpeningEditor() {
        GenerationSubmissionModel submission;
        QSignalSpy reset(&submission, &GenerationSubmissionModel::selectionResetRequested);
        QVERIFY(!submission.submit());
        QVERIFY(!submission.requestError().isEmpty());
        QCOMPARE(submission.jobIds().size(), 0);
        QCOMPARE(reset.size(), 0);
        dreamscapes::WorkspaceSession session;
        QSignalSpy editor(&session, &dreamscapes::WorkspaceSession::editorRequested);
        session.setStorageBrowserSection("files");
        session.showSubmission({"owned-job"});
        QCOMPARE(editor.size(), 0);
        QVERIFY(session.resultVisible());
        QCOMPARE(session.resultJobIds(), QStringList{"owned-job"});
    }
    void publishingProjectionsKeepAccountAndTagIdentity() {
        SocialPublishingController backend;
        QVERIFY(backend.providerNames().contains("X"));
        QCOMPARE(backend.providerId("X"), QString("x"));
        QVERIFY(backend.matchingAccount("x", "missing").isEmpty());
        PublishDraft draft;
        QVERIFY(draft.tags().isEmpty());
        QCOMPARE(draft.coverPage(), 0);
    }
    void opaqueSessionMetadataAndHiddenFileProjectionsSurvive() {
        const QVariantMap result{
            {"id", "chosen"}, {"imageSource", QUrl("file:///original.png")}, {"opaque", QVariantMap{{"seed", 9876}}}};
        dreamscapes::WorkspaceSession session;
        QVERIFY(session.finishPreparedImages({result}, {QUrl("file:///original.png")}));
        QCOMPARE(session.canvasGenerationResult(), result);
        session.setResultVisible(true);
        QCOMPARE(session.canvasGenerationResult(), result);
        iiSocietyContainer::Container container;
        container.synchronize(
            "drive", "/Volumes/Storage/Workspace/Product/Dreamscapes/build/native-mirror",
            {QVariantMap{{"key", "files/one.png"},
                         {"containerId", "drive"},
                         {"name", "one.png"},
                         {"section", "files"},
                         {"relativePath", "one.png"},
                         {"source", QUrl("file:///one.png")},
                         {"modified", QDateTime::fromString("2026-10-26T00:00:00Z", Qt::ISODate)},
                         {"metadata", QVariantMap{{"reference", QUrl("file:///reference.iisc")}, {"seed", 9876}}},
                         {"exists", true},
                         {"available", true}},
             QVariantMap{{"key", "files/two.png"},
                         {"containerId", "drive"},
                         {"name", "two.png"},
                         {"section", "files"},
                         {"relativePath", "two.png"},
                         {"modified", QDateTime::fromString("2026-10-25T00:00:00Z", Qt::ISODate)},
                         {"exists", true},
                         {"available", true}}},
            true);
        QCOMPARE(container.aliases().size(), 2);
        StorageBrowserModel browser;
        browser.setContainer(&container);
        browser.setActive(true);
        QTRY_COMPARE(browser.entries().size(), 2);
        QCOMPARE(browser.entries().front().toMap().value("source"), QVariant(QUrl("file:///one.png")));
        QCOMPARE(browser.entries().front().toMap().value("metadata"),
                 container.aliases().front().toMap().value("metadata"));
        QCOMPARE(browser.entries().front().toMap().value("modified"),
                 container.aliases().front().toMap().value("modified"));
        browser.setQuery("two");
        browser.setActive(false);
        QTest::qWait(25);
        browser.setActive(true);
        QTRY_COMPARE(browser.entries().size(), 1);
        QCOMPARE(browser.entries().front().toMap().value("name").toString(), QString("two.png"));
        browser.setContainer(nullptr);
        QVERIFY(browser.entries().isEmpty());
    }
    void storageSdkVersionsHonorExplicitSelection() {
        struct Old {
            QString selected;
            bool open(const QString &path = {}) {
                selected = path;
                return true;
            }
        } old;
        struct New {
            QString selected, application;
            bool open(const QString &path = {}) {
                selected = path;
                return true;
            }
            bool openForApplication(const QString &id) {
                application = id;
                return true;
            }
        } current;
        QVERIFY(dreamscapes::detail::openGenerationStorageBackend(old, "chosen"));
        QCOMPARE(old.selected, QString("chosen"));
        QVERIFY(dreamscapes::detail::openGenerationStorageBackend(old, {}));
        QVERIFY(old.selected.isEmpty());
        QVERIFY(dreamscapes::detail::openGenerationStorageBackend(current, {}));
        QCOMPARE(current.application, QString("com.iisacc.dreamscapes"));
        current.application.clear();
        QVERIFY(dreamscapes::detail::openGenerationStorageBackend(current, "explicit"));
        QCOMPARE(current.selected, QString("explicit"));
        QVERIFY(current.application.isEmpty());
    }
    void imageSelectionKeepsOriginalMetadataAcrossResultArrival() {
        const QVariantMap first{{"source", QUrl("file:///one.png")}, {"isImage", true},
                                {"metadata", QVariantMap{{"reference", QUrl("file:///reference.iisc")}, {"seed", 9876}}}};
        const QVariantMap second{{"source", QUrl("file:///two.png")}, {"isImage", true}};
        ImageSelectionModel model;
        model.setEntries({first});
        model.toggle(first);
        QCOMPARE(model.selectedImages(), QVariantList{first});
        model.setEntries({first, second});
        QCOMPARE(model.images(), QVariantList({first, second}));
        QCOMPARE(model.selectedImages(), QVariantList{first});
        model.selectAll();
        QCOMPARE(model.selectedImages(), QVariantList({first, second}));
    }
    void numericAndColorInputsAreValidatedNatively() {
        dreamscapes::ViewData data;
        QVERIFY(data.tools()->numberInput("bad", 0, 100, "%").isNull());
        QCOMPARE(data.tools()->numberInput("42%", 0, 100, "%").toDouble(), 42.);
        QVERIFY(data.tools()->dimensionInput(QVariantList{10, 20}, 0, "0 px").isNull());
        QCOMPARE(data.tools()->dimensionInput(QVariantList{10, 20}, 1, "42 px").toList(), QVariantList({10, 42.}));
        QCOMPARE(data.tools()->hexInput("#aabbcc").toString(), QString("#AABBCC"));
        QVERIFY(data.tools()->hexInput("#xyz").isNull());
        ColorManagerModel color;
        const auto before = color.color();
        QVERIFY(!color.editChannelText(0, "wrong"));
        QCOMPARE(color.color(), before);
    }
    void qmlDeclaresNativeModelsWithoutScriptStateStore() {
        registerStorageViewTypes();
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQml\nimport Dreamscapes.Storage 1.0\nNativeEditorToolState { catalog: NativeData; "
                          "toolId: \"background\" }",
                          QUrl());
        QScopedPointer<QObject> instance(component.create());
        QVERIFY2(instance, qPrintable(component.errorString()));
        QCOMPARE(instance->property("toolId").toString(), QString("background"));
    }
};
QTEST_GUILESS_MAIN(NativeViewModelAdapterTests)
#include "tst_viewmodeladapters.moc"
