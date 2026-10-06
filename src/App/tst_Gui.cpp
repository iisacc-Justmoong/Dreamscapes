#include "App/Views/Home/HomeCanvas.h"
#include "App/Views/Home/CanvasPresets.h"
#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorProject.h"
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <QDir>
#include <QClipboard>
#include <QBuffer>
#include <QDirIterator>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QJSValue>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QMediaPlayer>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QScopedValueRollback>
#include <QTimer>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWheelEvent>
#include "Generation/GenerationController.h"
#include "Generation/AdvancedImageParameters.h"
#include "../../tests/LocalRuntimeProbeReady.h"
#include "Views/Result/ImageFileExporter.h"
#include "Views/Result/PhotoLibraryExporter.h"
#include <iiSocietyHelper.h>
#include <iiSocietyContainer/DashboardFiles.h>
#include <iiSocietyContainer/SocietyApplication.h>
#include <QtTest>

#include <memory>
#include <optional>

namespace {
std::optional<GenerationRuntime> guiRuntimeOverride;
// GUI fixtures contain protocol-test model bytes, not native inference weights.
// Inject the process runtime explicitly instead of inheriting platform defaults.
class GuiGenerationController : public GenerationController
{
public:
    explicit GuiGenerationController(QObject *parent = nullptr)
        : GenerationController(guiRuntimeOverride.value_or(
              GenerationRuntime{qEnvironmentVariable("IILD_GENERATOR_EXECUTABLE"),
                                "auto", 10, 1024, {}}), parent) {}
};
class GuiAdvancedImageParameters : public AdvancedImageParameters
{
public:
    explicit GuiAdvancedImageParameters(QObject *parent = nullptr)
        : AdvancedImageParameters(qEnvironmentVariable("DREAMSCAPES_TEST_PRESET_FILE"), parent) {}
};

QUrl sourceUrl(const QString &file)
{
    return QUrl::fromLocalFile(QStringLiteral(DREAMSCAPES_QML_SOURCE_DIR) + '/' + file);
}

QQuickItem *item(QObject *root, const char *name)
{
    return root->findChild<QQuickItem *>(QString::fromLatin1(name));
}

QVariantList listProperty(QObject *object, const char *name)
{
    const auto value = object->property(name);
    return value.metaType() == QMetaType::fromType<QJSValue>()
        ? value.value<QJSValue>().toVariant().toList() : value.toList();
}

QRectF bounds(QQuickItem *control, QQuickItem *parent)
{
    return control->mapRectToItem(parent, control->boundingRect());
}

QQuickItem *visualItem(QQuickItem *root, const char *name)
{
    if (root->objectName() == QString::fromLatin1(name)) return root;
    for (auto *child : root->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}

QQuickItem *editorBackControl(QQuickWindow *window)
{
    auto *editor = item(window, "canvasEditor");
    return editor->property("mobileLayout").toBool() ? item(editor, "editorBackButton")
        : visualItem(item(editor, "editorSidebar"), "desktopAction_home");
}

QQuickItem *menuCommand(QQuickItem *root, const QString &label)
{
    if (root->property("entry").isValid() && root->property("label").toString() == label) return root;
    for (auto *child : root->childItems())
        if (auto *found = menuCommand(child, label)) return found;
    return nullptr;
}

void click(QQuickWindow *window, QQuickItem *control)
{
    // Auto-height canvases can put Home controls below the visible viewport.
    // Scroll only as far as needed, as a user would before clicking them.
    auto *home = item(window, "desktopHome");
    auto *content = item(window, "desktopHomeContent");
    auto *viewport = item(window, "desktopHomeViewport");
    bool belongsToContent = false;
    for (auto *parent = control; parent; parent = parent->parentItem())
        if (parent == content) { belongsToContent = true; break; }
    if (home && home->isVisible() && viewport && belongsToContent) {
        QTRY_VERIFY(bounds(home, window->contentItem()).bottom() <= window->height());
        QTRY_COMPARE(viewport->height(), home->height());
        const auto rect = bounds(control, viewport);
        const qreal offset = rect.top() < 0 ? rect.top() - 8
            : rect.bottom() > viewport->height() ? rect.bottom() - viewport->height() + 8 : 0;
        if (offset != 0) {
            const qreal maximum = qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height());
            QVERIFY(viewport->setProperty("contentY", qBound(0.0,
                viewport->property("contentY").toReal() + offset, maximum)));
            QTRY_VERIFY(bounds(control, viewport).top() >= 0
                && bounds(control, viewport).bottom() <= viewport->height());
        }
    }
    auto *mobileHome = item(window, "mobileHome");
    auto *mobileViewport = item(window, "mobileHomeViewport");
    bool belongsToMobileHome = false;
    for (auto *parent = control; parent; parent = parent->parentItem())
        if (parent == mobileViewport) { belongsToMobileHome = true; break; }
    if (mobileHome && mobileHome->isVisible() && mobileViewport && belongsToMobileHome) {
        const auto rect = bounds(control, mobileViewport);
        const qreal offset = rect.top() < 0 ? rect.top() - 8
            : rect.bottom() > mobileViewport->height() ? rect.bottom() - mobileViewport->height() + 8 : 0;
        if (offset != 0) {
            const qreal maximum = qMax(0.0, mobileViewport->property("contentHeight").toReal() - mobileViewport->height());
            QVERIFY(mobileViewport->setProperty("contentY", qBound(0.0, mobileViewport->property("contentY").toReal() + offset, maximum)));
            QTRY_VERIFY(bounds(control, mobileViewport).top() >= 0 && bounds(control, mobileViewport).bottom() <= mobileViewport->height());
        }
    }
    if (QGuiApplication::platformName() == "cocoa") {
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
    }
    QTest::mouseMove(window, control->mapToScene(control->boundingRect().center()).toPoint());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      control->mapToScene(control->boundingRect().center()).toPoint());
}

QQuickItem *menuEntry(QQuickItem *root, const QString &label)
{
    if (root->property("label").toString() == label)
        return root;
    for (auto *child : root->childItems()) {
        if (auto *entry = menuEntry(child, label))
            return entry;
    }
    return nullptr;
}
}

class GuiTests : public QObject
{
    Q_OBJECT

private slots:
    void historyAppearsBelowQuickGenerateAndScrolls_data();
    void historyAppearsBelowQuickGenerateAndScrolls();
    void historyShowsAnEmptyState();
    void mobileHomeUsesFigmaSectionsLimitsAndLvrsNavigation();
    void desktopHomeSidebarReflowsAndRoutes();
    void desktopHomeSidebarMatchesFigmaAndScrolls();
    void desktopHomeSearchesSocietyAndUpdates();
    void desktopHomeRendersFigmaFrame();
    void desktopHomeContinuousRowsAndPromptStarters();
    void newCanvasChoosesPrintAndCustomSizes_data();
    void newCanvasChoosesPrintAndCustomSizes();
    void newCanvasSearchCancelAndKeyboard();
    void newCanvasPaddingAndStaticDetails_data();
    void newCanvasPaddingAndStaticDetails();
    void canvasRoutesPreserveSelectionAndDraft_data();
    void canvasRoutesPreserveSelectionAndDraft();
    void homeEditorRoutes_data();
    void homeEditorRoutes();
    void generatedImageSelectionRoutes_data();
    void generatedImageSelectionRoutes();
    void multiCanvasProjectSelection_data();
    void multiCanvasProjectSelection();
    void mobileEditorToolbarSlidesAndSelects_data();
    void mobileEditorToolbarSlidesAndSelects();
    void editorToolSheets_data();
    void editorToolSheets();
    void editorToolNumericContracts();
    void desktopEditorLayoutCentersCanvas();
    void desktopEditorPanelResizes();
    void desktopEditorElementsMatchesFigma();
    void desktopEditorToolPanelsMatchFigma_data();
    void desktopEditorToolPanelsMatchFigma();
    void editorNativeCanvasEditsAndPersists();
    void editorToolbarOperations();
    void editorDocumentRenderQuality();
    void captureSocietyUrl(const QUrl &url) { m_societyUrl = url; }
    void foregroundApplicationPreparesBeforeGenerate();
    void generateOpensResultImmediatelyAndDisplaysEveryPreview();
    void referenceGenerateOpensResultImmediately_data();
    void referenceGenerateOpensResultImmediately();
    void resultCancelsTheActiveGeneration();
    void mainCreatesOneSharedWindow();
    void preferencesCategoriesAndDefaultModels();
    void preferencesLvrsViewsAndFolderPicker();
    void preferencesAccountAvatarFollowsSdk();
    void sharedContentSurvivesLayoutChanges();
    void homeCanvasMaxWidthAndAutoHeight_data();
    void homeCanvasMaxWidthAndAutoHeight();
    void homeCanvasScalesToFramePreservingPainting();
    void homeCanvasSliderRanges();
    void homeCanvasDragPaintAndColorPicker();
    void sharedPanelLayout_data();
    void sharedPanelLayout();
    void promptFieldsWrapAndGrow_data();
    void promptFieldsWrapAndGrow();
    void promptCursorStaysVisible_data();
    void promptCursorStaysVisible();
    void videoHomeSubmitPlaysAndExports();
    void videoWorkspaceRoutesEditsAndGenerates();
    void sharedControlsSubmitCurrentSelection_data();
    void sharedControlsSubmitCurrentSelection();
    void advancedGenerationDefaultsAndDynamicCollections();
    void advancedControlsAppearOnlyAfterAdd();
    void advancedWorkspacePreservesDraftAndScrolls();
    void advancedCanvasMatchesResolutionAndStreamsItsBatch();
    void packagedApplicationStarts();
    void generateButtonUsesSocietyStorage();
    void countSelectionCreatesThreeImagesInSociety();
    void newSubmissionReplacesDismissedResults_data();
    void newSubmissionReplacesDismissedResults();
    void dismissedGenerationCannotReopenOrContaminateTheNextSubmission();
    void resultGalleryLayoutAndSelection_data();
    void resultGalleryLayoutAndSelection();
    void resultScreenLayout_data();
    void resultScreenLayout();
    void resultCannotOpenProjectWithoutReadableImage();
    void resultImageContextMenu_data();
    void resultImageContextMenu();
    void resultImageSaveDialogPreservesSelectionAndCancel();
    void resultImageSaveToPhotos();
    void viewsLeaveWindowChromeAvailable();
    void homePromptDragDoesNotMoveWindow_data();
    void homePromptDragDoesNotMoveWindow();
private:
    QUrl m_societyUrl;
};

void GuiTests::advancedGenerationDefaultsAndDynamicCollections()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Home/AdvancedGenerate.qml"));
    std::unique_ptr<QObject> view(component.create());
    QVERIFY2(view, qPrintable(component.errorString()));
    QCOMPARE(view->property("seed").toInt(), -1);
    QCOMPARE(view->property("outputCount").toInt(), 4);
    QCOMPARE(view->property("maximumReferenceImages").toInt(), 20);
    QCOMPARE(view->property("controlNetCount").toInt(), 0);
    QCOMPARE(view->property("nextControlNetNumber").toInt(), 1);
    QCOMPARE(view->property("expanded").toBool(), true);
    QCOMPARE(listProperty(view.get(), "referenceImages").size(), 0);
    QCOMPARE(listProperty(view.get(), "loras").size(), 0);
    const QString capturePath = qEnvironmentVariable("DREAMSCAPES_ADVANCED_CAPTURE");
    if (!capturePath.isEmpty()) {
        auto *rootItem = qobject_cast<QQuickItem *>(view.get());
        QVERIFY(rootItem);
        QQuickWindow window;
        window.setColor(QColor("#0c0c0d"));
        window.resize(402, 844);
        rootItem->setParentItem(window.contentItem());
        rootItem->setSize(QSizeF(402, 844));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(1000);
        const auto image = window.grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(capturePath));
        rootItem->setParentItem(nullptr);
    }

    QVERIFY(view->setProperty("expanded", false));
    QCOMPARE(view->property("expanded").toBool(), false);
    const QString compactCapturePath = qEnvironmentVariable("DREAMSCAPES_ADVANCED_COMPACT_CAPTURE");
    if (!compactCapturePath.isEmpty()) {
        auto *rootItem = qobject_cast<QQuickItem *>(view.get());
        QQuickWindow window;
        window.setColor(QColor("#0c0c0d"));
        window.resize(402, 844);
        rootItem->setParentItem(window.contentItem());
        rootItem->setSize(QSizeF(402, 844));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(1000);
        const auto image = window.grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(compactCapturePath));
        rootItem->setParentItem(nullptr);
    }
    QVERIFY(view->setProperty("expanded", true));

    engine.rootContext()->setContextProperty("advancedView", view.get());
    auto evaluate = [&](const QString &script) -> QVariant {
        QQmlExpression expression(engine.rootContext(), view.get(), script);
        const auto result = expression.evaluate();
        if (expression.hasError())
            qWarning().noquote() << expression.error().toString();
        return result;
    };

    auto *embeddingSelector = view->findChild<QObject *>("textualEmbeddingSelector");
    QVERIFY(embeddingSelector);
    QVERIFY(view->findChild<QObject *>("textualEmbeddingDialog"));
    engine.rootContext()->setContextProperty("embeddingSelector", embeddingSelector);
    QCOMPARE(embeddingSelector->property("selectorIndex").toInt(), 0);
    QVERIFY(evaluate("advancedView.edit('textualEmbeddings', 'file:///detail.safetensors')").toBool());
    QTRY_COMPARE(embeddingSelector->property("selectorIndex").toInt(), 1);
    QVERIFY(evaluate("embeddingSelector.editValue('selectorIndex', 0)").toBool());
    QCOMPARE(evaluate("advancedView.values.textualEmbeddings").toString(), QString());
    QVERIFY(evaluate("advancedView.edit('promptWeighting', false)").toBool());
    QCOMPARE(evaluate("advancedView.values.promptWeighting").toBool(), false);
    auto *upscalerSelector = view->findChild<QObject *>("upscalerSelector");
    QVERIFY(upscalerSelector);
    auto *upscalerDialog = view->findChild<QObject *>("upscalerModelDialog");
    QVERIFY(upscalerDialog);
    engine.rootContext()->setContextProperty("upscalerSelector", upscalerSelector);
    engine.rootContext()->setContextProperty("upscalerDialog", upscalerDialog);
    QCOMPARE(upscalerSelector->property("selectorIndex").toInt(), 3);
    QVERIFY(evaluate("advancedView.selectLearnedUpscaler('file:///upscaler.safetensors')").toBool());
    QTRY_COMPARE(upscalerSelector->property("selectorIndex").toInt(), 4);
    QCOMPARE(evaluate("advancedView.values.upscalerModel").toString(), QString("file:///upscaler.safetensors"));
    QVERIFY(evaluate("upscalerSelector.editValue('selectorIndex', 1)").toBool());
    QCOMPARE(evaluate("advancedView.values.upscaler").toString(), QString("bilinear"));
    QVERIFY(evaluate("upscalerSelector.editValue('selectorIndex', 4)").toBool());
    evaluate("upscalerDialog.reject()");
    QCOMPARE(evaluate("advancedView.values.upscaler").toString(), QString("bilinear"));
    QCOMPARE(evaluate("advancedView.values.upscalerModel").toString(), QString("file:///upscaler.safetensors"));
    QTRY_COMPARE(upscalerSelector->property("selectorIndex").toInt(), 1);
    auto *detailerToggle = view->findChild<QObject *>("detailerToggle");
    auto *detailerDialog = view->findChild<QObject *>("detailerModelDialog");
    QVERIFY(detailerToggle && detailerDialog);
    engine.rootContext()->setContextProperty("detailerToggle", detailerToggle);
    engine.rootContext()->setContextProperty("detailerDialog", detailerDialog);
    QVERIFY(!detailerToggle->property("checked").toBool());
    QVERIFY(evaluate("advancedView.selectDetailer('file:///detector.safetensors')").toBool());
    QTRY_VERIFY(detailerToggle->property("checked").toBool());
    QCOMPARE(evaluate("advancedView.values.detailerModel").toString(), QString("file:///detector.safetensors"));
    QVERIFY(evaluate("detailerToggle.editValue('checked', false)").toBool());
    QVERIFY(!evaluate("advancedView.values.detailer").toBool());
    QVERIFY(evaluate("detailerToggle.editValue('checked', true)").toBool());
    evaluate("detailerDialog.reject()");
    QVERIFY(!evaluate("advancedView.values.detailer").toBool());
    QCOMPARE(evaluate("advancedView.values.detailerModel").toString(), QString("file:///detector.safetensors"));
    QTRY_VERIFY(!detailerToggle->property("checked").toBool());
    auto *refinerToggle = view->findChild<QObject *>("refinerToggle");
    auto *refinerDialog = view->findChild<QObject *>("refinerModelDialog");
    QVERIFY(refinerToggle && refinerDialog);
    engine.rootContext()->setContextProperty("refinerToggle", refinerToggle);
    engine.rootContext()->setContextProperty("refinerDialog", refinerDialog);
    QVERIFY(!refinerToggle->property("checked").toBool());
    QVERIFY(evaluate("advancedView.selectRefiner('file:///refiner.safetensors')").toBool());
    QTRY_VERIFY(refinerToggle->property("checked").toBool());
    QCOMPARE(evaluate("advancedView.values.refinerModel").toString(), QString("file:///refiner.safetensors"));
    QVERIFY(evaluate("refinerToggle.editValue('checked', false)").toBool());
    QVERIFY(!evaluate("advancedView.values.refiner").toBool());
    QVERIFY(evaluate("refinerToggle.editValue('checked', true)").toBool());
    evaluate("refinerDialog.reject()");
    QVERIFY(!evaluate("advancedView.values.refiner").toBool());
    QCOMPARE(evaluate("advancedView.values.refinerModel").toString(), QString("file:///refiner.safetensors"));
    QTRY_VERIFY(!refinerToggle->property("checked").toBool());
    QTemporaryDir refinerFiles(DREAMSCAPES_TEST_DIRECTORY "/refiner-picker-XXXXXX");
    QVERIFY(refinerFiles.isValid());
    QFile replacement(refinerFiles.filePath("replacement.safetensors"));
    QVERIFY(replacement.open(QIODevice::WriteOnly));
    QVERIFY(replacement.write("selection fixture") > 0);
    replacement.close();
    const auto replacementUrl = QUrl::fromLocalFile(replacement.fileName()).toString();
    QVERIFY(refinerDialog->setProperty("selectedFile", QUrl(replacementUrl)));
    evaluate("refinerDialog.accepted()");
    QTRY_VERIFY(refinerToggle->property("checked").toBool());
    QCOMPARE(evaluate("advancedView.values.refinerModel").toString(), replacementUrl);

    // Picker tests opt in explicitly; constructing the view must not add controls.
    QCOMPARE(evaluate("advancedView.addControlNet('Pose')").toInt(), 1);
    QCOMPARE(evaluate("advancedView.addControlNet('Canny')").toInt(), 2);
    auto *ipModelDialog = view->findChild<QObject *>("controlIPModelDialog");
    auto *ipVisionDialog = view->findChild<QObject *>("controlIPVisionDialog");
    QVERIFY(ipModelDialog && ipVisionDialog);
    engine.rootContext()->setContextProperty("ipModelDialog", ipModelDialog);
    engine.rootContext()->setContextProperty("ipVisionDialog", ipVisionDialog);
    const auto ipId = evaluate("advancedView.values.controlNets[0].id").toString();
    engine.rootContext()->setContextProperty("ipControlId", ipId);
    QVERIFY(evaluate("advancedView.selectIPAdapter(ipControlId, 'file:///ip.safetensors', 'file:///vision.safetensors')").toBool());
    QTRY_VERIFY(visualItem(qobject_cast<QQuickItem *>(view.get()), "controlIPToggle1"));
    auto *ipToggle = visualItem(qobject_cast<QQuickItem *>(view.get()), "controlIPToggle1");
    QTRY_VERIFY(ipToggle->property("checked").toBool());
    engine.rootContext()->setContextProperty("ipToggle", ipToggle);
    QVERIFY(evaluate("ipToggle.editValue('checked', false)").toBool());
    QVERIFY(!evaluate("advancedView.values.controlNets[0].ipAdapter").toBool());
    QCOMPARE(evaluate("advancedView.values.controlNets[0].ipAdapterModel").toString(), QString("file:///ip.safetensors"));
    QTRY_VERIFY(visualItem(qobject_cast<QQuickItem *>(view.get()), "controlIPToggle1"));
    engine.rootContext()->setContextProperty("ipToggle", visualItem(qobject_cast<QQuickItem *>(view.get()), "controlIPToggle1"));
    QVERIFY(evaluate("ipToggle.editValue('checked', true)").toBool());
    evaluate("ipModelDialog.reject()");
    QVERIFY(!evaluate("advancedView.values.controlNets[0].ipAdapter").toBool());
    QTRY_VERIFY(!visualItem(qobject_cast<QQuickItem *>(view.get()), "controlIPToggle1")->property("checked").toBool());
    evaluate("advancedView.chooseIPAdapter(ipControlId)");
    QVERIFY(ipModelDialog->setProperty("selectedFile", QUrl(replacementUrl)));
    evaluate("ipModelDialog.accepted()");
    QCOMPARE(evaluate("advancedView.values.controlNets[0].ipAdapterModel").toString(), QString("file:///ip.safetensors"));
    evaluate("ipVisionDialog.reject()");
    QVERIFY(!evaluate("advancedView.values.controlNets[0].ipAdapter").toBool());
    evaluate("advancedView.chooseIPAdapter(ipControlId)");
    QVERIFY(ipModelDialog->setProperty("selectedFile", QUrl(replacementUrl)));
    evaluate("ipModelDialog.accepted()");
    QVERIFY(ipVisionDialog->setProperty("selectedFile", QUrl(replacementUrl)));
    evaluate("ipVisionDialog.accepted()");
    QVERIFY(evaluate("advancedView.values.controlNets[0].ipAdapter").toBool());
    QTRY_VERIFY(visualItem(qobject_cast<QQuickItem *>(view.get()), "controlIPToggle1")->property("checked").toBool());
    QCOMPARE(evaluate("advancedView.values.controlNets[0].ipAdapterModel").toString(), replacementUrl);
    QCOMPARE(evaluate("advancedView.values.controlNets[0].ipAdapterVision").toString(), replacementUrl);
    QVERIFY(!evaluate("advancedView.values.controlNets[1].ipAdapter").toBool());
    QVERIFY(!evaluate("advancedView.selectIPAdapter(ipControlId, '', 'file:///vision.safetensors')").toBool());
    QVERIFY(evaluate("advancedView.values.controlNets[0].ipAdapter").toBool());
    evaluate("ipModelDialog.close(); ipVisionDialog.close()");

    auto *poseDetectorDialog = view->findChild<QObject *>("controlPoseDetectorDialog");
    auto *poseModelDialog = view->findChild<QObject *>("controlPoseModelDialog");
    QVERIFY(poseDetectorDialog && poseModelDialog);
    engine.rootContext()->setContextProperty("poseDetectorDialog", poseDetectorDialog);
    engine.rootContext()->setContextProperty("poseModelDialog", poseModelDialog);
    const auto previousProcess = evaluate("advancedView.values.controlNets[0].process").toString();
    evaluate("advancedView.choosePose(ipControlId); poseDetectorDialog.reject()");
    QCOMPARE(evaluate("advancedView.values.controlNets[0].process").toString(), previousProcess);
    evaluate("advancedView.choosePose(ipControlId)");
    QVERIFY(poseDetectorDialog->setProperty("selectedFile", QUrl("file:///detector.onnx")));
    evaluate("poseDetectorDialog.accepted(); poseModelDialog.reject()");
    QCOMPARE(evaluate("advancedView.values.controlNets[0].process").toString(), previousProcess);
    QVERIFY(evaluate("advancedView.selectPose(ipControlId, 'file:///detector.onnx', 'file:///pose.onnx')").toBool());
    QCOMPARE(evaluate("advancedView.values.controlNets[0].process").toString(), QString("Pose"));
    QCOMPARE(evaluate("advancedView.values.controlNets[0].poseModel").toString(), QString("file:///pose.onnx"));
    QVERIFY(!evaluate("advancedView.selectPose(ipControlId, '', 'file:///pose.onnx')").toBool());
    evaluate("poseDetectorDialog.close(); poseModelDialog.close()");

    auto *watermarkToggle = view->findChild<QObject *>("watermarkToggle");
    QVERIFY(watermarkToggle);
    engine.rootContext()->setContextProperty("watermarkToggle", watermarkToggle);
    QVERIFY(!evaluate("advancedView.values.watermark").toBool());
    QVERIFY(evaluate("watermarkToggle.editValue('checked', true)").toBool());
    QVERIFY(evaluate("advancedView.values.watermark").toBool());
    QVERIFY(evaluate("watermarkToggle.editValue('checked', false)").toBool());
    QVERIFY(!evaluate("advancedView.values.watermark").toBool());

    QCOMPARE(evaluate("advancedView.addControlNet('Depth')").toInt(), 3);
    QCOMPARE(view->property("controlNetCount").toInt(), 3);
    QCOMPARE(view->property("nextControlNetNumber").toInt(), 4);
    const auto reference = sourceUrl("Views/Result/Assets/right.svg").toString();
    for (int index = 0; index < 20; ++index)
        QVERIFY(evaluate(QString("advancedView.addReferenceImage('%1')").arg(reference)).toBool());
    QVERIFY(!evaluate("advancedView.addReferenceImage('file:///reference-overflow.png')").toBool());
    QCOMPARE(listProperty(view.get(), "referenceImages").size(), 20);
    QVERIFY(evaluate("advancedView.addLora('file:///portrait.safetensors', 'Portrait')").toBool());
    QCOMPARE(listProperty(view.get(), "loras").size(), 1);

    QSignalSpy saved(view.get(), SIGNAL(presetSaved(QString)));
    QVERIFY(evaluate("advancedView.savePreset('  Studio portrait  ')").toBool());
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved.constFirst().constFirst().toString(), QString("Studio portrait"));
    QVERIFY(!evaluate("advancedView.savePreset('   ')").toBool());
    QCOMPARE(saved.size(), 1);
}

void GuiTests::advancedControlsAppearOnlyAfterAdd()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Home/AdvancedGenerate.qml"));
    std::unique_ptr<QObject> view(component.create());
    QVERIFY2(view, qPrintable(component.errorString()));
    auto *panel = qobject_cast<QQuickItem *>(view.get());
    auto *draft = view->findChild<AdvancedImageParameters *>("advancedParameterStore");
    QVERIFY(panel && draft);
    QCOMPARE(view->property("controlNetCount").toInt(), 0);
    QVERIFY(!visualItem(panel, "controlNetHeader1"));
    auto *add = item(view.get(), "addControlNet");
    auto *viewport = item(view.get(), "advancedGenerationViewport");
    QVERIFY(add && viewport);
    QCOMPARE(add->property("label").toString(), "Add control");
    QCOMPARE(add->property("trailingIconName").toString(), "generaladd");
    QVERIFY(!add->property("showLeadingIcon").toBool());
    QVERIFY(!add->property("showDescription").toBool());

    QQuickWindow window;
    window.setColor(QColor("#1e1e1e"));
    window.resize(402, 844);
    panel->setParentItem(window.contentItem());
    panel->setSize(QSizeF(402, 844));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto scrollTo = [&](QQuickItem *row) {
        const auto y = viewport->property("contentY").toReal() + row->mapToItem(viewport, QPointF{}).y() - 80;
        viewport->setProperty("contentY", qBound(0.0, y,
            viewport->property("contentHeight").toReal() - viewport->height()));
        QTest::qWait(100);
    };
    QTRY_COMPARE(add->width(), 402.0);
    QCOMPARE(add->height(), 44.0); // LVRS Navigation defaults; no geometry override.
    const auto capture = qEnvironmentVariable("DREAMSCAPES_ADD_CONTROL_CAPTURE");
    if (!capture.isEmpty()) {
        auto grab = add->grabToImage();
        QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
        QVERIFY(ready.wait());
        QVERIFY(grab->image().save(capture));
    }
    scrollTo(add);
    click(&window, add);
    QTRY_COMPARE(view->property("controlNetCount").toInt(), 1);
    QTRY_VERIFY(visualItem(panel, "controlProcess1"));
    QVERIFY(visualItem(panel, "controlProcess1")->isVisible());
    QVERIFY(visualItem(panel, "controlIPToggle1")->isVisible());
    const auto first = draft->parameters().value("controlNets").toList().first().toMap();
    const auto firstId = first.value("id").toString();
    QVERIFY(!firstId.isEmpty());
    QCOMPARE(first.value("process").toString(), "None");
    QVERIFY(!first.value("applied").toBool());
    QVERIFY(draft->updateControlNet(firstId, {{"weight", 0.65}, {"process", "Canny"}}));
    QVERIFY(draft->savePreset("Optional control UI"));
    QTRY_VERIFY(visualItem(panel, "controlNetHeader1"));
    scrollTo(visualItem(panel, "controlNetHeader1"));
    click(&window, visualItem(panel, "controlNetHeader1"));
    QVERIFY(!visualItem(panel, "controlProcess1")->isVisible());
    click(&window, visualItem(panel, "controlNetHeader1"));
    QVERIFY(visualItem(panel, "controlProcess1")->isVisible());
    QCOMPARE(draft->parameters().value("controlNets").toList().first().toMap().value("weight").toDouble(), 0.65);
    scrollTo(add);
    click(&window, add);
    QTRY_COMPARE(view->property("controlNetCount").toInt(), 2);
    QVERIFY(visualItem(panel, "controlProcess2")->isVisible());
    const auto secondId = draft->parameters().value("controlNets").toList().last().toMap().value("id").toString();
    QVERIFY(firstId != secondId);
    QVERIFY(draft->removeControlNet(firstId));
    QVERIFY(draft->removeControlNet(secondId));
    QTRY_COMPARE(view->property("controlNetCount").toInt(), 0);
    QVERIFY(!visualItem(panel, "controlNetHeader1"));
    QVERIFY(add->isVisible());
    QVERIFY(draft->loadPreset("Optional control UI"));
    QTRY_COMPARE(view->property("controlNetCount").toInt(), 1);
    QVERIFY(visualItem(panel, "controlProcess1")->isVisible());
    QCOMPARE(draft->parameters().value("controlNets").toList().first().toMap().value("id").toString(), firstId);
    draft->reset();
    QTRY_COMPARE(view->property("controlNetCount").toInt(), 0);
    QVERIFY(!visualItem(panel, "controlNetHeader1"));
    QVERIFY(add->isVisible());
    QVERIFY(draft->removePreset("Optional control UI"));
    panel->setParentItem(nullptr);
}

void GuiTests::advancedWorkspacePreservesDraftAndScrolls()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/advanced-workspace-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/workspace.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly));
    QVERIFY(model.write("protocol fixture") > 0);
    model.close();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->resize(1374, 900);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *sidebar = item(window, "desktopSidebar");
    auto *quick = item(window, "quickGenerate");
    QVERIFY(!window->findChild<QQuickItem *>("imageGenerationWorkspace"));
    quick->setProperty("prompt", "Independent quick draft");
    click(window, visualItem(sidebar, "desktopAction_image"));
    auto *workspace = item(window, "imageGenerationWorkspace");
    auto *panel = item(window, "advancedGenerate");
    auto *viewport = item(window, "advancedGenerationViewport");
    auto *canvas = item(window, "advancedImageCanvas");
    auto *column = item(window, "advancedParameterColumn");
    auto *draft = window->findChild<AdvancedImageParameters *>("advancedParameterStore");
    QVERIFY(workspace && panel && viewport && canvas && column && draft);
    QVERIFY(workspace->isVisible());
    QVERIFY(!quick->isVisible());
    QCOMPARE(column->width(), 402.0);
    QVERIFY(bounds(sidebar, window->contentItem()).right() <= bounds(canvas, window->contentItem()).left());
    QVERIFY(bounds(canvas, workspace).right() <= bounds(column, workspace).left());
    QVERIFY(viewport->property("contentHeight").toDouble() > viewport->height());
    const auto canvasPosition = canvas->position();
    viewport->setProperty("contentY", 800);
    QCOMPARE(canvas->position(), canvasPosition);
    QVERIFY(draft->updateParameters({{"prompt", "Advanced draft"}, {"width", 768}, {"seed", 4294967290LL}, {"cfgScale", 4.5}}));
    QTRY_COMPARE(panel->property("prompt").toString(), "Advanced draft");
    QCOMPARE(panel->property("seed").toDouble(), 4294967290.0);
    QVERIFY(draft->savePreset("Workspace round trip"));
    QVERIFY(draft->updateParameters({{"prompt", "Changed"}, {"width", 1024}}));
    QVERIFY(draft->loadPreset("Workspace round trip"));
    QTRY_COMPARE(panel->property("prompt").toString(), "Advanced draft");
    QTRY_COMPARE(item(window, "advancedWidth")->property("inputText1").toString(), "768");
    click(window, visualItem(sidebar, "desktopAction_home"));
    QVERIFY(!workspace->isVisible());
    QCOMPARE(quick->property("prompt").toString(), "Independent quick draft");
    click(window, visualItem(sidebar, "desktopAction_image"));
    QCOMPARE(panel->property("prompt").toString(), "Advanced draft");
    QVERIFY(!window->property("resultVisible").toBool());
    auto *widthRow = item(window, "advancedWidth");
    QVariant edited;
    for (const QString &partial : {QString("7"), QString("76")}) {
        QVERIFY(QMetaObject::invokeMethod(widthRow, "editValue", Q_RETURN_ARG(QVariant, edited),
            Q_ARG(QVariant, "inputText1"), Q_ARG(QVariant, partial)));
        QVERIFY(panel->property("hasInvalidInputs").toBool());
        QCOMPARE(draft->parameters().value("width").toInt(), 768);
        QVERIFY(QMetaObject::invokeMethod(workspace, "submit"));
        QVERIFY(!workspace->property("requestError").toString().isEmpty());
        QVERIFY(listProperty(workspace, "jobIds").isEmpty());
    }
    QVERIFY(QMetaObject::invokeMethod(widthRow, "editValue", Q_RETURN_ARG(QVariant, edited),
        Q_ARG(QVariant, "inputText1"), Q_ARG(QVariant, "768")));
    QVERIFY(!panel->property("hasInvalidInputs").toBool());
    QVERIFY(workspace->property("requestError").toString().isEmpty());
    const auto capture = qEnvironmentVariable("DREAMSCAPES_WORKSPACE_CAPTURE");
    if (!capture.isEmpty()) {
        viewport->setProperty("contentY", 0);
        QTest::qWait(250);
        QVERIFY(window->grabWindow().save(capture));
    }
    const auto textCapture = qEnvironmentVariable("DREAMSCAPES_TEXT_CONDITIONING_CAPTURE");
    if (!textCapture.isEmpty()) {
        auto *row = item(window, "textualEmbeddingSelector");
        QCOMPARE(qRound(row->width()), qRound(panel->width()));
        const auto y = viewport->property("contentY").toReal() + row->mapToItem(viewport, QPointF{}).y() - 160;
        viewport->setProperty("contentY", qBound(0.0, y,
            viewport->property("contentHeight").toReal() - viewport->height()));
        QTest::qWait(250);
        QVERIFY(window->grabWindow().save(textCapture));
    }
    const auto refinerCapture = qEnvironmentVariable("DREAMSCAPES_REFINER_CAPTURE");
    if (!refinerCapture.isEmpty()) {
        auto *row = item(window, "refinerToggle");
        QCOMPARE(qRound(row->width()), qRound(panel->width()));
        const auto y = viewport->property("contentY").toReal() + row->mapToItem(viewport, QPointF{}).y() - 80;
        viewport->setProperty("contentY", qBound(0.0, y,
            viewport->property("contentHeight").toReal() - viewport->height()));
        QTest::qWait(250);
        QVERIFY(window->grabWindow().save(refinerCapture));
    }
    const auto ipCapture = qEnvironmentVariable("DREAMSCAPES_IP_CAPTURE");
    if (!ipCapture.isEmpty()) {
        QVERIFY(!draft->addControlNet("None").isEmpty());
        auto *row = visualItem(window->contentItem(), "controlIPToggle1");
        QVERIFY(row);
        QCOMPARE(qRound(row->width()), qRound(panel->width()));
        const auto y = viewport->property("contentY").toReal() + row->mapToItem(viewport, QPointF{}).y() - 360;
        viewport->setProperty("contentY", qBound(0.0, y,
            viewport->property("contentHeight").toReal() - viewport->height()));
        QTest::qWait(250);
        QVERIFY(window->grabWindow().save(ipCapture));
    }
    QVERIFY(draft->updateParameters({{"prompt", "slow advanced workspace"}, {"width", 64}, {"height", 64},
        {"outputCount", 2}, {"seed", 42}, {"steps", 3}}));
    QVERIFY(QMetaObject::invokeMethod(workspace, "submit"));
    QCOMPARE(listProperty(workspace, "jobIds").size(), 2);
    QVERIFY(workspace->property("requestError").toString().isEmpty());
    QVERIFY(!window->property("resultVisible").toBool());
    QTRY_COMPARE_WITH_TIMEOUT(listProperty(workspace, "results").size(), 2, 15000);
    QVERIFY(workspace->property("imageSource").toUrl().isValid());
    QVERIFY(!window->property("resultVisible").toBool());
    QVERIFY(workspace->isVisible());
    QCOMPARE(quick->property("prompt").toString(), "Independent quick draft");
    draft->reset();
    QTRY_COMPARE(panel->property("prompt").toString(), "");
    QTRY_COMPARE(item(window, "advancedWidth")->property("inputText1").toString(), "1024");
}

void GuiTests::advancedCanvasMatchesResolutionAndStreamsItsBatch()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/advanced-canvas-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/canvas.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly));
    QVERIFY(model.write("protocol fixture") > 0);
    model.close();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(1374, 900);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *controller = window->findChild<GenerationController *>("generationController");
    auto *quick = item(window, "quickGenerate");
    auto *sidebar = item(window, "desktopSidebar");
    QVERIFY(controller && controller->connected() && quick && sidebar);
    quick->setProperty("prompt", "Independent quick draft");
    click(window, visualItem(sidebar, "desktopAction_image"));
    auto *workspace = item(window, "imageGenerationWorkspace");
    auto *surface = item(window, "advancedImageCanvasSurface");
    auto *image = item(window, "advancedGeneratedImage");
    auto *draft = window->findChild<AdvancedImageParameters *>("advancedParameterStore");
    QVERIFY(workspace && surface && image && draft);
    QVERIFY(workspace->property("imageSource").toUrl().isEmpty());
    QTRY_VERIFY(qAbs(surface->width() - surface->height()) < 0.01);

    // Empty canvases, portrait/wide drafts and resized windows share one fit scale.
    for (const QSize size : {QSize(1024, 1024), QSize(1024, 576), QSize(576, 1024), QSize(768, 1536), QSize(1536, 768)}) {
        QVERIFY(draft->updateParameters({{"width", size.width()}, {"height", size.height()}}));
        for (const QSize windowSize : {QSize(1374, 900), QSize(960, 480), QSize(640, 720)}) {
            window->resize(windowSize);
            QTRY_VERIFY(qAbs(surface->width() / surface->height() - double(size.width()) / size.height()) < 0.001);
            QVERIFY(surface->width() > 0 && surface->height() > 0);
            QVERIFY(surface->parentItem()->boundingRect().contains(bounds(surface, surface->parentItem())));
            QTRY_COMPARE(bounds(surface, surface->parentItem()).center(), surface->parentItem()->boundingRect().center());
            QCOMPARE(workspace->property("canvasPixelWidth").toInt(), size.width());
            QCOMPARE(workspace->property("canvasPixelHeight").toInt(), size.height());
            const auto centerCapture = qEnvironmentVariable("DREAMSCAPES_CENTER_CAPTURE_DIR");
            if (!centerCapture.isEmpty() && windowSize == QSize(1374, 900)) {
                QTest::qWait(80);
                QVERIFY(window->grabWindow().save(centerCapture + QString("/image-%1x%2.png").arg(size.width()).arg(size.height())));
            }
        }
    }
    QVERIFY(draft->savePreset("Canvas portrait"));
    draft->reset();
    QTRY_VERIFY(qAbs(surface->width() - surface->height()) < 0.01);
    QVERIFY(draft->loadPreset("Canvas portrait"));
    QTRY_VERIFY(qAbs(surface->width() / surface->height() - 2.0) < 0.001);
    window->resize(1374, 900);

    // An earlier quick job may still be streaming while this batch is queued.
    const auto unrelatedId = controller->enqueue("live-gui", "1:1", 1);
    QVERIFY(!unrelatedId.isEmpty());
    QVERIFY(window->property("resultVisible").toBool());
    QVERIFY(QMetaObject::invokeMethod(window, "dismissResult"));
    QVERIFY(draft->updateParameters({{"prompt", "live-gui"}, {"width", 128}, {"height", 64},
        {"outputCount", 2}, {"seed", 42}, {"steps", 3}}));
    click(window, item(window, "advancedSubmit"));
    const auto ids = listProperty(workspace, "jobIds");
    QCOMPARE(ids.size(), 2);
    QVERIFY(workspace->property("pending").toBool());
    QVERIFY(!window->property("resultVisible").toBool());
    QVERIFY(workspace->isVisible());

    const auto advanceFrame = [&](int step) {
        QFile gate(QFileInfo(controller->previewImage().toLocalFile()).dir().filePath(QString("continue-%1").arg(step)));
        if (!gate.open(QIODevice::WriteOnly)) return false;
        gate.close();
        return true;
    };
    for (int step = 1; step <= 3; ++step) {
        QTRY_COMPARE(controller->previewJobId(), unrelatedId);
        QTRY_COMPARE(controller->previewStep(), step);
        QVERIFY(!workspace->property("showingPreview").toBool());
        QVERIFY(image->property("source").toUrl().isEmpty());
        QVERIFY(!window->property("resultVisible").toBool());
        QVERIFY(advanceFrame(step));
    }

    for (int index = 0; index < ids.size(); ++index) {
        for (int step = 1; step <= 3; ++step) {
            QTRY_COMPARE(controller->previewJobId(), ids[index].toString());
            QTRY_COMPARE(controller->previewStep(), step);
            QTRY_VERIFY(workspace->property("showingPreview").toBool());
            QTRY_COMPARE(image->property("source").toUrl(), controller->previewImage());
            QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready, not just assigned URL.
            QCOMPARE(QImage(controller->previewImage().toLocalFile()).pixelColor(0, 0).red(), step * 60);
            auto frame = image->grabToImage();
            QVERIFY(frame);
            QSignalSpy rendered(frame.data(), &QQuickItemGrabResult::ready);
            QVERIFY(rendered.wait(3000));
            const auto pixels = frame->image();
            QVERIFY(!pixels.isNull());
            QCOMPARE(pixels.pixelColor(pixels.width() / 2, pixels.height() / 2).red(), step * 60);
            QTRY_VERIFY(item(window, "advancedGenerationStatus")->property("text").toString().contains(QString("%1 / 3").arg(step)));
            QVERIFY(qAbs(surface->width() / surface->height() - 2.0) < 0.001);
            QVERIFY(workspace->isVisible());
            QVERIFY(!quick->isVisible());
            QVERIFY(!window->property("resultVisible").toBool());
            QCOMPARE(listProperty(workspace, "results").size(), index);
            QVERIFY(advanceFrame(step));
        }
    }
    QTRY_COMPARE_WITH_TIMEOUT(listProperty(workspace, "results").size(), 2, 10000);
    QTRY_VERIFY(!workspace->property("pending").toBool());
    QVERIFY(!workspace->property("showingPreview").toBool());
    QVERIFY(controller->previewJobId().isEmpty());
    const auto results = listProperty(workspace, "results");
    for (const auto &result : results)
        QCOMPARE(QImage(result.toMap().value("imageSource").toUrl().toLocalFile()).size(), QSize(128, 64));
    QTRY_COMPARE(image->property("source").toUrl(), results.last().toMap().value("imageSource").toUrl());
    QTRY_COMPARE(image->property("status").toInt(), 1);
    QVERIFY(workspace->setProperty("selectedResultId", ids.first().toString()));
    QTRY_COMPARE(image->property("source").toUrl(), results.first().toMap().value("imageSource").toUrl());
    QCOMPARE(quick->property("prompt").toString(), "Independent quick draft");
    QVERIFY(!window->property("resultVisible").toBool());
    const auto capture = qEnvironmentVariable("DREAMSCAPES_CANVAS_CAPTURE");
    if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture));
    click(window, item(window, "advancedImageSelectionToggle"));
    QTRY_VERIFY(item(window, "advancedImageSelection")->property("selectionMode").toBool());
    auto *firstThumbnail = visualItem(workspace, "advancedResultThumbnail0");
    auto *secondThumbnail = visualItem(workspace, "advancedResultThumbnail1");
    QVERIFY(firstThumbnail && secondThumbnail);
    QTRY_VERIFY(firstThumbnail->isVisible());
    // Thumbnail controls retain their layout size as asynchronous images arrive.
    QTRY_COMPARE(firstThumbnail->width(), 64.0);
    QTRY_COMPARE(firstThumbnail->height(), 64.0);
    QTRY_COMPARE(secondThumbnail->width(), 64.0);
    QTRY_COMPARE(secondThumbnail->height(), 64.0);
    click(window, firstThumbnail);
    QTRY_COMPARE(listProperty(item(window, "advancedImageSelection"), "selectedImages").size(), 1);
    click(window, secondThumbnail);
    QCOMPARE(listProperty(item(window, "advancedImageSelection"), "selectedImages").size(), 2);
    click(window, item(window, "advancedImageSelectionOpen"));
    QTRY_VERIFY(item(window, "canvasEditor")->isVisible());
    auto *nativeCanvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QVERIFY(nativeCanvas);
    QCOMPARE(window->findChild<EditorProject *>("editorProject")->canvasCount(), 2);
    QCOMPARE(nativeCanvas->document()->layers.size(), 1u);
    QCOMPARE(nativeCanvas->canvasWidth(), 128);
    QCOMPARE(nativeCanvas->canvasHeight(), 64);
    QCOMPARE(listProperty(item(window, "canvasEditor"), "generationResults"), results);
    click(window, editorBackControl(window));
    QTRY_VERIFY(workspace->isVisible());
    QCOMPARE(listProperty(item(window, "advancedImageSelection"), "selectedImages").size(), 2);
    QCOMPARE(quick->property("prompt").toString(), "Independent quick draft");
}

void GuiTests::historyAppearsBelowQuickGenerateAndScrolls_data()
{
    QTest::addColumn<int>("width");
    QTest::newRow("desktop") << 960;
    QTest::newRow("compact") << 320;
}

void GuiTests::historyAppearsBelowQuickGenerateAndScrolls()
{
    QFETCH(int, width);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/history-gui-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QImage image(32, 32, QImage::Format_RGB32);
    image.fill(Qt::cyan);
    for (int i = 0; i < 25; ++i) {
        const auto path = storage.filePath(QString("Generation History/image-%1.png").arg(i, 2, 10, QChar('0')));
        QVERIFY(image.save(path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-i - 60), QFileDevice::FileModificationTime));
    }
    QVERIFY(image.save(storage.filePath("Files/not-history.png")));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->resize(width, 844);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *history = item(window, "generationHistory");
    QVERIFY2(history, "Home must display persisted Society generation history below QuickGenerate.");
    auto *cards = item(history, "generationHistoryCards");
    auto *panel = item(window, "quickGenerate");
    auto *viewAll = item(history, "viewAllGenerationHistory");
    QVERIFY(cards && panel && viewAll);
    QTRY_COMPARE(cards->property("count").toInt(), 20);
    QVERIFY(history->mapToScene(QPointF()).y() >= panel->mapToScene(QPointF(0, panel->height())).y());
    const auto rows = listProperty(history, "files");
    QCOMPARE(rows.first().toMap().value("name").toString(), "image-00.png");
    QCOMPARE(rows.last().toMap().value("name").toString(), "image-19.png");
    QTRY_VERIFY(cards->property("contentWidth").toReal() > cards->width());
    auto *home = item(window, "desktopHome");
    QVERIFY(home);
    // The paint canvas adds vertical content above history. Bring the row into
    // the viewport before delivering horizontal touch and wheel events.
    QVERIFY(QMetaObject::invokeMethod(home, "scrollToItem", Q_ARG(QVariant, QVariant::fromValue(history))));
    QTRY_VERIFY(cards->mapToScene(QPointF(0, 80)).y() < window->height());
    const auto firstCard = [cards]() -> QQuickItem * {
        auto *content = cards->property("contentItem").value<QQuickItem *>();
        if (content) for (auto *child : content->childItems())
            if (child->objectName() == "generationHistoryCard0") return child;
        return nullptr;
    };
    QTRY_VERIFY(firstCard());
    QCOMPARE(firstCard()->width(), history->property("cardWidth").toReal());
    QTRY_COMPARE(firstCard()->property("previewStatus").toInt(), 1);
    QVERIFY(window->grabWindow().save(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/history-%1.png").arg(width)));
    cards->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_End);
    QTRY_COMPARE(cards->property("currentIndex").toInt(), 19);
    QTRY_VERIFY(cards->property("contentX").toReal() > 0);
    const auto previousOffset = cards->property("contentX").toReal();
    auto *model = window->findChild<QObject *>("generationHistoryModel");
    QVERIFY(model);
    QVERIFY(QMetaObject::invokeMethod(model, "refresh"));
    QTRY_VERIFY(!model->property("loading").toBool());
    QCOMPARE(cards->property("contentX").toReal(), previousOffset);
    QTest::keyClick(window, Qt::Key_Home);
    QTRY_COMPARE(cards->property("contentX").toReal(), 0.0);
    auto *touch = QTest::createTouchDevice();
    const auto touchStart = cards->mapToScene(QPointF(cards->width() - 30, 80)).toPoint();
    QTest::touchEvent(window, touch).press(0, touchStart, window);
    for (int distance : {20, 60, 100, 140}) {
        QTest::touchEvent(window, touch).move(0, touchStart - QPoint(distance, 0), window);
        QTest::qWait(25);
    }
    QTest::touchEvent(window, touch).release(0, touchStart - QPoint(140, 0), window);
    QTRY_VERIFY(cards->property("contentX").toReal() > 0);
    QTRY_VERIFY(!cards->property("moving").toBool());
    QTest::keyClick(window, Qt::Key_Home);
    QTRY_COMPARE(cards->property("contentX").toReal(), 0.0);
    const auto point = cards->mapToScene(QPointF(cards->width() / 2, 80));
    QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), QPoint(), QPoint(-120, 0),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &wheel);
    QTRY_VERIFY(cards->property("contentX").toReal() > 0);
    auto *viewport = item(window, "desktopHomeViewport");
    const auto verticalOffset = viewport->property("contentY").toReal();
    const auto horizontalOffset = cards->property("contentX").toReal();
    QWheelEvent verticalWheel(point, window->mapToGlobal(point.toPoint()), QPoint(), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &verticalWheel);
    QTRY_VERIFY(viewport->property("contentY").toReal() > verticalOffset);
    QCOMPARE(cards->property("contentX").toReal(), horizontalOffset);
    // Restore the row heading before its All action is clicked later.
    QVERIFY(QMetaObject::invokeMethod(home, "scrollToItem", Q_ARG(QVariant, QVariant::fromValue(history))));
    QVERIFY(image.save(storage.filePath("Generation History/new-result.png")));
    QTRY_COMPARE(listProperty(history, "files").first().toMap().value("name").toString(), "new-result.png");
    QVERIFY(QFile::remove(storage.filePath("Generation History/new-result.png")));
    QTRY_COMPARE(listProperty(history, "files").first().toMap().value("name").toString(), "image-00.png");
    QDesktopServices::setUrlHandler("society", this, "captureSocietyUrl");
    m_societyUrl.clear();
    click(window, viewAll);
    QDesktopServices::unsetUrlHandler("society");
    QCOMPARE(m_societyUrl, QUrl("society://generation-history"));
    window->setProperty("resultVisible", true);
    QTRY_VERIFY(!history->isVisible());
    window->setProperty("resultVisible", false);
    QTRY_VERIFY(history->isVisible());
    QTRY_COMPARE(cards->property("count").toInt(), 20);
    QVERIFY(QDir(storage.filePath("Generation History")).entryList(QDir::Files).size() == 25);
}

void GuiTests::historyShowsAnEmptyState()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/empty-history-gui-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *model = window->findChild<QObject *>("generationHistoryModel");
    auto *empty = item(window, "emptyGenerationHistory");
    QVERIFY(model && empty);
    QTRY_VERIFY(!model->property("loading").toBool());
    QTRY_VERIFY(empty->isVisible());
    QCOMPARE(empty->property("label").toString(), "No generated images yet");
    QVERIFY(!item(window, "generationHistoryCards")->isVisible());
    QVERIFY(item(window, "viewAllGenerationHistory")->isEnabled());
}

void GuiTests::mobileHomeUsesFigmaSectionsLimitsAndLvrsNavigation()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/mobile-home-gui-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QImage image(64, 64, QImage::Format_RGB32);
    image.fill(QColor("#8f6ec7"));
    const auto writeFiles = [&](const QString &section, const QString &prefix, int count) {
        for (int index = 0; index < count; ++index) {
            const auto path = storage.filePath(QString("%1/%2-%3.png").arg(section, prefix)
                .arg(index, 2, 10, QChar('0')));
            QVERIFY(image.save(path));
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-index - 60),
                                     QFileDevice::FileModificationTime));
        }
    };
    writeFiles("Files", "file", 25);
    writeFiles("Published", "published", 6);
    writeFiles("Generation History", "history", 25);

    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme);
    QVERIFY(theme->setProperty("targetOverride", "ios"));
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window);
    window->resize(402, 844);
    QVERIFY(QTest::qWaitForWindowExposed(window));

    auto *home = item(window, "mobileHome");
    auto *viewport = item(home, "mobileHomeViewport");
    // QuickGenerate keeps Main.qml as its QObject owner while its visual parent
    // moves into the mobile slot, so resolve it from the window object tree.
    auto *panel = item(window, "quickGenerate");
    auto *slot = item(home, "mobileQuickGenerateSlot");
    auto *navigation = item(home, "mobileHomeNavigation");
    auto *recent = item(home, "recentFileCards");
    auto *published = item(home, "recentPublishedList");
    auto *history = item(home, "mobileGenerationHistoryCards");
    QVERIFY(home && viewport && panel && slot && navigation && recent && published && history);
    QVERIFY(home->isVisible());
    QCOMPARE(panel->parentItem(), slot);
    QCOMPARE(panel->width(), 370.0);
    QCOMPARE(panel->property("contentInset").toReal(), 0.0);
    QCOMPARE(navigation->property("count").toInt(), 5);
    QCOMPARE(navigation->property("currentIndex").toInt(), 0);
    QVERIFY(navigation->property("searchVisible").toBool());
    QCOMPARE(navigation->height(), 88.0);
    const QStringList navigationAssets {"home", "tools", "storage", "notification", "account"};
    for (int index = 0; index < navigationAssets.size(); ++index) {
        auto *tab = visualItem(navigation, QString("mobileNavigationTab%1").arg(index).toUtf8().constData());
        QVERIFY(tab);
        QVERIFY(tab->property("preserveIconColors").toBool());
        const auto source = tab->property("iconSource").toUrl();
        QVERIFY(source.path().endsWith("/Navigation/" + navigationAssets[index] + ".svg"));
        QFile asset(source.isLocalFile() ? source.toLocalFile() : ":" + source.path());
        QVERIFY(asset.open(QIODevice::ReadOnly));
        QVERIFY(asset.size() > 0);
        auto *icon = visualItem(tab, "mobileNavigationIcon");
        QVERIFY(icon);
        QTRY_COMPARE(icon->property("status").toInt(), 1); // Image.Ready
        QCOMPARE(icon->width(), index == 2 ? 17.5 : 24.0);
        QCOMPARE(icon->height(), index == 2 ? 20.5545 : 24.0);
    }
    auto *search = visualItem(navigation, "mobileNavigationSearch");
    QVERIFY(search && search->property("preserveIconColors").toBool());
    QVERIFY(search->property("iconSource").toUrl().path().endsWith("/Navigation/search.svg"));
    QTRY_COMPARE(recent->property("count").toInt(), 20);
    QTRY_COMPARE(published->property("count").toInt(), 4);
    QTRY_COMPARE(history->property("count").toInt(), 20);
    QTRY_VERIFY(viewport->property("contentHeight").toReal() > viewport->height());
    QVERIFY(item(home, "newCanvasAction"));
    QVERIFY(item(home, "imageGenerationAction"));
    QVERIFY(item(home, "videoGenerationAction"));
    QVERIFY(item(home, "boardGenerationAction"));

    const QString capturePath = qEnvironmentVariable("DREAMSCAPES_MOBILE_HOME_CAPTURE");
    if (!capturePath.isEmpty()) {
        const auto capture = window->grabWindow();
        QVERIFY(!capture.isNull());
        QVERIFY(capture.save(capturePath));
    }
}

void GuiTests::desktopHomeSidebarReflowsAndRoutes()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/desktop-home-gui-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QImage preview(240, 180, QImage::Format_RGB32);
    preview.fill(QColor("#426788"));
    for (const auto &section : {QString("Files"), QString("Published"), QString("Generation History")}) {
        for (int index = 0; index < 6; ++index)
            QVERIFY(preview.save(storage.filePath(section + QString("/Study-%1.png").arg(index))));
    }
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *home = item(window, "desktopHome");
    auto *sidebar = item(window, "desktopSidebar");
    auto *prompt = item(window, "quickGenerate");
    auto *slot = item(window, "desktopQuickGenerateSlot");
    QVERIFY(home && sidebar && prompt && slot);
    QCOMPARE(window->property("nativeTitleBarHeight").toReal(), 48.0);
    QCOMPARE(window->property("nativeTitleBarLeftMargin").toReal(), 16.0);
    QCOMPARE(window->property("contentTopInset").toReal(), 56.0);
    QTRY_COMPARE(item(window, "desktopRecentFileCards")->property("count").toInt(), 6);
    QTRY_COMPARE(item(window, "desktopPublishedList")->property("count").toInt(), 4);
    for (int width : {1374, 960, 640, 320}) {
        window->resize(width, 900);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(80);
        QVERIFY(home->isVisible());
        QCOMPARE(prompt->parentItem(), slot);
        const auto contentInset = width < 700 ? 12.0 : 24.0;
        QCOMPARE(bounds(prompt, home).left(), bounds(sidebar, home).right() + contentInset);
        QCOMPARE(bounds(prompt, home).right(), home->width() - contentInset);
        QVERIFY(prompt->width() > 200);
        auto *field = item(prompt, "promptField");
        QVERIFY(field);
        QTRY_COMPARE(bounds(field, prompt).top(), 0.0);
        QVERIFY(!item(prompt, "homePaintCanvas")->isVisible());
        QVERIFY(!item(prompt, "homePaintToggle"));
    }
    window->resize(1374, 720);
    QTest::qWait(100);
    auto *videoAction = visualItem(sidebar, "desktopAction_video");
    auto *imageAction = visualItem(sidebar, "desktopAction_image");
    auto *boardAction = visualItem(sidebar, "desktopAction_board");
    auto *canvasAction = visualItem(sidebar, "desktopAction_canvas");
    QVERIFY(videoAction && imageAction && boardAction && canvasAction);
    prompt->setProperty("prompt", "Preserve this creative brief");
    click(window, videoAction);
    QVERIFY(item(window, "videoGenerationWorkspace")->isVisible());
    QVERIFY(!prompt->isVisible());
    click(window, imageAction);
    QCOMPARE(prompt->property("mediaType").toString(), "Image");
    QVERIFY(item(window, "imageGenerationWorkspace")->isVisible());
    QVERIFY(!prompt->isVisible());
    click(window, boardAction);
    QVERIFY(window->property("generationRequestError").toString().contains("Board"));
    QCOMPARE(prompt->property("prompt").toString(), "Preserve this creative brief");
    click(window, canvasAction);
    QTRY_VERIFY(item(window, "canvasCreateButton")->isVisible());
    click(window, item(window, "canvasCreateButton"));
    QVERIFY(window->property("editorVisible").toBool());
    QVERIFY(QMetaObject::invokeMethod(window, "closeCanvas"));
    QVERIFY(home->isVisible());
    QCOMPARE(prompt->property("prompt").toString(), "Preserve this creative brief");
    click(window, imageAction);
    QCOMPARE(sidebar->width(), 181.0);
    QCOMPARE(imageAction->height(), 24.0);
    auto *toolbar = item(window, "desktopHomeToolbar");
    QVERIFY(toolbar && toolbar->isVisible());
    QCOMPARE(bounds(toolbar, window->contentItem()).top(), 13.0);
    QCOMPARE(bounds(toolbar, window->contentItem()).right(), window->width() - 12.0);
    const auto controls = window->property("nativeTitleBarControlsRect").toRectF();
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "cocoa")
        QVERIFY2(!controls.isEmpty(), "Native macOS must expose the traffic-light geometry.");
#endif
    if (!controls.isEmpty()) {
        QCOMPARE(controls.left(), 16.0);
        QVERIFY(qAbs(controls.center().y() - 24.0) < 0.5);
        QVERIFY(controls.right() + 12.0 <= bounds(toolbar, window->contentItem()).left());
    }
    click(window, visualItem(sidebar, "desktopAction_home"));
    QVERIFY(bounds(toolbar, window->contentItem()).bottom() <= bounds(prompt, window->contentItem()).top());
    auto *recent = visualItem(home, "desktopRecentFile0");
    auto *published = item(home, "desktopPublishedList");
    QVERIFY(recent && published);
    QVERIFY(bounds(recent, home).bottom() < bounds(published, home).top());
    auto *cards = item(window, "generationHistoryCards");
    QVERIFY(cards);
    QVERIFY(bounds(cards, home).top() < bounds(published, home).top());
    const auto capture = qEnvironmentVariable("DREAMSCAPES_DESKTOP_HOME_CAPTURE");
    if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture));
}

void GuiTests::desktopHomeSidebarMatchesFigmaAndScrolls()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Home/DesktopHomeSidebar.qml"));
    QQuickWindow window;
    QScopedPointer<QQuickItem> sidebar(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(sidebar, qPrintable(component.errorString()));
    sidebar->setParentItem(window.contentItem());
    sidebar->setSize(QSizeF(181, 694));
    window.resize(181, 694);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_COMPARE(sidebar->property("contentHeight").toReal(), 262.0);
    QCOMPARE(sidebar->property("contentPadding").toReal(), 8.0);
    auto *viewport = visualItem(sidebar.data(), "list_itemsViewport");
    QVERIFY(viewport);
    QSignalSpy actions(sidebar.data(), SIGNAL(actionRequested(QString)));
    QVERIFY(actions.isValid());
    struct Row { const char *key; const char *label; const char *icon; const char *asset; qreal y; };
    const Row rows[] = {
        {"home", "Home", "nodeshomeFolder", "home.svg", 8},
        {"canvas", "New Canvas", "imagefitContent", "canvas.svg", 35},
        {"image", "Image", "unconditionalImageGeneration", "image.svg", 59},
        {"video", "Video", "imageToVideo", "video.svg", 83},
        {"audio", "Audio", "volume", "audio.svg", 107},
        {"board", "Board", "pattern", "board.svg", 131},
        {"tools", "Tools", "collection", "tools.svg", 155},
        {"files", "Files", "sqlFile", "sqlFile.svg", 182},
        {"assets", "Assets", "asset-library", "assets.svg", 206},
        {"history", "Generation History", "profileCPU", "history.svg", 230}
    };
    for (const auto &expected : rows) {
        auto *row = visualItem(sidebar.data(), qPrintable(QString("desktopAction_") + expected.key));
        QVERIFY(row && row->isVisible());
        QCOMPARE(bounds(row, sidebar.data()), QRectF(8, expected.y, 165, 24));
        QCOMPARE(row->property("label").toString(), QString(expected.label));
        QCOMPARE(row->property("iconName").toString(), QString(expected.icon));
        QVERIFY(!row->property("keyVisible").toBool());
        QVERIFY(!row->property("showChevron").toBool());
        QCOMPARE(row->property("state").toInt(), QString(expected.key) == "home" ? 1 : 0);
        QCOMPARE(row->property("backgroundColor").value<QColor>(), QString(expected.key) == "home"
            ? QColor("#25324D") : QColor("transparent"));
        QCOMPARE(row->property("backgroundColorPressed").value<QColor>(), QColor("#25324D"));
        auto *icon = visualItem(row, QString(expected.key) == "home" ? "iconButton_icon" : "menuItem_iconImage");
        QVERIFY(icon && icon->isVisible());
        QTRY_COMPARE(icon->property("status").toInt(), 1); // Image.Ready
        const auto extent = QString(expected.key) == "home" ? 16.0 : 18.0;
        QCOMPARE(bounds(icon, row), QRectF(4, 3, extent, extent));
        const auto source = icon->property("source").toUrl();
        QCOMPARE(source.fileName(), QString(expected.asset));
        QFile asset(source.isLocalFile() ? source.toLocalFile() : ':' + source.path());
        QVERIFY(asset.exists() && asset.size() > 0);
        auto *label = visualItem(row, "menuItem_labelNode");
        QVERIFY(label);
        QCOMPARE(bounds(label, row).left(), 30.0);
        QCOMPARE(label->property("font").value<QFont>().pixelSize(), 13);
        click(&window, row);
        QVERIFY(!actions.isEmpty());
        QCOMPARE(actions.last().first().toString(), QString(expected.key));
    }
    for (const auto &[name, y] : {std::pair{"divider1", 32.0}, std::pair{"divider2", 179.0}}) {
        auto *divider = visualItem(sidebar.data(), qPrintable(QString("desktopDivider_") + name));
        QVERIFY(divider && divider->isVisible());
        QCOMPARE(bounds(divider, sidebar.data()), QRectF(8, y, 165, 3));
        auto *line = visualItem(divider, "menuDivider_line");
        QVERIFY(line);
        QCOMPARE(bounds(line, sidebar.data()), QRectF(8, y + 1, 165, 1));
    }
    auto *history = visualItem(sidebar.data(), "desktopAction_history");
    QCOMPARE(sidebar->property("contentHeight").toReal() - bounds(history, sidebar.data()).bottom(), 8.0);

    window.resize(181, 120);
    sidebar->setHeight(120);
    QTRY_COMPARE(viewport->height(), 120.0);
    QVERIFY(viewport->property("interactive").toBool());
    auto *home = visualItem(sidebar.data(), "desktopAction_home");
    home->forceActiveFocus();
    history->forceActiveFocus();
    QTRY_COMPARE(viewport->property("contentY").toReal(), 142.0);
    QCOMPARE(bounds(history, sidebar.data()).bottom(), 112.0);
    QTest::keyClick(&window, Qt::Key_Space);
    QCOMPARE(actions.last().first().toString(), "history");
    home->forceActiveFocus();
    QTRY_COMPARE(viewport->property("contentY").toReal(), 0.0);
    QCOMPARE(bounds(home, sidebar.data()).top(), 8.0);

    sidebar->setProperty("compact", true);
    sidebar->setSize(QSizeF(48, 694));
    window.resize(48, 694);
    QTRY_COMPARE(bounds(home, sidebar.data()), QRectF(8, 8, 32, 24));
    for (const auto &expected : rows) {
        auto *row = visualItem(sidebar.data(), qPrintable(QString("desktopAction_") + expected.key));
        QCOMPARE(row->width(), 32.0);
        QCOMPARE(row->property("label").toString(), QString());
        QQmlExpression accessibleName(qmlContext(row), row, "Accessible.name");
        const auto name = accessibleName.evaluate();
        QVERIFY2(!accessibleName.hasError(), qPrintable(accessibleName.error().toString()));
        QCOMPARE(name.toString(), QString(expected.label));
    }

    QFile source(sourceUrl("Views/Home/DesktopHomeSidebar.qml").toLocalFile());
    QVERIFY(source.open(QIODevice::ReadOnly));
    const auto qml = QString::fromUtf8(source.readAll());
    const QRegularExpression externalViews(R"((?:^|\n)\s*(?:Item|Rectangle|Image|Flickable|ListView|RowLayout|ColumnLayout|Controls\.[A-Za-z]+)\s*\{)");
    QVERIFY(!externalViews.match(qml).hasMatch());
}

void GuiTests::desktopHomeRendersFigmaFrame()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/desktop-render-gui-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QImage preview(240, 180, QImage::Format_RGB32);
    preview.fill(QColor("#426788"));
    for (const auto &section : {QString("Files"), QString("Published"), QString("Generation History")})
        for (int index = 0; index < 6; ++index)
            QVERIFY(preview.save(storage.filePath(section + QString("/Study-%1.png").arg(index))));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(1374, 720);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE(item(window, "desktopPublishedList")->property("count").toInt(), 4);
    QTRY_COMPARE(item(window, "generationHistoryCards")->property("count").toInt(), 6);
    QTest::qWait(200);
    int exportedImages = 0;
    const auto checkImages = [&](auto &&self, QQuickItem *parent) -> void {
        for (auto *child : parent->childItems()) {
            const auto source = child->property("source").toUrl().toString();
            if (child->isVisible() && source.contains("Assets/Desktop/") && child->property("status").isValid()) {
                ++exportedImages;
                QCOMPARE(child->property("status").toInt(), 1); // Image.Ready
                const auto asset = QUrl(source).fileName();
                const QSizeF expected = asset == "home.svg" ? QSizeF(16, 16)
                    : asset == "files.svg" ? QSizeF(13.375, 15.6659)
                    : asset == "search.svg" ? QSizeF(12, 12)
                    : asset == "spark.svg" || asset == "open.svg" ? QSizeF(24, 24) : QSizeF(18, 18);
                QCOMPARE(child->size(), expected);
            }
            self(self, child);
        }
    };
    checkImages(checkImages, window->contentItem());
    QVERIFY(exportedImages >= 12); // 9 original sidebar exports + 3 toolbar; Files uses LVRS sqlFile.
    auto *filesIcon = visualItem(item(window, "desktopSidebar"), "desktopAction_files");
    QVERIFY(filesIcon);
    filesIcon = visualItem(filesIcon, "menuItem_iconImage");
    QVERIFY(filesIcon);
    QTRY_COMPARE(filesIcon->property("status").toInt(), 1);
    QCOMPARE(filesIcon->size(), QSizeF(18, 18));
    QVERIFY(filesIcon->property("source").toUrl().path().endsWith("/sqlFile.svg"));
    auto *styles = item(window, "desktopStyles");
    QVERIFY(styles);
    for (int index = 0; index < 5; ++index) {
        auto *card = visualItem(styles, qPrintable(QString("desktopStyle%1").arg(index)));
        QTRY_VERIFY(card);
        QTRY_COMPARE(card->property("previewStatus").toInt(), 1);
        QCOMPARE(card->height(), 224.0);
        QCOMPARE(card->width(), styles->property("cardWidth").toReal());
        const auto source = card->property("previewSource").toUrl();
        QVERIFY(source.isLocalFile());
        QFile asset(source.toLocalFile());
        QVERIFY(asset.exists() && asset.size() > 0);
        QCOMPARE(source.fileName(), index == 0 ? "landscape.svg" : index == 1 ? "studio.svg" : index == 2 ? "poster.svg" : "fluid.svg");
    }
    const auto contentCapture = qEnvironmentVariable("DREAMSCAPES_DESKTOP_HOME_CONTENT_CAPTURE");
    if (!contentCapture.isEmpty()) {
        auto *body = item(window, "desktopHomeContent");
        QVERIFY(body);
        auto grab = body->grabToImage();
        QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
        QVERIFY(ready.wait(3000));
        QVERIFY(grab->saveToFile(contentCapture));
    }
    const auto capture = qEnvironmentVariable("DREAMSCAPES_DESKTOP_HOME_CAPTURE");
    if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture));
}

void GuiTests::desktopHomeContinuousRowsAndPromptStarters()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/home-rows-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->resize(1374, 900);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *home = item(window, "desktopHome");
    auto *viewport = item(home, "desktopHomeViewport");
    auto *quick = item(window, "quickGenerate");
    QVERIFY(home && viewport && quick);
    QSignalSpy requests(window, SIGNAL(generateRequested(QString,QString,QString,int)));
    QVERIFY(requests.isValid());
    QQuickItem *previous = quick;
    for (const auto *name : {"desktopRecentFiles", "generationHistory", "desktopStyles", "desktopPromptStarters", "desktopPublishedList"}) {
        auto *row = item(home, name);
        QVERIFY2(row, name);
        QVERIFY(bounds(row, home).top() >= bounds(previous, home).bottom() + 24);
        QCOMPARE(row->width(), quick->width());
        previous = row;
    }
    QCOMPARE(quick->width(), 1145.0);
    QCOMPARE(bounds(quick, home).top(), 20.0);
    auto *paint = item(quick, "homePaintCanvas");
    auto *prompt = item(quick, "promptField");
    QVERIFY(paint && prompt);
    QVERIFY(!paint->isVisible());
    QVERIFY(!item(quick, "homePaintToggle"));
    QCOMPARE(bounds(prompt, quick).top(), 0.0);
    auto *actions = item(quick, "quickGenerateActions");
    QVERIFY(actions);
    QCOMPARE(bounds(actions, quick).top(), bounds(prompt, quick).bottom() + 16);
    auto *canvas = quick->findChild<HomeCanvas *>("homeCanvasPixels");
    QVERIFY(canvas);
    quick->setProperty("aspectRatio", "16:9");
    quick->setProperty("generationCount", 3);
    auto *styles = item(home, "desktopStyles");
    QVERIFY(QMetaObject::invokeMethod(home, "scrollToItem", Q_ARG(QVariant, QVariant::fromValue(styles))));
    auto *style = visualItem(styles, "desktopStyle0");
    QTRY_VERIFY(style);
    QTRY_COMPARE(style->property("previewStatus").toInt(), 1);
    click(window, style);
    QTRY_VERIFY(quick->property("prompt").toString().contains("landscape"));
    QTRY_COMPARE(viewport->property("contentY").toReal(), 0.0);
    auto *starters = item(home, "desktopPromptStarters");
    QVERIFY(QMetaObject::invokeMethod(home, "scrollToItem", Q_ARG(QVariant, QVariant::fromValue(starters))));
    auto *usePrompt = visualItem(starters, "homeUsePrompt0");
    QVERIFY(usePrompt);
    click(window, usePrompt);
    QTRY_VERIFY(quick->property("prompt").toString().contains("sculptural object"));
    QCOMPARE(quick->property("aspectRatio").toString(), "16:9");
    QCOMPARE(quick->property("generationCount").toInt(), 3);
    QVERIFY(!canvas->hasContent());
    QVERIFY(QMetaObject::invokeMethod(home, "scrollToItem", Q_ARG(QVariant, QVariant::fromValue(starters))));
    click(window, item(starters, "homeBrowsePrompts"));
    auto *promptMenu = starters->findChild<QObject *>("homePromptMenu");
    QVERIFY(promptMenu);
    QTRY_VERIFY(promptMenu->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(promptMenu, "triggerEntry", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(!promptMenu->property("visible").toBool());
    QTRY_COMPARE(viewport->property("contentY").toReal(), 0.0);
    QImage reference(80, 40, QImage::Format_ARGB32);
    reference.fill(QColor("#496c68"));
    const auto attachment = QUrl::fromLocalFile(storage.filePath("reference.png"));
    QVERIFY(reference.save(attachment.toLocalFile()));
    QVariant attached;
    QVERIFY(QMetaObject::invokeMethod(quick, "addAttachment", Q_RETURN_ARG(QVariant, attached),
                                     Q_ARG(QVariant, QVariant(QUrl::fromLocalFile(storage.filePath("missing.png"))))));
    QVERIFY(!attached.toBool());
    auto *notice = item(quick, "quickGenerateNotice");
    QVERIFY(notice);
    QTRY_VERIFY(notice->isVisible());
    QVERIFY(!notice->property("text").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(quick, "addAttachment", Q_RETURN_ARG(QVariant, attached),
                                     Q_ARG(QVariant, QVariant(attachment))));
    QVERIFY(attached.toBool());
    QTRY_VERIFY(!notice->isVisible());
    auto *attachmentSlot = item(quick, "homeAttachmentsSlot");
    QVERIFY(attachmentSlot);
    QTRY_VERIFY(attachmentSlot->isVisible());
    QTRY_COMPARE(attachmentSlot->height(), 64.0);
    QCOMPARE(bounds(attachmentSlot, quick).top(), bounds(prompt, quick).bottom() + 16);
    QCOMPARE(bounds(actions, quick).top(), bounds(attachmentSlot, quick).bottom() + 16);
    QVERIFY(!paint->isVisible());
    QCOMPARE(bounds(prompt, quick).top(), 0.0);
    QVERIFY(quick->property("hasCanvasInputs").toBool());
    auto *drag = visualItem(quick, "homeAttachmentDrag_0");
    QVERIFY(drag);
    QTRY_VERIFY(!drag->property("enabled").toBool());
    QVERIFY(!canvas->hasContent());
    QVariant arguments;
    QVERIFY(QMetaObject::invokeMethod(quick, "generationParameters", Q_RETURN_ARG(QVariant, arguments),
                                     Q_ARG(QVariant, QVariant(QString("model")))));
    const auto parameters = arguments.metaType() == QMetaType::fromType<QJSValue>()
        ? arguments.value<QJSValue>().toVariant().toMap() : arguments.toMap();
    QCOMPARE(parameters.value("referenceImages").toList(), QVariantList{attachment.toString()});
    QCOMPARE(parameters.value("width").toInt(), 1824);
    QCOMPARE(parameters.value("height").toInt(), 1024);
    QQuickItem *remove = nullptr;
    for (auto *control : drag->parentItem()->findChildren<QQuickItem *>()) {
        if (control->property("text").toString() == "Remove") { remove = control; break; }
    }
    QVERIFY(remove);
    click(window, remove);
    QTRY_VERIFY(!attachmentSlot->isVisible());
    QVERIFY(!quick->property("hasCanvasInputs").toBool());
    QTRY_COMPARE(bounds(actions, quick).top(), bounds(prompt, quick).bottom() + 16);
    QCOMPARE(quick->property("aspectRatio").toString(), "16:9");
    QCOMPARE(quick->property("prompt").toString(), "A sculptural object, soft studio light and a simple backdrop.");
    QCOMPARE(requests.size(), 0); // Selecting inspiration never submits a generation.
}

void GuiTests::desktopHomeSearchesSocietyAndUpdates()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/desktop-search-gui-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QImage preview(32, 32, QImage::Format_RGB32);
    preview.fill(Qt::darkCyan);
    for (const auto &section : {QString("Files"), QString("Published"), QString("Generation History")}) {
        for (int index = 0; index < 25; ++index) {
            const auto path = storage.filePath(section + QString("/Study-%1.png").arg(index, 2, 10, QChar('0')));
            QVERIFY(preview.save(path));
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::fromSecsSinceEpoch(1700000000 + index), QFileDevice::FileModificationTime));
        }
    }
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(1374, 900);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *home = item(window, "desktopHome");
    auto *search = item(window, "desktopHomeSearch");
    auto *model = window->findChild<QObject *>("generationHistoryModel");
    auto *recent = item(window, "desktopRecentFiles");
    auto *published = item(window, "desktopPublishedList");
    auto *history = item(window, "generationHistory");
    QVERIFY(home && search && model && recent && published && history);
    QTRY_COMPARE(listProperty(recent, "visibleFiles").size(), 20);
    QCOMPARE(listProperty(published, "visibleFiles").size(), 4);
    QCOMPARE(listProperty(history, "files").size(), 20);
    // Query the whole Society snapshot, including a file outside the initial 20.
    search->setProperty("text", "sTuDy-00");
    QTRY_COMPARE(model->property("query").toString(), "sTuDy-00");
    QTRY_COMPARE(listProperty(recent, "visibleFiles").size(), 1);
    QCOMPARE(listProperty(published, "visibleFiles").size(), 1);
    QCOMPARE(listProperty(history, "files").size(), 1);
    search->setProperty("text", "new-arrival");
    QTRY_COMPARE(listProperty(recent, "visibleFiles").size(), 0);
    QVERIFY(item(window, "desktopRecentFilesEmpty")->isVisible());
    const auto path = storage.filePath("Files/new-arrival.png");
    QVERIFY(preview.save(path));
    QTRY_COMPARE(listProperty(recent, "visibleFiles").size(), 1);
    QVERIFY(QFile::remove(path));
    QTRY_COMPARE(listProperty(recent, "visibleFiles").size(), 0);
    search->setProperty("text", "");
    QTRY_COMPARE(listProperty(recent, "visibleFiles").size(), 20);
    QDesktopServices::setUrlHandler("society", this, "captureSocietyUrl");
    click(window, visualItem(home, "desktopAction_history"));
    QCOMPARE(m_societyUrl, QUrl("society://generation-history"));
    QDesktopServices::unsetUrlHandler("society");
    click(window, visualItem(home, "desktopAction_audio"));
    QVERIFY(window->property("generationRequestError").toString().contains("Audio"));
    click(window, item(window, "desktopAccount"));
    QTRY_VERIFY(window->findChild<QObject *>("desktopAccountMenu")->property("opened").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->findChild<QObject *>("desktopAccountMenu")->property("visible").toBool());
    click(window, item(window, "desktopNotifications"));
    QTRY_VERIFY(window->findChild<QObject *>("desktopNotificationMenu")->property("opened").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
}

void GuiTests::foregroundApplicationPreparesBeforeGenerate()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/foreground-gui-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/gui.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly));
    model.write("foreground protocol test model");
    model.close();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *controller = window->findChild<GenerationController *>("generationController");
    QVERIFY(controller && controller->connected());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    window->requestActivate();
    QTRY_COMPARE(QGuiApplication::applicationState(), Qt::ApplicationActive);
    QTRY_VERIFY(controller->foreground());
    QTRY_VERIFY(controller->inferenceStatus().value("ready").toBool());
    QVERIFY(controller->jobs().isEmpty());
    QVERIFY(!controller->busy());
    QVERIFY(controller->latestImage().isEmpty());
    QVERIFY(QDir(storage.filePath("Generation History")).isEmpty());
}

void GuiTests::resultImageContextMenu_data()
{
    QTest::addColumn<QString>("gesture");
    QTest::addColumn<bool>("galleryMode");
    QTest::newRow("right-click") << QString("right-click") << false;
    QTest::newRow("mouse-hold") << QString("mouse-hold") << false;
    QTest::newRow("touch-hold") << QString("touch-hold") << false;
    QTest::newRow("gallery-right-click") << QString("right-click") << true;
    QTest::newRow("gallery-mouse-hold") << QString("mouse-hold") << true;
    QTest::newRow("gallery-touch-hold") << QString("touch-hold") << true;
}

void GuiTests::resultImageContextMenu()
{
    QFETCH(QString, gesture);
    QFETCH(bool, galleryMode);
    QTemporaryDir images(DREAMSCAPES_TEST_DIRECTORY "/result-menu-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(images.path()));
    QImage fixture(600, 300, QImage::Format_RGB32);
    fixture.fill(Qt::green);
    const auto source = QUrl::fromLocalFile(images.filePath("original image.png"));
    QVERIFY(fixture.save(source.toLocalFile()));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", images.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    if (gesture == "touch-hold") window->resize(320, 480);
    QVERIFY(window->setProperty("currentResult", QVariantMap{{"imageSource", source}}));
    QVERIFY(window->setProperty("resultVisible", true));
    auto *preview = item(window, "generatedImage");
    auto *result = item(window, "generationResult");
    auto *gallery = item(window, "resultGallery");
    QQuickItem *targetImage = preview;
    if (galleryMode) {
        const auto nextSource = QUrl::fromLocalFile(images.filePath("next image.png"));
        QVERIFY(fixture.save(nextSource.toLocalFile()));
        QVERIFY(window->setProperty("currentResult", QVariantMap{{"imageSource", nextSource}}));
        QVERIFY(result->setProperty("results", QVariantList{QVariantMap{{"imageSource", source}},
            QVariantMap{{"imageSource", nextSource}}}));
        QTRY_VERIFY(QMetaObject::invokeMethod(gallery, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, targetImage), Q_ARG(int, 0)) && targetImage);
        QTRY_VERIFY(targetImage->property("imageReady").toBool());
    } else QTRY_COMPARE(preview->property("status").toInt(), 1);
    auto *menu = window->findChild<QObject *>("imageContextMenu");
    QVERIFY2(menu, "Generated images need a file-save context menu.");
    const auto point = targetImage->mapToScene(targetImage->boundingRect().center()).toPoint();
    // A short primary click must not open the context menu.
    if (!galleryMode) QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
    QVERIFY(!menu->property("visible").toBool());
    if (gesture == "right-click") {
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
    } else if (gesture == "mouse-hold") {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_VERIFY(menu->property("opened").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
    } else {
        auto *touch = QTest::createTouchDevice();
        QTest::touchEvent(window, touch).press(0, point, window);
        QTRY_VERIFY(menu->property("opened").toBool());
        QTest::touchEvent(window, touch).release(0, point, window);
    }
    QTRY_VERIFY(menu->property("opened").toBool());
    auto *save = menuEntry(window->contentItem(), QStringLiteral("Save to File"));
    QVERIFY(save && save->isEnabled());
    const auto menuBounds = save->mapRectToScene(save->boundingRect());
    QVERIFY(menuBounds.left() >= 0 && menuBounds.right() <= window->width());
    QVERIFY(menuBounds.top() >= 0 && menuBounds.bottom() <= window->height());
    QCOMPARE(result->property("imageSource").toUrl(), source);
    if (galleryMode) QVERIFY(gallery->isVisible());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!menu->property("visible").toBool());
    // Temporary denoising previews cannot be exported as completed images.
    QVERIFY(result->setProperty("previewSource", source));
    if (galleryMode) {
        QVERIFY(result->setProperty("generationPending", true));
        QQuickItem *pending = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(gallery, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, pending), Q_ARG(int, 2)) && pending);
        QVERIFY(!pending->property("imageReady").toBool());
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
            pending->mapToScene(pending->boundingRect().center()).toPoint());
    } else QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
    QVERIFY(!menu->property("visible").toBool());
}

void GuiTests::resultImageSaveDialogPreservesSelectionAndCancel()
{
    QTemporaryDir images(DREAMSCAPES_TEST_DIRECTORY "/result-save-dialog-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(images.path()));
    QImage original(1200, 800, QImage::Format_RGB32);
    original.fill(Qt::yellow);
    const auto source = QUrl::fromLocalFile(images.filePath("original #1.png"));
    QVERIFY(original.save(source.toLocalFile()));
    QImage next(64, 64, QImage::Format_RGB32);
    next.fill(Qt::blue);
    const auto nextSource = QUrl::fromLocalFile(images.filePath("next.png"));
    QVERIFY(next.save(nextSource.toLocalFile()));
    const auto target = QUrl::fromLocalFile(images.filePath(QStringLiteral("저장한 이미지.png")));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", images.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    QVERIFY(window->setProperty("currentResult", QVariantMap{{"imageSource", source}}));
    QVERIFY(window->setProperty("resultVisible", true));
    auto *preview = item(window, "generatedImage");
    auto *result = item(window, "generationResult");
    auto *menu = window->findChild<QObject *>("imageContextMenu");
    auto *dialog = window->findChild<QObject *>("saveImageDialog");
    QVERIFY(preview && result && menu && dialog);
    QVERIFY(dialog->setProperty("currentFolder", QUrl::fromLocalFile(images.path())));
    QTRY_COMPARE(preview->property("status").toInt(), 1);
    auto *exporter = window->findChild<ImageFileExporter *>("imageFileExporter");
    QVERIFY(exporter);
    QSignalSpy saved(exporter, &ImageFileExporter::saved);
    QSignalSpy failed(exporter, &ImageFileExporter::failed);
    const auto point = preview->mapToScene(preview->boundingRect().center()).toPoint();
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
    QTRY_VERIFY(menu->property("opened").toBool());
    auto *entry = menuEntry(window->contentItem(), QStringLiteral("Save to File"));
    QVERIFY(entry);
    click(window, entry);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QCOMPARE(dialog->property("sourceImage").toUrl(), source);
    QCOMPARE(dialog->property("defaultSuffix").toString(), QString("png"));
    QVERIFY(dialog->setProperty("selectedFile", target));
    QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(!QFileInfo::exists(target.toLocalFile()));
    QCOMPARE(saved.size(), 0);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(preview->property("source").toUrl(), source);

    QTRY_VERIFY(!menu->property("visible").toBool());
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
    QTRY_VERIFY(menu->property("opened").toBool());
    click(window, entry);
    QTRY_VERIFY(dialog->property("visible").toBool());
    auto *fileName = item(window, "fileNameTextField");
    QVERIFY(fileName && fileName->isVisible());
    QVERIFY(fileName->setProperty("text", QFileInfo(target.toLocalFile()).fileName()));
    QVERIFY(QMetaObject::invokeMethod(fileName, "textEdited"));
    QVERIFY(QMetaObject::invokeMethod(fileName, "editingFinished"));
    QVERIFY(window->setProperty("currentResult", QVariantMap{{"imageSource", nextSource}}));
    QTRY_COMPARE(preview->property("source").toUrl(), nextSource);
    QCOMPARE(dialog->property("sourceImage").toUrl(), source);
    QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
    QTRY_COMPARE(saved.size(), 1);
    QCOMPARE(failed.size(), 0);
    QCOMPARE(saved.first().first().toUrl(), target);
    QFile originalFile(source.toLocalFile()), savedFile(target.toLocalFile());
    QVERIFY(originalFile.open(QIODevice::ReadOnly) && savedFile.open(QIODevice::ReadOnly));
    QCOMPARE(savedFile.readAll(), originalFile.readAll());
    QCOMPARE(QImage(target.toLocalFile()).size(), original.size());
    QCOMPARE(result->property("saveFeedback").toString(), QStringLiteral("File saved"));
    QCOMPARE(preview->property("source").toUrl(), nextSource);
}

void GuiTests::resultImageSaveToPhotos()
{
    QTemporaryDir images(DREAMSCAPES_TEST_DIRECTORY "/result-photos-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(images.path()));
    QImage image(600, 400, QImage::Format_RGB32);
    image.fill(Qt::magenta);
    const auto source = QUrl::fromLocalFile(images.filePath("photo.png"));
    QVERIFY(image.save(source.toLocalFile()));
    const auto nextSource = QUrl::fromLocalFile(images.filePath("next.png"));
    image.fill(Qt::cyan);
    QVERIFY(image.save(nextSource.toLocalFile()));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", images.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    window->resize(320, 480);
    QVERIFY(window->setProperty("currentResult", QVariantMap{{"imageSource", source}}));
    QVERIFY(window->setProperty("resultVisible", true));
    auto *result = item(window, "generationResult");
    auto *preview = item(window, "generatedImage");
    auto *menu = window->findChild<QObject *>("imageContextMenu");
    auto *dialog = window->findChild<QObject *>("saveImageDialog");
    QTRY_COMPARE(preview->property("status").toInt(), 1);
    int requests = 0;
    QString imported;
    PhotoLibraryExporter::Completion finish;
    auto *exporter = new PhotoLibraryExporter([&](const QString &path, auto callback) {
        ++requests;
        imported = path;
        finish = callback;
    }, window);
    QVERIFY(result->setProperty("photoLibrary", QVariant::fromValue(exporter)));
    const auto point = preview->mapToScene(preview->boundingRect().center()).toPoint();
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
    QTRY_VERIFY(menu->property("opened").toBool());
    auto *photo = menuEntry(window->contentItem(), QStringLiteral("Save to Photos"));
    QVERIFY(photo && photo->isEnabled());
    auto *file = menuEntry(window->contentItem(), QStringLiteral("Save to File"));
    QVERIFY(file && file->isEnabled());
    const auto photoBounds = photo->mapRectToScene(photo->boundingRect());
    QVERIFY(photoBounds.left() >= 0 && photoBounds.right() <= window->width());
    QVERIFY(photoBounds.top() >= 0 && photoBounds.bottom() <= window->height());
    click(window, photo);
    QTRY_VERIFY(exporter->busy());
    QCOMPARE(requests, 1);
    QCOMPARE(imported, source.toLocalFile());
    QVERIFY(!dialog->property("visible").toBool());
    QCOMPARE(result->property("saveFeedback").toString(), QStringLiteral("Saving to Photos…"));
    QVERIFY(QMetaObject::invokeMethod(result, "saveImageToPhotos"));
    QCOMPARE(requests, 1);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
    QTRY_VERIFY(menu->property("opened").toBool());
    photo = menuEntry(window->contentItem(), QStringLiteral("Save to Photos"));
    QVERIFY(photo && !photo->isEnabled());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QVERIFY(window->setProperty("currentResult", QVariantMap{{"imageSource", nextSource}}));
    finish("photo-library-id", {});
    QTRY_COMPARE(result->property("saveFeedback").toString(), QStringLiteral("Saved to Photos"));
    QCOMPARE(imported, source.toLocalFile());
    QTRY_COMPARE(preview->property("status").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(result, "saveImageToPhotos"));
    QCOMPARE(requests, 2);
    finish({}, "Permission to add photos was denied.");
    QTRY_COMPARE(result->property("saveFeedback").toString(), QStringLiteral("Permission to add photos was denied."));
    QVERIFY(!exporter->busy());
    // Desktop platforms without a system photo backend retain only file export.
    auto *unsupported = new PhotoLibraryExporter({}, window);
    QVERIFY(result->setProperty("photoLibrary", QVariant::fromValue(unsupported)));
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, point);
    QTRY_VERIFY(menu->property("opened").toBool());
    QVERIFY(menuEntry(window->contentItem(), QStringLiteral("Save to File")));
    QVERIFY(!menuEntry(window->contentItem(), QStringLiteral("Save to Photos")));
}

void GuiTests::viewsLeaveWindowChromeAvailable()
{
    QQmlApplicationEngine engine;
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *chrome = item(window, "windowChromeInteractionLayer");
    auto *handle = item(window, "windowDragHandle");
    auto *quick = item(window, "quickGenerate");
    QVERIFY(chrome && handle && quick);
    const auto handleBottom = [&] { return handle->mapToScene(QPointF(0, handle->height())).y(); };
    QVERIFY2(quick->mapToScene(QPointF()).y() >= handleBottom() + 8.0,
             "Home must leave the native traffic lights and LVRS move handle unobstructed.");
    auto *content = item(window, "appContent");
    QVERIFY(content && content->clip());
    for (const bool showResult : {false, true, false}) {
        QVERIFY(window->setProperty("resultVisible", showResult));
        QTRY_COMPARE(content->mapToScene(QPointF()).y(), window->property("contentTopInset").toReal());
        QVERIFY(content->mapToScene(QPointF()).y() >= handleBottom() + 8.0);
        const auto point = handle->mapToItem(chrome, handle->width() / 2, handle->height() / 2);
        QVariant excluded;
        QVERIFY(QMetaObject::invokeMethod(chrome, "movePointIsExcluded", Q_RETURN_ARG(QVariant, excluded),
                                         Q_ARG(QVariant, point.x()), Q_ARG(QVariant, point.y())));
        QVERIFY2(!excluded.toBool(), "The title bar must remain available for native dragging on every view.");
        if (showResult) {
            auto *back = item(window, "resultBackButton");
            QTRY_VERIFY(back->mapToScene(QPointF()).y() >= handleBottom());
            click(window, back);
            QTRY_VERIFY(!window->property("resultVisible").toBool());
        }
    }
    QVERIFY(window->setProperty("windowDragHandleHeight", 44));
    QVERIFY(window->setProperty("windowDragHandleTopMargin", 4));
    QTRY_COMPARE(content->mapToScene(QPointF()).y(), 56.0);
    QVERIFY(window->setProperty("windowChromeInteractionsEnabled", false));
    QTRY_COMPARE(content->mapToScene(QPointF()).y(), 56.0);
    QVERIFY(bounds(item(window, "desktopHomeToolbar"), window->contentItem()).bottom()
            <= quick->mapToScene(QPointF()).y());
}

void GuiTests::homePromptDragDoesNotMoveWindow_data()
{
    QTest::addColumn<int>("width");
    QTest::newRow("desktop") << 1374;
    QTest::newRow("compact") << 640;
}

void GuiTests::homePromptDragDoesNotMoveWindow()
{
    QFETCH(int, width);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/window-drag-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(width, 900);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *handle = item(window, "windowDragHandle");
    auto *quick = item(window, "quickGenerate");
    auto *prompt = item(quick, "promptField");
    QVERIFY(handle && quick && prompt);
    QTRY_COMPARE(handle->height(), 40.0);
    QCOMPARE(handle->mapToScene(QPointF()).y(), 0.0);
    QVERIFY(handle->isEnabled());
    QVERIFY(handle->contains(handle->mapFromScene(QPointF(width / 2, 39))));
    QVERIFY(!handle->contains(handle->mapFromScene(QPointF(width / 2, 40))));
    QSignalSpy moves(window, SIGNAL(windowMoveAttempted(bool)));
    QVERIFY(moves.isValid());
    const auto position = window->position();
    // The clear strip below the 40px handle must not initiate window movement.
    const QPoint bodyPoint(width / 2, 45);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, bodyPoint);
    QTest::mouseMove(window, bodyPoint + QPoint(30, 30), 30);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, bodyPoint + QPoint(30, 30));
    QCOMPARE(moves.count(), 0);
    QCOMPARE(window->position(), position);
    const QString draft = "Preserve the QuickGenerate prompt while selecting text.";
    QVERIFY(quick->setProperty("prompt", draft));
    QTRY_COMPARE(bounds(prompt, quick).top(), 0.0);
    QVERIFY(!item(quick, "homePaintCanvas")->isVisible());
    const auto start = prompt->mapToScene(QPointF(12, prompt->height() / 2)).toPoint();
    const auto finish = prompt->mapToScene(QPointF(qMin(160.0, prompt->width() - 12), prompt->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window, finish, 30);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, finish);
    QCOMPARE(quick->property("prompt").toString(), draft);
    QCOMPARE(moves.count(), 0);
    QCOMPARE(window->position(), position);
    if (QGuiApplication::platformName() == "offscreen") {
        // A press in the free top strip still reaches LVRS's native move request.
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(width / 2, 8));
        QCOMPARE(moves.count(), 1);
    }
}

void GuiTests::referenceGenerateOpensResultImmediately_data()
{
    QTest::addColumn<int>("count");
    QTest::newRow("single-reference") << 1;
    QTest::newRow("reference-batch") << 3;
}

void GuiTests::referenceGenerateOpensResultImmediately()
{
    QFETCH(int, count);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-reference-result-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/reference.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly));
    model.write("native fixture model");
    model.close();
    QImage reference(80, 40, QImage::Format_RGB888);
    reference.fill(QColor(11, 12, 13));
    const auto source = QUrl::fromLocalFile(storage.filePath("Files/reference.png"));
    QVERIFY(reference.save(source.toLocalFile()));
    GenerationRuntime runtime;
    runtime.nativeInference = true;
    iiLocalDiffusion::NativeAdvancedControls received;
    runtime.nativeGenerateAdvanced = [&](const auto &request, const auto &, const auto &, const auto &controls,
                                         const auto &, const auto &, const auto &) {
        received = controls;
        iiLocalDiffusion::NativeGenerationResult result;
        result.width = request.width;
        result.height = request.height;
        result.rgb.resize(request.width * request.height * 3, 100);
        return result;
    };
    QScopedValueRollback<std::optional<GenerationRuntime>> override(guiRuntimeOverride, runtime);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(960, 720);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *quick = item(window, "quickGenerate");
    auto *controller = window->findChild<GenerationController *>("generationController");
    QVERIFY(quick && controller && controller->connected());
    QVERIFY(quick->setProperty("prompt", "Reference portrait"));
    QVERIFY(quick->setProperty("aspectRatio", "3:4"));
    QVERIFY(quick->setProperty("generationCount", count));
    QVariant attached;
    QVERIFY(QMetaObject::invokeMethod(quick, "addAttachment", Q_RETURN_ARG(QVariant, attached),
                                     Q_ARG(QVariant, QVariant(source))));
    QVERIFY(attached.toBool());
    QSignalSpy queued(controller, &GenerationController::submissionQueued);
    click(window, item(quick, "generateButton"));
    QCOMPARE(queued.size(), 1);
    // Feedback must appear in the same submission turn, before any native work finishes.
    QVERIFY(window->property("resultVisible").toBool());
    QVERIFY(controller->latestImage().isEmpty());
    const auto ids = queued.at(0).at(0).toStringList();
    QCOMPARE(ids.size(), count);
    QCOMPARE(listProperty(window, "resultJobIds"), QVariant(ids).toList());
    QVERIFY(item(window, "generationResult")->isVisible());
    QCOMPARE(quick->property("prompt").toString(), "Reference portrait");
    for (const auto &job : controller->jobs()) {
        const auto value = job.toMap();
        QCOMPARE(value.value("width").toInt(), 1024);
        QCOMPARE(value.value("height").toInt(), 1368);
        QCOMPARE(value.value("aspectRatio").toString(), "3:4");
        QCOMPARE(value.value("advancedParameters").toMap().value("referenceImages").toList().size(), 1);
    }
    QVERIFY(QFile::remove(source.toLocalFile()));
    controller->setForeground(true);
    QTRY_COMPARE_WITH_TIMEOUT(controller->completedResults().size(), count, 15000);
    QCOMPARE(listProperty(window, "submissionResults").size(), count);
    QCOMPARE(received.references.size(), size_t(1));
    QCOMPARE(received.references[0].width, 80);
    QCOMPARE(received.references[0].height, 40);
    QCOMPARE(received.references[0].rgb[0], quint8(11));
    for (const auto &result : controller->completedResults()) {
        const QImage output(result.toMap().value("imageSource").toUrl().toLocalFile());
        QCOMPARE(output.size(), QSize(1024, 1368));
    }
}

void GuiTests::generateOpensResultImmediatelyAndDisplaysEveryPreview()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-live-generation-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/live.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly));
    model.write("protocol test model");
    model.close();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *controller = window->findChild<GenerationController *>("generationController");
    QVERIFY(window && controller && controller->connected());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *quick = item(window, "quickGenerate");
    auto *preview = item(window, "generatedImage");
    auto *project = item(window, "newProjectButton");
    QVERIFY(quick->setProperty("prompt", "live-gui"));
    click(window, item(window, "generateButton"));
    // Transition on accepted submission, before a process starts or produces a file.
    QVERIFY(window->property("resultVisible").toBool());
    QVERIFY(controller->latestImage().isEmpty());
    QVERIFY(controller->previewImage().isEmpty());
    QVERIFY(!project->isEnabled());
    QUrl previous;
    for (int step = 1; step <= 3; ++step) {
        QTRY_COMPARE(controller->previewStep(), step);
        QTRY_COMPARE(preview->property("source").toUrl(), controller->previewImage());
        QTRY_COMPARE(preview->property("status").toInt(), 1);
        QVERIFY(dreamscapesProbeImageReady(QJsonValue::fromVariant(preview->property("status"))));
        QVERIFY(!dreamscapesProbeImageReady(QJsonValue(QStringLiteral("Loading"))));
        QVERIFY(!dreamscapesProbeImageReady(QJsonValue()));
        QVERIFY(preview->property("source").toUrl() != previous);
        previous = controller->previewImage();
        QCOMPARE(preview->property("fillMode").toInt(), 1); // PreserveAspectFit during denoising too.
        QVERIFY(!project->isEnabled());
        QVERIFY(item(window, "resultStatus")->property("text").toString().contains(QString::number(step)));
        if (step == 1)
            QVERIFY(quick->setProperty("prompt", "next image draft"));
        QFile nextFrame(QFileInfo(controller->previewImage().toLocalFile()).dir().filePath(QString("continue-%1").arg(step)));
        QVERIFY(nextFrame.open(QIODevice::WriteOnly));
        nextFrame.close();
    }
    QTRY_VERIFY_WITH_TIMEOUT(!controller->latestImage().isEmpty(), 10000);
    QTRY_COMPARE(preview->property("source").toUrl(), controller->latestImage());
    QTRY_VERIFY(project->isEnabled());
    QCOMPARE(quick->property("prompt").toString(), "next image draft");
    QCOMPARE(item(window, "quickGenerate"), quick);
    QVERIFY(controller->previewImage().isEmpty());
}

void GuiTests::resultCancelsTheActiveGeneration()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-cancel-generation-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/model.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly)); model.write("fixture"); model.close();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    auto *controller = window->findChild<GenerationController *>("generationController");
    QVERIFY(item(window, "quickGenerate")->setProperty("prompt", "live"));
    click(window, item(window, "generateButton"));
    QTRY_VERIFY(controller->busy());
    auto *cancel = item(window, "cancelGenerationButton");
    QTRY_VERIFY(cancel && cancel->isVisible() && cancel->isEnabled());
    click(window, cancel);
    QTRY_VERIFY(!controller->busy());
    QCOMPARE(controller->jobs().first().toMap().value("state").toString(), "cancelled");
    QVERIFY(controller->latestImage().isEmpty());
    QVERIFY(!cancel->isVisible());
    QCOMPARE(item(window, "resultStatus")->property("text").toString(), "Cancelled");
}

void GuiTests::countSelectionCreatesThreeImagesInSociety()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-count-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/count.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly));
    model.write("count protocol test model");
    model.close();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    auto *controller = window->findChild<GenerationController *>("generationController");
    auto *quick = item(window, "quickGenerate");
    QVERIFY(controller && quick && controller->connected());
    QVERIFY(!controller->selectedModel().isEmpty());
    quick->setProperty("prompt", "A video must never enter the image queue");
    QVERIFY(quick->setProperty("mediaType", "Video"));
    click(window, item(quick, "generateButton"));
    QVERIFY(controller->jobs().isEmpty());
    QVERIFY(window->property("generationRequestError").toString().contains("LTX"));
    QVERIFY(!window->property("resultVisible").toBool());
    QVERIFY(quick->setProperty("mediaType", "Image"));
    // The restored visible selector must drive the actual three-job queue.
    auto *countMenu = quick->findChild<QObject *>("generationCountMenu");
    QVERIFY(countMenu);
    click(window, item(quick, "generationCountButton"));
    QTRY_VERIFY(countMenu->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(countMenu, "triggerEntry", Q_ARG(QVariant, 2)));
    QTRY_VERIFY(!countMenu->property("visible").toBool());
    QCOMPARE(quick->property("generationCount").toInt(), 3);
    quick->setProperty("prompt", "Three images from one submission");
    click(window, item(quick, "generateButton"));
    QTRY_COMPARE(controller->jobs().size(), 3);
    const auto allCompleted = [&] {
        for (const auto &entry : controller->jobs())
            if (entry.toMap().value("state") != "completed") return false;
        return true;
    };
    QTRY_VERIFY2_WITH_TIMEOUT(allCompleted(),
        qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(controller->jobs()).toJson(QJsonDocument::Compact))), 10000);
    QCOMPARE(QDir(storage.filePath("Generation History")).entryList(QDir::Files).size(), 3);
    QCOMPARE(quick->property("generationCount").toInt(), 3);
    QVERIFY(window->property("resultVisible").toBool());
    auto *gallery = item(window, "resultGallery");
    QVERIFY(gallery);
    QTRY_VERIFY(gallery->isVisible());
    QTRY_COMPARE(gallery->property("count").toInt(), 3);
    QCOMPARE(controller->property("completedResults").toList().size(), 3);
    QQuickItem *firstTile = nullptr;
    QTRY_VERIFY(QMetaObject::invokeMethod(gallery, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, firstTile), Q_ARG(int, 0)) && firstTile);
    QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, firstTile->mapToScene(firstTile->boundingRect().center()).toPoint(), 50);
    QTRY_VERIFY(!gallery->isVisible());
    QVERIFY(quick->setProperty("prompt", "Another batch from the detail view"));
    click(window, item(quick, "generateButton"));
    // A new submission retires the prior detail selection and starts with an empty result.
    auto *result = item(window, "generationResult");
    QVERIFY(!result->property("detailVisible").toBool());
    QCOMPARE(listProperty(result, "galleryResults").size(), 0);
    QVERIFY(result->property("imageSource").toUrl().isEmpty());
    QVERIFY(result->property("generationPending").toBool());
    QTRY_COMPARE(controller->jobs().size(), 6);
    QTRY_VERIFY_WITH_TIMEOUT(allCompleted(), 10000);
    QTRY_VERIFY(gallery->isVisible());
    QTRY_COMPARE(gallery->property("count").toInt(), 3);
    for (const auto &entry : listProperty(result, "galleryResults"))
        QCOMPARE(entry.toMap().value("prompt").toString(), "Another batch from the detail view");
    QCOMPARE(QDir(storage.filePath("Generation History")).entryList(QDir::Files).size(), 6);
}

void GuiTests::newSubmissionReplacesDismissedResults_data()
{
    QTest::addColumn<QSize>("viewport");
    QTest::addColumn<int>("previousCount");
    QTest::newRow("single-desktop") << QSize(960, 640) << 1;
    QTest::newRow("batch-mobile") << QSize(390, 844) << 3;
}

void GuiTests::newSubmissionReplacesDismissedResults()
{
    QFETCH(QSize, viewport);
    QFETCH(int, previousCount);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/result-submission-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/submission.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly)); model.write("protocol fixture"); model.close();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}, {"width", viewport.width()}, {"height", viewport.height()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *controller = window->findChild<GenerationController *>("generationController"); QVERIFY(controller);
    auto *quick = item(window, "quickGenerate");
    auto *result = item(window, "generationResult");
    const auto results = [&] { return listProperty(result, "galleryResults"); };
    quick->setProperty("generationCount", previousCount);
    quick->setProperty("prompt", "previous submission");
    click(window, item(window, "generateButton"));
    QTRY_COMPARE_WITH_TIMEOUT(controller->completedResults().size(), previousCount, 10000);
    QTRY_COMPARE(results().size(), previousCount);
    const auto oldImage = controller->latestImage();
    QVERIFY(QFileInfo::exists(oldImage.toLocalFile()));
    click(window, item(window, "resultBackButton"));
    QTRY_VERIFY(!window->property("resultVisible").toBool());
    QCOMPARE(results().size(), 0);
    QVERIFY(result->property("imageSource").toUrl().isEmpty());

    quick->setProperty("generationCount", 2);
    quick->setProperty("prompt", "live");
    click(window, item(window, "generateButton"));
    QVERIFY(window->property("resultVisible").toBool());
    QCOMPARE(results().size(), 0);
    QVERIFY(result->property("imageSource").toUrl().isEmpty());
    QVERIFY(result->property("generationPending").toBool());
    QVERIFY(!item(window, "resultStatus")->property("text").toString().isEmpty());
    QTRY_VERIFY(!controller->previewImage().isEmpty());
    QTRY_COMPARE(result->property("previewSource").toUrl(), controller->previewImage());
    QVERIFY(result->property("imageSource").toUrl() != oldImage);
    QTRY_COMPARE_WITH_TIMEOUT(controller->completedResults().size(), previousCount + 2, 10000);
    QTRY_COMPARE(results().size(), 2);
    for (const auto &entry : results()) QCOMPARE(entry.toMap().value("prompt").toString(), "live");
    QVERIFY(QFileInfo::exists(oldImage.toLocalFile()));
    QCOMPARE(QDir(storage.filePath("Generation History")).entryList(QDir::Files).size(), previousCount + 2);
}

void GuiTests::dismissedGenerationCannotReopenOrContaminateTheNextSubmission()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/dismissed-generation-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/session.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly)); model.write("protocol fixture"); model.close();
    QQmlApplicationEngine engine; engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml")); QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *controller = window->findChild<GenerationController *>("generationController"); QVERIFY(controller);
    auto *result = item(window, "generationResult");
    QVERIFY(!controller->enqueue("live-gui").isEmpty());
    QTRY_VERIFY(window->property("resultVisible").toBool());
    QTRY_COMPARE(controller->previewStep(), 1);
    click(window, item(window, "resultBackButton"));
    QVERIFY(!window->property("resultVisible").toBool());
    QVERIFY(result->property("imageSource").toUrl().isEmpty());
    // A new accepted request must open immediately while the dismissed worker still runs.
    const auto next = controller->enqueue("live"); QVERIFY(!next.isEmpty());
    QVERIFY(window->property("resultVisible").toBool());
    QVERIFY(result->property("previewSource").toUrl().isEmpty());
    QVERIFY(result->property("generationPending").toBool());
    QCOMPARE(item(window, "resultStatus")->property("text").toString(), "Queued");
    QVERIFY(!item(window, "cancelGenerationButton")->isEnabled());
    QVERIFY(!item(window, "newProjectButton")->isEnabled());
    for (int step = 1; step <= 3; ++step) {
        QTRY_COMPARE(controller->previewStep(), step);
        QVERIFY(result->property("previewSource").toUrl().isEmpty());
        QVERIFY(result->property("imageSource").toUrl().isEmpty());
        QFile release(QFileInfo(controller->previewImage().toLocalFile()).dir().filePath(QString("continue-%1").arg(step)));
        QVERIFY(release.open(QIODevice::WriteOnly)); release.close();
    }
    QTRY_VERIFY_WITH_TIMEOUT(!controller->completedResults().isEmpty(), 10000);
    QTRY_COMPARE(controller->jobs().first().toMap().value("state").toString(), "running");
    QCOMPARE(listProperty(result, "galleryResults").size(), 0);
    // Dismiss the second request too; its later previews and completion must keep Home open.
    click(window, item(window, "resultBackButton"));
    QVERIFY(!window->property("resultVisible").toBool());
    QTRY_COMPARE_WITH_TIMEOUT(controller->completedResults().size(), 2, 10000);
    QVERIFY(!window->property("resultVisible").toBool());
    QVERIFY(result->property("imageSource").toUrl().isEmpty());
    QVERIFY(result->property("previewSource").toUrl().isEmpty());
}

void GuiTests::resultGalleryLayoutAndSelection_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<int>("count");
    QTest::newRow("gallery-compact") << QSize(320, 480) << 80;
    QTest::newRow("gallery-mobile") << QSize(390, 844) << 80;
    QTest::newRow("gallery-desktop") << QSize(960, 640) << 80;
    QTest::newRow("gallery-wide") << QSize(1440, 900) << 80;
    QTest::newRow("gallery-1000") << QSize(960, 640) << 1000;
}

void GuiTests::resultGalleryLayoutAndSelection()
{
    QFETCH(QSize, size);
    QFETCH(int, count);
    QTemporaryDir images(DREAMSCAPES_TEST_DIRECTORY "/gallery-layout-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(images.path()));
    QVariantList results;
    for (int index = 0; index < count; ++index) {
        QImage fixture(192, index % 2 ? 256 : 128, QImage::Format_RGB32);
        fixture.fill(QColor::fromHsv((index * 37) % 360, 120, 190));
        QPainter painter(&fixture);
        painter.fillRect(0, 0, 64, fixture.height(), QColor::fromHsv((index * 37 + 40) % 360, 140, 140));
        painter.setPen(Qt::white);
        painter.setFont(QFont("Helvetica", 26));
        painter.drawText(fixture.rect(), Qt::AlignCenter, QString::number(index + 1));
        painter.end();
        const auto path = images.filePath(QString("image-%1.png").arg(index));
        QVERIFY(fixture.save(path));
        results.append(QVariantMap{{"imageSource", QUrl::fromLocalFile(path)},
            {"id", QString::number(index)}, {"prompt", QString("Image %1 prompt").arg(index + 1)},
            {"aspectRatio", index % 2 ? "3:4" : "3:2"}});
    }
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", images.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(size);
    QVERIFY(window->setProperty("currentResult", results.last()));
    QVERIFY(window->setProperty("resultVisible", true));
    auto *result = item(window, "generationResult");
    QVERIFY(result && result->setProperty("results", results));
    auto *gallery = item(window, "resultGallery");
    QVERIFY(gallery);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_VERIFY(gallery->isVisible());
    QTRY_COMPARE(gallery->property("count").toInt(), count);
    auto *quick = item(window, "quickGenerate");
    QVERIFY(quick->setProperty("prompt", "Next image draft"));
    auto *toolbar = item(window, "resultToolbar");
    QVERIFY(gallery->width() >= result->width() - 4);
    QVERIFY(gallery->height() >= result->height() - toolbar->height() - 8);
    QVERIFY(gallery->clip());
    QVERIFY(gallery->property("contentHeight").toReal() > gallery->height());
    QCOMPARE(gallery->property("cellWidth"), gallery->property("cellHeight"));
    const auto columns = gallery->property("columns").toInt();
    QVERIFY(columns >= 2);
    if (size.width() >= 960) QVERIFY(columns >= 4);
    QQuickItem *tile = nullptr;
    QTRY_VERIFY(QMetaObject::invokeMethod(gallery, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, tile), Q_ARG(int, 0)) && tile);
    QTRY_VERIFY(tile->property("imageReady").toBool());
    auto *thumbnail = item(tile, "galleryThumbnail");
    QVERIFY(thumbnail);
    QCOMPARE(thumbnail->property("fillMode").toInt(), 2); // Photo-style square crop.
    QVERIFY(thumbnail->property("sourceSize").toSize().width() <= 1024);
    const QString captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        QVERIFY(window->grabWindow().save(captureDirectory + '/' + QTest::currentDataTag() + ".png"));
    }
    if (size == QSize(390, 844)) {
        auto *touch = QTest::createTouchDevice();
        const auto start = gallery->mapToScene(QPointF(gallery->width() / 2, gallery->height() - 60)).toPoint();
        QTest::touchEvent(window, touch).press(0, start, window);
        for (int step = 1; step <= 6; ++step) {
            QTest::touchEvent(window, touch).move(0, start - QPoint(0, step * 30), window);
            QTest::qWait(20);
        }
        QTest::touchEvent(window, touch).release(0, start - QPoint(0, 180), window);
        QTRY_VERIFY(gallery->property("contentY").toReal() > 0);
        QVERIFY(gallery->isVisible()); // Scrolling a tile must not open its detail view.
        QVERIFY(QMetaObject::invokeMethod(gallery, "cancelFlick"));
    } else if (size == QSize(960, 640) && count == 80) {
        const auto point = gallery->mapToScene(gallery->boundingRect().center());
        QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), QPoint(), QPoint(0, -360),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(gallery->property("contentY").toReal() > 0);
        QVERIFY(gallery->isVisible());
    }
    // The last image must be reachable without constructing all 1000 thumbnails.
    QVERIFY(QMetaObject::invokeMethod(gallery, "positionViewAtEnd"));
    QTRY_VERIFY(gallery->property("contentY").toReal() > 0);
    QTRY_VERIFY(QMetaObject::invokeMethod(gallery, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, tile), Q_ARG(int, count - 1)) && tile);
    QTRY_VERIFY(tile->property("imageReady").toBool());
    const auto contentY = gallery->property("contentY").toReal();
    const auto *content = gallery->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    // GridView retains a small pool when jumping between distant pages.
    if (count == 1000) QVERIFY(content->childItems().size() < 200);
    click(window, tile);
    QTRY_VERIFY(!gallery->isVisible());
    auto *detail = item(window, "generatedImage");
    QTRY_COMPARE(detail->property("status").toInt(), 1);
    QCOMPARE(detail->property("source").toUrl(), results.last().toMap().value("imageSource").toUrl());
    QCOMPARE(detail->property("fillMode").toInt(), 1);
    QVERIFY(detail->height() > 242);
    QSignalSpy projectRequests(window, SIGNAL(newProjectRequested(QUrl,QVariant)));
    click(window, item(window, "newProjectButton"));
    QCOMPARE(projectRequests.size(), 1);
    QCOMPARE(projectRequests.first().at(1).toMap(), results.last().toMap());
    QVERIFY(item(window, "canvasEditor")->isVisible());
    click(window, editorBackControl(window));
    QTRY_VERIFY(result->isVisible());
    // New completions must not switch the image being inspected or exported.
    const auto selected = results.last().toMap();
    QObject *saveDialog = nullptr;
    if (size == QSize(960, 640) && count == 80) {
        saveDialog = window->findChild<QObject *>("saveImageDialog");
        QVERIFY(saveDialog && saveDialog->setProperty("currentFolder", QUrl::fromLocalFile(images.path())));
        QVERIFY(QMetaObject::invokeMethod(result, "saveImageToFile"));
        QTRY_VERIFY(saveDialog->property("visible").toBool());
        QCOMPARE(saveDialog->property("sourceImage").toUrl(), selected.value("imageSource").toUrl());
    }
    auto added = results.first().toMap();
    added["id"] = "next-batch";
    results.append(added);
    QVERIFY(result->setProperty("results", results));
    QVERIFY(window->setProperty("currentResult", added));
    QCOMPARE(detail->property("source").toUrl(), selected.value("imageSource").toUrl());
    if (saveDialog) {
        QCOMPARE(saveDialog->property("sourceImage").toUrl(), selected.value("imageSource").toUrl());
        QVERIFY(QMetaObject::invokeMethod(saveDialog, "reject"));
        QTRY_VERIFY(!saveDialog->property("visible").toBool());
    }
    click(window, item(window, "resultBackButton"));
    QTRY_VERIFY(gallery->isVisible());
    QVERIFY(window->property("resultVisible").toBool());
    QTRY_VERIFY(qAbs(gallery->property("contentY").toReal() - contentY) <= 1);
    QCOMPARE(item(window, "quickGenerate"), quick);
    QCOMPARE(quick->property("prompt").toString(), "Next image draft");
    window->resize(QSize(640, 640));
    QTRY_VERIFY(gallery->property("columns").toInt() != columns || size.width() == 640);
    QVERIFY(gallery->property("contentWidth").toReal() <= gallery->width() + 1);
    click(window, item(window, "resultBackButton"));
    QTRY_VERIFY(!window->property("resultVisible").toBool());
}

void GuiTests::generateButtonUsesSocietyStorage()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-generation-XXXXXX");
    QVERIFY(storage.isValid());
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/window model.safetensor"));
    QVERIFY(model.open(QIODevice::WriteOnly));
    model.write("protocol test model");
    model.close();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *controller = window->findChild<GenerationController *>("generationController");
    QVERIFY(controller && controller->connected());
    QCOMPARE(controller->selectedModel(), QString("window model.safetensor"));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *quick = item(window, "quickGenerate");
    QVERIFY(quick->setProperty("prompt", "Image from shared Society storage"));
    click(window, item(window, "generateButton"));
    QTRY_COMPARE(controller->jobs().size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(controller->jobs().first().toMap().value("state").toString(), QString("completed"), 10000);
    QTRY_COMPARE(item(window, "generatedImage")->property("status").toInt(), 1);
    QTRY_VERIFY(window->property("resultVisible").toBool());
    QVERIFY(controller->latestImage().toLocalFile().startsWith(storage.filePath("Generation History/")));
    QVERIFY(!QDirIterator(storage.filePath("Files"), QDir::Files | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories).hasNext());
    QVERIFY(QDir(storage.filePath("Asset Library")).isEmpty());

    auto *preview = item(window, "generatedImage");
    const auto firstImage = controller->latestImage();
    QCOMPARE(item(window, "quickGenerate"), quick);
    QVERIFY(!item(window, "storagePanel")->isVisible());
    auto *mediaMenu = quick->findChild<QObject *>("mediaTypeMenu");
    auto *mediaButton = item(quick, "mediaTypeButton");
    click(window, mediaButton);
    QTRY_VERIFY(mediaMenu->property("opened").toBool());
    QVERIFY(mediaMenu->property("y").toReal() >= 0);
    QVERIFY(mediaMenu->property("y").toReal() + mediaMenu->property("height").toReal()
            <= mediaButton->mapToScene(QPointF()).y());
    QVERIFY(QMetaObject::invokeMethod(mediaMenu, "triggerEntry", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(!mediaMenu->property("visible").toBool());
    QVERIFY(quick->setProperty("aspectRatio", "16:9"));
    QVERIFY(quick->setProperty("prompt", "slow another image"));
    click(window, item(quick, "generateButton"));
    QTRY_COMPARE(controller->jobs().size(), 2);
    auto *gallery = item(window, "resultGallery");
    auto *result = item(window, "generationResult");
    QVERIFY(!gallery->isVisible());
    QCOMPARE(listProperty(result, "galleryResults").size(), 0);
    QVERIFY(preview->property("source").toUrl().isEmpty());
    QVERIFY(result->property("generationPending").toBool());
    QCOMPARE(controller->property("completedResults").toList().first().toMap().value("imageSource").toUrl(), firstImage);
    QVERIFY(quick->setProperty("prompt", "draft typed during generation"));
    QTRY_COMPARE_WITH_TIMEOUT(controller->jobs().first().toMap().value("state").toString(), QString("completed"), 10000);
    QVERIFY(controller->latestImage() != firstImage);
    QTRY_COMPARE(gallery->property("count").toInt(), 1);
    QVERIFY(!gallery->isVisible());
    QTRY_COMPARE(preview->property("source").toUrl(), controller->latestImage());
    QTRY_COMPARE(preview->property("status").toInt(), 1);
    QCOMPARE(quick->property("prompt").toString(), "draft typed during generation");
    QCOMPARE(quick->property("aspectRatio").toString(), "16:9");

    // Project input is the displayed generation, even while a new prompt is being edited.
    QVERIFY(quick->setProperty("prompt", "an unfinished draft"));
    QVERIFY(quick->setProperty("aspectRatio", "9:16"));
    QSignalSpy projectRequests(window, SIGNAL(newProjectRequested(QUrl,QVariant)));
    QVERIFY(projectRequests.isValid());
    click(window, item(window, "newProjectButton"));
    QCOMPARE(projectRequests.size(), 1);
    QCOMPARE(projectRequests.first().at(0).toUrl(), controller->latestImage());
    const auto projectInput = projectRequests.first().at(1).toMap();
    QCOMPARE(projectInput.value("id"), controller->jobs().first().toMap().value("id"));
    QCOMPARE(projectInput.value("prompt").toString(), "slow another image");
    QCOMPARE(projectInput.value("aspectRatio").toString(), "16:9");
    QVERIFY(item(window, "canvasEditor")->isVisible());
    click(window, editorBackControl(window));
    QTRY_VERIFY(result->isVisible());

    // Rejected input must leave the current result intact and explain the failure.
    QVERIFY(QMetaObject::invokeMethod(quick, "generateRequested", Q_ARG(QString, "invalid request"),
        Q_ARG(QString, "Image"), Q_ARG(QString, "bad"), Q_ARG(int, 1)));
    QCOMPARE(controller->jobs().size(), 2);
    QVERIFY(window->property("resultVisible").toBool());
    QCOMPARE(preview->property("source").toUrl(), controller->latestImage());
    QCOMPARE(listProperty(result, "galleryResults").size(), 1);
    QCOMPARE(item(window, "resultStatus")->property("text").toString(), controller->errorString());
    QVERIFY(!controller->errorString().isEmpty());

    QVERIFY(!controller->enqueue("fail").isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(controller->jobs().first().toMap().value("state").toString(), QString("failed"), 10000);
    QVERIFY(window->property("resultVisible").toBool());
    QTRY_VERIFY(item(window, "resultStatus")->isVisible());
    QVERIFY(preview->property("source").toUrl().isEmpty());
    QCOMPARE(listProperty(result, "galleryResults").size(), 0);
    click(window, item(window, "resultBackButton"));
    QTRY_VERIFY(!window->property("resultVisible").toBool());
    QTRY_COMPARE(quick->mapToScene(QPointF()).y(), window->property("contentTopInset").toReal());
    QCOMPARE(item(window, "quickGenerate"), quick);
    QCOMPARE(quick->property("prompt").toString(), "an unfinished draft");
    QCOMPARE(quick->property("aspectRatio").toString(), "9:16");
    // Background notifications for the same result must not undo Back navigation.
    QVERIFY(QMetaObject::invokeMethod(controller, "jobsChanged"));
    QVERIFY(!window->property("resultVisible").toBool());
    // Returning from a bottom-aligned panel must keep the home controls reachable.
    QTRY_COMPARE(quick->height(), quick->implicitHeight());
    auto *storagePanel = item(window, "storagePanel");
    QTRY_VERIFY(storagePanel->height() > 0);
    QVERIFY(!item(window, "showGenerationResultButton"));
    QTRY_VERIFY(storagePanel->boundingRect().contains(bounds(item(window, "societyModelSelector"), storagePanel)));
    const QString captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        const auto capture = window->grabWindow();
        QVERIFY(!capture.isNull());
        QVERIFY(capture.save(captureDirectory + "/home-after-result.png"));
    }
    for (const auto size : {QSize(320, 480), QSize(390, 844), QSize(960, 640)}) {
        window->resize(size);
        QTRY_COMPARE(window->size(), size);
        auto *panel = item(window, "storagePanel");
        auto *selector = item(window, "societyModelSelector");
        QVERIFY(panel && selector);
        QTRY_VERIFY(selector->width() <= panel->width() + 1);
        QVERIFY(selector->width() > 0);
    }
    const auto savedImage = controller->latestImage();
    QVERIFY(!QImage(savedImage.toLocalFile()).isNull());
    window->close();
    delete window;

    QQmlApplicationEngine restored;
    restored.setInitialProperties({{"initialContainerPath", storage.path()}});
    restored.load(sourceUrl("Main.qml"));
    QCOMPARE(restored.rootObjects().size(), 1);
    auto *restoredWindow = qobject_cast<QQuickWindow *>(restored.rootObjects().first());
    QVERIFY(restoredWindow);
    QVERIFY(!restoredWindow->property("resultVisible").toBool());
    QVERIFY(item(restoredWindow, "quickGenerate")->property("prompt").toString().isEmpty());
    QCOMPARE(item(restoredWindow, "quickGenerate")->property("aspectRatio").toString(), "1:1");
    auto *restoredController = restoredWindow->findChild<GenerationController *>("generationController");
    QVERIFY(restoredController && restoredController->connected());
    QVERIFY(restoredController->jobs().isEmpty());
    QVERIFY(restoredController->latestResult().isEmpty());
    QVERIFY(!QImage(savedImage.toLocalFile()).isNull());
    QTemporaryDir otherStorage(DREAMSCAPES_TEST_DIRECTORY "/result-other-storage-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(otherStorage.path()));
    QVERIFY(restoredController->connectStorage(otherStorage.path()));
    QTRY_VERIFY(!restoredWindow->property("resultVisible").toBool());
}

void GuiTests::resultScreenLayout_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<QSize>("size");
    QTest::addColumn<QSize>("imageSize");
    QTest::newRow("result-figma-402") << QString("macos") << QSize(402, 575) << QSize(600, 300);
    QTest::newRow("result-desktop") << QString("macos") << QSize(960, 640) << QSize(300, 600);
    QTest::newRow("result-desktop-wide") << QString("macos") << QSize(1440, 900) << QSize(512, 512);
    QTest::newRow("result-compact") << QString("macos") << QSize(320, 480) << QSize(600, 300);
    QTest::newRow("result-ios") << QString("ios") << QSize(390, 844) << QSize(300, 600);
    QTest::newRow("result-android") << QString("android") << QSize(360, 844) << QSize(512, 512);
    QTest::newRow("result-landscape") << QString("ios") << QSize(844, 480) << QSize(300, 600);
}

void GuiTests::resultScreenLayout()
{
    QFETCH(QString, target);
    QFETCH(QSize, size);
    QFETCH(QSize, imageSize);
    QTemporaryDir images(DREAMSCAPES_TEST_DIRECTORY "/result-layout-XXXXXX");
    QVERIFY(images.isValid());
    QImage fixture(imageSize, QImage::Format_RGB32);
    fixture.fill(QColor("#4172a4"));
    QPainter painter(&fixture);
    painter.fillRect(0, 0, imageSize.width() / 3, imageSize.height(), QColor("#a44f41"));
    painter.fillRect(imageSize.width() * 2 / 3, 0, imageSize.width() / 3, imageSize.height(), QColor("#55a47b"));
    painter.end();
    const auto path = images.filePath("fit-fixture.png");
    QVERIFY(fixture.save(path));
    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", target));
    QVERIFY(iiSocietyContainer::SocietyDrive::create(images.path()));
    engine.setInitialProperties({{"initialContainerPath", images.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(size);
    QVERIFY(window->setProperty("currentResult", QVariantMap{{"imageSource", QUrl::fromLocalFile(path)}, {"prompt", "Previous Prompt"}}));
    QVERIFY(window->setProperty("resultVisible", true));
    auto *quick = item(window, "quickGenerate");
    QVERIFY(quick && quick->setProperty("prompt", "Previous Prompt"));
    auto *result = item(window, "generationResult");
    auto *preview = item(window, "generatedImage");
    auto *back = item(window, "resultBackButton");
    auto *project = item(window, "newProjectButton");
    QVERIFY(result && preview && back && project);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE(preview->property("status").toInt(), 1);
    const auto top = window->property("contentTopInset").toReal();
    const auto bottom = window->property("mobileSystemSafeBottomInset").toReal();
    const auto left = window->property("mobileSystemSafeLeftInset").toReal();
    const auto right = window->property("mobileSystemSafeRightInset").toReal();
    QTRY_COMPARE(quick->mapToScene(QPointF(0, quick->height())).y(), size.height() - bottom);
    QCOMPARE(back->mapToScene(QPointF()).y(), top);
    QCOMPARE(back->mapToScene(QPointF()).x(), left);
    QCOMPARE(project->mapToScene(QPointF(project->width(), 0)).x(), size.width() - right);
    QCOMPARE(back->height(), 22.0);
    QCOMPARE(project->height(), 22.0);
    QCOMPARE(preview->width(), size.width() - left - right);
    QCOMPARE(preview->height(), 242.0);
    QCOMPARE(preview->property("fillMode").toInt(), 1); // Image.PreserveAspectFit
    QVERIFY(preview->clip());
    const auto imageScale = qMin(preview->width() / imageSize.width(), preview->height() / imageSize.height());
    QVERIFY(qAbs(preview->property("paintedWidth").toReal() - imageSize.width() * imageScale) <= 0.5);
    QVERIFY(qAbs(preview->property("paintedHeight").toReal() - imageSize.height() * imageScale) <= 0.5);
    const auto upperGap = preview->mapToScene(QPointF()).y() - back->mapToScene(QPointF(0, back->height())).y();
    const auto lowerGap = quick->mapToScene(QPointF()).y() - preview->mapToScene(QPointF(0, preview->height())).y();
    QVERIFY(qAbs(upperGap - lowerGap) <= 1.0); // Qt snaps centered images to device-independent pixels.
    // The native title-bar reserve changes the available vertical gap. The
    // result composer itself must retain the compact 72px contract.
    if (size == QSize(402, 575)) QTRY_COMPARE(quick->height(), 72.0); // LVRS spacing spring settles after the Home → Result route.
    QVERIFY(project->isEnabled());
    const QString captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        const auto capture = window->grabWindow();
        QVERIFY(!capture.isNull());
        QVERIFY(capture.save(captureDirectory + '/' + QTest::currentDataTag() + ".png"));
    }
    // All bottom menus remain within the window and above their trigger.
    for (const auto *name : {"mediaTypeButton"}) {
        auto *button = item(quick, name);
        auto *menu = quick->findChild<QObject *>("mediaTypeMenu");
        QVERIFY(button && menu);
        click(window, button);
        QTRY_VERIFY(menu->property("opened").toBool());
        QVERIFY(menu->property("x").toReal() >= 0);
        QVERIFY(menu->property("x").toReal() + menu->property("width").toReal() <= size.width());
        QVERIFY(menu->property("y").toReal() >= 0);
        QVERIFY(menu->property("y").toReal() + menu->property("height").toReal() <= button->mapToScene(QPointF()).y());
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());
    }
    // Finish native popup/window teardown before the next platform profile.
    window->close();
    QTRY_VERIFY(!window->isVisible());
    QTest::qWait(100);
}

void GuiTests::newCanvasChoosesPrintAndCustomSizes_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("desktop") << QSize(1280, 800);
    QTest::newRow("compact") << QSize(640, 480);
    QTest::newRow("mobile") << QSize(390, 844);
    QTest::newRow("minimum") << QSize(320, 640);
}

void GuiTests::newCanvasPaddingAndStaticDetails_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<QSize>("size");
    QTest::newRow("desktop") << QString("macos") << QSize(1280, 800);
    QTest::newRow("compact") << QString("macos") << QSize(640, 480);
    QTest::newRow("mobile") << QString("ios") << QSize(390, 844);
    QTest::newRow("minimum") << QString("macos") << QSize(320, 640);
}

void GuiTests::newCanvasPaddingAndStaticDetails()
{
    QFETCH(QString, target);
    QFETCH(QSize, size);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", target));
    QQmlComponent component(&engine, sourceUrl("Main.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window);
    window->resize(size);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *dialog = root->findChild<QObject *>("newCanvasDialog");
    QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(window, "openNewCanvas"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QCOMPARE(dialog->property("padding").toReal(), 12.0);
    auto *body = dialog->property("contentItem").value<QQuickItem *>();
    auto *appContent = item(window, "appContent");
    QVERIFY(body && appContent);
    const auto modalBounds = [&] { return bounds(body, window->contentItem()).adjusted(-12, -12, 12, 12); };
    QTRY_VERIFY(modalBounds().top() >= bounds(appContent, window->contentItem()).top());
    QTRY_VERIFY(modalBounds().bottom() <= bounds(appContent, window->contentItem()).bottom());
    QCOMPARE(body->width(), dialog->property("width").toReal() - 24);
    QCOMPARE(body->height(), dialog->property("height").toReal() - 24);
    QTRY_VERIFY(visualItem(window->contentItem(), "canvasPreset_01-01-01")
        && visualItem(window->contentItem(), "canvasPresetCard_01-01-01"));
    auto *details = visualItem(window->contentItem(), "canvasPreset_01-01-01");
    auto *card = visualItem(window->contentItem(), "canvasPresetCard_01-01-01");
    QVERIFY2(!details->property("hovered").isValid() && !details->property("pressed").isValid(),
        "Preset details must be a static layout, detached from LVRS button behavior.");
    QVERIFY(!details->activeFocusOnTab());
    QCOMPARE(details->height(), 44.0);
    auto *label = visualItem(details, "canvasPresetLabel");
    auto *description = visualItem(details, "canvasPresetDescription");
    QVERIFY(label && description);
    QCOMPARE(label->property("text").toString(), "Square post");
    QCOMPARE(description->property("text").toString(), "1080 × 1080 px");
    QCOMPARE(bounds(label, details).left(), 12.0);
    QCOMPARE(bounds(description, details).top() - bounds(label, details).bottom(), 4.0);
    card->forceActiveFocus();
    auto *viewport = dialog->property(dialog->property("compact").toBool()
        ? "compactViewport" : "galleryViewport").value<QQuickItem *>();
    QVERIFY(viewport);
    QTRY_VERIFY(bounds(details, viewport).top() >= 0 && bounds(details, viewport).bottom() <= viewport->height());
    QTest::mouseMove(window, QPoint(0, 0));
    QTest::qWait(200);
    const auto geometry = details->size();
    const auto cardColor = card->property("color");
    const auto labelColor = label->property("color");
    QTest::mouseMove(window, details->mapToScene(details->boundingRect().center()).toPoint());
    QTest::qWait(200);
    QCOMPARE(details->size(), geometry);
    QCOMPARE(card->property("color"), cardColor);
    QCOMPARE(label->property("color"), labelColor);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier,
        details->mapToScene(details->boundingRect().center()).toPoint());
    QTest::qWait(100);
    QCOMPARE(details->size(), geometry);
    QCOMPARE(card->property("color"), cardColor);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier,
        details->mapToScene(details->boundingRect().center()).toPoint());
    QTest::qWait(220);
    QCOMPARE(details->size(), geometry);
    auto *next = visualItem(window->contentItem(), "canvasPresetCard_01-01-02");
    QVERIFY(next);
    next->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(dialog->property("draftHeight").toString(), "1350");
    window->resize(size.width(), size.height() + 80);
    QTRY_VERIFY(modalBounds().top() >= bounds(appContent, window->contentItem()).top());
    QTRY_VERIFY(modalBounds().bottom() <= bounds(appContent, window->contentItem()).bottom());
    auto *nextDetails = visualItem(next, "canvasPreset_01-01-02");
    QVERIFY(nextDetails);
    QTRY_VERIFY(bounds(nextDetails, viewport).top() >= 0
        && bounds(nextDetails, viewport).bottom() <= viewport->height());
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        QTest::qWait(200);
        QVERIFY(window->grabWindow().save(captureDirectory + "/modal-" + QTest::currentDataTag() + ".png"));
    }
}

void GuiTests::newCanvasChoosesPrintAndCustomSizes()
{
    QFETCH(QSize, size);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/new-canvas-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->resize(size);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *dialog = window->findChild<QObject *>("newCanvasDialog");
    QVERIFY(dialog);
    QVERIFY(QMetaObject::invokeMethod(window, "openNewCanvas"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    CanvasPresets catalogue;
    QVERIFY(QMetaObject::invokeMethod(dialog, "chooseCategory", Q_ARG(QVariant, QVariant(4))));
    const auto a4 = catalogue.preset("05-02-01");
    QVERIFY(QMetaObject::invokeMethod(dialog, "choose", Q_ARG(QVariant, QVariant(a4))));
    QTRY_COMPARE(dialog->property("unit").toString(), "mm");
    QVERIFY(item(window, "canvasPpiInput")->isVisible());
    auto *create = item(window, "canvasCreateButton");
    QVERIFY(create && create->isEnabled());
    QTRY_VERIFY(bounds(create, window->contentItem()).right() <= window->width());
    QTRY_VERIFY(bounds(create, window->contentItem()).bottom() <= window->height());
    const auto rect = bounds(create, window->contentItem());
    QVERIFY2(rect.left() >= 0 && rect.right() <= window->width()
        && rect.top() >= 0 && rect.bottom() <= window->height(), qPrintable(QString::number(rect.right())));
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        QTest::qWait(300); // Capture after LVRS colour/motion and layout have settled.
        QVERIFY(window->grabWindow().save(captureDirectory + "/new-canvas-" + QTest::currentDataTag() + ".png"));
    }
    click(window, create);
    QTRY_VERIFY(item(window, "canvasEditor")->isVisible());
    QTRY_VERIFY(!dialog->property("visible").toBool());
    auto *canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QVERIFY(canvas && canvas->documentReady());
    QCOMPARE(canvas->canvasWidth(), 2480);
    QCOMPARE(canvas->canvasHeight(), 3508);
    QCOMPARE(canvas->specification().value("unit").toString(), "mm");
    click(window, editorBackControl(window));
    QVERIFY(QMetaObject::invokeMethod(window, "openNewCanvas"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "customSize"));
    QVERIFY(dialog->setProperty("draftWidth", "1800"));
    QVERIFY(dialog->setProperty("draftHeight", "1200"));
    QVERIFY(dialog->setProperty("canvasBackground", "Transparent"));
    QVERIFY(dialog->setProperty("draftWidth", "0"));
    QTRY_VERIFY(!create->isEnabled());
    QVERIFY(dialog->setProperty("draftWidth", "1800"));
    QTRY_VERIFY(create->isEnabled());
    click(window, create);
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QTRY_COMPARE(canvas->canvasWidth(), 1800);
    QCOMPARE(canvas->canvasHeight(), 1200);
    QCOMPARE(canvas->specification().value("background").toString(), "Transparent");
    QVERIFY(canvas->document()->layers.empty());
}

void GuiTests::newCanvasSearchCancelAndKeyboard()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/new-canvas-keys-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->resize(1280, 800);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *dialog = window->findChild<QObject *>("newCanvasDialog");
    auto *quick = item(window, "quickGenerate");
    QVERIFY(quick->setProperty("prompt", "preserved canvas draft"));
    QVERIFY(QMetaObject::invokeMethod(window, "openNewCanvas"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "search", Q_ARG(QVariant, QVariant("A4"))));
    QTRY_COMPARE(dialog->property("resultCount").toInt(), 11);
    for (const auto &platform : {QString("TikTok"), QString("KakaoTalk"), QString("LINE rich menu")}) {
        QVERIFY(QMetaObject::invokeMethod(dialog, "search", Q_ARG(QVariant, QVariant(platform))));
        QTRY_COMPARE(dialog->property("resultCount").toInt(), 0);
        QVERIFY(!item(window, "canvasCreateButton")->isEnabled());
    }
    QVERIFY(QMetaObject::invokeMethod(dialog, "search", Q_ARG(QVariant, QVariant("missing-format"))));
    QTRY_COMPARE(dialog->property("resultCount").toInt(), 0);
    QVERIFY(!item(window, "canvasCreateButton")->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(dialog, "chooseCategory", Q_ARG(QVariant, QVariant(1))));
    QTRY_COMPARE(dialog->property("resultCount").toInt(), 17);
    // Repeater delegates can have a model owner outside the QObject child tree.
    QTRY_VERIFY(visualItem(window->contentItem(), "canvasPresetCard_02-01-02"));
    auto *preset = visualItem(window->contentItem(), "canvasPresetCard_02-01-02");
    QVERIFY(preset);
    preset->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(dialog->property("draftWidth").toString(), "3840");
    QTest::keyClick(window, Qt::Key_Tab);
    QVERIFY(window->activeFocusItem());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QVERIFY(!item(window, "canvasEditor")->isVisible());
    QCOMPARE(quick->property("prompt").toString(), "preserved canvas draft");
}

void GuiTests::canvasRoutesPreserveSelectionAndDraft_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<QSize>("size");
    QTest::newRow("canvas-desktop") << QString("macos") << QSize(960, 640);
    QTest::newRow("canvas-mobile") << QString("ios") << QSize(390, 844);
}

void GuiTests::canvasRoutesPreserveSelectionAndDraft()
{
    QFETCH(QString, target);
    QFETCH(QSize, size);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/canvas-routing-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QImage fixture(192, 128, QImage::Format_RGB32);
    fixture.fill(QColor("#8f6ec7"));
    const auto path = storage.filePath("canvas.png");
    QVERIFY(fixture.save(path));
    const QVariantMap selected{{"imageSource", QUrl::fromLocalFile(path)},
        {"id", "selected-result"}, {"prompt", "completed image"}, {"aspectRatio", "3:2"}};
    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", target));
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(size);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *quick = item(window, "quickGenerate");
    auto *editor = item(window, "canvasEditor");
    auto *result = item(window, "generationResult");
    QVERIFY(quick && editor && result);
    QVERIFY(quick->setProperty("prompt", "unfinished draft"));
    QVERIFY(!editor->isVisible());
    if (target == "ios") {
        click(window, item(window, "newCanvasAction"));
        QTRY_VERIFY(item(window, "canvasCreateButton")->isVisible());
        click(window, item(window, "canvasCreateButton"));
        QTRY_VERIFY(editor->isVisible());
        QVERIFY(item(window, "editorBlankCanvas")->isVisible());
        QVERIFY(editor->property("imageSource").toUrl().isEmpty());
        QVERIFY(!item(window, "mobileHome")->isVisible());
        QVERIFY(!quick->isVisible());
        click(window, editorBackControl(window));
        QTRY_VERIFY(item(window, "mobileHome")->isVisible());
    }
    QVERIFY(result->setProperty("result", selected));
    QVERIFY(window->setProperty("resultVisible", true));
    QTRY_COMPARE(item(window, "generatedImage")->property("status").toInt(), 1);
    auto *button = item(window, "newProjectButton");
    QCOMPARE(button->property("text").toString(), "New Canvas");
    QVERIFY(button->isEnabled());
    click(window, button);
    QTRY_VERIFY(editor->isVisible());
    QVERIFY(!result->isVisible());
    QVERIFY(!quick->isVisible());
    QVERIFY(!item(window, "storagePanel")->isVisible());
    QCOMPARE(editor->property("imageSource").toUrl(), QUrl::fromLocalFile(path));
    const auto metadata = editor->property("generationResult").value<QJSValue>().toVariant().toMap();
    QCOMPARE(metadata, selected);
    auto *nativeCanvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QVERIFY(nativeCanvas && nativeCanvas->documentReady());
    QCOMPARE(nativeCanvas->canvasWidth(), 192);
    QCOMPARE(nativeCanvas->canvasHeight(), 128);
    QVERIFY(nativeCanvas->rasterLayerSelected());
    QCOMPARE(nativeCanvas->selectedRasterPixels()->pixels.front(), QColor("#8f6ec7").rgba());
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        QVERIFY(window->grabWindow().save(captureDirectory + '/' + QTest::currentDataTag() + ".png"));
    }
    click(window, editorBackControl(window));
    QTRY_VERIFY(result->isVisible());
    QVERIFY(quick->isVisible());
    QCOMPARE(quick->property("prompt").toString(), "unfinished draft");
    QCOMPARE(result->property("imageSource").toUrl(), QUrl::fromLocalFile(path));
    if (target == "ios") {
        click(window, item(window, "resultBackButton"));
        click(window, item(window, "newCanvasAction"));
        QTRY_VERIFY(item(window, "canvasCreateButton")->isVisible());
        click(window, item(window, "canvasCreateButton"));
        QTRY_VERIFY(editor->isVisible());
        QVERIFY(editor->property("imageSource").toUrl().isEmpty());
        QVERIFY(editor->property("generationResult").value<QJSValue>().toVariant().toMap().isEmpty());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(item(window, "mobileHome")->isVisible());
    }
    QVERIFY(QMetaObject::invokeMethod(window, "openCanvas", Q_ARG(QVariant, QUrl::fromLocalFile(path)), Q_ARG(QVariant, selected)));
    nativeCanvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    const auto nativePath = storage.filePath("canvas.iisc");
    QVERIFY(nativeCanvas->saveDocumentAs(QUrl::fromLocalFile(nativePath)));
    click(window, editorBackControl(window));
    const QVariantMap nativeFile{{"path", nativePath}, {"mediaType", "Canvas"}, {"previewSource", QUrl::fromLocalFile(path)}};
    QVERIFY(QMetaObject::invokeMethod(window, "openHomeFile", Q_ARG(QVariant, nativeFile)));
    QTRY_VERIFY(editor->isVisible());
    nativeCanvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QCOMPARE(nativeCanvas->filePath(), nativePath);
    QVERIFY(nativeCanvas->rasterLayerSelected());
    QCOMPARE(nativeCanvas->selectedRasterPixels()->pixels.front(), QColor("#8f6ec7").rgba());
    QCOMPARE(quick->property("prompt").toString(), "unfinished draft");
}

void GuiTests::homeEditorRoutes_data()
{
    QTest::addColumn<QString>("target");
    QTest::newRow("home-editor-desktop") << QString("macos");
    QTest::newRow("home-editor-mobile") << QString("ios");
}

void GuiTests::multiCanvasProjectSelection_data() {
    QTest::addColumn<QString>("target");
    QTest::newRow("multi-canvas-desktop") << QString("macos");
    QTest::newRow("multi-canvas-mobile") << QString("ios");
}
void GuiTests::multiCanvasProjectSelection()
{
    QFETCH(QString,target);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/multi-canvas-gui-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QVariantList results;
    const QList<QSize> sizes{{64,40},{32,60},{24,24},{48,48},{80,40}};
    const QList<QColor> colors{Qt::red,Qt::blue,Qt::cyan,Qt::green,Qt::yellow};
    for (int i=0;i<5;++i) {
        QImage image(sizes[i],QImage::Format_ARGB32); image.fill(colors[i]);
        const auto path=storage.filePath(QString("page %1 %% #.png").arg(i)); QVERIFY(image.save(path));
        results.append(QVariantMap{{"id","same-job"},{"imageSource",QUrl::fromLocalFile(path)},
            {"mediaType",i==2 ? "Video" : "Image"},{"prompt","Project selection fixture"}});
    }
    QQmlApplicationEngine engine;
    auto *theme=engine.singletonInstance<QObject *>("LVRS","Theme");
    QVERIFY(theme && theme->setProperty("targetOverride",target));
    engine.setInitialProperties({{"initialContainerPath",storage.path()}}); engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(),1);
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->resize(target=="macos" ? QSize(1280,900) : QSize(390,844));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *result=item(window,"generationResult"); auto *gallery=item(window,"resultGallery");
    auto *selection=item(window,"resultImageSelection"); auto *editor=item(window,"canvasEditor");
    auto *project=window->findChild<EditorProject *>("editorProject");
    QVERIFY(result && gallery && selection && editor && project);
    QVERIFY(result->setProperty("results",results)); QVERIFY(window->setProperty("resultVisible",true));
    const auto tap=[&](int index,Qt::KeyboardModifiers modifiers) {
        QVERIFY(QMetaObject::invokeMethod(gallery,"positionViewAtIndex",Q_ARG(int,index),Q_ARG(int,4)));
        const auto name=QString("resultImageTile%1").arg(index).toLatin1();
        QQuickItem *tile=nullptr;
        QTRY_VERIFY((tile=visualItem(result,name.constData())));
        QTRY_VERIFY(gallery->contains(tile->mapToItem(gallery,tile->boundingRect().center())));
        QTest::mouseClick(window,Qt::LeftButton,modifiers,tile->mapToScene(tile->boundingRect().center()).toPoint());
    };
    tap(0,Qt::NoModifier); QVERIFY(!result->property("detailVisible").toBool());
    tap(3,Qt::MetaModifier); QTRY_COMPARE(listProperty(selection,"selectedImages").size(),2);
    tap(4,Qt::MetaModifier|Qt::ShiftModifier); QTRY_COMPARE(listProperty(selection,"selectedImages").size(),3);
    tap(0,Qt::ControlModifier); QTRY_COMPARE(listProperty(selection,"selectedImages").size(),2);
    tap(4,Qt::ShiftModifier); QTRY_COMPARE(listProperty(selection,"selectedImages").size(),4);
    tap(1,Qt::NoModifier); QCOMPARE(listProperty(selection,"selectedImages").size(),1);
    QTest::keyClick(window,Qt::Key_Right,Qt::ShiftModifier);
    QTest::keyClick(window,Qt::Key_Right,Qt::ShiftModifier);
    QTRY_COMPARE(listProperty(selection,"selectedImages").size(),2);
    QTest::keyClick(window,Qt::Key_A,Qt::ControlModifier);
    QTRY_COMPARE(listProperty(selection,"selectedImages").size(),4);
    click(window,item(window,"resultImageSelectionOpen"));
    QTRY_VERIFY(editor->isVisible()); QCOMPARE(project->canvasCount(),4); QCOMPARE(project->currentIndex(),0);
    auto *previousButton=item(window,"editorPreviousCanvas"); auto *nextButton=item(window,"editorNextCanvas");
    QTRY_COMPARE(previousButton->mapToScene(previousButton->boundingRect().center()).y(),
                 nextButton->mapToScene(nextButton->boundingRect().center()).y());
    auto *first=project->currentCanvas(); QCOMPARE(first->canvasWidth(),64); QCOMPARE(first->canvasHeight(),40);
    click(window,item(window,"editorNextCanvas")); QTRY_COMPARE(project->currentIndex(),1);
    auto *second=project->currentCanvas(); QCOMPARE(second->canvasWidth(),32); QCOMPARE(second->canvasHeight(),60);
    second->configureTool("brush",{{"field-4",6},{"field-5",100},{"field-8",100},{"field-9",100}});
    second->setBrushColor(Qt::white); QVERIFY(second->beginStrokeAt({16,30})); QVERIFY(second->endStrokeAt({16,30}));
    const auto painted=iiSharedCanvas::renderFrame(*second->document(),0).pixels.pixels;
    QVERIFY(second->canUndo());
    item(window,"editorNextCanvas")->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_Right,Qt::AltModifier);
    QCOMPARE(project->currentIndex(),1); // Focus outside the canvas keeps navigation local to the control.
    second->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_Right,Qt::AltModifier); QTRY_COMPARE(project->currentIndex(),2);
    QCOMPARE(project->currentCanvas()->canvasWidth(),48);
    QTest::keyClick(window,Qt::Key_PageDown); QTRY_COMPARE(project->currentIndex(),3);
    QVERIFY(!item(window,"editorNextCanvas")->isEnabled());
    QTest::keyClick(window,Qt::Key_PageUp); QTRY_COMPARE(project->currentIndex(),2);
    click(window,item(window,"editorPreviousCanvas")); QTRY_COMPARE(project->currentIndex(),1);
    QCOMPARE(project->currentCanvas(),second); QVERIFY(second->canUndo());
    QCOMPARE(iiSharedCanvas::renderFrame(*second->document(),0).pixels.pixels,painted);
    QTest::keySequence(window, QKeySequence(QKeySequence::Save));
    auto *dialog=window->findChild<QObject *>("editorDocumentSaveDialog"); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool()); QCOMPARE(dialog->property("defaultSuffix").toString(),"iiscp");
    const auto path=storage.filePath("whole project %20 #.iiscp");
    QVERIFY(dialog->setProperty("selectedFile",QUrl::fromLocalFile(path)));
    QVERIFY(QMetaObject::invokeMethod(dialog,"accepted")); QVERIFY(QMetaObject::invokeMethod(dialog,"close"));
    QCOMPARE(project->filePath(),path); QVERIFY(!project->modified());
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));
        QVERIFY(project->saveDocumentAs(QUrl::fromLocalFile(capture+"/"+QTest::currentDataTag()+".iiscp")));
        QTRY_VERIFY(!second->rendering() && second->residentTileCount()>0);
        const auto point=second->mapToScene({second->panX()+second->zoom()*16,second->panY()+second->zoom()*30});
        QTRY_COMPARE(window->grabWindow().pixelColor((point*window->devicePixelRatio()).toPoint()),QColor::fromRgba(painted[30*32+16]));
        QVERIFY(window->grabWindow().save(capture+"/"+QTest::currentDataTag()+".png"));
    }
    QVERIFY(QMetaObject::invokeMethod(editor,"openDocumentSource",Q_ARG(QVariant,QUrl::fromLocalFile(path)),Q_ARG(QVariant,false)));
    QCOMPARE(project->canvasCount(),4); QCOMPARE(project->currentIndex(),1);
    QCOMPARE(iiSharedCanvas::renderFrame(*project->currentCanvas()->document(),0).pixels.pixels,painted);
    click(window,editorBackControl(window)); QTRY_VERIFY(result->isVisible());
    QCOMPARE(listProperty(selection,"selectedImages").size(),4);
}

void GuiTests::homeEditorRoutes()
{
    QFETCH(QString, target);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/home-editor-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    EditorCanvas source;
    QVERIFY(source.createCanvas({{"width", 72}, {"height", 48}, {"unit", "px"}, {"background", "White"}}));
    const auto nativePath = storage.filePath("Files/native %20 #.iisc");
    QVERIFY(source.saveDocumentAs(QUrl::fromLocalFile(nativePath)));
    QImage red(64, 40, QImage::Format_ARGB32); red.fill(Qt::red);
    QImage blue(32, 60, QImage::Format_ARGB32); blue.fill(Qt::blue);
    QVERIFY(red.save(storage.filePath("Generation History/red %20 #.png")));
    QVERIFY(blue.save(storage.filePath("Generation History/blue.png")));
    QFile broken(storage.filePath("broken.iisc"));
    QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("invalid"); broken.close();
    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", target));
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(target == "macos" ? QSize(1280, 900) : QSize(390, 844));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *editor = item(window, "canvasEditor");
    auto *canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    auto *quick = item(window, "quickGenerate");
    QVERIFY(editor && canvas && quick);
    QVERIFY(quick->setProperty("prompt", "Keep the Home draft"));
    auto *openButton = item(window, target == "macos" ? "desktopRecentFilesOpenFile" : "mobileHomeOpenFile");
    QVERIFY(openButton);
    auto *dialog = window->findChild<QObject *>("homeOpenFileDialog");
    QVERIFY(dialog);
    click(window, openButton);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QVERIFY(!editor->isVisible());
    const auto before = iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
    click(window, openButton);
    QVERIFY(dialog->setProperty("selectedFile", QUrl::fromLocalFile(broken.fileName())));
    QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QVERIFY(!editor->isVisible());
    QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, before);
    QVERIFY(!window->property("generationRequestError").toString().isEmpty());
    click(window, openButton);
    QVERIFY(dialog->setProperty("selectedFile", QUrl::fromLocalFile(nativePath)));
    QVERIFY(QMetaObject::invokeMethod(dialog, "accepted"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(editor->isVisible());
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QCOMPARE(canvas->filePath(), nativePath);
    QCOMPARE(canvas->canvasWidth(), 72);
    QCOMPARE(iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels.front(), 0xffffffffU);
    QVERIFY(window->property("generationRequestError").toString().isEmpty());
    click(window, editorBackControl(window));
    auto *recentCards = item(window, target == "macos" ? "desktopRecentFileCards" : "recentFileCards");
    QVERIFY(recentCards);
    QTRY_COMPARE(recentCards->property("count").toInt(), 1);
    auto *recent = visualItem(recentCards, target == "macos" ? "desktopRecentFile0" : "recentFileCard0");
    QVERIFY(recent);
    click(window, recent);
    QTRY_VERIFY(editor->isVisible());
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QCOMPARE(canvas->filePath(), nativePath);
    click(window, editorBackControl(window));
    auto *newCanvas = target == "macos" ? visualItem(item(window, "desktopSidebar"), "desktopAction_canvas") : item(window, "newCanvasAction");
    QVERIFY(newCanvas);
    click(window, newCanvas);
    QTRY_VERIFY(item(window, "canvasCreateButton")->isVisible());
    click(window, item(window, "canvasCreateButton"));
    QTRY_VERIFY(editor->isVisible());
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QVERIFY(canvas->filePath().isEmpty());
    QVERIFY(canvas->canvasWidth() > 0 && canvas->canvasHeight() > 0);
    QVERIFY(listProperty(editor, "generationResults").isEmpty());
    click(window, editorBackControl(window));
    auto *selection = item(window, target == "macos" ? "generationHistorySelection" : "mobileGenerationHistorySelection");
    QVERIFY(selection);
    QTRY_COMPARE(listProperty(selection, "images").size(), 2);
    click(window, item(selection, target == "macos" ? "generationHistorySelectionToggle" : "mobileGenerationHistorySelectionToggle"));
    auto *historyCards = item(window, target == "macos" ? "generationHistoryCards" : "mobileGenerationHistoryCards");
    QVERIFY(historyCards);
    auto *first = visualItem(historyCards, target == "macos" ? "generationHistoryCard0" : "mobileGenerationHistoryCard0");
    auto *second = visualItem(historyCards, target == "macos" ? "generationHistoryCard1" : "mobileGenerationHistoryCard1");
    QVERIFY(first && second);
    click(window, first);
    click(window, second);
    QCOMPARE(listProperty(selection, "selectedImages").size(), 2);
    QVERIFY(first->property("selected").toBool() && second->property("selected").toBool());
    click(window, item(selection, target == "macos" ? "generationHistorySelectionOpen" : "mobileGenerationHistorySelectionOpen"));
    QTRY_VERIFY(editor->isVisible());
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    auto *project = window->findChild<EditorProject *>("editorProject");
    QVERIFY(project); QCOMPARE(project->canvasCount(),2);
    QCOMPARE(canvas->document()->layers.size(), 1u);
    QCOMPARE(listProperty(editor, "generationResults").size(), 2);
    QVERIFY(canvas->filePath().isEmpty());
    const auto saved = storage.filePath("selected.iiscp");
    QVERIFY(project->saveDocumentAs(QUrl::fromLocalFile(saved)));
    EditorProject observer;
    QVERIFY(observer.openDocumentSource(QUrl::fromLocalFile(saved)));
    QCOMPARE(observer.canvasCount(), 2);
    const auto capture = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));
        const auto artifact = capture + "/" + QTest::currentDataTag() + ".iiscp";
        QFile::remove(artifact);
        QVERIFY(project->saveDocumentAs(QUrl::fromLocalFile(artifact)));
        QTRY_VERIFY(!canvas->rendering() && canvas->residentTileCount() > 0);
        const int x=canvas->canvasWidth()/2,y=canvas->canvasHeight()/2;
        const auto expected = QColor::fromRgba(iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels[y*canvas->canvasWidth()+x]);
        const auto position = canvas->mapToScene({canvas->panX() + canvas->zoom() * x, canvas->panY() + canvas->zoom() * y});
        QTRY_COMPARE(window->grabWindow().pixelColor((position * window->devicePixelRatio()).toPoint()), expected);
        QVERIFY(window->grabWindow().save(capture + "/" + QTest::currentDataTag() + ".png"));
    }
    click(window, editorBackControl(window));
    QCOMPARE(listProperty(selection, "selectedImages").size(), 2);
    QCOMPARE(quick->property("prompt").toString(), "Keep the Home draft");
    click(window, first);
    QCOMPARE(listProperty(selection, "selectedImages").size(), 1);
    click(window, item(selection, target == "macos" ? "generationHistorySelectionToggle" : "mobileGenerationHistorySelectionToggle"));
    click(window, second);
    QTRY_VERIFY(editor->isVisible());
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QCOMPARE(canvas->document()->layers.size(), 1u);
}

void GuiTests::generatedImageSelectionRoutes_data() { homeEditorRoutes_data(); }
void GuiTests::generatedImageSelectionRoutes()
{
    QFETCH(QString, target);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/result-editor-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QVariantList results;
    for (int index = 0; index < 3; ++index) {
        QImage image(index == 0 ? QSize(64, 40) : QSize(32, 60), QImage::Format_ARGB32);
        image.fill(index == 0 ? Qt::red : index == 1 ? Qt::blue : Qt::green);
        const auto path = storage.filePath(QString("image-%1.png").arg(index));
        QVERIFY(image.save(path));
        results.append(QVariantMap{{"id", "one-job-with-several-outputs"}, {"imageSource", QUrl::fromLocalFile(path)}, {"mediaType", "Image"}, {"prompt", "Generated batch"}});
    }
    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", target));
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(target == "macos" ? QSize(1280, 900) : QSize(390, 844));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *result = item(window, "generationResult");
    auto *editor = item(window, "canvasEditor");
    auto *canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    auto *quick = item(window, "quickGenerate");
    QVERIFY(result && editor && canvas && quick);
    quick->setProperty("prompt", "Keep the result draft");
    QVERIFY(result->setProperty("results", QVariantList{results[0], results[1]}));
    QVERIFY(window->setProperty("resultVisible", true));
    QTRY_VERIFY(visualItem(result, "resultImageTile1"));
    QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, visualItem(result, "resultImageTile0")->mapToScene(visualItem(result, "resultImageTile0")->boundingRect().center()).toPoint(), 50);
    QTRY_VERIFY(result->property("detailVisible").toBool());
    click(window, item(window, "resultImageSelectionToggle"));
    QTRY_VERIFY(result->property("galleryVisible").toBool());
    QVERIFY(!result->property("detailVisible").toBool());
    click(window, visualItem(result, "resultImageTile0"));
    click(window, visualItem(result, "resultImageTile1"));
    auto *selection = item(window, "resultImageSelection");
    QCOMPARE(listProperty(selection, "selectedImages").size(), 2);
    QVERIFY(result->setProperty("results", results));
    QTRY_COMPARE(listProperty(result, "galleryResults").size(), 3);
    QCOMPARE(listProperty(selection, "selectedImages").size(), 2);
    click(window, item(window, "resultImageSelectionOpen"));
    QTRY_VERIFY(editor->isVisible());
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    auto *project = window->findChild<EditorProject *>("editorProject");
    QCOMPARE(project->canvasCount(),2);
    QCOMPARE(canvas->document()->layers.size(), 1u);
    QCOMPARE(canvas->canvasWidth(), 64);
    QCOMPARE(canvas->canvasHeight(), 40);
    QCOMPARE(listProperty(editor, "generationResults"), QVariantList({results[0], results[1]}));
    click(window, editorBackControl(window));
    QTRY_VERIFY(result->isVisible());
    QCOMPARE(listProperty(selection, "selectedImages").size(), 2);
    QCOMPARE(quick->property("prompt").toString(), "Keep the result draft");
    QVERIFY(result->setProperty("results", QVariantList{results[0]}));
    QTRY_COMPARE(listProperty(selection, "selectedImages").size(), 1);
    QTRY_VERIFY(result->property("galleryVisible").toBool());
    click(window, visualItem(result, "resultImageTile0"));
    QCOMPARE(listProperty(selection, "selectedImages").size(), 0);
    QVERIFY(!item(selection, "resultImageSelectionOpen")->isEnabled());
    click(window, visualItem(result, "resultImageTile0"));
    click(window, item(selection, "resultImageSelectionOpen"));
    QTRY_VERIFY(editor->isVisible());
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QCOMPARE(canvas->document()->layers.size(), 1u);
}

void GuiTests::mobileEditorToolbarSlidesAndSelects_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<QSize>("size");
    QTest::newRow("toolbar-ios-320") << QString("ios") << QSize(320, 640);
    QTest::newRow("toolbar-ios-390") << QString("ios") << QSize(390, 844);
    QTest::newRow("toolbar-ios-402") << QString("ios") << QSize(402, 874);
    QTest::newRow("toolbar-android-360") << QString("android") << QSize(360, 800);
    QTest::newRow("toolbar-landscape") << QString("ios") << QSize(844, 480);
    QTest::newRow("toolbar-full-reference") << QString("ios") << QSize(1760, 480);
}

void GuiTests::mobileEditorToolbarSlidesAndSelects()
{
    QFETCH(QString, target);
    QFETCH(QSize, size);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/toolbar-gui-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", target));
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(size);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    click(window, item(window, "newCanvasAction"));
    QTRY_VERIFY(item(window, "canvasCreateButton")->isVisible());
    click(window, item(window, "canvasCreateButton"));
    auto *editor = item(window, "canvasEditor");
    auto *toolbar = item(editor, "editorToolbar");
    auto *list = item(toolbar, "editorToolList");
    QVERIFY(editor && toolbar && list);
    QTRY_VERIFY(toolbar->isVisible());
    QCOMPARE(toolbar->height(), 84.0);
    QCOMPARE(toolbar->width(), editor->width() - 16);
    QCOMPARE(bounds(toolbar, editor).bottom(), editor->height() - 8);
    QVERIFY(bounds(item(editor, "editorWorkspace"), editor).bottom() <= toolbar->y() - 16);
    QCOMPARE(list->property("count").toInt(), 19);
    QCOMPARE(editor->property("selectedTool").toString(), "elements");
    QSignalSpy selections(editor, SIGNAL(toolSelected(QString)));
    QVERIFY(selections.isValid());
    auto *content = list->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    const auto toolFor = [content](const QString &key) -> QQuickItem * {
        for (auto *child : content->childItems())
            if (child->objectName() == "editorTool-" + key) return child;
        return nullptr;
    };
    QTRY_VERIFY(toolFor("elements") && toolFor("text"));
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        const auto capture = toolbar->grabToImage();
        QVERIFY(capture);
        QTRY_VERIFY(!capture->image().isNull());
        QVERIFY(capture->image().save(captureDirectory + '/' + QTest::currentDataTag() + ".png"));
    }
    click(window, toolFor("text"));
    QCOMPARE(editor->property("selectedTool").toString(), "text");
    QCOMPARE(selections.size(), 1);
    QVERIFY(toolFor("text")->property("selected").toBool());
    QVERIFY(!toolFor("elements")->property("selected").toBool());
    auto *toolSheet = editor->findChild<QObject *>("editorToolSheet");
    QVERIFY(toolSheet);
    QTRY_VERIFY(toolSheet->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(toolSheet, "dismiss"));
    QTRY_VERIFY(!toolSheet->property("visible").toBool());
    if (size.width() < 1760) {
        const auto start = list->mapToScene(QPointF(qMin(220.0, list->width() - 20), 34)).toPoint();
        auto *touch = QTest::createTouchDevice();
        QTest::touchEvent(window, touch).press(0, start, window);
        for (int distance : {20, 60, 100, 160}) {
            QTest::touchEvent(window, touch).move(0, start - QPoint(distance, 0), window);
            QTest::qWait(25);
        }
        QTest::touchEvent(window, touch).release(0, start - QPoint(160, 0), window);
        QTRY_VERIFY(list->property("contentX").toReal() > 0);
        QTRY_VERIFY(!list->property("moving").toBool());
        QCOMPARE(selections.size(), 1); // Sliding over a tab must not activate it.
        QCOMPARE(editor->property("selectedTool").toString(), "text");
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
        QVERIFY(QMetaObject::invokeMethod(toolSheet, "dismiss"));
        QTRY_VERIFY(!toolSheet->property("visible").toBool());
        QTRY_COMPARE(list->property("contentX").toReal(), 0.0);
        const int beforeDrag = selections.size();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        for (int distance : {20, 60, 100, 160})
            QTest::mouseMove(window, start - QPoint(distance, 0), 25);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start - QPoint(160, 0));
        QTRY_VERIFY(list->property("contentX").toReal() > 0);
        QTRY_VERIFY(!list->property("moving").toBool());
        QCOMPARE(selections.size(), beforeDrag);
    }

    QFile manifest(sourceUrl("Views/Editor/Assets/manifest.json").toLocalFile());
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const auto assets = QJsonDocument::fromJson(manifest.readAll()).object()["icons"].toArray();
    QCOMPARE(assets.size(), 19);
    for (int index = 0; index < assets.size(); ++index) {
        const auto asset = assets[index].toObject();
        const auto file = asset["file"].toString();
        const auto key = file.chopped(4);
        const auto path = sourceUrl("Views/Editor/Assets/" + file).toLocalFile();
        QVERIFY(QFileInfo(path).size() > 0);
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index)));
        QTRY_VERIFY(toolFor(key));
        auto *tool = toolFor(key);
        QCOMPARE(tool->width(), 86.0);
        QCOMPARE(tool->height(), 68.0);
        QCOMPARE(tool->property("text").toString(), asset["label"].toString());
        auto *icon = item(tool, "editorToolIcon");
        auto *slot = item(tool, "editorToolIconSlot");
        QVERIFY(icon && slot);
        QTRY_COMPARE(icon->property("status").toInt(), 1);
        QCOMPARE(icon->property("source").toUrl(), QUrl::fromLocalFile(path));
        QCOMPARE(slot->size(), QSizeF(22, 22));
        QCOMPARE(icon->width(), asset["width"].toString().toDouble());
        QCOMPARE(icon->height(), asset["height"].toString().toDouble());
        QCOMPARE(icon->x(), asset["x"].toDouble());
        QCOMPARE(icon->y(), asset["y"].toDouble());
        QVERIFY(QMetaObject::invokeMethod(toolSheet, "dismiss"));
        QTRY_VERIFY(!toolSheet->property("visible").toBool());
    }
    QCOMPARE(editor->property("selectedTool").toString(), "eraser");
    auto *last = toolFor("eraser");
    QVERIFY(bounds(last, list).right() <= list->width() + 1);
    QTest::keyClick(window, Qt::Key_Home);
    QTRY_COMPARE(editor->property("selectedTool").toString(), "elements");
    QVERIFY(QMetaObject::invokeMethod(toolSheet, "dismiss"));
    QTRY_VERIFY(!toolSheet->property("visible").toBool());
    QTest::keyClick(window, Qt::Key_End);
    QTRY_COMPARE(editor->property("selectedTool").toString(), "eraser");
    window->resize(320, 640);
    // Home/End may destroy and recreate ListView delegates; resolve the current one.
    QTRY_VERIFY(toolFor("eraser") && bounds(toolFor("eraser"), list).right() <= list->width() + 1);
    QCOMPARE(editor->property("selectedTool").toString(), "eraser");
    // A bottom system-safe inset belongs to Main, not to the toolbar's content.
    const auto bottomInset = window->property("mobileSystemSafeBottomInset").toReal();
    QTRY_VERIFY(bounds(toolbar, window->contentItem()).bottom() <= window->height() - bottomInset - 8);
    QVERIFY(theme->setProperty("targetOverride", "macos"));
    QTRY_VERIFY(toolbar->isVisible()); // Desktop now retains the same tool strip.
    QTRY_VERIFY(!toolSheet->property("visible").toBool());
}

void GuiTests::editorDocumentRenderQuality()
{
    using namespace iiSharedCanvas;
    QTemporaryDir files(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/editor-quality-XXXXXX");
    QVERIFY(files.isValid());
    QString input = qEnvironmentVariable("DREAMSCAPES_RENDER_IMAGE");
    const bool supplied = !input.isEmpty();
    if (!supplied) {
        QImage checker(640, 480, QImage::Format_ARGB32);
        for (int y = 0; y < checker.height(); ++y) for (int x = 0; x < checker.width(); ++x)
            checker.setPixel(x, y, (x + y) % 2 ? 0xffffffffU : 0xff000000U);
        input = files.filePath("detail.png");
        QVERIFY(checker.save(input));
    }
    const QImage original(input);
    QVERIFY(!original.isNull());
    const auto originalBytes = iiFileProvider::File::read(input);
    EditorCanvas prepared;
    QVERIFY(prepared.openDocumentSource(QUrl::fromLocalFile(input)));
    const auto originalPixels = std::get<RasterAsset>(prepared.document()->assets.front()).pixels.pixels;
    const auto working = files.filePath("working.iisc");
    QVERIFY(prepared.saveDocumentAs(QUrl::fromLocalFile(working)));
    const auto encoded = encodeIisc(*prepared.document());
    QVERIFY(encoded.ok());
    const auto snapshot = files.filePath("snapshot.iisc");
    iiFileProvider::File::create(snapshot, QByteArray(reinterpret_cast<const char *>(encoded.bytes.data()), encoded.bytes.size()));
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QQuickWindow window;
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    window.resize(1440, 1000);
    editor->setParentItem(window.contentItem()); editor->setSize(window.size());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QStringList documents{working, snapshot};
    const auto existing = qEnvironmentVariable("DREAMSCAPES_RENDER_DOCUMENT");
    if (!existing.isEmpty()) documents.append(existing);
    for (const auto &path : documents) {
        QVERIFY(QMetaObject::invokeMethod(editor.data(), "openDocumentSource",
            Q_ARG(QVariant, QUrl::fromLocalFile(path)), Q_ARG(QVariant, false)));
        auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
        QVERIFY(canvas && canvas->documentReady());
        QCOMPARE(canvas->canvasWidth(), original.width());
        QCOMPARE(canvas->canvasHeight(), original.height());
        QCOMPARE(std::get<RasterAsset>(canvas->document()->assets.front()).pixels.pixels, originalPixels);
        QVERIFY(canvas->smoothRendering());
        QCOMPARE(canvas->window(), &window);
        QCOMPARE(canvas->renderDevicePixelRatio(), window.effectiveDevicePixelRatio());
        const qreal zoom = supplied ? 0.45 : 0.25;
        canvas->setZoom(zoom);
        canvas->setPanX((canvas->width() - original.width() * zoom) / 2);
        canvas->setPanY((canvas->height() - original.height() * zoom) / 2);
        QTRY_VERIFY_WITH_TIMEOUT(!canvas->rendering(), 15000);
        QTest::qWait(150);
        const QImage screen = window.grabWindow();
        QVERIFY(!screen.isNull());
        const qreal ratio = qreal(screen.width()) / window.width();
        const auto position = canvas->mapToScene({canvas->panX(), canvas->panY()});
        const QRect area(qRound(position.x() * ratio), qRound(position.y() * ratio),
            qRound(original.width() * zoom * ratio), qRound(original.height() * zoom * ratio));
        QVERIFY(screen.rect().contains(area));
        const auto displayed = screen.copy(area);
        const auto expected = original.scaled(displayed.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        double difference = 0;
        for (int y = 4; y < displayed.height() - 4; ++y) for (int x = 4; x < displayed.width() - 4; ++x) {
            const auto actual = displayed.pixelColor(x, y), reference = expected.pixelColor(x, y);
            difference += qAbs(actual.red() - reference.red()) + qAbs(actual.green() - reference.green()) + qAbs(actual.blue() - reference.blue());
        }
        const double mean = difference / ((displayed.width() - 8) * (displayed.height() - 8) * 3);
        qInfo() << "Editor render quality:" << canvas->graphicsBackend() << "DPR" << ratio
                << "LOD" << canvas->renderLevelOfDetail() << "mean RGB error" << mean;
        QVERIFY2(mean < 8.0, qPrintable(QString("Displayed image error %1 exceeds reference tolerance").arg(mean)));
        const auto evidence = QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/verification/iisc-render-quality";
        QVERIFY(QDir().mkpath(evidence));
        const auto name = path == working ? "working" : path == snapshot ? "snapshot" : "existing";
        QVERIFY(screen.save(evidence + QString("/editor-%1.png").arg(name)));
        QVERIFY(displayed.save(evidence + QString("/canvas-%1.png").arg(name)));
        if (supplied && path == working)
            QVERIFY(canvas->saveDocumentAs(QUrl::fromLocalFile(evidence + "/original-quality.iisc")));
    }
    QCOMPARE(iiFileProvider::File::read(input), originalBytes);
    // Shortcuts belong to the editor and must retire before their window context.
    editor.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void GuiTests::editorToolbarOperations()
{
    using namespace iiSharedCanvas;
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window; window.resize(1440, 900); editor->setParentItem(window.contentItem()); editor->setSize(window.size()); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    auto *toolbar = item(editor.data(), "editorToolbar"); auto *sheet = editor->findChild<QObject *>("editorToolSheet");
    QVERIFY(canvas && toolbar && sheet);
    QVERIFY(canvas->createCanvas({{"width", 64}, {"height", 64}, {"unit", "px"}, {"background", "Transparent"}}));
    const auto tool = [&](int i) { return QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, i)); };
    const auto edit = [&](const char *key, QVariant value) { return QMetaObject::invokeMethod(sheet, "setValue", Q_ARG(QVariant, QString::fromLatin1(key)), Q_ARG(QVariant, value)); };
    const auto point = [&](int x, int y) { return canvas->mapToScene({canvas->panX() + x * canvas->zoom(), canvas->panY() + y * canvas->zoom()}).toPoint(); };
    QVERIFY(tool(1)); QVERIFY(edit("selector", "Free Text")); QVERIFY(edit("field-0", "Hello")); QVERIFY(edit("field-2", 12));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point(2, 22));
    QVERIFY(!canvas->selectedLayerId().isEmpty()); const auto before = renderFrame(*canvas->document(), 0).pixels.pixels;
    QVERIFY(std::ranges::any_of(before, [](auto p) { return p >> 24; }));
    QVERIFY(edit("field-0", "Changed")); QVERIFY(renderFrame(*canvas->document(), 0).pixels.pixels != before);
    canvas->forceActiveFocus();
    QTest::keySequence(&window, QKeySequence(QKeySequence::Undo));
    QCOMPARE(renderFrame(*canvas->document(), 0).pixels.pixels, before);
    QTRY_COMPARE(sheet->property("values").value<QJSValue>().toVariant().toMap().value("field-0").toString(), QString("Hello"));
    QVERIFY(tool(10)); QVERIFY(edit("selector", "Rectangle")); QVERIFY(edit("field-2", 0)); QVERIFY(edit("field-5", false));
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, point(4, 4)); QTest::mouseMove(&window, point(16, 16), 30); QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, point(16, 16));
    QVERIFY(canvas->toolState().value("selectionActive").toBool());
    QVERIFY(tool(14)); QVERIFY(edit("selector", "Solid")); QVERIFY(edit("field-1", "#804020")); QVERIFY(edit("field-2", 100));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point(8, 8));
    QVERIFY(canvas->rasterLayerSelected()); QCOMPARE(canvas->selectedRasterPixels()->pixels[8 * 64 + 8], 0xff804020U);
    QVERIFY(canvas->setLayerOpacity(canvas->selectedLayerId(), 0.5));
    QVERIFY(tool(9)); QTRY_COMPARE(sheet->property("values").value<QJSValue>().toVariant().toMap().value("field-10").toDouble(), 50.0);
    QVERIFY(canvas->setLayerOpacity(canvas->selectedLayerId(), 1.0));
    QVERIFY(tool(11)); QVERIFY(edit("field-1", 1.0)); QVERIFY(qRed(canvas->selectedRasterPixels()->pixels[8 * 64 + 8]) > 128);
    auto *preview = visualItem(editor.data(), "editorPreview-field-0"); QVERIFY(preview && preview->isEnabled()); click(&window, preview);
    auto *surface = editor->findChild<QObject *>("editorToolPreviewSheet"); QVERIFY(surface); QTRY_VERIFY(surface->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(surface, "close")); QTRY_VERIFY(!surface->property("visible").toBool());
    QVERIFY(tool(2)); QVERIFY(!visualItem(editor.data(), "editorAction-field-15"));
    QVERIFY(!sheet->property("definition").value<QJSValue>().toVariant().toMap()["selector"].toMap()["options"].toStringList().contains("RAW Capture"));
    QVERIFY(tool(0)); QVERIFY(visualItem(editor.data(), "editorControl-field-1"));
    QVERIFY(!visualItem(editor.data(), "editorControl-field-4"));
    QVERIFY(edit("selector", "Ellipse"));
    QTRY_VERIFY(visualItem(editor.data(), "editorControl-field-4"));
    QVERIFY(!visualItem(editor.data(), "editorControl-field-1"));
    QVERIFY(canvas->createCanvas({{"width", 64}, {"height", 64}, {"unit", "px"}, {"background", "Transparent"}}));
    for (int index = 0; index < 19; ++index) {
        QVERIFY(tool(index));
        const auto definition = sheet->property("definition").value<QJSValue>().toVariant().toMap();
        const auto key = definition["key"].toString();
        auto modes = definition["selector"].toMap()["options"].toStringList();
        if (modes.isEmpty()) modes.append(QString());
        for (const auto &mode : modes) {
            if (!mode.isEmpty()) QVERIFY(edit("selector", mode));
            QCoreApplication::processEvents();
            auto *panel = item(editor.data(), "editorToolPanel"); QVERIFY(panel);
            const auto fields = panel->property("presentedFields").value<QJSValue>().toVariant().toList(); QVERIFY(!fields.isEmpty());
            const auto values = sheet->property("values").value<QJSValue>().toVariant().toMap();
            for (const auto &field : fields) QVERIFY2(canvas->toolControlState(key, field.toMap()["id"].toString(), values).value("supported").toBool(), qPrintable(key + '/' + mode));
            for (const auto &field : definition["fields"].toList()) {
                const auto f = field.toMap();
                if (canvas->toolControlState(key, f["id"].toString(), values).value("supported").toBool()) continue;
                const auto name = (f["type"].toString() == "Action" ? "editorAction-" : "editorControl-") + f["id"].toString();
                QVERIFY2(!visualItem(panel, qPrintable(name)), qPrintable(key + '/' + mode + ": unsupported control rendered"));
            }
        }
    }
    editor.reset(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void GuiTests::editorNativeCanvasEditsAndPersists()
{
    using namespace iiSharedCanvas;
    QTemporaryDir files(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/editor-native-XXXXXX");
    QVERIFY(files.isValid());
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(1440, 900);
    editor->setParentItem(window.contentItem());
    editor->setSize(window.size());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *sheet = editor->findChild<QObject *>("editorToolSheet");
    QVERIFY(canvas && toolbar && sheet);
    QVERIFY(canvas->createCanvas({{"width", 64}, {"height", 64}, {"unit", "px"}, {"background", "Transparent"}}));
    auto selectTool = [&](int index) { return QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index)); };
    auto setField = [&](const char *field, QVariant value) {
        return QMetaObject::invokeMethod(sheet, "setValue", Q_ARG(QVariant, QString::fromLatin1(field)), Q_ARG(QVariant, value));
    };
    QVERIFY(selectTool(15)); // Brush, through the same toolbar and panel as the user.
    QVERIFY(setField("field-4", 8));
    QVERIFY(setField("field-5", 100));
    QVERIFY(setField("field-8", 100));
    QVERIFY(setField("field-9", 100));
    QVERIFY(setField("field-17", 0));
    QVERIFY(setField("field-21", 0));
    QVERIFY(setField("field-22", 0));
    auto *commands = editor->findChild<QObject *>("editorDocumentContextMenu");
    QVERIFY(commands);
    const auto command = [&](const QString &label) {
        auto *surface = item(editor.data(), "editorCanvasSurface");
        QTest::mouseClick(&window, Qt::RightButton, Qt::NoModifier, surface->mapToScene({20, 20}).toPoint());
        QTRY_VERIFY(commands->property("opened").toBool());
        QTRY_COMPARE(commands->property("scale").toReal(), 1.0);
        auto *content = commands->property("contentItem").value<QQuickItem *>();
        QVERIFY(content);
        QQuickItem *button = nullptr;
        QTRY_VERIFY((button = menuCommand(content, label)) && button->isVisible());
        click(&window, button);
        QTRY_VERIFY(!commands->property("visible").toBool());
    };
    command("Paint Color");
    auto *picker = item(editor.data(), "editorPaintColorPicker");
    QVERIFY(picker);
    QTRY_VERIFY(picker->isVisible());
    engine.rootContext()->setContextProperty("nativePicker", picker);
    QQmlExpression color(engine.rootContext(), picker, "nativePicker.setHex('#ff0000'); nativePicker.accept()");
    color.evaluate();
    QVERIFY2(!color.hasError(), qPrintable(color.error().toString()));
    QTRY_VERIFY(!picker->isVisible());
    QCOMPARE(canvas->brushColor(), QColor(Qt::red));
    auto position = [&](double x, double y) {
        return canvas->mapToScene({canvas->panX() + x * canvas->zoom(), canvas->panY() + y * canvas->zoom()}).toPoint();
    };
    auto stroke = [&](double y) {
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, position(16, y));
        QTest::mouseMove(&window, position(32, y), 25);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, position(48, y));
    };
    stroke(32);
    QTRY_VERIFY(canvas->rasterLayerSelected());
    const auto paintId = canvas->selectedLayerId();
    QCOMPARE(canvas->selectedRasterPixels()->pixels[32 * 64 + 32], 0xffff0000U);
    const auto path = files.filePath("edited.iisc");
    QTest::keySequence(&window, QKeySequence(QKeySequence::Save));
    auto *saveDialog = editor->findChild<QObject *>("editorDocumentSaveDialog");
    QVERIFY(saveDialog);
    QTRY_VERIFY(saveDialog->property("visible").toBool());
    QVERIFY(saveDialog->setProperty("selectedFile", QUrl::fromLocalFile(path)));
    QVERIFY(QMetaObject::invokeMethod(saveDialog, "accepted"));
    QVERIFY(QMetaObject::invokeMethod(saveDialog, "close"));
    QTRY_COMPARE(canvas->filePath(), path);
    QTRY_VERIFY(!saveDialog->property("visible").toBool());
    DocumentFile observer;
    QVERIFY(observer.open(path.toStdString()).ok());
    QCOMPARE(renderFrame(*observer.document(), 0).pixels.pixels[32 * 64 + 32], 0xffff0000U);
    observer.close();
    QVERIFY(selectTool(18)); // Pixel eraser.
    QVERIFY(setField("field-0", 8));
    QVERIFY(setField("field-1", 100));
    QVERIFY(setField("field-2", 100));
    stroke(32);
    QCOMPARE(canvas->selectedRasterPixels()->pixels[32 * 64 + 32], 0U);
    QVERIFY(observer.open(path.toStdString()).ok());
    QCOMPARE(renderFrame(*observer.document(), 0).pixels.pixels[32 * 64 + 32], 0U);
    observer.close();
    command("Undo");
    QCOMPARE(canvas->selectedRasterPixels()->pixels[32 * 64 + 32], 0xffff0000U);
    command("Redo");
    QCOMPARE(canvas->selectedRasterPixels()->pixels[32 * 64 + 32], 0U);
    command("Undo");
    command("Layers");
    auto *layers = editor->findChild<QObject *>("editorDocumentLayersSheet");
    QVERIFY(layers);
    QTRY_VERIFY(layers->property("visible").toBool());
    auto *name = item(editor.data(), "editorLayerName");
    QVERIFY(name && name->setProperty("text", "Red paint"));
    QVERIFY(QMetaObject::invokeMethod(name, "accepted", Q_ARG(QString, QStringLiteral("Red paint"))));
    QCOMPARE(QString::fromStdString(layerProperties(canvas->document()->layers.front()).name), "Red paint");
    auto *opacity = item(editor.data(), "editorLayerOpacity");
    QVERIFY(opacity && opacity->setProperty("value", 50));
    QVERIFY(QMetaObject::invokeMethod(opacity, "moved"));
    QCOMPARE(layerProperties(canvas->document()->layers.front()).opacity, 0.5);
    QTRY_VERIFY(visualItem(window.contentItem(), qPrintable("editorLayerVisible-" + paintId)));
    auto *visibility = visualItem(window.contentItem(), qPrintable("editorLayerVisible-" + paintId));
    click(&window, visibility);
    QCOMPARE(layerProperties(canvas->document()->layers.front()).visible, false);
    QTRY_VERIFY(visualItem(window.contentItem(), qPrintable("editorLayerVisible-" + paintId)));
    click(&window, visualItem(window.contentItem(), qPrintable("editorLayerVisible-" + paintId)));
    QVERIFY(QMetaObject::invokeMethod(layers, "close"));
    QTRY_VERIFY(!layers->property("visible").toBool());
    QVERIFY(selectTool(0)); // Native vector creation by dragging on the canvas.
    QVERIFY(setField("selector", "Rectangle"));
    QVERIFY(setField("field-2", "Solid"));
    QVERIFY(setField("field-10", 0));
    QVERIFY(setField("field-16", "#8B7CFF"));
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, position(8, 8));
    QTest::mouseMove(&window, position(28, 24), 25);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, position(28, 24));
    QCOMPARE(canvas->document()->layers.size(), 2u);
    QVERIFY(std::holds_alternative<VectorLayer>(canvas->document()->layers.back()));
    const auto vectorId = canvas->selectedLayerId();
    QVERIFY(observer.open(path.toStdString()).ok());
    QCOMPARE(observer.document()->layers.size(), 2u);
    QCOMPARE(layerProperties(observer.document()->layers.front()).name, std::string("Red paint"));
    QCOMPARE(layerProperties(observer.document()->layers.front()).opacity, 0.5);
    QVERIFY(std::holds_alternative<VectorLayer>(observer.document()->layers.back()));
    observer.close();
    QVERIFY(selectTool(10)); // Object selection resolves the native rendered layer.
    QVERIFY(setField("selector", "Object"));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, position(16, 16));
    QCOMPARE(canvas->selectedLayerId(), vectorId);
    QVERIFY(canvas->createCanvas({{"width", 32}, {"height", 32}, {"unit", "px"}}));
    QTest::keySequence(&window, QKeySequence(QKeySequence::Open));
    auto *openDialog = editor->findChild<QObject *>("editorDocumentOpenDialog");
    QVERIFY(openDialog);
    QTRY_VERIFY(openDialog->property("visible").toBool());
    QVERIFY(openDialog->setProperty("selectedFile", QUrl::fromLocalFile(path)));
    QVERIFY(QMetaObject::invokeMethod(openDialog, "accepted"));
    QVERIFY(QMetaObject::invokeMethod(openDialog, "close"));
    canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    QTRY_COMPARE(canvas->filePath(), path);
    QTRY_VERIFY(!openDialog->property("visible").toBool());
    QCOMPARE(canvas->document()->layers.size(), 2u);
    QVERIFY(canvas->selectLayer(paintId));
    QCOMPARE(canvas->selectedRasterPixels()->pixels[32 * 64 + 32], 0xffff0000U);
    QTRY_VERIFY(!canvas->rendering());
    const auto capture = qEnvironmentVariable("DREAMSCAPES_NATIVE_EDITOR_CAPTURE");
    if (!capture.isEmpty()) QVERIFY(window.grabWindow().save(capture));
    const auto example = qEnvironmentVariable("DREAMSCAPES_NATIVE_EDITOR_DOCUMENT");
    if (!example.isEmpty()) QVERIFY(canvas->saveDocumentAs(QUrl::fromLocalFile(example)));
}

void GuiTests::desktopEditorLayoutCentersCanvas()
{
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(2267, 1316);
    editor->setParentItem(window.contentItem());
    editor->setSize(window.size());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *list = item(toolbar, "editorToolList");
    auto *workspace = item(editor.data(), "editorWorkspace");
    auto *surface = item(editor.data(), "editorCanvasSurface");
    auto *dock = item(editor.data(), "editorDesktopPanel");
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    QVERIFY(toolbar && list && workspace && surface && dock && canvas);
    QCOMPARE(toolbar->property("radius").toReal(), 0.0);
    QCOMPARE(QQmlProperty(toolbar, "border.width").read().toReal(), 0.0);
    const auto toolbarFill = theme->property("panelBackground05").value<QColor>();
    QCOMPARE(toolbarFill.alpha(), 255);
    QCOMPARE(toolbar->property("color").value<QColor>(), toolbarFill);
    QCOMPARE(bounds(toolbar, editor.data()), QRectF(181, 0, 1744, 54));
    QCOMPARE(bounds(workspace, editor.data()), QRectF(181, 54, 1744, 1262));
    QCOMPARE(bounds(surface, workspace), workspace->boundingRect());
    QVERIFY(!item(editor.data(), "editorDocumentOpen")->isVisible());
    QCOMPARE(item(editor.data(), "editorDocumentControls")->height(), 0.0);
    QCOMPARE(list->property("count").toInt(), 19);
    auto *content = list->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    QFile manifest(sourceUrl("Views/Editor/Assets/manifest.json").toLocalFile());
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const auto icons = QJsonDocument::fromJson(manifest.readAll()).object()["icons"].toArray();
    for (int index = 0; index < icons.size(); ++index) {
        const auto asset = icons[index].toObject();
        const auto key = asset["file"].toString().chopped(4);
        QQuickItem *tool = nullptr;
        QTRY_VERIFY((tool = visualItem(content, qPrintable("editorTool-" + key))));
        QCOMPARE(bounds(tool, toolbar), QRectF(8 + index * 40, 8, 36, 36));
        QCOMPARE(tool->property("text").toString(), asset["label"].toString());
        auto *slot = item(tool, "editorToolIconSlot");
        auto *icon = item(tool, "editorToolIcon");
        QVERIFY(slot && icon);
        QCOMPARE(bounds(slot, tool), QRectF(7, 8, 22, 22));
        QVERIFY(!item(tool, "editorToolLabel")->isVisible());
        QTRY_COMPARE(icon->property("status").toInt(), 1);
        QCOMPARE(icon->size(), QSizeF(asset["width"].toString().toDouble(), asset["height"].toString().toDouble()));
        QCOMPARE(icon->position(), QPointF(asset["x"].toDouble(), asset["y"].toDouble()));
        click(&window, tool);
        QCOMPARE(editor->property("selectedTool").toString(), key);
        QVERIFY(tool->property("selected").toBool());
        auto *background = tool->property("background").value<QQuickItem *>();
        QVERIFY(background);
        QCOMPARE(QQmlProperty(background, "border.width").read().toReal(), 0.0);
    }
    const auto toolbarCapture = toolbar->grabToImage();
    QVERIFY(toolbarCapture);
    QTRY_VERIFY(!toolbarCapture->image().isNull());
    const auto flatImage = toolbarCapture->image();
    for (const QPoint corner : {QPoint(0, 0), QPoint(flatImage.width() - 1, 0),
                                QPoint(0, flatImage.height() - 1),
                                QPoint(flatImage.width() - 1, flatImage.height() - 1)})
        QCOMPARE(flatImage.pixelColor(corner), QColor::fromRgba(toolbarFill.rgba()));
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QVERIFY(editor->setProperty("desktopPanelExpanded", true));
    const auto centered = [&]() {
        const QPointF center = canvas->mapToItem(workspace,
            {canvas->panX() + canvas->canvasWidth() * canvas->zoom() / 2,
             canvas->panY() + canvas->canvasHeight() * canvas->zoom() / 2});
        return QLineF(center, bounds(surface, workspace).center()).length() < 0.01;
    };
    for (const QSize documentSize : {QSize(821, 1032), QSize(1600, 900), QSize(400, 400)}) {
        QVERIFY(canvas->createCanvas({{"width", documentSize.width()}, {"height", documentSize.height()},
                                      {"unit", "px"}, {"background", "White"}}));
        QTRY_VERIFY(centered());
        for (const QSize windowSize : {QSize(1280, 800), QSize(900, 700), QSize(2267, 1316)}) {
            window.resize(windowSize);
            editor->setSize(windowSize);
            QTRY_COMPARE(canvas->size(), surface->size());
            QTRY_VERIFY(centered());
            for (const qreal panelWidth : {280.0, 342.0, 720.0}) {
                QVERIFY(editor->setProperty("desktopPanelWidth", panelWidth));
                QTRY_COMPARE(canvas->size(), surface->size());
                QTRY_VERIFY(centered());
                QCOMPARE(bounds(surface, workspace), workspace->boundingRect());
            }
            QVERIFY(editor->setProperty("desktopPanelExpanded", false));
            QTRY_VERIFY(!dock->isVisible());
            QTRY_VERIFY(centered());
            QVERIFY(editor->setProperty("desktopPanelExpanded", true));
            QTRY_VERIFY(dock->isVisible());
            QTRY_VERIFY(centered());
        }
    }
    canvas->panBy(100, 70);
    QVERIFY(!centered());
    QTest::mouseClick(&window, Qt::RightButton, Qt::NoModifier, surface->mapToScene({30, 30}).toPoint());
    auto *menu = editor->findChild<QObject *>("editorDocumentContextMenu");
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("opened").toBool());
    QTRY_COMPARE(menu->property("scale").toReal(), 1.0);
    auto *menuContent = menu->property("contentItem").value<QQuickItem *>();
    QVERIFY(menuContent);
    auto *fit = menuCommand(menuContent, "Fit");
    QVERIFY(fit && fit->isVisible());
    click(&window, fit);
    QTRY_VERIFY(!menu->property("visible").toBool());
    QTRY_VERIFY(centered());
    QVERIFY(editor->setProperty("desktopPanelWidth", 342));
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        const auto capture = editor->grabToImage();
        QVERIFY(capture);
        QTRY_VERIFY(!capture->image().isNull());
        QVERIFY(capture->image().save(captureDirectory + "/desktop-centered.png"));
        QVERIFY(toolbarCapture->image().save(captureDirectory + "/desktop-flat-toolbar.png"));
    }
    QVERIFY(editor->setProperty("mobileLayout", true));
    QTRY_COMPARE(toolbar->height(), 84.0);
    QCOMPARE(toolbar->property("radius").toReal(), 18.0);
    QCOMPARE(QQmlProperty(toolbar, "border.width").read().toReal(), 1.0);
    QCOMPARE(list->position(), QPointF(9, 9));
    QTRY_VERIFY(item(editor.data(), "editorDocumentOpen")->isVisible());
    QCOMPARE(bounds(toolbar, editor.data()).bottom(), editor->height() - 8);
    QVERIFY(!item(editor.data(), "editorCanvasContextArea")->property("enabled").toBool());
}

void GuiTests::desktopEditorPanelResizes()
{
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(1600, 1000);
    editor->setParentItem(window.contentItem());
    editor->setSize(window.size());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *dock = item(editor.data(), "editorDesktopPanel");
    auto *handle = item(editor.data(), "editorDesktopPanelResizeHandle");
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *state = editor->findChild<QObject *>("editorToolSheet");
    QVERIFY(dock && handle && toolbar && state);
    QCOMPARE(dock->width(), 342.0);
    QCOMPARE(handle->property("cursorShape").toInt(), int(Qt::SizeHorCursor));
    const auto drag = [&](int delta) {
        const auto start = handle->mapToScene(QPointF(handle->width() / 2, 220)).toPoint();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&window, start + QPoint(delta, 0), 20);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, start + QPoint(delta, 0));
    };
    drag(-258);
    QTRY_COMPARE(dock->width(), 600.0);
    drag(400);
    QTRY_COMPARE(dock->width(), 280.0);
    drag(-600);
    QTRY_COMPARE(dock->width(), 720.0);
    // Click and edit a real tool after dragging, then retain its values and width.
    auto *panel = item(dock, "editorToolPanel");
    click(&window, visualItem(panel, "editorChoice-selector-1"));
    QCOMPARE(state->property("values").value<QJSValue>().toVariant().toMap()["selector"].toString(), "Ellipse");
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(!dock->isVisible() && !handle->isVisible());
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(dock->isVisible() && handle->isVisible());
    QCOMPARE(dock->width(), 720.0);
    QCOMPARE(state->property("values").value<QJSValue>().toVariant().toMap()["selector"].toString(), "Ellipse");
    window.resize(900, 700);
    editor->setSize(window.size());
    QTRY_COMPARE(dock->width(), editor->property("desktopPanelMaximumWidth").toReal());
    QVERIFY(dock->width() < 720);
    QCOMPARE(editor->property("desktopPanelWidth").toReal(), 720.0);
    window.resize(1600, 1000);
    editor->setSize(window.size());
    QTRY_COMPARE(dock->width(), 720.0);
    const auto resetPoint = handle->mapToScene(QPointF(handle->width() / 2, 220)).toPoint();
    QTest::mouseDClick(&window, Qt::LeftButton, Qt::NoModifier, resetPoint);
    QTRY_COMPARE(dock->width(), 342.0);

    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        for (const int width : {280, 342, 432, 720}) {
            QVERIFY(editor->setProperty("desktopPanelWidth", width));
            QTRY_COMPARE(dock->width(), qreal(width));
            QTest::qWait(30);
            const auto capture = dock->grabToImage(QSize(width, 1000));
            QVERIFY(capture);
            QTRY_VERIFY(!capture->image().isNull());
            QVERIFY(capture->image().save(captureDirectory + QString("/elements-%1.png").arg(width)));
        }
    }

    // Exercise all design controls, including long labels, at narrow/default/wide widths.
    QVERIFY(state->setProperty("engine", QVariant::fromValue<QObject *>(nullptr)));
    const auto tools = listProperty(toolbar, "tools");
    for (int toolIndex = 0; toolIndex < tools.size(); ++toolIndex) {
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex)));
        QVERIFY(editor->setProperty("desktopPanelExpanded", true));
        for (const qreal width : {280.0, 342.0, 432.0, 720.0}) {
            QVERIFY(editor->setProperty("desktopPanelWidth", width));
            QTRY_COMPARE(dock->width(), width);
            QTest::qWait(5);
            const auto definition = state->property("definition").value<QJSValue>().toVariant().toMap();
            auto fields = definition["fields"].toList();
            if (definition.contains("selector")) fields.prepend(definition["selector"]);
            qreal previousBottom = 0;
            for (const auto &entry : fields) {
                const auto field = entry.toMap();
                const auto id = field["id"].toString();
                const auto type = field["type"].toString();
                if (type == "Action") continue;
                auto *control = visualItem(panel, qPrintable("editorControl-" + id));
                QVERIFY2(control, qPrintable(tools[toolIndex].toMap()["key"].toString() + '/' + id));
                QCOMPARE(control->width(), width - 34);
                const auto rect = bounds(control, panel);
                QVERIFY2(rect.top() >= previousBottom - 0.01, qPrintable(id));
                previousBottom = rect.bottom();
                auto *caption = visualItem(control, qPrintable("editorCaption-" + id));
                if (type != "Visual") {
                    QVERIFY(caption);
                    QCOMPARE(bounds(caption, control).topLeft(), QPointF(0, 0));
                    QVERIFY(caption->height() >= caption->property("contentHeight").toReal());
                }
                const auto checkRight = [&](const QString &name) {
                    auto *parameter = visualItem(control, qPrintable(name + id));
                    QVERIFY(parameter);
                    const auto parameterRect = bounds(parameter, control);
                    QVERIFY(parameterRect.left() >= -0.01);
                    QVERIFY(qAbs(parameterRect.right() - control->width()) < 0.01);
                    QVERIFY(parameterRect.bottom() <= control->height() + 0.01);
                    if (caption && type != "Visual") {
                        const auto labelRect = bounds(caption, control);
                        QVERIFY2(labelRect.bottom() <= parameterRect.top() + 0.01
                            || labelRect.right() + 7.9 <= parameterRect.left(),
                            qPrintable(QString("%1/%2 width %3").arg(type, id).arg(width)));
                    }
                };
                if (type == "Field") checkRight("editorInput-");
                else if (type == "Toggle") checkRight("editorToggle-");
                else if (type == "Visual") checkRight("editorPreview-");
                else if (type == "Dimensions") {
                    for (int index = 0; index < 2; ++index) {
                        auto *input = visualItem(control, qPrintable(QString("editorDimension-%1-%2").arg(id).arg(index)));
                        QVERIFY(input);
                        QCOMPARE(bounds(input, control).right(), control->width());
                        QVERIFY(bounds(input, control).top() >= bounds(caption, control).bottom() + 1.9);
                    }
                } else if (type == "Slider") {
                    checkRight("editorNumeric-");
                    auto *slider = visualItem(control, qPrintable("editorSlider-" + id));
                    QVERIFY(slider);
                    QCOMPARE(bounds(slider, control).right(), control->width());
                    QVERIFY(bounds(slider, control).top() >= bounds(caption, control).bottom() + 7.9);
                } else if (type == "Color") {
                    checkRight("editorColor-");
                    auto *input = visualItem(control, qPrintable("editorColorInput-" + id));
                    auto *picker = visualItem(control, qPrintable("editorColor-" + id));
                    QVERIFY(input && picker);
                    QCOMPARE(bounds(input, control).right() + 8, bounds(picker, control).left());
                } else if (type == "Choices" || type == "Segmented") {
                    const auto options = field["options"].toStringList();
                    QRectF previous;
                    for (int index = 0; index < options.size(); ++index) {
                        auto *button = visualItem(control, qPrintable(QString("editorChoice-%1-%2").arg(id).arg(index)));
                        QVERIFY(button);
                        const auto buttonRect = bounds(button, control);
                        QVERIFY(buttonRect.left() >= -0.01 && buttonRect.right() <= control->width() + 0.01);
                        QVERIFY(buttonRect.top() >= bounds(caption, control).bottom() + 7.9);
                        QVERIFY(buttonRect.bottom() <= control->height() + 0.01);
                        if (index && buttonRect.top() == previous.top())
                            QVERIFY(buttonRect.left() >= previous.right() + 7.9);
                        else if (index)
                            QCOMPARE(previous.right(), control->width());
                        previous = buttonRect;
                    }
                    if (!options.isEmpty()) QCOMPARE(previous.right(), control->width());
                }
                if (QTest::currentTestFailed()) return;
            }
            QRectF previousAction;
            for (const auto &entry : fields) {
                const auto field = entry.toMap();
                if (field["type"].toString() != "Action") continue;
                auto *action = visualItem(panel, qPrintable("editorAction-" + field["id"].toString()));
                QVERIFY(action);
                const auto rect = bounds(action, panel);
                QVERIFY(rect.left() >= -0.01 && rect.right() <= panel->width() + 0.01);
                QVERIFY(rect.top() >= previousBottom);
                if (!previousAction.isNull()) {
                    if (rect.top() == previousAction.top()) QVERIFY(rect.left() >= previousAction.right() + 7.9);
                    else QCOMPARE(previousAction.right(), panel->width());
                }
                previousAction = rect;
            }
            if (!previousAction.isNull()) QCOMPARE(previousAction.right(), panel->width());
        }
    }
    QVERIFY(editor->setProperty("mobileLayout", true));
    QTRY_VERIFY(!handle->isVisible());
}

void GuiTests::desktopEditorElementsMatchesFigma()
{
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(2267, 1316);
    editor->setParentItem(window.contentItem());
    editor->setSize(QSizeF(2267, 1316));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    // This case verifies the preserved design catalog, independently of the
    // supported native controls exercised by editorToolbarOperations.
    auto *designState = editor->findChild<QObject *>("editorToolSheet");
    QVERIFY(designState && designState->setProperty("engine", QVariant::fromValue<QObject *>(nullptr)));
    auto *sidebar = item(editor.data(), "editorSidebar");
    auto *dock = item(editor.data(), "editorDesktopPanel");
    auto *panel = item(dock, "editorToolPanel");
    auto *viewport = item(dock, "editorDesktopPanelViewport");
    auto *toolbar = item(editor.data(), "editorToolbar");
    QVERIFY(sidebar && dock && panel && viewport && toolbar);
    QCOMPARE(bounds(sidebar, editor.data()), QRectF(0, 0, 181, 1316));
    QCOMPARE(bounds(dock, editor.data()), QRectF(1925, 0, 342, 1316));
    QCOMPARE(bounds(toolbar, editor.data()), QRectF(181, 0, 1744, 54));
    auto *nativeCanvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    QVERIFY(nativeCanvas && nativeCanvas->documentReady());
    QCOMPARE(nativeCanvas->canvasWidth(), 1024);
    QCOMPARE(nativeCanvas->canvasHeight(), 1024);
    QVERIFY(!item(editor.data(), "editorEmptyPreview"));
    QCOMPARE(panel->property("fieldCount").toInt(), 18);
    QCOMPARE(panel->width(), 308.0);
    QCOMPARE(bounds(visualItem(sidebar, "desktopAction_home"), sidebar), QRectF(16, 0, 149, 24));
    auto *filesRow = visualItem(sidebar, "desktopAction_files");
    auto *filesIcon = visualItem(filesRow, "desktopFilesIcon");
    QVERIFY(filesIcon && filesIcon->isVisible());
    auto *filesImage = visualItem(filesIcon, "iconButton_icon");
    QVERIFY(filesImage);
    QTRY_COMPARE(filesImage->property("status").toInt(), 1); // Image.Ready
    QCOMPARE(filesImage->property("source").toUrl().fileName(), QString("files.svg"));
    const auto filesBounds = bounds(filesImage, filesRow);
    QVERIFY(qAbs(filesBounds.top() - 4.1875) < 0.01);
    QVERIFY(qAbs(filesBounds.height() - 15.6659) < 0.01);
    QVERIFY(qAbs(filesBounds.center().x() - 13) < 0.01);
    QVERIFY(!item(panel, "editorToolClose")->isVisible());
    qreal previousBottom = 0;
    for (int index = 0; index < 18; ++index) {
        auto *control = visualItem(panel, qPrintable(QString("editorControl-field-%1").arg(index)));
        QVERIFY(control);
        QTRY_VERIFY(bounds(control, dock).top() >= previousBottom);
        const auto rect = bounds(control, dock);
        previousBottom = rect.bottom();
        QCOMPARE(control->width(), 308.0);
    }
    auto *dimension = visualItem(panel, "editorDimension-field-0-1");
    QCOMPARE(dimension->size(), QSizeF(206, 22));
    QCOMPARE(bounds(dimension, panel).right(), panel->width());
    auto *numeric = visualItem(panel, "editorNumeric-field-1");
    QCOMPARE(numeric->size(), QSizeF(206, 22));
    QCOMPARE(bounds(numeric, panel).right(), panel->width());
    auto *slider = visualItem(panel, "editorSlider-field-1");
    QCOMPARE(slider->size(), QSizeF(308, 22));
    QCOMPARE(bounds(slider, panel).right(), panel->width());
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        const auto capture = editor->grabToImage(QSize(2267, 1316));
        QVERIFY(capture);
        QTRY_VERIFY(!capture->image().isNull());
        QVERIFY(capture->image().save(captureDirectory + "/desktop-elements.png"));
    }
    auto *state = editor->findChild<QObject *>("editorToolSheet");
    QVERIFY(state);
    const auto values = [state]() { return state->property("values").value<QJSValue>().toVariant().toMap(); };
    click(&window, visualItem(panel, "editorChoice-selector-1"));
    QCOMPARE(values()["selector"].toString(), "Ellipse");
    click(&window, numeric);
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    for (const auto character : QStringLiteral("32 px")) QTest::keyClick(&window, character.toLatin1());
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(values()["field-1"].toDouble(), 32.0);
    click(&window, visualItem(panel, "editorToggle-field-3"));
    QCOMPARE(values()["field-3"].toBool(), false);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(dock->isVisible());
    QCOMPARE(values()["field-1"].toDouble(), 32.0);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 1)));
    QCOMPARE(state->property("toolId").toString(), "text");
    QVERIFY(!state->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QCOMPARE(values()["selector"].toString(), "Ellipse");
    click(&window, visualItem(panel, "editorToolReset"));
    QCOMPARE(values()["selector"].toString(), "Rectangle");
    QCOMPARE(values()["field-1"].toDouble(), 24.0);
    QCOMPARE(values()["field-3"].toBool(), true);
    window.resize(1200, 700);
    editor->setSize(QSizeF(1200, 700));
    QTRY_VERIFY(viewport->property("contentHeight").toReal() > viewport->height());
    const auto maximum = viewport->property("contentHeight").toReal() - viewport->height();
    QVERIFY(viewport->setProperty("contentY", maximum));
    auto *color = visualItem(panel, "editorColor-field-17");
    QTRY_VERIFY(bounds(color, viewport).bottom() <= viewport->height() + 1);
    click(&window, color);
    auto *picker = editor->findChild<QObject *>("editorColorSheet");
    QTRY_VERIFY(picker && picker->property("opened").toBool());
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY(!picker->property("visible").toBool());
    QVERIFY(dock->isVisible());
    QSignalSpy back(editor.data(), SIGNAL(backRequested()));
    click(&window, visualItem(sidebar, "desktopAction_home"));
    QCOMPARE(back.size(), 1);
}

void GuiTests::desktopEditorToolPanelsMatchFigma_data()
{
    QFile file(sourceUrl("Views/Editor/fixtures/FigmaPanels.json").toLocalFile());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto panels = QJsonDocument::fromJson(file.readAll()).array();
    QCOMPARE(panels.size(), 19);
    QTest::addColumn<QVariantMap>("specification");
    for (const auto &panel : panels) {
        const auto spec = panel.toObject().toVariantMap();
        QTest::newRow(qPrintable(spec["key"].toString())) << spec;
    }
}

void GuiTests::desktopEditorToolPanelsMatchFigma()
{
    QFETCH(QVariantMap, specification);
    const auto key = specification["key"].toString();
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    // Historical panel fixtures use a 432px reference; current window defaults are checked separately.
    QVERIFY(editor->setProperty("desktopPanelWidth", 432));
    QQuickWindow window;
    const QSize size(1200, specification["height"].toInt());
    window.resize(size);
    editor->setParentItem(window.contentItem());
    editor->setSize(size);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *state = editor->findChild<QObject *>("editorToolSheet");
    QVERIFY(toolbar && state);
    // Full Figma fixtures remain a design-only surface. Runtime tests below
    // assert that unsupported controls are absent from an attached editor.
    QVERIFY(state->setProperty("engine", QVariant::fromValue<QObject *>(nullptr)));
    const auto tools = listProperty(toolbar, "tools");
    int toolIndex = -1;
    for (int index = 0; index < tools.size(); ++index)
        if (tools[index].toMap()["key"].toString() == key) toolIndex = index;
    QVERIFY(toolIndex >= 0);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex)));
    QVERIFY(editor->setProperty("desktopPanelExpanded", true));
    auto *dock = item(editor.data(), "editorDesktopPanel");
    auto *panel = item(dock, "editorToolPanel");
    auto *viewport = item(dock, "editorDesktopPanelViewport");
    QVERIFY(dock && panel && viewport && dock->isVisible());
    const auto definition = state->property("definition").value<QJSValue>().toVariant().toMap();
    QCOMPARE(definition["figmaNode"].toString(), specification["figmaNode"].toString());
    const auto fields = specification["fields"].toList();
    const auto actionsSpec = specification["actions"].toMap();
    const auto actionLabels = actionsSpec["labels"].toStringList();
    QCOMPARE(panel->property("fieldCount").toInt(), fields.size() + actionLabels.size());
    QCOMPARE(panel->width(), 398.0);
    for (const auto &entry : fields) {
        const auto spec = entry.toMap();
        const auto id = spec["id"].toString();
        const auto type = spec["type"].toString();
        auto *control = visualItem(panel, qPrintable("editorControl-" + id));
        QVERIFY2(control, qPrintable(key + '/' + id));
        const auto controlRect = bounds(control, dock);
        QCOMPARE(controlRect.left(), 17.0);
        QCOMPARE(controlRect.width(), 398.0);
        QVERIFY(controlRect.height() > 0);
        QCOMPARE(control->property("field").value<QJSValue>().toVariant().toMap()["label"].toString(), spec["label"].toString());
        if (type == "Field" || type == "Slider" || type == "Color") {
            const auto name = (type == "Field" ? "editorInput-" : type == "Slider" ? "editorNumeric-" : "editorColorInput-") + id;
            auto *input = visualItem(control, qPrintable(name));
            QVERIFY(input);
            QCOMPARE(input->size(), QSizeF(206, 22));
            QCOMPARE(input->property("text").toString(), spec["textFields"].toStringList().first());
            if (type != "Color") QCOMPARE(bounds(input, control).right(), control->width());
        }
        if (type == "Slider") {
            auto *slider = visualItem(control, qPrintable("editorSlider-" + id));
            QVERIFY(slider);
            QCOMPARE(slider->size(), QSizeF(320, 22));
            QCOMPARE(bounds(slider, control).right(), control->width());
            QVERIFY(bounds(slider, control).top() >= 30);
        } else if (type == "Toggle") {
            QCOMPARE(bounds(visualItem(control, qPrintable("editorToggle-" + id)), control), QRectF(360, 0, 38, 22));
        } else if (type == "Visual") {
            auto *preview = visualItem(control, qPrintable("editorPreview-" + id));
            QVERIFY(preview);
            QCOMPARE(preview->size(), QSizeF(280, 44));
            auto *disclosure = visualItem(preview, "editorPreviewDisclosure");
            QVERIFY(disclosure);
            auto *icon = visualItem(disclosure, "iconButton_icon");
            QVERIFY(icon);
            QTRY_COMPARE(icon->property("status").toInt(), 1);
            QCOMPARE(icon->size(), QSizeF(18, 18));
            QCOMPARE(bounds(icon, preview), QRectF(250, 13, 18, 18));
            QCOMPARE(icon->property("source").toUrl().fileName(), QString("general-chevron-right.svg"));
            QFile asset(icon->property("source").toUrl().toLocalFile());
            QVERIFY(asset.exists() && asset.size() > 0);
            auto *value = visualItem(preview, "editorPreviewValue");
            QVERIFY(value);
            QCOMPARE(value->property("text").toString(), QString("View"));
            QCOMPARE(value->size(), QSizeF(72, 11));
            QCOMPARE(value->property("style").toInt(), 6);
        }
        if (type == "Choices" || type == "Segmented") {

            const auto options = spec["options"].toStringList();
            for (int index = 0; index < options.size(); ++index) {
                auto *button = visualItem(control, qPrintable(QString("editorChoice-%1-%2").arg(id).arg(index)));
                QVERIFY(button);
                const auto rect = bounds(button, control);
                QVERIFY(rect.left() >= 0 && rect.right() <= control->width() + 0.01);
                QVERIFY(rect.top() >= 25 && rect.bottom() <= control->height() + 0.01);
                QCOMPARE(button->height(), 22.0);
                QCOMPARE(button->property("text").toString(), options[index]);
            }
        }
    }
    if (!actionsSpec.isEmpty()) {
        const auto model = definition["fields"].toList();
        int index = 0;

        for (const auto &entry : model) {
            const auto field = entry.toMap();
            if (field["type"].toString() != "Action") continue;
            auto *button = visualItem(panel, qPrintable("editorAction-" + field["id"].toString()));
            QVERIFY(button);
            const auto rect = bounds(button, dock);
            QVERIFY(rect.left() >= 17 && rect.right() <= 415.01);
            QCOMPARE(rect.height(), 22.0);
            QCOMPARE(button->property("text").toString(), actionLabels[index++]);
        }
        QCOMPARE(index, actionLabels.size());
    }
    QTRY_VERIFY(panel->implicitHeight() > 0);
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        const auto capture = dock->grabToImage(QSize(432, size.height()));
        QVERIFY(capture);
        QTRY_VERIFY(!capture->image().isNull());
        QVERIFY(capture->image().save(captureDirectory + '/' + key + ".png"));
    }
    const auto values = [state]() { return state->property("values").value<QJSValue>().toVariant().toMap(); };
    const auto defaults = values();
    QSignalSpy actionSignals(editor.data(), SIGNAL(toolActionRequested(QString,QString,QVariant)));
    int optionStates = 0;
    const auto model = definition["fields"].toList();
    const auto dismissEditingSurface = [&]() {
        // Choices can open a real surface too (for example Image fill).
        // Close it before continuing the independent control-state sweep.
        for (const auto *name : {"editorDocumentOpenDialog", "editorDocumentPlaceDialog", "editorDocumentSaveDialog",
                                "editorDocumentLayersSheet", "editorPaintColorSheet", "editorColorSheet", "editorActionSheet",
                                "editorToolPreviewSheet", "editorSourceDialog", "editorExportDialog", "editorAdvancedGenerationSheet"}) {
            auto *surface = editor->findChild<QObject *>(QString::fromLatin1(name));
            if (surface && surface->property("visible").toBool()) {
                QVERIFY(QMetaObject::invokeMethod(surface, "close"));
                QTRY_VERIFY(!surface->property("visible").toBool());
            }
        }
        QTRY_VERIFY(!editor->property("modalActive").toBool());
    };
    auto exerciseChoices = [&](const QVariantMap &field) {
        const auto id = field["id"].toString();
        const auto options = field["options"].toStringList();
        for (int index = 0; index < options.size(); ++index) {
            auto *button = visualItem(panel, qPrintable(QString("editorChoice-%1-%2").arg(id).arg(index)));
            QVERIFY(button);
            if (!button->isEnabled()) continue;
            click(&window, button);
            QCOMPARE(values()[id].toString(), options[index]);
            QQmlExpression checked(qmlContext(button), button, "Accessible.checked");
            QVERIFY(checked.evaluate().toBool());
            ++optionStates;
            dismissEditingSurface();
        }
    };
    if (definition.contains("selector")) exerciseChoices(definition["selector"].toMap());
    for (const auto &entry : model) {
        const auto field = entry.toMap();
        const auto id = field["id"].toString();
        const auto type = field["type"].toString();
        if (type == "Choices" || type == "Segmented") exerciseChoices(field);
        else if (type == "Toggle") {
            auto *toggle = visualItem(panel, qPrintable("editorToggle-" + id));
            if (!toggle->isEnabled()) continue;
            click(&window, toggle);
            QCOMPARE(values()[id].toBool(), !defaults[id].toBool());
        } else if (type == "Action" || type == "Visual") {
            auto *button = visualItem(panel, qPrintable((type == "Action" ? "editorAction-" : "editorPreview-") + id));
            if (!button->isEnabled()) continue;
            click(&window, button);
            QVERIFY(!actionSignals.isEmpty());
            QCOMPARE(actionSignals.last()[0].toString(), key);
            QCOMPARE(actionSignals.last()[1].toString(), id);
            dismissEditingSurface();
        }
    }
    const auto draft = values();
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, (toolIndex + 1) % tools.size())));
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex)));
    QCOMPARE(values(), draft);
    click(&window, visualItem(panel, "editorToolReset"));
    QCOMPARE(values(), defaults);
    window.resize(1200, 480);
    editor->setSize(QSizeF(1200, 480));
    QTRY_VERIFY(viewport->property("contentHeight").toReal() > viewport->height());
    const auto maximum = viewport->property("contentHeight").toReal() - viewport->height();
    QVERIFY(viewport->setProperty("contentY", maximum));
    const auto last = fields.last().toMap();
    auto *lastControl = visualItem(panel, qPrintable("editorControl-" + last["id"].toString()));
    QVERIFY(bounds(lastControl, viewport).bottom() <= viewport->height() + 1);
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex)));
    QTRY_VERIFY(dock->isVisible());
    QCOMPARE(values(), defaults);
    qInfo() << key << "checked fields" << fields.size() + actionLabels.size() << "option states" << optionStates;
}

void GuiTests::editorToolSheets_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("sheets-402") << QSize(402, 874);
    QTest::newRow("sheets-320") << QSize(320, 640);
}

void GuiTests::editorToolNumericContracts()
{
    QFile file(sourceUrl("Views/Editor/EditorToolDefinitions.js").toLocalFile());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString script = QString::fromUtf8(file.readAll());
    script.remove(".pragma library");
    QJSEngine engine;
    const auto evaluated = engine.evaluate(script);
    QVERIFY2(!evaluated.isError(), qPrintable(evaluated.toString()));
    const auto validation = engine.evaluate(R"JS(
        var errors = [];
        tools.forEach(function(tool) {
            tool.fields.forEach(function(field) {
                if (field.type !== 'Slider') return;
                var decoded = parsed(field, formatted(field, field.initial));
                if (JSON.stringify(decoded) !== JSON.stringify(field.initial)) errors.push(tool.key + '/' + field.label);
                if (parsed(field, 'NaN') !== null || parsed(field, '12 garbage') !== null
                    || parsed(field, String(field.maximum + 1)) !== null) errors.push('invalid accepted: ' + field.label);
            });
        });
        var range = tool('masking').fields[16];
        if (parsed(range, '90 — 20%') !== null) errors.push('inverted depth range');
        if (JSON.stringify(parsed(range, '10 — 90%')) !== '[10,90]') errors.push('depth endpoints');
        if (tool('missing') !== null) errors.push('unknown tool');
        var exposure = tool('camera-photo').fields[1];
        if (parsed(exposure, formatted(exposure, 0.01, true)) !== 0.01) errors.push('desktop exposure precision lost');
        var nativeEffects = runtimeDefinition(tool('effects'));
        var distortion = nativeEffects.fields.filter(function(field) { return field.id === 'field-45' })[0];
        if (parsed(distortion, '-50%') !== -50) errors.push('manual distortion coefficient');
        if (tool('effects').fields[45].type !== 'Toggle') errors.push('runtime mutated design catalog');
        if (runtimeDefinition(tool('camera-photo')).selector.options.indexOf('RAW Capture') >= 0) errors.push('unimplemented RAW advertised');
        errors.join('\n');
    )JS");
    QVERIFY2(!validation.isError(), qPrintable(validation.toString()));
    QCOMPARE(validation.toString(), QString());
}

void GuiTests::editorToolSheets()
{
    QFETCH(QSize, size);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "ios"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"mobileLayout", true}}));
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *editor = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(editor);
    QQuickWindow window;
    window.setColor(QColor("#0B0B0B"));
    window.resize(size);
    editor->setParentItem(window.contentItem());
    editor->setSize(size);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *toolbar = item(editor, "editorToolbar");
    auto *sheet = editor->findChild<QObject *>("editorToolSheet");
    QVERIFY(toolbar && sheet);
    QSignalSpy back(editor, SIGNAL(backRequested()));
    QSignalSpy actions(editor, SIGNAL(toolActionRequested(QString,QString,QVariant)));
    const auto tools = listProperty(toolbar, "tools");
    QCOMPARE(tools.size(), 19);
    const QList<int> counts{18, 16, 16, 16, 13, 16, 16, 20, 31, 27, 17, 48, 48, 23, 16, 27, 20, 26, 14};
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    int total = 0;
    for (int index = 0; index < tools.size(); ++index) {
        const auto key = tools[index].toMap()["key"].toString();
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index)));
        QTRY_VERIFY(sheet->property("opened").toBool());
        QCOMPARE(sheet->property("toolId").toString(), key);
        auto *panel = item(sheet, "editorToolPanel");
        auto *viewport = item(sheet, "sheet_viewport");
        QVERIFY(panel && viewport);
        const auto catalog = sheet->property("definition").value<QJSValue>().toVariant().toMap()["fields"].toList();
        QCOMPARE(catalog.size(), counts[index]); total += catalog.size();
        const auto fields = panel->property("presentedFields").value<QJSValue>().toVariant().toList();
        QCOMPARE(panel->property("fieldCount").toInt(), fields.size());
        QVERIFY(!fields.isEmpty());
        auto *canvas = qobject_cast<EditorCanvas *>(item(editor, "editorBlankCanvas")); QVERIFY(canvas);
        const auto values = sheet->property("values").value<QJSValue>().toVariant().toMap();
        for (const auto &entry : catalog) {
            const auto field = entry.toMap();
            if (canvas->toolControlState(key, field["id"].toString(), values).value("supported").toBool()) continue;
            const auto name = (field["type"].toString() == "Action" ? "editorAction-" : "editorControl-") + field["id"].toString();
            QVERIFY2(!visualItem(panel, qPrintable(name)), qPrintable(key + ": unsupported control rendered: " + name));
        }
        for (const auto &entry : fields) {
            const auto field = entry.toMap();
            const auto name = (field["type"].toString() == "Action" ? "editorAction-" : "editorControl-") + field["id"].toString();
            auto *rendered = visualItem(panel, qPrintable(name));
            QVERIFY2(rendered, qPrintable(key + ": " + name));
            const auto rectangle = bounds(rendered, panel);
            QVERIFY2(rectangle.left() >= -1 && rectangle.right() <= panel->width() + 1,
                     qPrintable(key + ": " + name));
        }
        QVERIFY(sheet->property("y").toReal() >= 23);
        QVERIFY(qAbs(sheet->property("y").toReal() + sheet->property("height").toReal() - editor->height()) < 1);
        QCOMPARE(sheet->property("width").toReal(), editor->width());
        QCOMPARE(viewport->property("contentY").toReal(), 0.0);
        // Every rendered control stays inside the sheet, including long labels/options.
        const auto controls = panel->findChildren<QQuickItem *>();
        for (auto *control : controls) {
            if (!control->objectName().startsWith("editor") || !control->isVisible()) continue;
            const auto rect = bounds(control, panel);
            QVERIFY2(rect.left() >= -1 && rect.right() <= panel->width() + 1,
                     qPrintable(key + ": " + control->objectName()));
        }
        if (!captureDirectory.isEmpty() && (index == 0 || index == 11 || index == 12 || index == 18)) {
            QVERIFY(QDir().mkpath(captureDirectory));
            QVERIFY(window.grabWindow().save(captureDirectory + '/' + QTest::currentDataTag() + '-' + key + ".png"));
        }
        const qreal end = viewport->property("contentHeight").toReal() - viewport->height();
        if (end > 0) {
            // The real flickable must make the final control/action reachable.
            QVERIFY(QMetaObject::invokeMethod(viewport, "flick", Q_ARG(qreal, 0), Q_ARG(qreal, -1800)));
            QTRY_VERIFY(viewport->property("contentY").toReal() > 0);
            QVERIFY(QMetaObject::invokeMethod(viewport, "cancelFlick"));
            viewport->setProperty("contentY", end);
            QVERIFY(qAbs(viewport->property("contentY").toReal() + viewport->height()
                         - viewport->property("contentHeight").toReal()) < 1);
            auto last = fields.last().toMap();
            for (const auto &entry : fields)
                if (entry.toMap()["type"].toString() == "Action") last = entry.toMap();
            const auto name = (last["type"].toString() == "Action" ? "editorAction-" : "editorControl-") + last["id"].toString();
            const auto rectangle = bounds(visualItem(panel, qPrintable(name)), viewport);
            QVERIFY2(rectangle.top() >= -1 && rectangle.bottom() <= viewport->height() + 1,
                     qPrintable(key + ": final control is unreachable"));
        }
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY(!sheet->property("visible").toBool());
        QCOMPARE(back.size(), 0); // Escape dismisses the tool before exiting the editor.
    }
    QCOMPARE(total, 428);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(sheet->property("opened").toBool());
    auto *panel = item(sheet, "editorToolPanel");
    auto *viewport = item(sheet, "sheet_viewport");
    const auto reveal = [viewport, &window](QQuickItem *control) {
        const auto rectangle = bounds(control, viewport->property("contentItem").value<QQuickItem *>());
        viewport->setProperty("contentY", qBound(0.0, rectangle.top() - 20,
            qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height())));
        QTest::qWait(30);
        click(&window, control);
    };
    const auto values = [sheet]() {
        return sheet->property("values").value<QJSValue>().toVariant().toMap();
    };
    auto *dimension = visualItem(panel, "editorDimension-field-0-0");
    QVERIFY(dimension);
    reveal(dimension);
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    for (const char character : QByteArrayLiteral("1536 px")) QTest::keyClick(&window, character);
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(values()["field-0"].toList(), QVariantList({1536, 1080}));
    auto *choice = visualItem(panel, "editorChoice-field-2-1");
    QVERIFY(choice);
    reveal(choice);
    QCOMPARE(values()["field-2"].toString(), "Gradient");
    auto *toggle = visualItem(panel, "editorToggle-field-3");
    QVERIFY(toggle);
    reveal(toggle);
    QCOMPARE(values()["field-3"].toBool(), false);
    auto *slider = visualItem(panel, "editorSlider-field-1");
    QVERIFY(slider);
    reveal(slider);
    slider->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_End);
    QCOMPARE(values()["field-1"].toInt(), 256);
    auto *numeric = visualItem(panel, "editorNumeric-field-1");
    reveal(numeric);
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    for (const char character : QByteArrayLiteral("37 px")) QTest::keyClick(&window, character);
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(values()["field-1"].toInt(), 37);
    // Invalid numerical edits restore the last committed value.
    numeric->setProperty("text", "NaN");
    QVERIFY(QMetaObject::invokeMethod(numeric, "commit"));
    QCOMPARE(values()["field-1"].toInt(), 37);
    QCOMPARE(numeric->property("text").toString(), "37 px");
    auto *color = visualItem(panel, "editorColor-field-16");
    QVERIFY(color);
    reveal(color);
    auto *colorSheet = sheet->findChild<QObject *>("editorColorSheet");
    QTRY_VERIFY(colorSheet && colorSheet->property("opened").toBool());
    auto *picker = item(colorSheet, "editorColorPicker");
    QVERIFY(picker);
    QVERIFY(QMetaObject::invokeMethod(picker, "setColor", Q_ARG(QColor, QColor("#123456"))));
    QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
    QTRY_VERIFY(!colorSheet->property("visible").toBool());
    QCOMPARE(values()["field-16"].toString(), "#123456");
    reveal(color);
    QTRY_VERIFY(colorSheet->property("opened").toBool());
    QCOMPARE(picker->property("previousColor").value<QColor>(), QColor("#123456"));
    QVERIFY(QMetaObject::invokeMethod(picker, "setColor", Q_ARG(QColor, QColor("#ABCDEF"))));
    QVERIFY(QMetaObject::invokeMethod(picker, "cancel"));
    QTRY_VERIFY(!colorSheet->property("visible").toBool());
    QCOMPARE(values()["field-16"].toString(), "#123456");
    QVERIFY(QMetaObject::invokeMethod(sheet, "dismiss"));
    QTRY_VERIFY(!sheet->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 1)));
    QTRY_VERIFY(sheet->property("opened").toBool());
    auto *textInput = visualItem(item(sheet, "editorToolPanel"), "editorInput-field-0");
    QVERIFY(textInput);
    click(&window, textInput);
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    for (const char character : QByteArrayLiteral("My caption")) QTest::keyClick(&window, character);
    QCOMPARE(values()["field-0"].toString(), "My caption");
    QVERIFY(QMetaObject::invokeMethod(sheet, "dismiss"));
    QTRY_VERIFY(!sheet->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(sheet->property("opened").toBool());
    QCOMPARE(values()["field-1"].toInt(), 37);
    QCOMPARE(values()["field-2"].toString(), "Gradient");
    QCOMPARE(values()["field-16"].toString(), "#123456");
    click(&window, visualItem(item(sheet, "editorToolPanel"), "editorToolReset"));
    QCOMPARE(values()["field-1"].toInt(), 24);
    QCOMPARE(values()["field-2"].toString(), "Solid");
    QCOMPARE(values()["field-3"].toBool(), true);
    QCOMPARE(values()["field-16"].toString(), "#8B7CFF");
    const auto drafts = sheet->property("settingsByTool").value<QJSValue>().toVariant().toMap();
    QCOMPARE(drafts["text"].toMap()["field-0"].toString(), "My caption");
    // Dragging the native grabber dismisses the sheet without leaving the canvas.
    auto *grabber = item(sheet, "sheet_grabber");
    QVERIFY(grabber);
    const auto start = grabber->mapToScene(grabber->boundingRect().center()).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
    for (int offset : {20, 50, 90, 130, 160})
        QTest::mouseMove(&window, start + QPoint(0, offset), 30);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, start + QPoint(0, 160));
    QTRY_VERIFY(!sheet->property("visible").toBool());
    QCOMPARE(back.size(), 0);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 4)));
    QTRY_VERIFY(sheet->property("opened").toBool());
    panel = item(sheet, "editorToolPanel");
    viewport = item(sheet, "sheet_viewport");
    auto *save = visualItem(panel, "editorAction-field-12");
    QVERIFY(save);
    reveal(save);
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions.first()[0].toString(), "file");
    QCOMPARE(actions.first()[1].toString(), "field-12");
    auto *saveDialog = editor->findChild<QObject *>("editorDocumentSaveDialog");
    QTRY_VERIFY(saveDialog && saveDialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(saveDialog, "close"));
    QTRY_VERIFY(!saveDialog->property("visible").toBool());
    QVERIFY(sheet->property("opened").toBool());
    // An outside press closes only the tool sheet.
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    QTRY_VERIFY(!sheet->property("visible").toBool());
    QCOMPARE(back.size(), 0);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(sheet->property("opened").toBool());
    // Rotation and keyboard-reduced content keep the panel inside available bounds.
    window.resize(844, 360);
    editor->setSize(QSizeF(844, 360));
    QTRY_VERIFY(sheet->property("height").toReal() <= 336);
    QCOMPARE(sheet->property("width").toReal(), 844.0);
    editor->setProperty("mobileLayout", false);
    QTRY_VERIFY(!sheet->property("visible").toBool());
    QVERIFY(item(editor, "editorBlankCanvas")->isVisible());
    QCOMPARE(actions.size(), 1);
}

void GuiTests::resultCannotOpenProjectWithoutReadableImage()
{
    QQmlApplicationEngine engine;
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *project = item(window, "newProjectButton");
    QVERIFY(project);
    QVERIFY(!project->isEnabled());
    // Inject the unreadable image at the view boundary; accepted submissions only expose verified files.
    QVERIFY(item(window, "generationResult")->setProperty("result",
        QVariantMap{{"imageSource", sourceUrl("missing-result.png")}}));
    QVERIFY(window->setProperty("resultVisible", true));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE(item(window, "generatedImage")->property("status").toInt(), 3);
    QVERIFY(!project->isEnabled());
    QSignalSpy requests(window, SIGNAL(newProjectRequested(QUrl,QVariant)));
    QVERIFY(requests.isValid());
    click(window, project);
    QCOMPARE(requests.size(), 0);
    QVERIFY(item(window, "resultStatus")->isVisible());
}

void GuiTests::mainCreatesOneSharedWindow()
{
    QPointer<QQuickWindow> window;
    {
        QQmlApplicationEngine engine;
        engine.load(sourceUrl("Main.qml"));
        QCOMPARE(engine.rootObjects().size(), 1);
        window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QVERIFY2(window, "Main must directly create the shared application window.");
        QCOMPARE(window->property("primaryColor").value<QColor>(), QColor("#0A84FF"));
        auto *material = item(window, "applicationWindowMaterial");
        QVERIFY(material);
        QCOMPARE(material->property("primaryColor").value<QColor>(), QColor("#0A84FF"));
        QCOMPARE(material->property("color").value<QColor>(), QColor("#0B0B0B"));
        QCOMPARE(material->property("tintOpacity").toReal(), 0.5);
        QCOMPARE(material->property("intenseOpacity").toReal(), 0.0);
        QCOMPARE(material->property("faintOpacity").toReal(), 0.0);
        QCOMPARE(material->property("blurRadius").toReal(), 64.0);
        QCOMPARE(window->findChildren<QQuickWindow *>().size(), 0);
        QCOMPARE(QGuiApplication::topLevelWindows().size(), 1);
        auto *platform = engine.singletonInstance<QObject *>("LVRS", "Platform");
        QVERIFY(platform);
        QCOMPARE(window->property("isMobilePlatform").toBool(), platform->property("mobile").toBool());
        QCOMPARE(window->property("canonicalPlatform").toString(), platform->property("canonicalOs").toString());
        QVERIFY(window->isVisible());
        QVERIFY(!window->transientParent());
        QCOMPARE(window->title(), "Dreamscapes");
        QVERIFY(!item(window, "connectSocietyHost"));
        QVERIFY(!item(window, "societyHostLink"));
        QVERIFY(!item(window, "joinSocietyHost"));
        QVERIFY(item(window, "quickGenerate"));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *menu = window->findChild<QObject *>("globalMenuBar");
        QVERIFY(menu);
        QCOMPARE(menu->property("window").value<QObject *>(), window.data());
        auto *preferencesAction = window->findChild<QObject *>("globalPreferencesAction");
        QVERIFY(preferencesAction);
        auto *editMenu = window->findChild<QObject *>("globalEditMenu");
        QVERIFY(editMenu);
        QCOMPARE(preferencesAction->parent(), editMenu);
        QCOMPARE(preferencesAction->property("role").toInt(), 0); // NoRole keeps the action in Edit on macOS.
        QVERIFY(QMetaObject::invokeMethod(preferencesAction, "triggered"));
        auto *preferences = window->findChild<QQuickWindow *>("preferencesWindow");
        QVERIFY(preferences);
        QTRY_VERIFY(preferences->isVisible());
        QVERIFY(visualItem(preferences->contentItem(), "preferencesCategoryGeneral"));
        QVERIFY(visualItem(preferences->contentItem(), "preferencesDriveDetails"));
        QVERIFY(QTest::qWaitForWindowExposed(preferences));
        click(preferences, visualItem(preferences->contentItem(), "preferencesCategoryStorage"));
        QVERIFY(visualItem(preferences->contentItem(), "preferencesDriveDetails")->isVisible());
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/preferences-drive-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        auto *location = visualItem(preferences->contentItem(), "preferencesDriveLocation");
        auto *apply = visualItem(preferences->contentItem(), "applySocietyDrive");
        auto *current = visualItem(preferences->contentItem(), "preferencesCurrentDrive");
        QVERIFY(location && apply && current);
        location->setProperty("text", storage.path());
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QTRY_COMPARE(current->property("text").toString(), storage.path());
        QCOMPARE(iiSocietyContainer::SharedStorage::open()->drive().rootPath(), storage.path());
        location->setProperty("text", storage.filePath("missing"));
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QVERIFY(preferences->property("locationFailed").toBool());
        QCOMPARE(current->property("text").toString(), storage.path());
        location->setProperty("text", storage.path());
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QCOMPARE(preferences->transientParent(), window.data());
        const auto capture = qEnvironmentVariable("DREAMSCAPES_PREFERENCES_SCREENSHOT");
        if (!capture.isEmpty()) {
            QTest::qWait(150);
            QVERIFY(preferences->grabWindow().save(capture));
        }
        preferences->close();
        window->requestActivate();
        QTRY_VERIFY(window->isActive());
        QTest::keySequence(window, QKeySequence("Ctrl+,"));
        QTRY_VERIFY(preferences->isVisible());
        preferences->close();
        QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
        QCOMPARE(window->findChildren<QQuickWindow *>("preferencesWindow").size(), 1);
        QTRY_VERIFY(preferences->isVisible());
        for (const auto &key : {QKeySequence(Qt::Key_Escape), QKeySequence(QKeySequence::Close)}) {
            preferences->requestActivate();
            QTRY_VERIFY(preferences->isActive());
            QTest::keySequence(preferences, key);
            QTRY_VERIFY(!preferences->isVisible());
            QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
        }
        QSignalSpy lastWindowClosed(qGuiApp, &QGuiApplication::lastWindowClosed);
        QVERIFY(window->close());
        QTRY_COMPARE(lastWindowClosed.size(), 1);
    }
    QVERIFY(window.isNull());
    QCOMPARE(QGuiApplication::topLevelWindows().size(), 0);
}

void GuiTests::preferencesCategoriesAndDefaultModels()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/preferences-models-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QStringList imageNames {"first.safetensors", "second.safetensors"};
    for (int index = 0; index < 30; ++index)
        imageNames << QString("extra-%1.safetensors").arg(index, 2, 10, QChar('0'));
    for (const auto &name : imageNames) {
        QFile file(storage.filePath("Models/" + name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write("protocol fixture") > 0);
    }
    QVERIFY(QDir().mkpath(storage.filePath("Models/ltx")));
    QFile manifest(storage.filePath("Models/ltx/model_index.json"));
    QVERIFY(manifest.open(QIODevice::WriteOnly));
    QVERIFY(manifest.write(R"({"_class_name":"LTXPipeline"})") > 0);
    manifest.close();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
    auto *preferences = window->findChild<QQuickWindow *>("preferencesWindow");
    QVERIFY(preferences);
    QVERIFY(QTest::qWaitForWindowExposed(preferences));
    const QStringList categories {"Account", "General", "Appearance", "Storage",
        "Image", "Video", "Audio", "Agents", "Share", "Integration", "Publish",
        "About", "Accessibility", "KeyboardShortcut"};
    auto *sidebar = visualItem(preferences->contentItem(), "preferencesSidebar");
    QVERIFY(sidebar);
    QCOMPARE(sidebar->width(), 207.0);
    qreal previousBottom = 0.0;
    for (const auto &category : categories) {
        auto *row = visualItem(preferences->contentItem(), qPrintable("preferencesCategory" + category));
        QVERIFY2(row, qPrintable(category));
        const auto top = row->mapToItem(sidebar, QPointF {}).y();
        QVERIFY(top >= previousBottom);
        QCOMPARE(row->height(), category == "Account" ? 44.0 : 24.0);
        const auto firstInGroup = category == "General" || category == "Image"
            || category == "Share" || category == "About";
        QCOMPARE(top - previousBottom, firstInGroup ? 12.0 : category == "Account" ? 10.0 : 0.0);
        previousBottom = top + row->height();
    }
    auto *account = visualItem(preferences->contentItem(), "preferencesCategoryAccount");
    QVERIFY(account->mapToScene(QPointF {}).y() >= preferences->property("contentTopInset").toReal());
    QCOMPARE(account->property("label").toString(), QString("Display Name"));
    QCOMPARE(account->property("description").toString(), QString("@user_id"));
    auto *accountIcon = visualItem(account, "listItem_leadingIcon");
    QVERIFY(accountIcon);
    auto *accountImage = accountIcon->childItems().value(0);
    QVERIFY(accountImage && accountImage->property("source").isValid());
    QCOMPARE(accountIcon->width(), 18.0);
    QCOMPARE(accountIcon->height(), 18.0);
    QCOMPARE(account->property("iconName").toString(), QString("user"));
    QVERIFY(account->property("iconSource").toUrl().isEmpty());
    QTRY_VERIFY(accountImage->property("source").toUrl().toString().endsWith("/user.svg"));
    QTRY_COMPARE(accountImage->property("status").toInt(), 1);
    auto *general = visualItem(preferences->contentItem(), "preferencesCategoryGeneral");
    auto *menuIcon = visualItem(general, "menuItem_iconImage");
    QVERIFY(menuIcon);
    QCOMPARE(menuIcon->width(), 18.0);
    QCOMPARE(menuIcon->height(), 18.0);
    const QStringList iconNames {"settings", "stroke", "sqlFile", "imageToImage",
        "render-preview", "audioClassification", "reinforcementLearning", "cwmShare",
        "persistenceRelationship", "export", "statusinfo", "accessMethod", "keyboard"};
    for (int index = 1; index < categories.size(); ++index) {
        auto *row = visualItem(preferences->contentItem(), qPrintable("preferencesCategory" + categories[index]));
        auto *icon = visualItem(row, "menuItem_iconImage");
        QVERIFY(icon);
        QCOMPARE(row->property("iconName").toString(), iconNames[index - 1]);
        QVERIFY(row->property("iconSource").toUrl().isEmpty());
        QCOMPARE(icon->width(), 18.0);
        QCOMPARE(icon->height(), 18.0);
        QTRY_VERIFY(icon->property("source").toUrl().toString().endsWith('/' + iconNames[index - 1] + ".svg"));
        QTRY_COMPARE(icon->property("status").toInt(), 1); // Image.Ready: SVG decoded by LVRS.
    }
    QCOMPARE(preferences->property("currentCategory").toString(), QString("General"));
    for (const auto &category : categories) {
        click(preferences, visualItem(preferences->contentItem(), qPrintable("preferencesCategory" + category)));
        QCOMPARE(preferences->property("currentCategory").toString(), category);
        if (category != "Account" && category != "Storage" && category != "Image" && category != "Video")
            QVERIFY(visualItem(preferences->contentItem(), "preferencesCategoryDetails")->isVisible());
    }
    click(preferences, visualItem(preferences->contentItem(), "preferencesCategoryImage"));
    QCOMPARE(preferences->property("currentCategory").toString(), QString("Image"));
    auto *image = visualItem(preferences->contentItem(), "defaultImageGenerationModel");
    auto *video = visualItem(preferences->contentItem(), "defaultVideoGenerationModel");
    QVERIFY(image && video);
    QVERIFY(image->isVisible() && !video->isVisible());
    QCOMPARE(listProperty(image, "models").size(), 32);
    QCOMPARE(listProperty(video, "models").size(), 1);
    auto *controller = window->findChild<GenerationController *>("generationController");
    QVERIFY(controller);
    QTRY_COMPARE(image->width(), preferences->width() - sidebar->width() - 40);
    QTRY_VERIFY(bounds(image, preferences->contentItem()).left() >= sidebar->width() + 20);
    QSignalSpy selected(image, SIGNAL(modelSelected(QString)));
    click(preferences, image);
    auto *imageMenu = image->findChild<QObject *>("modelPreferenceMenu");
    QVERIFY(imageMenu);
    QTRY_VERIFY(imageMenu->property("opened").toBool());
    QTest::keyClick(preferences, Qt::Key_End);
    auto *options = imageMenu->findChild<QQuickItem *>("modelPreferenceOptions");
    QVERIFY(options);
    QTRY_VERIFY(options->property("contentY").toReal() > 0);
    QTest::keyClick(preferences, Qt::Key_Return);
    QTRY_COMPARE(selected.size(), 1);
    QTRY_VERIFY(!imageMenu->property("visible").toBool());
    QCOMPARE(controller->property("defaultImageModel").toString(), QString("second.safetensors"));
    QCOMPARE(controller->selectedModel(), QString("second.safetensors"));
    const auto imageCapture = qEnvironmentVariable("DREAMSCAPES_IMAGE_PREFERENCES_SCREENSHOT");
    if (!imageCapture.isEmpty()) {
        QTest::qWait(200);
        QVERIFY(preferences->grabWindow().save(imageCapture));
    }
    click(preferences, visualItem(preferences->contentItem(), "preferencesCategoryVideo"));
    QVERIFY(video->isVisible() && !image->isVisible());
    click(preferences, video);
    auto *videoMenu = video->findChild<QObject *>("modelPreferenceMenu");
    QVERIFY(videoMenu);
    QTRY_VERIFY(videoMenu->property("opened").toBool());
    QTest::keyClick(preferences, Qt::Key_End);
    QTest::keyClick(preferences, Qt::Key_Return);
    QTRY_COMPARE(controller->property("defaultVideoModel").toString(), QString("ltx"));
    QTRY_VERIFY(!videoMenu->property("visible").toBool());
    QCOMPARE(controller->selectedVideoModel(), QString("ltx"));
    const auto capture = qEnvironmentVariable("DREAMSCAPES_GENERATE_PREFERENCES_SCREENSHOT");
    if (!capture.isEmpty()) {
        QTest::qWait(200);
        QVERIFY(preferences->grabWindow().save(capture));
    }
    click(preferences, visualItem(preferences->contentItem(), "preferencesCategoryAccount"));
    QVERIFY(visualItem(preferences->contentItem(), "preferencesAccountDetails")->isVisible());
    QVERIFY(!image->isVisible());
    const auto sidebarCapture = qEnvironmentVariable("DREAMSCAPES_SIDEBAR_PREFERENCES_SCREENSHOT");
    if (!sidebarCapture.isEmpty()) {
        QTest::qWait(200);
        QVERIFY(preferences->grabWindow().save(sidebarCapture));
    }
    preferences->resize(560, 360);
    QTRY_COMPARE(preferences->size(), QSize(560, 360));
    auto *scroll = visualItem(preferences->contentItem(), "preferencesSidebar");
    QVERIFY(scroll);
    QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->height());
    auto *lastCategory = visualItem(preferences->contentItem(), "preferencesCategoryKeyboardShortcut");
    lastCategory->forceActiveFocus();
    QTRY_VERIFY(scroll->property("contentY").toReal() > 0);
    const auto lastTop = lastCategory->mapToItem(scroll, QPointF {}).y();
    QVERIFY(lastTop >= 0 && lastTop + lastCategory->height() <= scroll->height());
    QTest::keyClick(preferences, Qt::Key_Space);
    QTRY_COMPARE(preferences->property("currentCategory").toString(), QString("KeyboardShortcut"));
    account->forceActiveFocus();
    QTRY_VERIFY(scroll->property("contentY").toReal() <= 10);
    QTest::keyClick(preferences, Qt::Key_Return);
    QTRY_COMPARE(preferences->property("currentCategory").toString(), QString("Account"));
    preferences->close();
    QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
    QCOMPARE(window->findChildren<QQuickWindow *>("preferencesWindow").size(), 1);
    QCOMPARE(preferences->property("currentCategory").toString(), QString("Account"));
    QVERIFY(window->close());
}

void GuiTests::preferencesLvrsViewsAndFolderPicker()
{
    const QRegularExpression foreignView(QStringLiteral(
        R"(\b(?:Controls\.[A-Za-z]+|RowLayout|ColumnLayout|GridLayout|Rectangle|Flow|Flickable|ListView|FolderDialog|Item)\s*\{)"));
    for (const auto *name : {"PreferencesWindow.qml", "ModelPreferenceCombo.qml", "PreferenceList.qml", "SocietyFolderPicker.qml"}) {
        QFile file(sourceUrl("Views/Preferences/" + QString::fromLatin1(name)).toLocalFile());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto source = QString::fromUtf8(file.readAll());
        QVERIFY2(!source.contains("import QtQuick.Controls") && !source.contains("import QtQuick.Dialogs"), name);
        QVERIFY2(!foreignView.match(source).hasMatch(), name);
    }

    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/preferences-folder-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QVERIFY(QDir().mkpath(storage.filePath("Models/Children #1")));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
    auto *preferences = window->findChild<QQuickWindow *>("preferencesWindow");
    QVERIFY(preferences);
    QVERIFY(QTest::qWaitForWindowExposed(preferences));
    auto find = [preferences](const char *name) { return visualItem(preferences->contentItem(), name); };
    auto renderFolderRows = [preferences] {
        if (QGuiApplication::platformName() != "cocoa") return true;
        // Model updates precede the native window's layout/render pass.
        QSignalSpy rendered(preferences, &QQuickWindow::frameSwapped);
        preferences->update();
        return rendered.wait(5000);
    };
    click(preferences, find("preferencesCategoryStorage"));
    auto *browse = find("browseSocietyDrive");
    auto *location = find("preferencesDriveLocation");
    QVERIFY(browse && location);
    QTRY_COMPARE(find("preferencesDriveDetails")->width(), preferences->width() - find("preferencesSidebar")->width());
    QTRY_VERIFY(bounds(browse, preferences->contentItem()).left() >= find("preferencesSidebar")->width() + 20);
    QTRY_VERIFY(bounds(browse, preferences->contentItem()).bottom() <= preferences->height());
    const auto originalText = location->property("text").toString();
    QSignalSpy browseClicks(browse, SIGNAL(clicked()));
    click(preferences, browse);
    QTRY_COMPARE(browseClicks.size(), 1);
    auto *picker = find("preferencesFolderPicker");
    QVERIFY(picker);
    QTRY_VERIFY(picker->property("open").toBool());
    QTRY_COMPARE(picker->property("revealProgress").toReal(), 1.0);
    auto *model = picker->findChild<QObject *>("preferencesFolderModel");
    QVERIFY(model);
    QTRY_COMPARE(model->property("folder").toUrl().toLocalFile(), storage.path());
    QTRY_VERIFY(find("preferencesFolderList"));
    auto *folders = find("preferencesFolderList");
    QTRY_VERIFY(folders->isEnabled());
    QTRY_VERIFY(menuEntry(folders, "Models"));
    QVERIFY(renderFolderRows());
    auto *modelFolder = menuEntry(folders, "Models");
    modelFolder->forceActiveFocus();
    QTRY_VERIFY(bounds(modelFolder, folders).top() >= 0 && bounds(modelFolder, folders).bottom() <= folders->height());
    const auto folderCapture = qEnvironmentVariable("DREAMSCAPES_FOLDER_PREFERENCES_SCREENSHOT");
    if (!folderCapture.isEmpty()) QVERIFY(preferences->grabWindow().save(folderCapture));
    click(preferences, modelFolder);
    QTRY_COMPARE(model->property("folder").toUrl().toLocalFile(), storage.filePath("Models"));
    QTRY_VERIFY(folders->isEnabled());
    QTRY_VERIFY(menuEntry(folders, "Children #1"));
    QVERIFY(renderFolderRows());
    auto *childFolder = menuEntry(folders, "Children #1");
    childFolder->forceActiveFocus();
    QTRY_VERIFY(bounds(childFolder, folders).top() >= 0 && bounds(childFolder, folders).bottom() <= folders->height());
    QSignalSpy childClicks(childFolder, SIGNAL(clicked()));
    click(preferences, childFolder);
    QTRY_COMPARE(childClicks.size(), 1);
    QTRY_COMPARE(model->property("folder").toUrl().toLocalFile(), storage.filePath("Models/Children #1"));
    QTest::keyClick(preferences, Qt::Key_Escape);
    QTRY_VERIFY(!picker->property("open").toBool());
    QVERIFY(preferences->isVisible());
    QCOMPARE(location->property("text").toString(), originalText);
    QTRY_VERIFY(!picker->isVisible());

    click(preferences, browse);
    QTRY_VERIFY(picker->property("open").toBool());
    QTRY_COMPARE(picker->property("revealProgress").toReal(), 1.0);
    QTRY_COMPARE(model->property("folder").toUrl().toLocalFile(), storage.path());
    QTRY_VERIFY(menuEntry(find("preferencesFolderList"), "Models"));
    QTRY_VERIFY(find("preferencesFolderList")->isEnabled());
    QVERIFY(renderFolderRows());
    menuEntry(find("preferencesFolderList"), "Models")->forceActiveFocus();
    QTRY_VERIFY(bounds(menuEntry(find("preferencesFolderList"), "Models"), find("preferencesFolderList")).bottom() <= find("preferencesFolderList")->height());
    click(preferences, menuEntry(find("preferencesFolderList"), "Models"));
    QTRY_COMPARE(model->property("folder").toUrl().toLocalFile(), storage.filePath("Models"));
    click(preferences, find("preferencesFolderUp"));
    QTRY_COMPARE(model->property("folder").toUrl().toLocalFile(), storage.path());
    auto *choose = find("modalPrimaryButton");
    QVERIFY(choose);
    QTRY_VERIFY(choose->isEnabled());
    click(preferences, choose);
    QTRY_VERIFY(!picker->property("open").toBool());
    QCOMPARE(QUrl(location->property("text").toString()).toLocalFile(), storage.path());
    // Wait until the LVRS overlay finishes hiding before interacting with the form.
    QTRY_VERIFY(!picker->isVisible());
    click(preferences, find("applySocietyDrive"));
    QVERIFY(!preferences->property("locationFailed").toBool());
    QCOMPARE(iiSocietyContainer::SharedStorage::open()->drive().rootPath(), storage.path());
    QVERIFY(window->close());
}

void GuiTests::preferencesAccountAvatarFollowsSdk()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/preferences-avatar-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QHash<QByteArray, QByteArray> photos;
    for (const auto &entry : {std::pair('a', QColor(Qt::red)), std::pair('b', QColor(Qt::blue))}) {
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(entry.second);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "WEBP"));
        photos.insert("/media/avatars/" + QByteArray(64, entry.first) + ".webp", bytes);
    }
    int requests = 0;
    connect(&server, &QTcpServer::newConnection, &server, [&] {
        while (server.hasPendingConnections()) {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                auto request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n")) return;
                const auto path = request.split(' ').value(1);
                const auto body = photos.value(path);
                ++requests;
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/webp\r\nContent-Length: "
                    + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        }
    });
    iisacc::accounts::AccountManager manager(QUrl("http://127.0.0.1:" + QString::number(server.serverPort())));
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *controller = window->findChild<GenerationController *>("generationController");
    QVERIFY(controller);
    QVERIFY(qobject_cast<iisacc::accounts::Account *>(controller->account()));
    QVERIFY(QMetaObject::invokeMethod(window, "openPreferences"));
    auto *preferences = window->findChild<QQuickWindow *>("preferencesWindow");
    QVERIFY(preferences);
    QCOMPARE(preferences->property("account").value<QObject *>(), controller->account());
    QVERIFY(QTest::qWaitForWindowExposed(preferences));
    auto *row = visualItem(preferences->contentItem(), "preferencesCategoryAccount");
    QVERIFY(row);
    auto *slot = visualItem(row, "listItem_leadingIcon");
    QVERIFY(slot);
    auto *icon = slot->childItems().value(0);
    QVERIFY(icon && icon->property("source").isValid());
    // Supply the same SDK Account type through a loopback fixture; no real login or account mutation.
    QVERIFY(preferences->setProperty("account", QVariant::fromValue<QObject *>(manager.account())));
    for (const auto hash : {'a', 'b'}) {
        QVERIFY(manager.readAccount({{"sub", "avatar_test"}, {"email", "avatar@example.com"},
            {"avatarUrl", "/media/avatars/" + QString(64, QChar(hash)) + ".webp"}}));
        QTRY_COMPARE(row->property("iconSource").toUrl(), manager.account()->avatarUrl());
        QTRY_COMPARE(icon->property("source").toUrl(), manager.account()->avatarUrl());
        QTRY_COMPARE(icon->property("status").toInt(), 1);
        QCOMPARE(icon->width(), 18.0);
        QCOMPARE(icon->height(), 18.0);
    }
    QCOMPARE(requests, 2);
    const auto capture = qEnvironmentVariable("DREAMSCAPES_AVATAR_PREFERENCES_SCREENSHOT");
    if (!capture.isEmpty()) QVERIFY(preferences->grabWindow().save(capture));
    manager.clear();
    QTRY_VERIFY(row->property("iconSource").toUrl().isEmpty());
    QTRY_VERIFY(icon->property("source").toUrl().toString().endsWith("/user.svg"));
    QTRY_COMPARE(icon->property("status").toInt(), 1);
    QCOMPARE(preferences->property("account").value<QObject *>(), manager.account());
    QVERIFY(window->close());
}

void GuiTests::sharedContentSurvivesLayoutChanges()
{
    QQmlApplicationEngine engine;
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    QVERIFY(window);
    QPointer<QQuickItem> panel = item(window, "quickGenerate");
    QVERIFY(panel);
    QVERIFY(panel->setProperty("prompt", "a quiet forest"));
    QVERIFY(panel->setProperty("aspectRatio", "16:9"));
    QVERIFY(panel->setProperty("generationCount", 1000));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QSignalSpy widthClassChanges(window, SIGNAL(widthClassChanged()));
    QVERIFY(widthClassChanges.isValid());
    QSignalSpy requests(window, SIGNAL(generateRequested(QString,QString,QString,int)));
    QVERIFY(requests.isValid());
    for (const auto size : {QSize(960, 640), QSize(320, 844), QSize(800, 600),
                            QSize(1440, 900), QSize(390, 844)}) {
        window->resize(size);
        QTRY_COMPARE(window->size(), size);
        const auto available = size.width() - window->property("mobileSystemSafeLeftInset").toReal()
                                            - window->property("mobileSystemSafeRightInset").toReal();
        const auto inset = available < 700 ? 12.0 : 24.0;
        QTRY_COMPARE(panel->width(), available - (available < 700 ? 48.0 : 181.0) - inset * 2);
        QCOMPARE(engine.rootObjects().constFirst(), window);
        QCOMPARE(item(window, "quickGenerate"), panel.data());
        QCOMPARE(panel->property("prompt").toString(), "a quiet forest");
        QCOMPARE(panel->property("aspectRatio").toString(), "16:9");
        QCOMPARE(panel->property("generationCount").toInt(), 1000);
    }
    QVERIFY(widthClassChanges.size() >= 3);
    auto *generate = item(panel, "generateButton");
    QVERIFY(generate);
    click(window, generate);
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests.constFirst(), QVariantList({"a quiet forest", "Image", "16:9", 1000}));
}

void GuiTests::homeCanvasMaxWidthAndAutoHeight_data()
{
    QTest::addColumn<QString>("ratio");
    QTest::addColumn<int>("availableWidth");
    QTest::addColumn<QSize>("outputSize");
    const QList<QPair<QString, QSize>> ratios{
        {"1:1", {1024, 1024}}, {"4:3", {1368, 1024}}, {"3:4", {1024, 1368}},
        {"16:9", {1824, 1024}}, {"9:16", {1024, 1824}}};
    for (const auto &[ratio, outputSize] : ratios)
        for (const int width : {248, 432, 1103, 1104, 1105, 1121, 1600})
            QTest::newRow(qPrintable(ratio + '-' + QString::number(width))) << ratio << width << outputSize;
}

void GuiTests::homeCanvasMaxWidthAndAutoHeight()
{
    QFETCH(QString, ratio);
    QFETCH(int, availableWidth);
    QFETCH(QSize, outputSize);
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Home/HomePaintCanvas.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *view = qobject_cast<QQuickItem *>(root.get());
    auto *canvas = root->findChild<HomeCanvas *>("homeCanvasPixels");
    QVERIFY(view && canvas);
    QQuickWindow window;
    window.resize(availableWidth, 600);
    view->setParentItem(window.contentItem());
    view->setWidth(availableWidth);
    QVERIFY(view->setProperty("aspectRatio", ratio));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_COMPARE(canvas->canvasWidth(), outputSize.width());
    QTRY_COMPARE(canvas->canvasHeight(), outputSize.height());
    auto *stage = canvas->parentItem();
    QVERIFY(stage);
    QTRY_COMPARE(canvas->width(), qMin(1080.0, availableWidth - 24.0));
    QCOMPARE(canvas->width(), stage->width());
    auto *viewport = item(root.get(), "homeCanvasViewport");
    QVERIFY(viewport);
    QTRY_COMPARE(viewport->width(), availableWidth - 24.0); // Existing panel padding only.
    QTRY_COMPARE(viewport->property("contentWidth").toReal(), viewport->width());
    const qreal expectedLeft = (viewport->width() - canvas->width()) / 2;
    QTRY_VERIFY(qAbs(stage->x() - expectedLeft) < 0.01);
    QVERIFY(bounds(canvas, viewport).left() >= 0);
    QVERIFY(bounds(canvas, viewport).right() <= viewport->width());
    QVERIFY(!item(root.get(), "homeCanvasScrollBar")->isVisible());
    const qreal expectedHeight = canvas->width() * outputSize.height() / outputSize.width();
    QTRY_VERIFY(qAbs(canvas->height() - expectedHeight) < 0.01);
    QTRY_VERIFY(qAbs(stage->height() - expectedHeight) <= 1.0); // Layouts round to a display pixel.
    QVERIFY(view->implicitHeight() > stage->height());
    QVERIFY(!canvas->hasContent());
    // Resizing the presentation leaves the document and its painted pixels intact.
    QTemporaryDir files(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/canvas-resize-XXXXXX");
    QVERIFY(files.isValid());
    QImage image(20, 10, QImage::Format_ARGB32); image.fill(Qt::red);
    const auto source = QUrl::fromLocalFile(files.filePath("reference.png"));
    QVERIFY(image.save(source.toLocalFile()));
    QVERIFY(canvas->addAttachment(source));
    QVERIFY(canvas->pasteAttachment(source, canvas->width()/2, canvas->height()/2));
    QVERIFY(canvas->hasContent());
    const auto pixels = canvas->selectedRasterPixels()->pixels;
    view->setWidth(availableWidth + 100);
    QTRY_COMPARE(viewport->width(), availableWidth + 76.0);
    QTRY_COMPARE(canvas->width(), qMin(1080.0, availableWidth + 76.0));
    QTRY_VERIFY(qAbs(canvas->height() - canvas->width() * outputSize.height() / outputSize.width()) < 0.01);
    QCOMPARE(canvas->canvasWidth(), outputSize.width());
    QCOMPARE(canvas->canvasHeight(), outputSize.height());
    QVERIFY(canvas->selectedRasterPixels()->pixels == pixels);
    QVERIFY(canvas->canUndo());
    view->setParentItem(nullptr);
}

void GuiTests::homeCanvasScalesToFramePreservingPainting()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Home/HomePaintCanvas.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *view = qobject_cast<QQuickItem *>(root.get());
    auto *canvas = root->findChild<HomeCanvas *>("homeCanvasPixels");
    auto *viewport = item(root.get(), "homeCanvasViewport");
    auto *scrollbar = item(root.get(), "homeCanvasScrollBar");
    QVERIFY(view && canvas && viewport && scrollbar);
    QQuickWindow window;
    window.resize(432, 760);
    view->setParentItem(window.contentItem());
    view->setWidth(window.width());
    QVERIFY(view->setProperty("aspectRatio", "16:9"));
    view->setHeight(view->implicitHeight());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_COMPARE(viewport->width(), 408.0);
    QTRY_COMPARE(canvas->width(), 408.0);
    QTRY_VERIFY(!scrollbar->isVisible());
    const auto zoom = canvas->zoom();
    const auto point = viewport->mapToScene(QPointF(150, 150));
    QWheelEvent wheel(point, window.mapToGlobal(point.toPoint()), QPoint(), QPoint(-120, 0),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&window, &wheel);
    QCOMPARE(viewport->property("contentX").toReal(), 0.0);
    QCOMPARE(canvas->zoom(), zoom);
    QTRY_VERIFY(qAbs(bounds(canvas, viewport).right() - viewport->width()) < 0.01);
    const auto strokeStart = viewport->mapToScene(QPointF(150, 150)).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, strokeStart);
    QTest::mouseMove(&window, strokeStart + QPoint(40, 20), 30);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, strokeStart + QPoint(40, 20));
    QTRY_VERIFY(canvas->hasContent());
    QCOMPARE(viewport->property("contentX").toReal(), 0.0);
    QVERIFY(canvas->canUndo());
    const auto pixels = canvas->selectedRasterPixels()->pixels;
    QVERIFY(canvas->undo());
    QVERIFY(!canvas->hasContent());
    QVERIFY(canvas->redo());
    QVERIFY(canvas->selectedRasterPixels()->pixels == pixels);
    view->setWidth(1600);
    view->setHeight(view->implicitHeight());
    QTRY_COMPARE(canvas->width(), 1080.0);
    QTRY_COMPARE(viewport->property("contentX").toReal(), 0.0);
    QTRY_VERIFY(!scrollbar->isVisible());
    QVERIFY(canvas->selectedRasterPixels()->pixels == pixels);
    view->setWidth(432);
    QTRY_COMPARE(canvas->width(), 408.0);
    QTRY_VERIFY(qAbs(canvas->height() - 408.0 * 1024 / 1824) < 0.01);
    QTRY_COMPARE(viewport->property("contentX").toReal(), 0.0);
    QTRY_VERIFY(!scrollbar->isVisible());
    QVERIFY(canvas->selectedRasterPixels()->pixels == pixels);
    QVERIFY(canvas->canUndo());
    view->setParentItem(nullptr);
}

void GuiTests::homeCanvasSliderRanges()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Home/HomePaintCanvas.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *view = qobject_cast<QQuickItem *>(root.get()); QVERIFY(view);
    QQuickWindow window;
    window.resize(1121, 1300);
    view->setParentItem(window.contentItem()); view->setWidth(window.width());
    view->setHeight(view->implicitHeight());
    auto *canvas = root->findChild<HomeCanvas *>("homeCanvasPixels");
    auto *size = item(root.get(), "homeBrushSizeSlider");
    auto *opacity = item(root.get(), "homeOpacitySlider");
    QVERIFY(canvas && size && opacity);
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(size->isVisible() && opacity->isVisible());
    QCOMPARE(size->property("from").toReal(), 1.0);
    QCOMPARE(size->property("to").toReal(), 500.0);
    QCOMPARE(size->property("stepSize").toReal(), 1.0);
    QCOMPARE(opacity->property("from").toReal(), 0.0);
    QCOMPARE(opacity->property("to").toReal(), 100.0);
    QCOMPARE(opacity->property("stepSize").toReal(), 1.0);
    QCOMPARE(size->width(), 120.0);
    QCOMPARE(opacity->width(), 120.0);
    auto *wheel = item(root.get(), "homeColorButton");
    QVERIFY(wheel);
    QVERIFY(bounds(size, view).right() < bounds(opacity, view).left());
    QVERIFY(bounds(opacity, view).right() < bounds(wheel, view).left());
    QVERIFY(item(root.get(), "homeBrushSizeIcon"));
    QVERIFY(item(root.get(), "homeOpacityIcon"));
    for (int value : {1, 500}) {
        QVERIFY(size->setProperty("value", value));
        QVERIFY(QMetaObject::invokeMethod(size, "moved"));
        QCOMPARE(canvas->brushSize(), qreal(value));
    }
    for (int value : {0, 37, 100}) {
        QVERIFY(opacity->setProperty("value", value));
        QVERIFY(QMetaObject::invokeMethod(opacity, "moved"));
        QCOMPARE(canvas->brushOpacity(), value / 100.0);
    }
    click(&window, size);
    QVERIFY(canvas->brushSize() > 1 && canvas->brushSize() < 500);
    QCOMPARE(canvas->brushSize(), size->property("value").toReal());
    QVERIFY(opacity->setProperty("value", 0));
    QVERIFY(QMetaObject::invokeMethod(opacity, "moved"));
    const auto center = canvas->mapToScene(canvas->boundingRect().center()).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, center);
    QTest::mouseMove(&window, center + QPoint(20,0), 30);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, center + QPoint(20,0));
    QVERIFY(!canvas->hasContent());
    // Both controls remain visible and inside the narrow canvas panel.
    for (int width : {248, 400, 432}) {
        view->setWidth(width); view->setHeight(view->implicitHeight());
        QTRY_VERIFY(bounds(size, view).right() <= view->width());
        QTRY_VERIFY(bounds(opacity, view).right() <= view->width());
        QVERIFY(size->isVisible() && opacity->isVisible());
    }
    view->setParentItem(nullptr);
}

void GuiTests::homeCanvasDragPaintAndColorPicker()
{
    QTemporaryDir files(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/home-drag-XXXXXX");
    QImage image(80, 40, QImage::Format_ARGB32); image.fill(QColor("#496c68"));
    const auto source = QUrl::fromLocalFile(files.filePath("reference.png"));
    QVERIFY(image.save(source.toLocalFile()));
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Home/HomePaintCanvas.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *view = qobject_cast<QQuickItem *>(root.get()); QVERIFY(view);
    QQuickWindow window;
    window.setColor(QColor("#171717")); window.resize(1280, 850);
    view->setParentItem(window.contentItem()); view->setWidth(window.width());
    view->setProperty("aspectRatio", "16:9");
    view->setHeight(view->implicitHeight());
    auto *canvas = root->findChild<HomeCanvas *>("homeCanvasPixels"); QVERIFY(canvas);
    QVERIFY(canvas->addAttachment(source));
    view->setHeight(view->implicitHeight());
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(100);
    auto *card = visualItem(view, "homeAttachmentDrag_0"); QVERIFY(card);
    if (!qEnvironmentVariable("DREAMSCAPES_HOME_CANVAS_CAPTURE").isEmpty())
        QVERIFY(window.grabWindow().save(qEnvironmentVariable("DREAMSCAPES_HOME_CANVAS_CAPTURE") + ".before.png"));
    const auto from = card->mapToScene(QPointF(40,30)).toPoint();
    const auto to = canvas->mapToScene(canvas->boundingRect().center()).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(&window, from + QPoint(12,-12), 30);
    QTest::mouseMove(&window, from + QPoint(24,-24), 30);
    QTest::mouseMove(&window, to, 50);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, to);
    QTRY_VERIFY(canvas->hasContent());
    QCOMPARE(canvas->selectedRasterPixels()->pixels[512*1824+912], quint32(0xff496c68));
    click(&window, item(root.get(), "homeUndoButton")); QTRY_VERIFY(!canvas->hasContent());
    click(&window, item(root.get(), "homeRedoButton")); QTRY_VERIFY(canvas->hasContent());
    click(&window, item(root.get(), "homeClearButton")); QTRY_VERIFY(!canvas->hasContent());
    click(&window, item(root.get(), "homeColorButton"));
    auto *picker = item(root.get(), "homeColorPicker"); QVERIFY(picker);
    QTRY_VERIFY(picker->isVisible());
    engine.rootContext()->setContextProperty("pickerUnderTest", picker);
    QQmlExpression color(engine.rootContext(), picker, "pickerUnderTest.setHex('#ff0000'); pickerUnderTest.accept()");
    color.evaluate(); QVERIFY2(!color.hasError(), qPrintable(color.error().toString()));
    QCOMPARE(canvas->brushColor(), QColor(Qt::red));
    QTRY_VERIFY(!picker->isVisible()); // Wait for the LVRS popover exit before pointer input.
    click(&window, item(root.get(), "homeBrushButton"));
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, to);
    QTest::mouseMove(&window, to + QPoint(24,0), 50);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, to + QPoint(24,0));
    QTRY_VERIFY(canvas->hasContent());
    QCOMPARE(canvas->selectedRasterPixels()->pixels[512*1824+912], quint32(0xffff0000));
    click(&window, item(root.get(), "homeEraserButton"));
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, to);
    QTest::mouseMove(&window, to + QPoint(24,0), 50);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, to + QPoint(24,0));
    QCOMPARE(canvas->selectedRasterPixels()->pixels[512*1824+912] >> 24, quint32(0));
    QVERIFY(canvas->undo());
    const auto capture = qEnvironmentVariable("DREAMSCAPES_HOME_CANVAS_CAPTURE");
    if (!capture.isEmpty()) {
        // The design's illustrative attachment is a fixture, never production content.
        const auto sample = QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/home-canvas-verification/landscape.svg";
        if (QFileInfo::exists(sample)) {
            QVERIFY(canvas->clearSelectedLayer());
            QVERIFY(canvas->addAttachment(QUrl::fromLocalFile(sample)));
            QVERIFY(canvas->pasteAttachment(QUrl::fromLocalFile(sample), canvas->width()/2, canvas->height()/2));
        }
            QVERIFY(window.grabWindow().save(capture));
    }
    view->setParentItem(nullptr);
}

void GuiTests::promptFieldsWrapAndGrow_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QByteArray>("fieldName");
    QTest::addColumn<int>("width");
    QTest::addColumn<QString>("text");
    for (const auto &field : {QByteArray("promptField"), QByteArray("advancedPromptField"),
                              QByteArray("advancedNegativePromptField")}) {
        for (const int width : {248, 402, 1280}) {
            const QStringList samples{QString("A richly detailed landscape with soft evening light. ").repeated(30),
                                      QString::fromUtf8("노을이 비치는 바닷가와 부드러운 빛의 풍경을 표현한다. ").repeated(30),
                                      QString(1600, 'W')};
            for (int language = 0; language < samples.size(); ++language) {
                const auto tag = field + '-' + QByteArray::number(width) + '-' + QByteArray::number(language);
                QTest::newRow(tag.constData()) << (field == "promptField"
                    ? QString("Views/Home/QuickGenerate.qml") : QString("Views/Home/AdvancedGenerate.qml"))
                    << field << width << samples[language];
            }
        }
    }
}

void GuiTests::promptFieldsWrapAndGrow()
{
    QFETCH(QString, file);
    QFETCH(QByteArray, fieldName);
    QFETCH(int, width);
    QFETCH(QString, text);
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl(file));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *view = qobject_cast<QQuickItem *>(root.get());
    QQuickWindow window;
    window.resize(width, 844);
    view->setParentItem(window.contentItem());
    view->setSize(QSizeF(width, 844));
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *field = item(root.get(), fieldName.constData());
    QVERIFY(field);
    auto *input = field->property("inputItem").value<QQuickItem *>();
    QVERIFY(input);
    const qreal minimum = field->height();
    QCOMPARE(minimum, 22.0);
    QVERIFY(field->setProperty("text", text));
    QTRY_VERIFY2(field->height() > minimum * 2, "Long prompts must wrap and expand vertically.");
    QTRY_VERIFY(input->height() >= input->property("contentHeight").toReal());
    QVERIFY(input->property("contentWidth").toReal() <= input->width() + 1);
    QCOMPARE(field->property("text").toString(), text);
    auto *following = item(root.get(), fieldName == "promptField" ? "quickGenerateActions" : "advancedModel");
    QVERIFY(following);
    QTRY_VERIFY(bounds(following, view).top() >= bounds(field, view).bottom());
    const qreal narrowHeight = field->height();
    view->setWidth(width * 2);
    QTRY_VERIFY(field->height() < narrowHeight);
    view->setWidth(width);
    QTRY_COMPARE(field->height(), narrowHeight);
    QCOMPARE(field->property("text").toString(), text);
    input->forceActiveFocus();
    QVERIFY(input->setProperty("cursorPosition", text.size()));
    if (auto *viewport = item(root.get(), "advancedGenerationViewport")) {
        const auto caret = [&] { return input->mapRectToItem(viewport,
            input->property("cursorRectangle").toRectF()); };
        QTRY_VERIFY(caret().top() >= 0 && caret().bottom() <= viewport->height());
        QVERIFY(input->setProperty("cursorPosition", 0));
        QTRY_VERIFY(caret().top() >= 0 && caret().bottom() <= viewport->height());
    }
    QVERIFY(field->setProperty("text", "short prompt"));
    QTRY_COMPARE(field->height(), minimum);
    QVERIFY(field->setProperty("text", ""));
    QTRY_COMPARE(field->height(), minimum);

    const QString composition = QString::fromUtf8("한국어 입력기 조합도 줄바꿈되어야 한다. ").repeated(20);
    QInputMethodEvent preedit(composition, {});
    QCoreApplication::sendEvent(input, &preedit);
    QTRY_COMPARE(input->property("preeditText").toString(), composition);
    QTRY_VERIFY(field->height() > minimum);
    QCOMPARE(field->property("text").toString(), QString());
    QInputMethodEvent commit;
    commit.setCommitString(composition);
    QCoreApplication::sendEvent(input, &commit);
    QTRY_COMPARE(field->property("text").toString(), composition);
    QVERIFY(QMetaObject::invokeMethod(input, "selectAll"));
    QVERIFY(QMetaObject::invokeMethod(input, "copy"));
    QCOMPARE(QGuiApplication::clipboard()->text(), composition);
    QGuiApplication::clipboard()->setText(text);
    QVERIFY(QMetaObject::invokeMethod(input, "paste"));
    QCOMPARE(field->property("text").toString(), text);
    QVERIFY(QMetaObject::invokeMethod(input, "undo"));
    QCOMPARE(field->property("text").toString(), composition);
    QVERIFY(QMetaObject::invokeMethod(input, "redo"));
    QCOMPARE(field->property("text").toString(), text);
    if (fieldName == "promptField") {
        QSignalSpy submitted(root.get(), SIGNAL(generateRequested(QString,QString,QString,int)));
        QCOMPARE(submitted.count(), 0);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(submitted.count(), 1);
        QCOMPARE(submitted.first().first().toString(), text.trimmed());
    } else {
        QCOMPARE(root->property(fieldName == "advancedPromptField" ? "prompt" : "negativePrompt").toString(), text);
    }
}

void GuiTests::promptCursorStaysVisible_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<bool>("resultVisible");
    QTest::newRow("desktop-home") << QString("macos") << false;
    QTest::newRow("mobile-home") << QString("ios") << false;
    QTest::newRow("desktop-result") << QString("macos") << true;
    QTest::newRow("mobile-result") << QString("ios") << true;
}

void GuiTests::promptCursorStaysVisible()
{
    QFETCH(QString, target);
    QFETCH(bool, resultVisible);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", target));
    QQmlComponent component(&engine, sourceUrl("Main.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window);
    window->resize(target == "ios" ? 402 : 960, 640);
    QVERIFY(window->setProperty("resultVisible", resultVisible));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *quick = item(window, "quickGenerate");
    auto *field = item(quick, "promptField");
    auto *input = field->property("inputItem").value<QQuickItem *>();
    QVERIFY(input);
    input->forceActiveFocus();
    const QString draft = QString::fromUtf8("긴 프롬프트의 마지막 입력 줄까지 확인할 수 있어야 한다. ").repeated(180);
    QVERIFY(field->setProperty("text", draft));
    QVERIFY(input->setProperty("cursorPosition", draft.size()));
    QTRY_VERIFY(field->height() > window->height());
    auto *viewport = item(window, resultVisible ? "resultComposerViewport"
        : target == "ios" ? "mobileHomeViewport" : "desktopHomeViewport");
    QVERIFY(viewport);
    const auto caretBounds = [&] {
        return input->mapRectToItem(viewport, input->property("cursorRectangle").toRectF());
    };
    QTRY_VERIFY(caretBounds().top() >= 0 && caretBounds().bottom() <= viewport->height());
    QVERIFY(input->setProperty("cursorPosition", 0));
    QTRY_VERIFY(caretBounds().top() >= 0 && caretBounds().bottom() <= viewport->height());
    auto *generate = item(quick, "generateButton");
    QVERIFY(generate);
    const qreal maximum = qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height());
    QVERIFY(viewport->setProperty("contentY", qBound(0.0,
        viewport->property("contentY").toReal() + bounds(generate, viewport).bottom()
            - viewport->height() + 8, maximum)));
    QTRY_VERIFY(bounds(generate, viewport).top() >= 0
        && bounds(generate, viewport).bottom() <= viewport->height());
    QCOMPARE(quick->property("prompt").toString(), draft);
    if (resultVisible) {
        QVERIFY(item(window, "generationResult")->height() >= 22);
        QVERIFY(window->setProperty("resultVisible", false));
        QCOMPARE(quick->property("prompt").toString(), draft);
    }
}

void GuiTests::sharedPanelLayout_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::newRow("desktop-960") << QString("macos") << 960 << 640;
    QTest::newRow("desktop-1440") << QString("macos") << 1440 << 900;
    QTest::newRow("figma-exact") << QString("macos") << 1172 << 640;
    QTest::newRow("desktop-compact") << QString("macos") << 320 << 480;
    QTest::newRow("figma-402") << QString("macos") << 402 << 844;
    QTest::newRow("ios-320") << QString("ios") << 320 << 844;
    QTest::newRow("ios-390") << QString("ios") << 390 << 844;
    QTest::newRow("android-360") << QString("android") << 360 << 844;
    QTest::newRow("mobile-landscape") << QString("ios") << 844 << 480;
}

void GuiTests::sharedPanelLayout()
{
    QFETCH(QString, target);
    QFETCH(int, width);
    QFETCH(int, height);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme);
    QVERIFY(theme->setProperty("targetOverride", target));
    QQmlComponent component(&engine, sourceUrl("Main.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window);
    window->resize(width, height);
    auto *panel = item(root.get(), "quickGenerate");
    QVERIFY2(panel, "The shared window must contain the existing QuickGenerate panel at every size.");
    auto *prompt = item(panel, "promptField");
    auto *media = item(panel, "mediaTypeButton");
    auto *ratio = item(panel, "aspectRatioButton");
    auto *quantity = item(panel, "generationCountButton");
    auto *generate = item(panel, "generateButton");
    QVERIFY(prompt && media && ratio && quantity && generate);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    const bool mobileHomeLayout = target == "ios" || target == "android";
    const auto availableWidth = width - root->property("mobileSystemSafeLeftInset").toReal()
                                      - root->property("mobileSystemSafeRightInset").toReal();
    const bool compactDesktop = availableWidth < 700;
    const auto expectedPanelWidth = mobileHomeLayout ? qMin(370.0, availableWidth - 32.0)
        : availableWidth - (compactDesktop ? 48.0 : 181.0)
            - (compactDesktop ? 24.0 : 48.0);
    QTRY_COMPARE(panel->width(), expectedPanelWidth);

    const auto padding = 0.0;
    const auto gap = theme->property("gap8").toReal();
    QCOMPARE(prompt->height(), 22.0); // Current LVRS TextField contract.
    QCOMPARE(generate->height(), 22.0);
    auto *homePaint = item(panel, "homePaintCanvas");
    QVERIFY(homePaint);
    QVERIFY(!homePaint->isVisible());
    const auto expectedHeight = mobileHomeLayout ? prompt->height() + gap + generate->height()
        : prompt->height() + 16 + 28;
    QCOMPARE(panel->height(), expectedHeight);
    QCOMPARE(panel->mapToScene(QPointF()).y(), root->property("contentTopInset").toReal()
                                              + (mobileHomeLayout ? 33.0 : 20.0));
    QCOMPARE(panel->width(), expectedPanelWidth);
    QCOMPARE(bounds(prompt, panel).left(), padding);
    QCOMPARE(bounds(prompt, panel).top(), padding);
    QCOMPARE(bounds(prompt, panel).right(), panel->width() - padding);
    QTRY_COMPARE(bounds(generate, panel).right(), panel->width() - padding);
    QCOMPARE(bounds(media, panel).top(), bounds(prompt, panel).bottom() + (mobileHomeLayout ? gap : 19));
    QCOMPARE(prompt->property("placeholderText").toString(), "Prompt");
    QCOMPARE(media->property("text").toString(), "Image");
    QCOMPARE(ratio->property("text").toString(), "1:1");
    QCOMPARE(quantity->property("text").toString(), !mobileHomeLayout && panel->width() >= 520 ? "1 image" : "1");
    QCOMPARE(generate->property("text").toString(), "Generate");
    QVERIFY(!item(root.get(), "helloLabel"));

    QVERIFY(!item(panel, "quickGenerateComposer"));
    for (auto* control : {media, ratio, quantity}) {
        auto* chevron = item(control, "quickGenerateChevron");
        QVERIFY(chevron);
        QCOMPARE(chevron->size(), QSizeF(18, 18));
        QTRY_COMPARE(chevron->property("status").toInt(), 1);
    }
    const QString captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        QVERIFY(panel->setProperty("generationCount", 100));
        QTest::qWait(100);
        const auto image = window->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(captureDirectory + '/' + QTest::currentDataTag() + ".png"));
        auto grab = item(panel, "quickGenerateContent")->grabToImage();
        QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
        QVERIFY(ready.wait(3000));
        QVERIFY(grab->saveToFile(captureDirectory + "/quick-" + QTest::currentDataTag() + ".png"));
    }

    for (const auto &aspect : {QString("1:1"), QString("16:9"), QString("9:16")}) {
        QVERIFY(panel->setProperty("aspectRatio", aspect));
        QCoreApplication::processEvents();
        QVERIFY(bounds(media, panel).right() <= bounds(ratio, panel).left());
        for (int count : {1, 1000}) {
            QVERIFY(panel->setProperty("generationCount", count));
            QTRY_VERIFY(bounds(ratio, panel).right() <= bounds(quantity, panel).left());
            QTRY_VERIFY(bounds(quantity, panel).right() <= bounds(generate, panel).left());
            QTRY_VERIFY(quantity->width() >= quantity->implicitWidth());
        }
        QVERIFY(ratio->width() >= ratio->implicitWidth());
        QVERIFY(generate->width() >= generate->implicitWidth());
    }
}

void GuiTests::videoHomeSubmitPlaysAndExports()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-video-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QVERIFY(QDir().mkpath(storage.filePath("Models/Checkpoint/video")));
    QFile manifest(storage.filePath("Models/Checkpoint/video/model_index.json"));
    QVERIFY(manifest.open(QIODevice::WriteOnly)); manifest.write(R"({"_class_name":"LTXPipeline"})"); manifest.close();
    auto runtime=GenerationRuntime{QStringLiteral(DREAMSCAPES_FAKE_GENERATOR),"cpu",1,64,{}};
    runtime.nativeInference=true;
    QScopedValueRollback<std::optional<GenerationRuntime>> override(guiRuntimeOverride,runtime);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath",storage.path()}});
    engine.load(sourceUrl("Main.qml")); QCOMPARE(engine.rootObjects().size(),1);
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->resize(960,720); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *quick=item(window,"quickGenerate");
    auto *controller=window->findChild<GenerationController *>("generationController");
    QVERIFY(quick && controller);
    auto *home=item(window,"desktopHome"); QVERIFY(home);
    QVERIFY(quick->setProperty("mediaType","Video"));
    QTRY_COMPARE(quick->property("mediaType").toString(),QString("Video"));
    auto *options=item(quick,"videoGenerationOptions"); QVERIFY(options && options->isVisible());
    QVERIFY(item(quick,"videoModelButton")->isEnabled());
    QVERIFY(quick->setProperty("prompt","video"));
    QVERIFY(quick->setProperty("videoDuration",1));
    QVERIFY(quick->setProperty("generationCount",2));
    QSignalSpy submitted(controller,&GenerationController::submissionQueued);
    click(window,item(quick,"generateButton"));
    QCOMPARE(submitted.size(),1); QVERIFY(window->property("resultVisible").toBool());
    controller->setForeground(true);
    QTRY_COMPARE_WITH_TIMEOUT(controller->completedResults().size(),2,15000);
    auto *result=item(window,"generationResult"); QVERIFY(result);
    QCOMPARE(listProperty(window,"submissionResults").size(),2);
    QVERIFY(QMetaObject::invokeMethod(result,"selectImage",Q_ARG(QVariant,0),Q_ARG(QVariant,true)));
    auto *video=item(result,"generatedVideo"); QVERIFY(video && video->isVisible());
    auto *player=result->findChild<QObject *>("generatedVideoPlayer"); QVERIFY(player);
    QTRY_VERIFY_WITH_TIMEOUT(player->property("hasVideo").toBool(),10000);
    QTRY_COMPARE_WITH_TIMEOUT(player->property("duration").toInt(),1000,10000);
    QVERIFY(!item(result,"newProjectButton")->isEnabled());
    click(window,item(result,"videoPlayButton"));
    QTRY_COMPARE(player->property("playbackState").toInt(),int(QMediaPlayer::PlayingState));
    QTRY_VERIFY(player->property("position").toInt()>0);
    const auto source=controller->completedResults()[0].toMap().value("mediaSource").toUrl();
    ImageFileExporter exporter;
    const auto destination=QUrl::fromLocalFile(storage.filePath("Files/export.mp4"));
    QVERIFY(exporter.saveVideo(source,destination));
    QFile original(source.toLocalFile()),saved(destination.toLocalFile());
    QVERIFY(original.open(QIODevice::ReadOnly)); QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(),original.readAll());
    // The persisted poster card must reopen the associated MP4, including after a fresh presentation.
    auto *history=window->findChild<QObject *>("generationHistoryModel"); QVERIFY(history);
    QVERIFY(QMetaObject::invokeMethod(history,"refresh"));
    QTRY_COMPARE_WITH_TIMEOUT(listProperty(window,"generationHistoryEntries").size(),2,10000);
    const auto stored=listProperty(window,"generationHistoryEntries")[0].toMap();
    QCOMPARE(stored.value("mediaType").toString(),QString("Video"));
    QVERIFY(QMetaObject::invokeMethod(window,"openHomeFile",Q_ARG(QVariant,QVariant(stored))));
    QVERIFY(window->property("resultVisible").toBool());
    QTRY_COMPARE(player->property("source").toUrl(),stored.value("mediaSource").toUrl());
    auto *poster=item(result,"videoPoster"); QVERIFY(poster);
    QTRY_COMPARE(poster->property("status").toInt(),1); // Image.Ready
    QVERIFY(poster->isVisible());
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if(!capture.isEmpty()) { QVERIFY(QDir().mkpath(capture)); QVERIFY(window->grabWindow().save(capture+"/video-result.png")); }
    window->close();
}

void GuiTests::videoWorkspaceRoutesEditsAndGenerates()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-video-workspace-XXXXXX");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QVERIFY(QDir().mkpath(storage.filePath("Models/Checkpoint/ltx")));
    QFile manifest(storage.filePath("Models/Checkpoint/ltx/model_index.json"));
    QVERIFY(manifest.open(QIODevice::WriteOnly)); manifest.write(R"({"_class_name":"LTXConditionPipeline"})"); manifest.close();
    auto runtime=GenerationRuntime{QStringLiteral(DREAMSCAPES_FAKE_GENERATOR),"cpu",1,64,{}};
    runtime.nativeInference=true;
    QScopedValueRollback<std::optional<GenerationRuntime>> override(guiRuntimeOverride,runtime);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"initialContainerPath",storage.path()}});
    engine.load(sourceUrl("Main.qml")); QCOMPARE(engine.rootObjects().size(),1);
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->resize(1813,1248); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *sidebar=item(window,"desktopSidebar"); QVERIFY(sidebar);
    click(window,visualItem(sidebar,"desktopAction_video"));
    auto *workspace=item(window,"videoGenerationWorkspace"); QVERIFY(workspace && workspace->isVisible());
    auto *parameters=item(workspace,"videoParameterPanel"); QVERIFY(parameters);
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if(!capture.isEmpty()) { QVERIFY(QDir().mkpath(capture)); QTest::qWait(120); QVERIFY(window->grabWindow().save(capture+"/video-workspace-default.png")); }
    auto *timeline=item(workspace,"videoTimeline"); QVERIFY(timeline);
    auto *state=workspace->findChild<QObject *>("videoTimelineState"); QVERIFY(state);
    auto *upper=item(workspace,"videoUpperWorkspace"); QVERIFY(upper);
    QTRY_COMPARE(parameters->width(),300.0);
    QTRY_COMPARE(timeline->height(),406.0);
    QTRY_VERIFY(bounds(upper,workspace).bottom()+12 <= bounds(timeline,workspace).top()+.01);
    QVERIFY(bounds(parameters,upper).left() > bounds(item(workspace,"videoPreviewPanel"),upper).right());
    QVERIFY(bounds(timeline,workspace).bottom() <= workspace->height()-16);
    auto *parameterViewport=item(workspace,"videoParameterViewport"); QVERIFY(parameterViewport);
    QVERIFY(parameterViewport->property("contentHeight").toReal()>parameterViewport->height());
    auto *aspect=item(workspace,"videoPreviewAspectFrame"); QVERIFY(aspect);
    QTRY_VERIFY(qAbs(aspect->width()/aspect->height()-16.0/9)<.001);
    auto *previewSurface=item(workspace,"videoPreviewSurface"); QVERIFY(previewSurface);
    for (const QSize resolution : {QSize(1024,1024),QSize(1024,576),QSize(576,1024),QSize(1536,384),QSize(384,1536)}) {
        QVERIFY(QMetaObject::invokeMethod(parameters,"edit",Q_ARG(QVariant,"width"),Q_ARG(QVariant,resolution.width())));
        QVERIFY(QMetaObject::invokeMethod(parameters,"edit",Q_ARG(QVariant,"height"),Q_ARG(QVariant,resolution.height())));
        for (const QSize windowSize : {QSize(1813,1248),QSize(1280,800),QSize(960,720),QSize(640,700)}) {
            window->resize(windowSize);
            QTRY_VERIFY(qAbs(aspect->width()/aspect->height()-double(resolution.width())/resolution.height())<.001);
            QTRY_COMPARE(bounds(aspect,previewSurface).center(),previewSurface->boundingRect().center());
            QVERIFY(previewSurface->boundingRect().adjusted(-.01,-.01,.01,.01).contains(bounds(aspect,previewSurface)));
            const auto centerCapture=qEnvironmentVariable("DREAMSCAPES_CENTER_CAPTURE_DIR");
            if(!centerCapture.isEmpty() && windowSize==QSize(1813,1248)) {
                QTest::qWait(80);
                QVERIFY(window->grabWindow().save(centerCapture+QString("/video-%1x%2.png").arg(resolution.width()).arg(resolution.height())));
            }
        }
    }
    window->resize(1813,1248);
    auto *widthRow=item(workspace,"videoWidth"); QVERIFY(widthRow);
    QVERIFY(widthRow->setProperty("inputText1","65"));
    QVERIFY(QMetaObject::invokeMethod(widthRow,"edited",Q_ARG(QString,"inputText1"),Q_ARG(QVariant,"65")));
    QVERIFY(parameters->property("hasInvalidInputs").toBool());
    QVERIFY(widthRow->property("invalidEdit").toBool());
    QCOMPARE(widthRow->property("inputText1").toString(),QString("65"));
    QVERIFY(QMetaObject::invokeMethod(parameters,"resetDraft"));
    QVERIFY(!parameters->property("hasInvalidInputs").toBool());
    QTRY_VERIFY(!widthRow->property("invalidEdit").toBool());
    QTRY_COMPARE(widthRow->property("inputText1").toString(),QString("1024"));
    QVERIFY(QMetaObject::invokeMethod(parameters,"savePreset"));
    QVERIFY(widthRow->setProperty("inputText1","65"));
    QVERIFY(QMetaObject::invokeMethod(widthRow,"edited",Q_ARG(QString,"inputText1"),Q_ARG(QVariant,"65")));
    QVERIFY(parameters->property("hasInvalidInputs").toBool());
    QCOMPARE(widthRow->property("inputText1").toString(),QString("65"));
    QVERIFY(QMetaObject::invokeMethod(parameters,"loadPreset"));
    QVERIFY(!parameters->property("hasInvalidInputs").toBool());
    QTRY_VERIFY(!widthRow->property("invalidEdit").toBool());
    QTRY_COMPARE(widthRow->property("inputText1").toString(),QString("1024"));
    QCOMPARE(listProperty(state,"shots").size(),4);
    QCOMPARE(state->property("keptFrames").toInt(),84);
    state->setProperty("playhead",60);
    click(window,item(workspace,"videoSplitShot"));
    QCOMPARE(listProperty(state,"shots").size(),5);
    click(window,item(workspace,"videoTimelineUndo"));
    QCOMPARE(listProperty(state,"shots").size(),4);
    const auto keyUrl=QUrl::fromLocalFile(storage.filePath("Files/key.png"));
    QImage reference(64,32,QImage::Format_RGB32); reference.fill(Qt::red); QVERIFY(reference.save(keyUrl.toLocalFile()));
    QVERIFY(QMetaObject::invokeMethod(state,"addKey",Q_ARG(QVariant,keyUrl.toString()),Q_ARG(QVariant,60)));
    QCOMPARE(listProperty(state,"keys").size(),1);
    QVERIFY(QMetaObject::invokeMethod(state,"editKey",Q_ARG(QVariant,"frame"),Q_ARG(QVariant,61)));
    QVERIFY(QMetaObject::invokeMethod(state,"editKey",Q_ARG(QVariant,"value"),Q_ARG(QVariant,.65)));
    auto *prompt=item(workspace,"videoPrompt"); QVERIFY(prompt);
    QVERIFY(prompt->setProperty("text","video"));
    QVERIFY(QMetaObject::invokeMethod(parameters,"edit",Q_ARG(QVariant,"outputCount"),Q_ARG(QVariant,2)));
    QVERIFY(QMetaObject::invokeMethod(parameters,"edit",Q_ARG(QVariant,"steps"),Q_ARG(QVariant,7)));
    QVERIFY(QMetaObject::invokeMethod(parameters,"edit",Q_ARG(QVariant,"width"),Q_ARG(QVariant,64)));
    QVERIFY(QMetaObject::invokeMethod(parameters,"edit",Q_ARG(QVariant,"height"),Q_ARG(QVariant,32)));
    for(const auto size : {QSize(1280,800),QSize(960,720),QSize(640,700)}) {
        window->resize(size); QTest::qWait(60);
        QCOMPARE(parameters->width(),300.0); QCOMPARE(timeline->height(),406.0);
        QTRY_VERIFY(bounds(parameters,item(workspace,"videoUpperWorkspace")).left()>=0);
    }
    window->resize(1813,1248); QTest::qWait(80);
    if(!capture.isEmpty()) { QVERIFY(QDir().mkpath(capture)); QVERIFY(window->grabWindow().save(capture+"/video-workspace.png")); }
    click(window,visualItem(sidebar,"desktopAction_home"));
    QVERIFY(!workspace->isVisible());
    click(window,visualItem(sidebar,"desktopAction_video"));
    QCOMPARE(prompt->property("text").toString(),QString("video"));
    auto *controller=window->findChild<GenerationController *>("generationController"); QVERIFY(controller);
    QSignalSpy submitted(controller,&GenerationController::submissionQueued);
    click(window,item(workspace,"videoGenerateComposition"));
    QCOMPARE(submitted.size(),1); QCOMPARE(submitted[0][0].toStringList().size(),2);
    QVERIFY(!window->property("resultVisible").toBool()); QVERIFY(workspace->isVisible());
    const auto recipe=controller->jobs()[0].toMap().value("videoParameters").toMap();
    QCOMPARE(recipe.value("frames").toInt(),84); QCOMPARE(recipe.value("shots").toList().size(),3);
    QCOMPARE(controller->jobs()[0].toMap().value("steps").toInt(),7);
    const auto condition=recipe.value("shots").toList()[2].toMap().value("conditions").toList()[0].toMap();
    QCOMPARE(condition.value("frame").toInt(),13); QCOMPARE(condition.value("strength").toDouble(),.65);
    QVERIFY(condition.value("image").toString()!=keyUrl.toLocalFile());
    QVERIFY(QFile::remove(keyUrl.toLocalFile()));
    controller->setForeground(true);
    QTRY_COMPARE_WITH_TIMEOUT(controller->completedResults().size(),2,15000);
    QCOMPARE(listProperty(workspace,"results").size(),2);
    auto *player=workspace->findChild<QObject *>("videoWorkspacePlayer"); QVERIFY(player);
    QTRY_VERIFY_WITH_TIMEOUT(player->property("hasVideo").toBool(),10000);
    QTRY_COMPARE_WITH_TIMEOUT(player->property("duration").toInt(),3500,10000);
    click(window,item(workspace,"videoWorkspacePlay"));
    QTRY_COMPARE(player->property("playbackState").toInt(),int(QMediaPlayer::PlayingState));
    QTRY_VERIFY(player->property("position").toInt()>0);
    if(!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture+"/video-workspace-result.png"));
    window->close();
}

void GuiTests::sharedControlsSubmitCurrentSelection_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<int>("width");
    QTest::newRow("desktop") << QString("macos") << 960;
    QTest::newRow("desktop-compact") << QString("macos") << 320;
    QTest::newRow("ios") << QString("ios") << 320;
    QTest::newRow("android") << QString("android") << 360;
}

void GuiTests::sharedControlsSubmitCurrentSelection()
{
    QFETCH(QString, target);
    QFETCH(int, width);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme);
    QVERIFY(theme->setProperty("targetOverride", target));
    QQmlComponent component(&engine, sourceUrl("Main.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window);
    window->resize(width, 844);
    auto *panel = item(root.get(), "quickGenerate");
    QVERIFY(panel);
    auto *prompt = item(panel, "promptField");
    auto *media = item(panel, "mediaTypeButton");
    auto *generate = item(panel, "generateButton");
    auto *ratio = item(panel, "aspectRatioButton");
    auto *quantity = item(panel, "generationCountButton");
    auto *ratioMenu = panel->findChild<QObject *>("aspectRatioMenu");
    QVERIFY(prompt && media && generate && ratio && quantity && ratioMenu);
    auto *mediaMenu = panel->findChild<QObject *>("mediaTypeMenu");
    QVERIFY(mediaMenu);
    QCOMPARE(listProperty(mediaMenu, "items"), QVariantList({"Image", "Video"}));
    QSignalSpy requests(root.get(), SIGNAL(generateRequested(QString,QString,QString,int)));
    QVERIFY(requests.isValid());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    click(window, generate);
    QCOMPARE(requests.size(), 0);
    auto *input = prompt->property("inputItem").value<QQuickItem *>();
    QVERIFY(input);
    QTRY_VERIFY(input->hasActiveFocus());
    for (const char key : QByteArray("   a quiet forest   ")) QTest::keyClick(window, key);
    click(window, media);
    QTRY_VERIFY(mediaMenu->property("opened").toBool());
    QVERIFY(mediaMenu->property("x").toReal() >= 0);
    QVERIFY(mediaMenu->property("x").toReal() + mediaMenu->property("width").toReal() <= window->width());
    auto *menuContent = mediaMenu->property("contentItem").value<QQuickItem *>();
    QVERIFY(menuContent);
    auto *video = menuEntry(menuContent, "Video");
    QVERIFY(video); click(window, video);
    QTRY_VERIFY(!mediaMenu->property("visible").toBool());
    QCOMPARE(panel->property("mediaType").toString(), "Video");
    QCOMPARE(media->property("text").toString(), "Video");
    window->resize(width, 600);
    QTRY_COMPARE(window->height(), 600);
    click(window, generate);
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests.at(0), QVariantList({"a quiet forest", "Video", "1:1", 1}));
    auto *controller = window->findChild<GenerationController *>("generationController");
    QVERIFY(controller);
    QVERIFY(controller->jobs().isEmpty());
    QVERIFY(!window->property("resultVisible").toBool());
    auto *notice = item(panel, "quickGenerateNotice");
    QTRY_VERIFY(notice->isVisible());
    QVERIFY(!notice->property("text").toString().isEmpty());
    QCOMPARE(notice->property("text").toString(),controller->errorString());
    QCOMPARE(prompt->property("text").toString(), "   a quiet forest   ");
    click(window, media);
    QTRY_VERIFY(mediaMenu->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(mediaMenu, "triggerEntry", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(!mediaMenu->property("visible").toBool());
    click(window, ratio);
    QTRY_VERIFY(ratioMenu->property("opened").toBool());
    QVERIFY(ratioMenu->property("x").toReal() >= 0);
    QVERIFY(ratioMenu->property("x").toReal() + ratioMenu->property("width").toReal() <= window->width());
    auto *ratioContent = ratioMenu->property("contentItem").value<QQuickItem *>();
    QVERIFY(ratioContent);
    auto *wideRatio = menuEntry(ratioContent, "16:9");
    QVERIFY(wideRatio);
    click(window, wideRatio);
    QTRY_VERIFY(!ratioMenu->property("visible").toBool());
    QCOMPARE(ratio->property("text").toString(), "16:9");

    const QVariantList counts{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20, 25, 30, 40, 50, 100, 200, 500, 1000};
    QCOMPARE(panel->property("generationCounts").value<QJSValue>().toVariant().toList(), counts);
    auto *countMenu = panel->findChild<QObject *>("generationCountMenu");
    QVERIFY(countMenu);
    for (int index = 0; index < counts.size(); ++index) {
        QVERIFY(QMetaObject::invokeMethod(countMenu, "triggerEntry", Q_ARG(QVariant, index)));
        QCOMPARE(panel->property("generationCount").toInt(), counts.at(index).toInt());
        QCOMPARE(quantity->property("text").toString(), panel->property("canvasEnabled").toBool() && panel->width() >= 520
            ? counts.at(index).toString() + (counts.at(index).toInt() == 1 ? " image" : " images") : counts.at(index).toString());
    }
    panel->setProperty("generationCount", 1);
    window->resize(width, 480);
    QTRY_COMPARE(window->height(), 480);
    if (panel->property("canvasEnabled").toBool()) {
        auto *home = item(root.get(), "desktopHome");
        QVERIFY(QMetaObject::invokeMethod(home, "scrollToItem", Q_ARG(QVariant, QVariant::fromValue(quantity))));
    }
    click(window, quantity);
    QTRY_VERIFY(countMenu->property("opened").toBool());
    auto *countList = item(panel, "generationCountList");
    QVERIFY(countList);
    QTRY_VERIFY(countList->property("contentHeight").toReal() > countList->height());
    QTest::keyClick(window, Qt::Key_End);
    QTRY_COMPARE(countList->property("currentIndex").toInt(), 19);
    QTRY_VERIFY(menuEntry(countList, "1000"));
    auto *lastCount = menuEntry(countList, "1000");
    QTRY_VERIFY(countList->boundingRect().contains(bounds(lastCount, countList)));
    QVERIFY(countMenu->property("y").toReal() >= 0);
    QVERIFY(countMenu->property("y").toReal() + countMenu->property("height").toReal() <= window->height());
    click(window, lastCount);
    QTRY_VERIFY(!countMenu->property("visible").toBool());
    QCOMPARE(panel->property("generationCount").toInt(), 1000);
    click(window, quantity);
    QTRY_VERIFY(countMenu->property("opened").toBool());
    QTRY_VERIFY(countList->boundingRect().contains(bounds(menuEntry(countList, "1000"), countList)));
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY(!countMenu->property("visible").toBool());

    if (panel->property("canvasEnabled").toBool()) {
        auto *home = item(root.get(), "desktopHome");
        QVERIFY(QMetaObject::invokeMethod(home, "scrollToItem", Q_ARG(QVariant, QVariant::fromValue(prompt))));
    }
    click(window, prompt);
    QTest::keyClick(window, Qt::Key_Return);
    QCOMPARE(requests.size(), 2);
    QCOMPARE(requests.at(1), QVariantList({"a quiet forest", "Image", "16:9", 1000}));
    prompt->setProperty("text", "   ");
    click(window, generate);
    QCOMPARE(requests.size(), 2);
}

void GuiTests::packagedApplicationStarts()
{
    QTemporaryDir presence(DREAMSCAPES_TEST_DIRECTORY "/helper-XXXXXX");
    iiSocietyHelper::Helper observer;
    QVERIFY(observer.start({"com.iisacc.dreamscapes.test", "Dreamscapes test", "1"},
                            {presence.path(), 100, 5000}));
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("SOCIETY_HELPER_DIRECTORY", presence.path());
    environment.insert("IILOCALLLM_APP_ENDPOINTS", presence.filePath("apps"));
    for (const auto *key : {"DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"})
        environment.remove(QString::fromLatin1(key));
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    // Verify the installed entry point too, without copying another build-tree app.
    const auto installedExecutable = qEnvironmentVariable("DREAMSCAPES_TEST_APP_PATH");
    process.start(installedExecutable.isEmpty() ? QStringLiteral(DREAMSCAPES_EXECUTABLE_PATH)
                                               : installedExecutable, {});
    QVERIFY(process.waitForStarted());
    QByteArray output;
    QElapsedTimer timer;
    timer.start();
    while (!output.contains("LVRS bootstrap.entry.root-loaded")
           && !output.contains("LVRS bootstrap.entry.root-load-failed") && timer.elapsed() < 30000
           && process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        output += process.readAll();
    }
    const bool running = process.state() == QProcess::Running;
    // Both participants must actually observe each other through the installed SDK.
    QElapsedTimer discovery;
    discovery.start();
    while (!output.contains("LVRS bootstrap.entry.root-load-failed")
           && process.state() == QProcess::Running && discovery.elapsed() < 15000 && (observer.peers().isEmpty()
           || !output.contains("com.iisacc.dreamscapes observed com.iisacc.dreamscapes.test"))) {
        QTest::qWait(50);
        output += process.readAll();
    }
    const auto peers = observer.peers();
    process.terminate();
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished();
    }
    output += process.readAll();
    // Source GUI fixtures register their own types. Only this real entry-point
    // check catches a missing production registration in the static module.
    QVERIFY2(!output.contains("LVRS bootstrap.entry.root-load-failed"), output.constData());
    QVERIFY2(running, output.constData());
    QVERIFY2(output.contains("LVRS bootstrap.entry.root-loaded"), output.constData());
    QVERIFY2(peers.size() == 1, output.constData());
    QCOMPARE(peers.first().application.id, "com.iisacc.dreamscapes");
    QVERIFY2(output.contains("com.iisacc.dreamscapes observed com.iisacc.dreamscapes.test"), output.constData());
    QVERIFY2(output.contains("\"windowCount\":1"), output.constData());
    QVERIFY2(!output.contains("failed to load") && !output.contains("is not installed")
        && !output.contains("QML Image: Cannot open:"), output.constData());
}

int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QQuickStyle::setStyle("Basic");
    QGuiApplication application(argc, argv);
    QTemporaryDir settings(DREAMSCAPES_TEST_DIRECTORY "/gui-storage-XXXXXX");
    if (!settings.isValid()) return 1;
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settings.filePath("storage.json").toUtf8());
    qunsetenv("SOCIETY_CONTAINER_PATH");
    qputenv("IILD_GENERATOR_EXECUTABLE", DREAMSCAPES_FAKE_GENERATOR);
    qputenv("DREAMSCAPES_TEMP_DIRECTORY", DREAMSCAPES_TEST_DIRECTORY);
    qputenv("SOCIETY_DISABLE_SESSION_RESTORE", "1");
    qmlRegisterType<GuiGenerationController>("Dreamscapes.Storage", 1, 0, "GenerationController");
    qputenv("DREAMSCAPES_TEST_PRESET_FILE", settings.filePath("advanced-presets.json").toUtf8());
    qmlRegisterType<GuiAdvancedImageParameters>("Dreamscapes.Storage", 1, 0, "AdvancedImageParameters");
    qmlRegisterType<iiSocietyContainer::DashboardFiles>("Dreamscapes.Storage", 1, 0, "DashboardFiles");
    qmlRegisterType<iiSocietyContainer::SocietyApplication>("Dreamscapes.Storage", 1, 0, "SocietyApplication");
    qmlRegisterType<HomeCanvas>("Dreamscapes.Storage", 1, 0, "HomeCanvas");
    qmlRegisterType<CanvasPresets>("Dreamscapes.Storage", 1, 0, "CanvasPresets");
    qmlRegisterType<EditorCanvas>("Dreamscapes.Storage", 1, 0, "EditorCanvas");
    qmlRegisterType<EditorProject>("Dreamscapes.Storage", 1, 0, "EditorProject");
    qmlRegisterType<ImageFileExporter>("Dreamscapes.Storage", 1, 0, "ImageFileExporter");
    qmlRegisterType<PhotoLibraryExporter>("Dreamscapes.Storage", 1, 0, "PhotoLibraryExporter");
    application.setQuitOnLastWindowClosed(false);
    GuiTests tests;
    // Qt emits lastWindowClosed only while the application event loop is running.
    QTimer::singleShot(0, &application, [&]() {
        application.exit(QTest::qExec(&tests, argc, argv));
    });
    return application.exec();
}

#include "tst_Gui.moc"
