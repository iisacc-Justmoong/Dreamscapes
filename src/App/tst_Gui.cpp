#include "App/Views/Home/HomeCanvas.h"
#include "App/Views/Home/CanvasPresets.h"
#include "App/Views/Home/CanvasPreview.h"
#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorCanvasFeedback.h"
#include "App/Views/Editor/EditorTimelineModel.h"
#include "App/Views/Editor/EditorProject.h"
#include "App/Views/ColorManagerModel.h"
#include "App/Views/Editor/EditorCameraPhoto.h"
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include "App/Generation/ImageCanvas.h"
#include <QDir>
#include <QClipboard>
#include <QBuffer>
#include <QDirIterator>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QApplication>
#include <QImage>
#include <QInputMethodEvent>
#include <QJSValue>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QMediaPlayer>
#include <QMap>
#include <QMouseEvent>
#include <QLineF>
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
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QSet>
#include <QStyleHints>
#include <QTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWheelEvent>
#include "Generation/GenerationController.h"
#include "Generation/AdvancedImageParameters.h"
#include "Generation/VideoGeneration.h"
#include "../../tests/LocalRuntimeProbeReady.h"
#include "Views/Result/ImageFileExporter.h"
#include "Views/Result/PhotoLibraryExporter.h"
#include <iiSocietyHelper.h>
#include <iiSocietyContainer/DashboardFiles.h>
#include <iiSocietyContainer/SocietyApplication.h>
#include <SharedStorage.h>
#include <QtTest>

#include <memory>
#include <limits>
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

QVariantMap mapProperty(QObject *object, const char *name)
{
    const auto value = object->property(name);
    return value.metaType() == QMetaType::fromType<QJSValue>()
        ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
}

bool isCompactEditorTool(const QString &key)
{
    return QStringList{"audio-track", "canvas", "generative", "select", "retouch", "fill", "brush"}.contains(key);
}

bool iconColorsNear(const QColor &left, const QColor &right)
{
    // Qt's raster and native renderers may round premultiplied edge channels.
    constexpr int tolerance = 8;
    return qAbs(left.red() - right.red()) <= tolerance
        && qAbs(left.green() - right.green()) <= tolerance
        && qAbs(left.blue() - right.blue()) <= tolerance;
}

bool captureToolbarIconColor(QQuickItem *icon, const QColor &expected,
                             QColor *dominant, QString *error)
{
    const auto fail = [&](const QString &message) {
        *error = message;
        return false;
    };
    const auto capture = icon->grabToImage();
    if (!capture) return fail("Could not capture the toolbar icon");
    QSignalSpy ready(capture.data(), &QQuickItemGrabResult::ready);
    if (capture->image().isNull() && !ready.wait(5000))
        return fail("Toolbar icon capture did not finish");
    const auto image = capture->image();
    QMap<QRgb, int> coreColors;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const auto pixel = image.pixelColor(x, y);
            if (pixel.alpha() >= 230) ++coreColors[pixel.rgb()];
        }
    if (coreColors.isEmpty()) return fail("Toolbar icon has no opaque stroke pixels");
    auto mostFrequent = coreColors.cbegin();
    for (auto color = coreColors.cbegin(); color != coreColors.cend(); ++color)
        if (color.value() > mostFrequent.value()) mostFrequent = color;
    *dominant = QColor::fromRgb(mostFrequent.key());
    const int largest = qMax(dominant->red(), qMax(dominant->green(), dominant->blue()));
    const int smallest = qMin(dominant->red(), qMin(dominant->green(), dominant->blue()));
    if (largest - smallest < 24)
        return fail("Toolbar icon is achromatic: " + dominant->name());
    if (!expected.isValid() || !iconColorsNear(*dominant, expected))
        return fail("Toolbar icon color " + dominant->name()
                    + " does not match " + expected.name());
    for (auto color = coreColors.cbegin(); color != coreColors.cend(); ++color)
        if (!iconColorsNear(QColor::fromRgb(color.key()), *dominant))
            return fail("Toolbar icon contains more than one stroke color");
    return true;
}

QVariantList graphicRows(const QVariantList &rows)
{
    QVariantList flattened;
    for (const auto &entry : rows) {
        flattened.append(entry);
        flattened.append(graphicRows(entry.toMap()["rows"].toList()));
    }
    return flattened;
}

QRectF bounds(QQuickItem *control, QQuickItem *parent)
{
    return control->mapRectToItem(parent, control->boundingRect());
}

QQuickItem *visualItem(QQuickItem *root, const char *name)
{
    if (!root) return nullptr;
    if (root->objectName() == QString::fromLatin1(name)) return root;
    for (auto *child : root->childItems())
        if (auto *found = visualItem(child, name)) return found;
    return nullptr;
}

QQuickItem *visualText(QQuickItem *root, const QString &text)
{
    if (root->isVisible() && root->property("text").toString() == text) return root;
    for (auto *child : root->childItems())
        if (auto *found = visualText(child, text)) return found;
    return nullptr;
}

class MouseInputRecorder : public QObject
{
public:
    QVariantList presses;
    QVariantList releases;
    QVariantList moves;
    QVariantList doubleClicks;
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
            || event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonDblClick) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            auto *window = qobject_cast<QQuickWindow *>(watched);
            auto *grabber = window ? window->mouseGrabberItem() : nullptr;
            auto &events = event->type() == QEvent::MouseButtonPress ? presses
                : event->type() == QEvent::MouseButtonRelease ? releases
                : event->type() == QEvent::MouseButtonDblClick ? doubleClicks : moves;
            events.append(QVariantMap{{"local", mouse->position()}, {"global", mouse->globalPosition()},
                {"timestamp", QVariant::fromValue(mouse->timestamp())},
                {"grabber", grabber ? grabber->objectName() + ":" + grabber->metaObject()->className() : QString("null")},
                {"focusIsTargetWindow", QGuiApplication::focusWindow() == window}});
        }
        return false;
    }
};

QStringList visualHitPath(QQuickItem *root, const QPointF &scenePoint)
{
    auto *hit = root;
    while (auto *child = hit->childAt(hit->mapFromScene(scenePoint).x(), hit->mapFromScene(scenePoint).y())) hit = child;
    QStringList result;
    for (auto *ancestor = hit; ancestor; ancestor = ancestor->parentItem())
        result.append(ancestor->objectName() + ":" + ancestor->metaObject()->className());
    return result;
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
    QVERIFY2(control, "Cannot click a missing control.");
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
    const bool nativeCocoa = QGuiApplication::platformName() == "cocoa";
    auto *graphicPanel = nativeCocoa && qEnvironmentVariableIsSet("DREAMSCAPES_NATIVE_CLICK_INTERVAL")
        ? visualItem(window->contentItem(), "editorGraphicPanel") : nullptr;
    const bool compareWallClock = nativeCocoa && qEnvironmentVariableIsSet("DREAMSCAPES_NATIVE_CLICK_INTERVAL")
        && ((control->objectName() == "graphicNavigate-710:19322" && graphicPanel
                && graphicPanel->property("currentViewId").toString() == "713:32714")
            || control->objectName() == "graphicAction-718:67748");
    const int clickDelay = nativeCocoa ? QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50 : -1;
    QElapsedTimer nativeElapsed; nativeElapsed.start();
    if (nativeCocoa) {
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        if (compareWallClock) QTest::qWait(clickDelay);
    }
    MouseInputRecorder inputRecorder;
    const auto spySignal = [&](const char *signature) -> std::unique_ptr<QSignalSpy> {
        if (!nativeCocoa) return {};
        const int signalIndex = control->metaObject()->indexOfSignal(signature);
        if (signalIndex < 0) return {};
        return std::make_unique<QSignalSpy>(control, control->metaObject()->method(signalIndex));
    };
    const auto clicked = spySignal("clicked()");
    const auto doubleClicked = spySignal("doubleClicked()");
    const auto pressed = spySignal("pressed()");
    const auto released = spySignal("released()");
    const QString controlName = control->objectName();
    const auto clickRect = bounds(control, window->contentItem());
    const auto hitPath = nativeCocoa ? visualHitPath(window->contentItem(), clickRect.center()) : QStringList{};
    if (nativeCocoa) window->installEventFilter(&inputRecorder);
    QTest::mouseMove(window, control->mapToScene(control->boundingRect().center()).toPoint());
    // QWindow QtTest mouse events use a simulated clock. The default 500 ms
    // spacing can become a double-click under the user's native style hints.
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      control->mapToScene(control->boundingRect().center()).toPoint(), clickDelay);
    if (nativeCocoa) window->removeEventFilter(&inputRecorder);
    if (clicked && (clicked->isEmpty() || (compareWallClock
        && (controlName == "graphicNavigate-710:19322" || controlName == "graphicAction-718:67748"))))
        qInfo() << "Native click diagnostic" << controlName << "bounds" << clickRect
            << "active" << window->isActive() << "focusIsTargetWindow" << (QGuiApplication::focusWindow() == window)
            << "mouseGrabber" << window->mouseGrabberItem() << "doubleClickInterval" << QGuiApplication::styleHints()->mouseDoubleClickInterval()
            << "pressed" << (pressed ? pressed->count() : -1) << "released" << (released ? released->count() : -1)
            << "clicked" << clicked->count() << "doubleClicked" << (doubleClicked ? doubleClicked->count() : -1)
            << "pressEvents" << inputRecorder.presses
            << "releaseEvents" << inputRecorder.releases << "doubleClickEvents" << inputRecorder.doubleClicks
            << "wallElapsedMs" << nativeElapsed.elapsed() << "clickDelay" << clickDelay << "hit" << hitPath;
}

void escapeResult(QQuickWindow *window)
{
    item(window, "generationResult")->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Escape);
}

void editResultImage(QQuickWindow *window, int index)
{
    auto *result = item(window, "generationResult");
    auto *selection = item(result, "resultImageSelection");
    QVERIFY(selection && selection->isVisible() && selection->isEnabled());
    click(window, item(selection, "resultImageSelectionToggle"));
    QTRY_VERIFY(selection->property("selectionMode").toBool());
    const auto entries = listProperty(result, "galleryResults");
    QVERIFY(index >= 0 && index < entries.size());
    QVERIFY(QMetaObject::invokeMethod(selection, "toggle", Q_ARG(QVariant, entries[index])));
    click(window, item(selection, "resultImageSelectionOpen"));
    QTRY_VERIFY(item(window, "canvasEditor")->isVisible());
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
    void mainStartupPerformance();
    void homeDefersEditorUntilNeeded();
    void historyAppearsBelowQuickGenerateAndScrolls_data();
    void historyAppearsBelowQuickGenerateAndScrolls();
    void historyShowsAnEmptyState();
    void mobileHomeUsesFigmaSectionsLimitsAndLvrsNavigation();
    void desktopHomeSidebarReflowsAndRoutes();
    void desktopHomeSidebarMatchesFigmaAndScrolls();
    void editorSidebarKeepsSharedWindowLayout();
    void canvasToolFeedbackFollowsNativePointer();
    void desktopHomeSearchesSocietyAndUpdates();
    void desktopHomeRendersFigmaFrame();
    void desktopHomeContinuousRowsAndPromptStarters();
    void homeCanvasPreviews_data();
    void homeCanvasPreviews();
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
    void editorToolControlsDoNotCommitUneditedFocus_data();
    void editorToolControlsDoNotCommitUneditedFocus();
    void panelRowsKeepLabelsPassive_data();
    void panelRowsKeepLabelsPassive();
    void desktopEditorLayoutCentersCanvas();
    void desktopEditorToolbarScrollsAndNavigates();
    void editorTimelineLayout_data();
    void editorTimelineLayout();
    void editorTimelineControlsAndPlayback();
    void videoTimelineTracks();
    void editorTimelineTrackManagement();
    void editorCanvasPanAndZoomStayInsideViewport();
    void desktopEditorPanelResizes();
    void editorLayerPanelFourStates();
    void editorButtonInteractionStates_data();
    void editorButtonInteractionStates();
    void editorPanelButtonKindsStayFixed();
    void editorTextPanelFourStates_data();
    void editorTextPanelFourStates();
    void editorFileThreeViews_data();
    void editorFileThreeViews();
    void editorCameraPhotoViews_data();
    void editorCameraPhotoViews();
    void societyContainerMirrorIsOwnerProvided();
    void storageBrowserRoutes_data();
    void storageBrowserRoutes();
    void storageBrowserUnavailableEntriesAndContainerChanges();
    void storageBrowserDownloadOpensFreshAlias_data();
    void storageBrowserDownloadOpensFreshAlias();
    void editorTextPanelMobile_data();
    void editorTextPanelMobile();
    void desktopEditorElementsMatchesFigma();
    void editorElementsTransformAndInspectorStayInSync();
    void desktopEditorToolPanelsMatchFigma_data();
    void desktopEditorToolPanelsMatchFigma();
    void editorCompactPanelsMatchFigma_data();
    void editorCompactPanelsMatchFigma();
    void editorGraphicPanelAllFigmaViews_data();
    void editorGraphicPanelAllFigmaViews();
    void editorColorToolSelectionDoesNotEditDocument_data();
    void editorColorToolSelectionDoesNotEditDocument();
    void editorGraphicPanelEditsPersistAndReset();
    void editorGraphicPanelCollectionsAndPickers();
    void editorGraphicPanelMobileRoutes_data();
    void editorGraphicPanelMobileRoutes();
    void editorNativeCanvasEditsAndPersists();
    void editorToolbarOperations();
    void editorDocumentRenderQuality();
    void captureSocietyUrl(const QUrl &url) { m_societyUrl = url; }
    void foregroundApplicationPreparesBeforeGenerate();
    void generateOpensResultImmediatelyAndDisplaysEveryPreview();
    void referenceGenerateOpensResultImmediately_data();
    void referenceGenerateOpensResultImmediately();
    void resultCancelsTheActiveGeneration_data();
    void resultCancelsTheActiveGeneration();
    void mainCreatesOneSharedWindow();
    void preferencesCategoriesAndDefaultModels();
    void preferencesPromptsWrapAndPersist();
    void generationPromptDefaultsSeedNewDrafts();
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
    void videoWorkspaceStartsEmptyAndUsesGenerationDefaults();
    void videoWorkspaceRoutesEditsAndGenerates();
    void sharedControlsSubmitCurrentSelection_data();
    void sharedControlsSubmitCurrentSelection();
    void advancedGenerationDefaultsAndDynamicCollections();
    void advancedAttachmentsPersistNativeSources();
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

void GuiTests::advancedAttachmentsPersistNativeSources()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-native-attachments-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    GuiGenerationController controller;
    QVERIFY(controller.connectStorage(storage.path()));
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Home/AdvancedGenerate.qml"));
    std::unique_ptr<QObject> view(component.createWithInitialProperties({{"generation", QVariant::fromValue(&controller)}}));
    QVERIFY2(view, qPrintable(component.errorString()));
    auto *draft = view->findChild<AdvancedImageParameters *>("advancedParameterStore");
    QVERIFY(draft);
    const auto png = storage.filePath("Files/reference.png");
    QImage pixels(19, 11, QImage::Format_ARGB32); pixels.fill(QColor(20, 40, 80, 128));
    QVERIFY(pixels.save(png));
    QVariant accepted;
    QVERIFY(QMetaObject::invokeMethod(view.get(), "addReferenceImage", Q_RETURN_ARG(QVariant, accepted),
        Q_ARG(QVariant, QUrl::fromLocalFile(png))));
    QVERIFY(accepted.toBool());
    const auto native = QUrl(draft->parameters().value("referenceImages").toList().first().toString());
    QVERIFY(native.toLocalFile().endsWith(".iisc")); QVERIFY(QFileInfo::exists(native.toLocalFile()));
    const auto control = draft->addControlNet("Canny");
    QVERIFY(QMetaObject::invokeMethod(view.get(), "selectControlImage", Q_RETURN_ARG(QVariant, accepted),
        Q_ARG(QVariant, control), Q_ARG(QVariant, "imageSource"), Q_ARG(QVariant, QUrl::fromLocalFile(png))));
    QVERIFY(accepted.toBool());
    QCOMPARE(QUrl(draft->parameters().value("controlNets").toList().first().toMap().value("imageSource").toString()), native);
    QVERIFY(QFile::remove(png));
    const QImage preview(controller.imagePreview(native).toLocalFile());
    QCOMPARE(preview.size(), pixels.size()); QCOMPARE(preview.pixelColor(1, 1), pixels.pixelColor(1, 1));
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
    click(&window, visualItem(add, "panelRow_navigation"));
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
    click(&window, visualItem(visualItem(panel, "controlNetHeader1"), "panelRow_navigation"));
    QVERIFY(!visualItem(panel, "controlProcess1")->isVisible());
    click(&window, visualItem(visualItem(panel, "controlNetHeader1"), "panelRow_navigation"));
    QVERIFY(visualItem(panel, "controlProcess1")->isVisible());
    QCOMPARE(draft->parameters().value("controlNets").toList().first().toMap().value("weight").toDouble(), 0.65);
    scrollTo(add);
    click(&window, visualItem(add, "panelRow_navigation"));
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/advanced-workspace-XXXXXX.societycontainer");
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/advanced-canvas-XXXXXX.societycontainer");
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
    QCOMPARE(nativeCanvas->document()->layers.size(), 2u);
    QCOMPARE(nativeCanvas->editingWidth(), 128);
    QCOMPARE(nativeCanvas->editingHeight(), 64);
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/history-gui-XXXXXX.societycontainer");
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
    QVERIFY(m_societyUrl.isEmpty());
    QTRY_COMPARE(window->property("storageBrowserSection").toString(), "generation-history");
    auto *browser = item(window, "storageBrowser"); QVERIFY(browser && browser->isVisible());
    QCOMPARE(listProperty(browser, "sectionFiles").size(), 25);
    click(window, item(browser, "storageBrowserBack"));
    window->setProperty("resultVisible", true);
    QTRY_VERIFY(!history->isVisible());
    window->setProperty("resultVisible", false);
    QTRY_VERIFY(history->isVisible());
    QTRY_COMPARE(cards->property("count").toInt(), 20);
    QVERIFY(QDir(storage.filePath("Generation History")).entryList(QDir::Files).size() == 25);
}

void GuiTests::historyShowsAnEmptyState()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/empty-history-gui-XXXXXX.societycontainer");
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/mobile-home-gui-XXXXXX.societycontainer");
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/desktop-home-gui-XXXXXX.societycontainer");
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

void GuiTests::editorSidebarKeepsSharedWindowLayout()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/sidebar-layout-gui-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    auto *home = item(window, "desktopHome");
    auto *homeSidebar = item(window, "desktopSidebar");
    auto *content = item(window, "appContent");
    auto *prompt = item(window, "quickGenerate");
    QVERIFY(home && homeSidebar && content && prompt);
    QVERIFY(!item(window, "canvasEditor")); // Preserve lazy Editor startup.
    prompt->setProperty("prompt", "Keep this brief across editor navigation");

    const QStringList actions{"home", "canvas", "image", "video", "audio", "board", "tools", "files", "assets", "history"};
    const auto row = [](QQuickItem *sidebar, const QString &action) {
        return visualItem(sidebar, qPrintable("desktopAction_" + action));
    };
    const auto icon = [](QQuickItem *action) {
        return visualItem(action, action->objectName() == "desktopAction_home"
            ? "iconButton_icon" : "menuItem_iconImage");
    };
    for (const int width : {1374, 900, 899, 800, 700, 699, 640, 1374}) {
        window->resize(width, width == 640 ? 480 : 800);
        QTRY_COMPARE(home->width(), qreal(width));
        QTRY_COMPARE(homeSidebar->width(), width < 700 ? 48.0 : 181.0);
        QTRY_COMPARE(row(homeSidebar, "home")->mapToItem(homeSidebar, QPointF()).y(), 8.0);
        const auto windowGeometry = window->geometry();
        const auto frameGeometry = window->frameGeometry();
        const auto contentBounds = bounds(content, window->contentItem());
        const auto sidebarBounds = bounds(homeSidebar, window->contentItem());
        const auto titleBarControls = window->property("nativeTitleBarControlsRect").toRectF();
        const auto homeWindowImage = window->grabWindow();
        QVERIFY(!homeWindowImage.isNull());
        const QPointF samplePoint = homeSidebar->mapToScene(
            {homeSidebar->width() / 2, homeSidebar->height() - 20});
        const QPoint samplePixel = (samplePoint * (qreal(homeWindowImage.width()) / window->width())).toPoint();
        QHash<QString, QRectF> rowBounds, labelBounds, iconBounds;
        for (const auto &action : actions) {
            auto *homeRow = row(homeSidebar, action);
            QVERIFY(homeRow && icon(homeRow));
            // Compare layout independently of LVRS hover/focus deformation.
            QVERIFY(homeRow->setProperty("motionEnabled", false));
            QTRY_COMPARE(bounds(visualItem(homeRow, "menuItem_labelNode"), homeRow).left(), 30.0);
            rowBounds.insert(action, bounds(homeRow, window->contentItem()));
            labelBounds.insert(action, bounds(visualItem(homeRow, "menuItem_labelNode"), homeRow));
            iconBounds.insert(action, bounds(icon(homeRow), homeRow));
        }
        const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
        if (!captureDirectory.isEmpty() && width == 800) {
            QVERIFY(QDir().mkpath(captureDirectory));
            const auto capture = homeSidebar->grabToImage();
            QVERIFY(capture);
            QTRY_VERIFY(!capture->image().isNull());
            QVERIFY(capture->image().save(captureDirectory + "/home-sidebar.png"));
        }
        // Use the production navigation entry instead of toggling editorVisible.
        click(window, row(homeSidebar, "tools"));
        QTRY_VERIFY(window->property("editorVisible").toBool());
        auto *editor = item(window, "canvasEditor");
        auto *editorSidebar = item(editor, "editorSidebar");
        QVERIFY(editor && editorSidebar && editorSidebar->isVisible());
        QTRY_COMPARE(bounds(editorSidebar, window->contentItem()), sidebarBounds);
        QCOMPARE(window->geometry(), windowGeometry);
        QCOMPARE(window->frameGeometry(), frameGeometry);
        QCOMPARE(bounds(content, window->contentItem()), contentBounds);
        QCOMPARE(window->property("nativeTitleBarControlsRect").toRectF(), titleBarControls);
        for (const auto &action : actions) {
            auto *editorRow = row(editorSidebar, action);
            auto *homeRow = row(homeSidebar, action);
            QVERIFY(editorRow && icon(editorRow));
            QVERIFY(editorRow->setProperty("motionEnabled", false));
            QTRY_COMPARE(bounds(editorRow, window->contentItem()), rowBounds.value(action));
            QCOMPARE(bounds(visualItem(editorRow, "menuItem_labelNode"), editorRow), labelBounds.value(action));
            QCOMPARE(bounds(icon(editorRow), editorRow), iconBounds.value(action));
            QCOMPARE(icon(editorRow)->property("source").toUrl(), icon(homeRow)->property("source").toUrl());
            QCOMPARE(editorRow->property("label"), homeRow->property("label"));
        }
        for (const auto &divider : {"desktopDivider_divider1", "desktopDivider_divider2"})
            QCOMPARE(bounds(visualItem(editorSidebar, divider), editorSidebar),
                     bounds(visualItem(homeSidebar, divider), homeSidebar));
        auto *workspace = item(editor, "editorWorkspace");
        QCOMPARE(bounds(workspace, editor).left(), editorSidebar->width());
        // The shared sidebar must expose the same window material as Home.
        const auto homeCapture = homeSidebar->grabToImage();
        const auto editorCapture = editorSidebar->grabToImage();
        QVERIFY(homeCapture && editorCapture);
        QTRY_VERIFY(!homeCapture->image().isNull() && !editorCapture->image().isNull());
        QCOMPARE(homeSidebar->property("backgroundColor").value<QColor>(), QColor("transparent"));
        QCOMPARE(editorSidebar->property("backgroundColor").value<QColor>(), QColor("transparent"));
        const auto editorWindowImage = window->grabWindow();
        QCOMPARE(editorWindowImage.pixelColor(samplePixel), homeWindowImage.pixelColor(samplePixel));
        auto *workBackground = item(editor, "editorWorkAreaBackground");
        QVERIFY(workBackground);
        QCOMPARE(bounds(workBackground, editor).left(), editorSidebar->width());
        QCOMPARE(bounds(workBackground, editor).right(), editor->width());
        if (!captureDirectory.isEmpty() && width == 800) {
            QVERIFY(editorCapture->image().save(captureDirectory + "/editor-sidebar.png"));
            QVERIFY(window->grabWindow().save(captureDirectory + "/editor-window.png"));
        }
        // Editor -> Image -> Video -> Home retains one sidebar geometry.
        click(window, row(editorSidebar, "image"));
        QTRY_VERIFY(home->isVisible());
        QCOMPARE(home->property("selectedAction").toString(), "image");
        QCOMPARE(bounds(homeSidebar, window->contentItem()), sidebarBounds);
        click(window, row(homeSidebar, "video"));
        QCOMPARE(home->property("selectedAction").toString(), "video");
        QCOMPARE(bounds(homeSidebar, window->contentItem()), sidebarBounds);
        click(window, row(homeSidebar, "home"));
        QCOMPARE(prompt->property("prompt").toString(), "Keep this brief across editor navigation");
    }
    // Exercise the fullscreen content inset without moving native macOS Spaces.
    if (QGuiApplication::platformName() == "offscreen") {
        QVERIFY(window->setProperty("windowChromeInteractionsEnabled", false));
        window->showFullScreen();
        QTRY_COMPARE(window->visibility(), QWindow::FullScreen);
        const auto top = content->mapToScene(QPointF()).y();
        click(window, row(homeSidebar, "tools"));
        QTRY_VERIFY(window->property("editorVisible").toBool());
        QTRY_COMPARE(content->mapToScene(QPointF()).y(), top);
        click(window, row(item(window, "editorSidebar"), "home"));
        QTRY_VERIFY(home->isVisible());
        QCOMPARE(content->mapToScene(QPointF()).y(), top);
    }
}

void GuiTests::canvasToolFeedbackFollowsNativePointer()
{
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window; window.resize(1280, 800);
    editor->setParentItem(window.contentItem()); editor->setSize(window.size()); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window)); window.requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    auto *feedback = item(editor.data(), "editorCanvasFeedback");
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *sheet = editor->findChild<QObject *>("editorToolSheet");
    QVERIFY(canvas && feedback && toolbar && sheet);
    QCOMPARE(feedback->acceptedMouseButtons(), Qt::NoButton);
    QVERIFY(canvas->createCanvas({{"width", 96}, {"height", 96}, {"unit", "px"}, {"background", "Transparent"}}));
    canvas->setZoom(3);
    const auto choose = [&](const QString &key) {
        const auto tools = listProperty(toolbar, "tools");
        for (int index = 0; index < tools.size(); ++index) if (tools[index].toMap()["key"] == key)
            return QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index));
        return false;
    };
    const auto set = [&](const QString &field, const QVariant &value) {
        return QMetaObject::invokeMethod(sheet, "setValue", Q_ARG(QVariant, field), Q_ARG(QVariant, value));
    };
    const auto point = [&](QPointF document) {
        return canvas->mapToScene({canvas->panX() + (document.x() + canvas->editingOrigin().x() - canvas->canvasOriginX()) * canvas->zoom(),
            canvas->panY() + (document.y() + canvas->editingOrigin().y() - canvas->canvasOriginY()) * canvas->zoom()}).toPoint();
    };
    const auto hasOutlinePixels = [&](const QImage &image) {
        int light = 0, dark = 0;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
            const auto color = image.pixelColor(x, y);
            if (color.alpha() < 128) continue;
            if (color.red() > 150 && color.green() > 150 && color.blue() > 150) ++light;
            if (color.red() < 100 && color.green() < 100 && color.blue() < 100) ++dark;
        }
        return light > 5 && dark > 5;
    };
    QVERIFY(choose("select"));
    QVERIFY(set("selector", "Rectangle")); QVERIFY(set("field-4", "Free"));
    QVERIFY(set("field-5", false)); QVERIFY(set("field-6", 0)); QVERIFY(set("field-2", 0));
    QTRY_COMPARE(canvas->toolValues("select")["selector"].toString(), "Rectangle");
    QTRY_VERIFY(feedback->isVisible());
    QVERIFY(canvas->isVisible());
    QCOMPARE(feedback->property("canvas").value<EditorCanvas *>(), canvas);
    const auto bytes = iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
    QTest::mousePress(&window, Qt::LeftButton, {}, point({16, 16}));
    QTest::mouseMove(&window, point({72, 58}), 30);
    QTRY_VERIFY(canvas->interactionFeedback()["gestureActive"].toBool());
    QVERIFY(!canvas->interactionFeedback()["gestureBounds"].toRectF().isEmpty());
    auto capture = feedback->grabToImage(); QVERIFY(capture); QTRY_VERIFY(!capture->image().isNull());
    const auto diagnosticDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!hasOutlinePixels(capture->image()) && !diagnosticDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(diagnosticDirectory)); capture->image().save(diagnosticDirectory + "/feedback-diagnostic.png");
        qInfo() << "Feedback geometry" << feedback->size() << canvas->interactionFeedback();
    }
    QVERIFY(hasOutlinePixels(capture->image()));
    QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, bytes);
    const auto directory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!directory.isEmpty()) { QVERIFY(QDir().mkpath(directory)); QVERIFY(window.grabWindow().save(directory + "/rectangle-drag.png")); }
    QTest::mouseRelease(&window, Qt::LeftButton, {}, point({72, 58}));
    QTRY_VERIFY(!canvas->interactionFeedback()["selectionBounds"].toRectF().isEmpty());
    QVERIFY(set("selector", "Lasso")); QVERIFY(set("field-1", 0));
    QTest::mousePress(&window, Qt::LeftButton, {}, point({20, 20}));
    QTest::mouseMove(&window, point({64, 24}), 20); QTest::mouseMove(&window, point({58, 64}), 20);
    QTRY_VERIFY(canvas->interactionFeedback()["gesturePoints"].toInt() >= 3);
    capture = feedback->grabToImage(); QVERIFY(capture); QTRY_VERIFY(!capture->image().isNull());
    QVERIFY(hasOutlinePixels(capture->image()));
    if (!directory.isEmpty()) QVERIFY(window.grabWindow().save(directory + "/lasso-drag.png"));
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY(!canvas->interactionFeedback()["gestureActive"].toBool());
    QTest::mouseRelease(&window, Qt::LeftButton, {}, point({58, 64}));
    QVERIFY(choose("brush")); QVERIFY(set("field-4", 24)); QVERIFY(set("field-6", false)); QVERIFY(set("field-7", "Round"));
    canvas->clearAreaSelection(); QTest::mouseMove(&window, point({48, 48}));
    QTRY_COMPARE(canvas->interactionFeedback()["cursorBounds"].toRectF().width(), 24 * canvas->zoom());
    auto *paintSurface = item(feedback, "canvasFeedbackSurface");
    QVERIFY(paintSurface);
    QVERIFY(paintSurface->width() <= 128 && paintSurface->height() <= 128);
    capture = feedback->grabToImage(); QVERIFY(capture); QTRY_VERIFY(!capture->image().isNull());
    QVERIFY(hasOutlinePixels(capture->image()));
    if (!directory.isEmpty()) { QVERIFY(capture->image().save(directory + "/brush-cursor-overlay.png")); QVERIFY(window.grabWindow().save(directory + "/brush-cursor.png")); }
    QTest::mouseMove(&window, QPoint(20, 20));
    QTRY_VERIFY(!canvas->interactionFeedback()["pointerVisible"].toBool());
    QVERIFY(canvas->cursor().shape() != Qt::BlankCursor);
    QVERIFY(canvas->addPaintLayer());
    QVERIFY(choose("background")); QVERIFY(set("backgroundView", "Mask"));
    QVERIFY(set("maskTool", "Gradient")); QVERIFY(set("maskOverlay", false));
    QTRY_VERIFY(feedback->isVisible());
    QTest::mousePress(&window, Qt::LeftButton, {}, point({16, 48}));
    QTest::mouseMove(&window, point({72, 48}), 30);
    QTRY_VERIFY(canvas->interactionFeedback()["gestureActive"].toBool());
    QVERIFY(paintSurface->isVisible() && paintSurface->height() > 0);
    capture = feedback->grabToImage(); QVERIFY(capture); QTRY_VERIFY(!capture->image().isNull());
    QVERIFY(hasOutlinePixels(capture->image()));
    if (!directory.isEmpty()) QVERIFY(window.grabWindow().save(directory + "/horizontal-mask-guide.png"));
    QTest::keyClick(&window, Qt::Key_Escape); QTest::mouseRelease(&window, Qt::LeftButton, {}, point({72, 48}));
}

void GuiTests::desktopHomeRendersFigmaFrame()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/desktop-render-gui-XXXXXX.societycontainer");
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

void GuiTests::homeCanvasPreviews_data()
{
    QTest::addColumn<QString>("target");
    QTest::newRow("desktop") << QStringLiteral("macos");
    QTest::newRow("mobile") << QStringLiteral("ios");
}

void GuiTests::homeCanvasPreviews()
{
    QFETCH(QString, target);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/home-previews-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    iiSharedCanvas::Document document;
    document.extent = {160, 80};
    document.assets.emplace_back(iiSharedCanvas::RasterAsset{"paint", makeRasterLayer(160, 80, 0xffff0000U)});
    document.layers.emplace_back(iiSharedCanvas::StaticBitmapLayer{{"layer", "Layer"}, iiSharedCanvas::StaticSource{"paint"}});
    const auto snapshotPath = storage.filePath("Files/Snapshot.iisc");
    const auto encoded = iiSharedCanvas::encodeIisc(document); QVERIFY(encoded.ok());
    QFile snapshot(snapshotPath); QVERIFY(snapshot.open(QIODevice::WriteOnly));
    snapshot.write(reinterpret_cast<const char *>(encoded.bytes.data()), qint64(encoded.bytes.size())); snapshot.close();
    const auto workingPath = storage.filePath("Files/Working.iisc");
    iiSharedCanvas::DocumentFile owner; QVERIFY(owner.create(workingPath.toStdString(), document).ok());
    QQmlApplicationEngine engine;
    QQmlComponent setup(&engine);
    setup.setData("import QtQuick; import LVRS 1.0 as LV; QtObject { Component.onCompleted: LV.Theme.targetOverride = '" + target.toUtf8() + "' }", {});
    std::unique_ptr<QObject> theme(setup.create()); QVERIFY2(theme, qPrintable(setup.errorString()));
    const auto resetTheme = qScopeGuard([&] {
        QQmlComponent reset(&engine); reset.setData("import QtQuick; import LVRS 1.0 as LV; QtObject { Component.onCompleted: LV.Theme.targetOverride = '' }", {});
        std::unique_ptr<QObject> restored(reset.create());
    });
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml")); QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->resize(target == "ios" ? 430 : 1374, 900);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *model = window->findChild<QObject *>("generationHistoryModel"); QVERIFY(model);
    QTRY_COMPARE(model->property("recentFiles").toList().size(), 2);
    const auto prefix = target == "ios" ? QStringLiteral("recentFileCard") : QStringLiteral("desktopRecentFile");
    auto *home = item(window, target == "ios" ? "mobileHome" : "desktopHome"); QVERIFY(home);
    QQuickItem *first = nullptr;
    for (int index = 0; index < 2; ++index) {
        QQuickItem *card = nullptr;
        QTRY_VERIFY((card = visualItem(home, qPrintable(prefix + QString::number(index)))));
        QTRY_COMPARE_WITH_TIMEOUT(card->property("previewStatus").toInt(), 1, 15000);
        QCOMPARE(card->property("previewSource").toUrl().host(), QStringLiteral("iisc-preview"));
        if (!index) first = card;
    }
    const auto painted = window->grabWindow();
    const auto sample = first->mapToScene(QPointF(first->width() / 2, 32)) * window->devicePixelRatio();
    const auto color = painted.pixelColor(sample.toPoint());
    QVERIFY2(color.red() > color.blue() + 80, "the real card must paint module artwork, not the blue placeholder icon");
    const auto evidence = qEnvironmentVariable("DREAMSCAPES_PREVIEW_EVIDENCE");
    if (!evidence.isEmpty()) { QVERIFY(QDir().mkpath(evidence)); QVERIFY(window->grabWindow().save(evidence + "/home-" + target + ".png")); }
    QFile original(snapshotPath); QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray(reinterpret_cast<const char *>(encoded.bytes.data()), qsizetype(encoded.bytes.size())));
}

void GuiTests::desktopHomeContinuousRowsAndPromptStarters()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/home-rows-XXXXXX.societycontainer");
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
    const auto references = parameters.value("referenceImages").toList();
    QCOMPARE(references.size(), 1);
    QVERIFY(!QImage(QUrl(references.first().toString()).toLocalFile()).isNull());
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/desktop-search-gui-XXXXXX.societycontainer");
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
    m_societyUrl.clear();
    click(window, visualItem(home, "desktopAction_history"));
    QVERIFY(m_societyUrl.isEmpty());
    QTRY_COMPARE(window->property("storageBrowserSection").toString(), "generation-history");
    QCOMPARE(listProperty(item(window, "storageBrowser"), "sectionFiles").size(), 25);
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/foreground-gui-XXXXXX.societycontainer");
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
    QTemporaryDir images(DREAMSCAPES_TEST_DIRECTORY "/result-menu-XXXXXX.societycontainer");
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
    QTemporaryDir images(DREAMSCAPES_TEST_DIRECTORY "/result-save-dialog-XXXXXX.societycontainer");
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
    QTemporaryDir images(DREAMSCAPES_TEST_DIRECTORY "/result-photos-XXXXXX.societycontainer");
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
            auto *cancel = item(window, "cancelGenerationButton");
            QTRY_VERIFY(cancel->mapToScene(QPointF()).y() >= handleBottom());
            click(window, cancel);
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/window-drag-XXXXXX.societycontainer");
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-reference-result-XXXXXX.societycontainer");
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-live-generation-XXXXXX.societycontainer");
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
    auto *generate = item(quick, "generateButton");
    QSignalSpy submissions(controller, &GenerationController::submissionQueued);
    QVERIFY(quick->setProperty("prompt", "live-gui"));
    click(window, item(window, "generateButton"));
    // Transition on accepted submission, before a process starts or produces a file.
    QVERIFY(window->property("resultVisible").toBool());
    QVERIFY(controller->latestImage().isEmpty());
    QVERIFY(controller->previewImage().isEmpty());
    QVERIFY(!generate->isEnabled());
    QVERIFY(!item(window, "resultBackButton"));
    QVERIFY(!item(window, "newProjectButton"));
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
        QVERIFY(!generate->isEnabled());
        QVERIFY(item(window, "resultStatus")->property("text").toString().contains(QString::number(step)));
        if (step == 1) {
            QVERIFY(quick->setProperty("prompt", "next image draft"));
            click(window, generate);
            QVERIFY(QMetaObject::invokeMethod(quick, "submit"));
            QVERIFY(QMetaObject::invokeMethod(quick, "focusPrompt"));
            QTest::keyClick(window, Qt::Key_Return);
            QCOMPARE(submissions.size(), 1);
            QCOMPARE(controller->jobs().size(), 1);
        }
        QFile nextFrame(QFileInfo(controller->previewImage().toLocalFile()).dir().filePath(QString("continue-%1").arg(step)));
        QVERIFY(nextFrame.open(QIODevice::WriteOnly));
        nextFrame.close();
    }
    QTRY_VERIFY_WITH_TIMEOUT(!controller->latestImage().isEmpty(), 10000);
    QTRY_COMPARE(preview->property("source").toUrl(), controller->latestImage());
    QTRY_VERIFY(generate->isEnabled());
    QCOMPARE(quick->property("prompt").toString(), "next image draft");
    QCOMPARE(item(window, "quickGenerate"), quick);
    QVERIFY(controller->previewImage().isEmpty());
}

void GuiTests::resultCancelsTheActiveGeneration_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<QString>("phase");
    for (const auto &target : {QString("macos"), QString("ios")})
        for (const auto &phase : {QString("queued"), QString("loading"), QString("preview"), QString("native")})
            QTest::newRow(qPrintable(target + '-' + phase)) << target << phase;
}

void GuiTests::resultCancelsTheActiveGeneration()
{
    QFETCH(QString, target);
    QFETCH(QString, phase);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-cancel-generation-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/model.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly)); model.write("fixture"); model.close();
    std::atomic_int nativeCalls{0};
    std::atomic_bool cancellationObserved{false};
    std::atomic_bool releaseNative{false};
    GenerationRuntime runtime{qEnvironmentVariable("IILD_GENERATOR_EXECUTABLE"), "auto", 10, 1024, {}};
    runtime.nativeInference = phase == "native";
    runtime.nativeGenerate = [&](const auto &request, const auto &, const auto &cancelled,
                                 const auto &progress, const auto &preview) {
        ++nativeCalls;
        progress({iiLocalDiffusion::NativeGenerationStage::Denoising, 1, request.steps});
        iiLocalDiffusion::NativeGenerationPreview frame;
        frame.width = frame.height = 64;
        frame.step = frame.sequence = 1;
        frame.total = request.steps;
        frame.rgb.resize(64 * 64 * 3, 100);
        preview(frame);
        QElapsedTimer deadline;
        deadline.start();
        while (!cancelled.load() && deadline.elapsed() < 10000) QThread::msleep(5);
        cancellationObserved = cancelled.load();
        // Keep the worker alive until the test has verified the immediate Home route.
        while (cancelled.load() && !releaseNative.load() && deadline.elapsed() < 10000) QThread::msleep(5);
        iiLocalDiffusion::NativeGenerationResult result;
        result.cancelled = cancelled.load();
        return result;
    };
    QScopedValueRollback<std::optional<GenerationRuntime>> override(guiRuntimeOverride, runtime);
    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", target));
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    window->resize(target == "ios" ? QSize(390, 844) : QSize(960, 720));
    QVERIFY(window && QTest::qWaitForWindowExposed(window));
    auto *controller = window->findChild<GenerationController *>("generationController");
    auto *quick = item(window, "quickGenerate");
    auto *generate = item(quick, "generateButton");
    QVERIFY(controller && quick && generate);
    const auto prompt = phase == "preview" ? "live-hold" : "hold";
    QVERIFY(quick->setProperty("prompt", prompt));
    QVERIFY(quick->setProperty("generationCount", 3));
    QVERIFY(quick->setProperty("aspectRatio", "3:4"));
    QSignalSpy submissions(controller, &GenerationController::submissionQueued);
    click(window, generate);
    QCOMPARE(submissions.size(), 1);
    QCOMPARE(controller->jobs().size(), 3);
    QVERIFY(window->property("resultVisible").toBool());
    QVERIFY(!generate->isEnabled());
    auto *cancel = item(window, "cancelGenerationButton");
    QVERIFY(cancel && cancel->isVisible() && cancel->isEnabled());
    if (phase == "queued") {
        for (const auto &job : controller->jobs()) QCOMPARE(job.toMap().value("state").toString(), "queued");
        // No event-loop turn may start a worker before cancellation of a queued batch.
        QVERIFY(QMetaObject::invokeMethod(cancel, "clicked"));
    } else {
        controller->setForeground(true);
        QTRY_VERIFY(controller->busy());
        if (phase == "preview" || phase == "native") QTRY_VERIFY(!controller->previewImage().isEmpty());
        else QTRY_COMPARE(controller->inferenceStatus().value("state").toString(), QString("loading"));
        QVERIFY(!generate->isEnabled());
        QVERIFY(quick->setProperty("prompt", "draft typed while generating"));
        click(window, generate);
        QVERIFY(QMetaObject::invokeMethod(quick, "submit"));
        QVERIFY(QMetaObject::invokeMethod(quick, "focusPrompt"));
        QTest::keyClick(window, Qt::Key_Return);
        QCOMPARE(submissions.size(), 1);
        QCOMPARE(controller->jobs().size(), 3);
        const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
        if (!captureDirectory.isEmpty() && (phase == "preview" || phase == "native")) {
            QTRY_COMPARE(item(window, "generatedImage")->property("status").toInt(), 1);
            QVERIFY(QDir().mkpath(captureDirectory));
            QVERIFY(window->grabWindow().save(captureDirectory + '/' + target + '-' + phase + ".png"));
        }
        click(window, cancel);
    }
    // Home changes synchronously, including while the native worker unwinds.
    QVERIFY(!window->property("resultVisible").toBool());
    QVERIFY(!window->property("editorVisible").toBool());
    QVERIFY(item(window, target == "ios" ? "mobileHome" : "desktopHome")->isVisible());
    QCOMPARE(item(window, "desktopHome")->property("selectedAction").toString(), "home");
    QVERIFY(listProperty(window, "resultJobIds").isEmpty());
    QVERIFY(item(window, "generationResult")->property("imageSource").toUrl().isEmpty());
    QCOMPARE(quick->property("prompt").toString(), phase == "queued" ? prompt : "draft typed while generating");
    QCOMPARE(quick->property("aspectRatio").toString(), "3:4");
    QCOMPARE(quick->property("generationCount").toInt(), 3);
    if (phase == "native") {
        QTRY_VERIFY(cancellationObserved.load());
        QVERIFY(controller->busy());
    }
    releaseNative = true;
    QTRY_VERIFY_WITH_TIMEOUT(!controller->busy(), 5000);
    for (const auto &job : controller->jobs()) QCOMPARE(job.toMap().value("state").toString(), "cancelled");
    QVERIFY(controller->latestImage().isEmpty());
    QVERIFY(controller->completedResults().isEmpty());
    QVERIFY(QDir(storage.filePath("Generation History")).isEmpty());
    QVERIFY(!cancel->isVisible());
    QVERIFY(generate->isEnabled());
    QCOMPARE(nativeCalls.load(), phase == "native" ? 1 : 0);
    QVERIFY(QMetaObject::invokeMethod(controller, "jobsChanged"));
    QVERIFY(!window->property("resultVisible").toBool());
}

void GuiTests::countSelectionCreatesThreeImagesInSociety()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-count-XXXXXX.societycontainer");
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/result-submission-XXXXXX.societycontainer");
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
    click(window, item(window, "cancelGenerationButton"));
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/dismissed-generation-XXXXXX.societycontainer");
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
    escapeResult(window);
    QVERIFY(!window->property("resultVisible").toBool());
    QVERIFY(result->property("imageSource").toUrl().isEmpty());
    // A new accepted request must open immediately while the dismissed worker still runs.
    const auto next = controller->enqueue("live"); QVERIFY(!next.isEmpty());
    QVERIFY(window->property("resultVisible").toBool());
    QVERIFY(result->property("previewSource").toUrl().isEmpty());
    QVERIFY(result->property("generationPending").toBool());
    QCOMPARE(item(window, "resultStatus")->property("text").toString(), "Queued");
    QVERIFY(item(window, "cancelGenerationButton")->isEnabled());
    QVERIFY(!item(window, "generateButton")->isEnabled());
    click(window, item(window, "cancelGenerationButton"));
    QVERIFY(!window->property("resultVisible").toBool());
    QCOMPARE(controller->jobs().first().toMap().value("state").toString(), "cancelled");
    QCOMPARE(controller->jobs().last().toMap().value("state").toString(), "running");
    // Cancel only owns the displayed submission; the dismissed worker survives.
    QVERIFY(!controller->enqueue("live").isEmpty());
    QVERIFY(window->property("resultVisible").toBool());
    QVERIFY(result->property("previewSource").toUrl().isEmpty());
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
    escapeResult(window);
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
    QTemporaryDir images(DREAMSCAPES_TEST_DIRECTORY "/gallery-layout-XXXXXX.societycontainer");
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
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        QTest::mouseMove(window, point.toPoint());
        gallery->forceActiveFocus();
        QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), QPoint(), QPoint(0, -360),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &wheel);
        QTRY_VERIFY2(gallery->property("contentY").toReal() > 0,
            qPrintable(QString("Wheel accepted=%1, active=%2, contentHeight=%3, height=%4, count=%5")
                .arg(wheel.isAccepted()).arg(window->isActive())
                .arg(gallery->property("contentHeight").toReal()).arg(gallery->height())
                .arg(gallery->property("count").toInt())));
        QVERIFY(gallery->isVisible());
    }
    // The last image must be reachable without constructing all 1000 thumbnails.
    QVERIFY(QMetaObject::invokeMethod(gallery, "cancelFlick"));
    QTRY_VERIFY(QMetaObject::invokeMethod(gallery, "positionViewAtEnd")
                && gallery->property("contentY").toReal() > 0);
    QTRY_VERIFY(QMetaObject::invokeMethod(gallery, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, tile), Q_ARG(int, count - 1)) && tile);
    QTRY_VERIFY(tile->property("imageReady").toBool());
    const auto contentY = gallery->property("contentY").toReal();
    const auto *content = gallery->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    // GridView retains a small pool when jumping between distant pages.
    if (count == 1000) QVERIFY(content->childItems().size() < 200);
    QTRY_VERIFY(gallery->boundingRect().contains(bounds(tile, gallery)));
    QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier,
                      tile->mapToScene(tile->boundingRect().center()).toPoint(), 50);
    QTRY_VERIFY(!gallery->isVisible());
    auto *detail = item(window, "generatedImage");
    QTRY_COMPARE(detail->property("status").toInt(), 1);
    QCOMPARE(detail->property("source").toUrl(), results.last().toMap().value("imageSource").toUrl());
    QCOMPARE(detail->property("fillMode").toInt(), 1);
    QVERIFY(detail->height() > 242);
    QSignalSpy projectRequests(result, SIGNAL(imagesRequested(QVariant)));
    editResultImage(window, count - 1);
    QCOMPARE(projectRequests.size(), 1);
    QCOMPARE(projectRequests.first().at(0).toList(), QVariantList{results.last()});
    QVERIFY(item(window, "canvasEditor")->isVisible());
    click(window, editorBackControl(window));
    QTRY_VERIFY(result->isVisible());
    QVERIFY(QMetaObject::invokeMethod(item(result, "resultImageSelection"), "reset"));
    QVERIFY(QMetaObject::invokeMethod(result, "selectImage", Q_ARG(QVariant, count - 1), Q_ARG(QVariant, true)));
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
    escapeResult(window);
    QTRY_VERIFY(gallery->isVisible());
    QVERIFY(window->property("resultVisible").toBool());
    QTRY_VERIFY(qAbs(gallery->property("contentY").toReal() - contentY) <= 1);
    QCOMPARE(item(window, "quickGenerate"), quick);
    QCOMPARE(quick->property("prompt").toString(), "Next image draft");
    window->resize(QSize(640, 640));
    QTRY_VERIFY(gallery->property("columns").toInt() != columns || size.width() == 640);
    QVERIFY(gallery->property("contentWidth").toReal() <= gallery->width() + 1);
    click(window, item(window, "cancelGenerationButton"));
    QTRY_VERIFY(!window->property("resultVisible").toBool());
}

void GuiTests::generateButtonUsesSocietyStorage()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-generation-XXXXXX.societycontainer");
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
    QVERIFY(controller->latestResult().value("imageSource").toUrl().toLocalFile().startsWith(storage.filePath("Generation History/")));
    QVERIFY(controller->latestImage().toLocalFile().endsWith(".png"));
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
    QSignalSpy projectRequests(result, SIGNAL(imagesRequested(QVariant)));
    QVERIFY(projectRequests.isValid());
    editResultImage(window, 0);
    QCOMPARE(projectRequests.size(), 1);
    const auto projectInput = projectRequests.first().at(0).toList().first().toMap();
    QCOMPARE(projectInput.value("imageSource").toUrl(), controller->latestImage());
    QCOMPARE(projectInput.value("id"), controller->jobs().first().toMap().value("id"));
    QCOMPARE(projectInput.value("prompt").toString(), "slow another image");
    QCOMPARE(projectInput.value("aspectRatio").toString(), "16:9");
    QVERIFY(item(window, "canvasEditor")->isVisible());
    click(window, editorBackControl(window));
    QTRY_VERIFY(result->isVisible());
    QVERIFY(QMetaObject::invokeMethod(item(result, "resultImageSelection"), "reset"));

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
    click(window, item(window, "cancelGenerationButton"));
    QTRY_VERIFY(!window->property("resultVisible").toBool());
    QTRY_COMPARE(item(window, "desktopHome")->property("selectedAction").toString(), "home");
    QTRY_COMPARE(item(window, "desktopHomeViewport")->property("contentY").toReal(), 0.0);
    QVERIFY(quick->mapToScene(QPointF()).y() >= window->property("contentTopInset").toReal());
    QCOMPARE(item(window, "quickGenerate"), quick);
    QCOMPARE(quick->property("prompt").toString(), "an unfinished draft");
    QCOMPARE(quick->property("aspectRatio").toString(), "9:16");
    // Background notifications for the same result must not undo Back navigation.
    QVERIFY(QMetaObject::invokeMethod(controller, "jobsChanged"));
    QVERIFY(!window->property("resultVisible").toBool());
    // Returning from a bottom-aligned panel must keep the home controls reachable.
    QTRY_COMPARE(quick->height(), quick->implicitHeight());
    auto *storagePanel = item(window, "storagePanel");
    click(window, item(window, "desktopGenerationSettings"));
    QTRY_VERIFY(storagePanel->isVisible());
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
    QTemporaryDir otherStorage(DREAMSCAPES_TEST_DIRECTORY "/result-other-storage-XXXXXX.societycontainer");
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
    QTemporaryDir images(DREAMSCAPES_TEST_DIRECTORY "/result-layout-XXXXXX.societycontainer");
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
    auto *cancel = item(window, "cancelGenerationButton");
    auto *select = item(window, "resultImageSelectionToggle");
    QVERIFY(result && preview && cancel && select);
    QVERIFY(!item(window, "resultBackButton"));
    QVERIFY(!item(window, "newProjectButton"));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE(preview->property("status").toInt(), 1);
    const auto top = window->property("contentTopInset").toReal();
    const auto bottom = window->property("mobileSystemSafeBottomInset").toReal();
    const auto left = window->property("mobileSystemSafeLeftInset").toReal();
    const auto right = window->property("mobileSystemSafeRightInset").toReal();
    QTRY_COMPARE(quick->mapToScene(QPointF(0, quick->height())).y(), size.height() - bottom);
    QCOMPARE(cancel->mapToScene(QPointF()).y(), top);
    QCOMPARE(select->mapToScene(QPointF()).y(), top);
    QCOMPARE(select->mapToScene(QPointF(select->width(), 0)).x(), size.width() - right);
    QCOMPARE(cancel->height(), 22.0);
    QCOMPARE(select->height(), 22.0);
    QCOMPARE(preview->width(), size.width() - left - right);
    QCOMPARE(preview->height(), 242.0);
    QCOMPARE(preview->property("fillMode").toInt(), 1); // Image.PreserveAspectFit
    QVERIFY(preview->clip());
    const auto imageScale = qMin(preview->width() / imageSize.width(), preview->height() / imageSize.height());
    QVERIFY(qAbs(preview->property("paintedWidth").toReal() - imageSize.width() * imageScale) <= 0.5);
    QVERIFY(qAbs(preview->property("paintedHeight").toReal() - imageSize.height() * imageScale) <= 0.5);
    const auto upperGap = preview->mapToScene(QPointF()).y() - cancel->mapToScene(QPointF(0, cancel->height())).y();
    const auto lowerGap = quick->mapToScene(QPointF()).y() - preview->mapToScene(QPointF(0, preview->height())).y();
    QVERIFY(qAbs(upperGap - lowerGap) <= 1.0); // Qt snaps centered images to device-independent pixels.
    // The native title-bar reserve changes the available vertical gap. The
    // result composer itself must retain the compact 72px contract.
    if (size == QSize(402, 575)) QTRY_COMPARE(quick->height(), 72.0); // LVRS spacing spring settles after the Home → Result route.
    QVERIFY(cancel->isEnabled());
    QVERIFY(select->isEnabled());
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/new-canvas-XXXXXX.societycontainer");
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/new-canvas-keys-XXXXXX.societycontainer");
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/canvas-routing-XXXXXX.societycontainer");
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
    QVERIFY(quick && result);
    QVERIFY(!editor);
    QVERIFY(quick->setProperty("prompt", "unfinished draft"));
    if (target == "ios") {
        click(window, item(window, "newCanvasAction"));
        QTRY_VERIFY(item(window, "canvasCreateButton")->isVisible());
        click(window, item(window, "canvasCreateButton"));
        editor = item(window, "canvasEditor");
        QVERIFY(editor);
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
    QVERIFY(!item(window, "newProjectButton"));
    editResultImage(window, 0);
    editor = item(window, "canvasEditor");
    QVERIFY(editor);
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
    QVERIFY(QMetaObject::invokeMethod(item(result, "resultImageSelection"), "reset"));
    if (target == "ios") {
        escapeResult(window);
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
    const auto nativePath = storage.filePath("Files/canvas.iisc");
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
    QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected")));
    QFETCH(QString,target);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/multi-canvas-gui-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QVariantList results;
    const QList<QSize> sizes{{64,40},{32,60},{24,24},{48,48},{80,40}};
    const QList<QColor> colors{Qt::red,Qt::blue,Qt::cyan,Qt::green,Qt::yellow};
    for (int i=0;i<5;++i) {
        QImage image(sizes[i],QImage::Format_ARGB32); image.fill(colors[i]);
        if (i==1) {
            // Sharp stripes make the neighboring page's blur visible in native captures.
            // The lower half stays blue for the brush and persistence assertions below.
            for (int y=0;y<20;++y)
                for (int x=0;x<image.width();++x)
                    image.setPixelColor(x,y,(x/2)%2 ? Qt::white : Qt::black);
        }
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
    QVERIFY(result && gallery && selection);
    QVERIFY(!editor && !project);
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
    editor=item(window,"canvasEditor");
    project=window->findChild<EditorProject *>("editorProject");
    QVERIFY(editor && project);
    QTRY_VERIFY(editor->isVisible()); QCOMPARE(project->canvasCount(),4); QCOMPARE(project->currentIndex(),0);
    auto *previousButton=item(window,"editorPreviousCanvas"); auto *nextButton=item(window,"editorNextCanvas");
    QTRY_COMPARE(previousButton->mapToScene(previousButton->boundingRect().center()).y(),
                 nextButton->mapToScene(nextButton->boundingRect().center()).y());
    auto *deck=item(window,"editorPageDeck");
    auto *activeClip=item(window,"editorActivePageClip");
    auto *previewStrip=item(window,"editorPageStrip");
    auto *previewList=item(window,"editorPagePreviewList");
    auto *surface=item(window,"editorCanvasSurface");
    QVERIFY(deck && activeClip && previewStrip && previewList && surface);
    QVERIFY(deck->isVisible() && previewStrip->isVisible());
    QVERIFY(activeClip->clip());
    QVERIFY(previewStrip->height()<=80);
    const auto stripBounds=previewStrip->mapRectToScene(previewStrip->boundingRect());
    const auto surfaceBounds=surface->mapRectToScene(surface->boundingRect());
    QVERIFY(stripBounds.top()>=surfaceBounds.bottom()-1);
    QVERIFY(stripBounds.bottom()<=window->height());
    auto *workspaceCanvas=project->currentCanvas();
    QCOMPARE(workspaceCanvas->document()->artboards.size(),4u);
    QTRY_VERIFY(workspaceCanvas->property("pagePresentation").toBool());
    QTRY_VERIFY(!workspaceCanvas->rendering() && workspaceCanvas->residentTileCount()>0);
    const auto activeCenter=[&] {
        const auto *canvas=project->currentCanvas();
        const auto &board=canvas->document()->artboards[project->currentIndex()];
        return canvas->mapToScene({canvas->panX()+canvas->zoom()*(board.region.origin.x+board.region.extent.width/2.0-canvas->canvasOriginX()),canvas->panY()+canvas->zoom()*(board.region.origin.y+board.region.extent.height/2.0-canvas->canvasOriginY())});
    };
    QTRY_VERIFY(QLineF(activeCenter(),surface->mapToScene(surface->boundingRect().center())).length()<1);
    QTRY_COMPARE(window->grabWindow().pixelColor((activeCenter()*window->devicePixelRatio()).toPoint()),colors[0]);
    QQuickItem *nearCard=nullptr,*middleCard=nullptr,*farCard=nullptr;
    QTRY_VERIFY((nearCard=visualItem(deck,"editorPageCard1")));
    QTRY_VERIFY((middleCard=visualItem(deck,"editorPageCard2")));
    QTRY_VERIFY((farCard=visualItem(deck,"editorPageCard3")));
    QCOMPARE(nearCard->property("pageIndex").toInt(),1);
    QCOMPARE(nearCard->property("distance").toInt(),1);
    QCOMPARE(middleCard->property("distance").toInt(),2);
    QCOMPARE(farCard->property("distance").toInt(),3);
    QVERIFY(activeClip->z()>nearCard->z());
    QVERIFY(nearCard->z()>middleCard->z() && middleCard->z()>farCard->z());
    QVERIFY(nearCard->property("depthScale").toDouble()>middleCard->property("depthScale").toDouble());
    QVERIFY(middleCard->property("depthScale").toDouble()>farCard->property("depthScale").toDouble());
    QVERIFY(nearCard->property("darkenAmount").toDouble()>0);
    QVERIFY(nearCard->property("darkenAmount").toDouble()<middleCard->property("darkenAmount").toDouble());
    QVERIFY(middleCard->property("darkenAmount").toDouble()<farCard->property("darkenAmount").toDouble());
    QVERIFY(nearCard->property("blurAmount").toDouble()>0);
    QVERIFY(nearCard->property("blurAmount").toDouble()<middleCard->property("blurAmount").toDouble());
    QVERIFY(middleCard->property("blurAmount").toDouble()<farCard->property("blurAmount").toDouble());
    QQuickItem *firstPreview=nullptr,*firstPreviewImage=nullptr;
    QTRY_VERIFY((firstPreview=visualItem(previewStrip,"editorPagePreview0")));
    QTRY_VERIFY((firstPreviewImage=visualItem(previewStrip,"editorPagePreviewImage0")));
    QVERIFY(firstPreview->width()<=100 && firstPreview->height()<=76);
    QVERIFY(firstPreviewImage->width()<=80 && firstPreviewImage->height()<=48);
    QTRY_COMPARE(firstPreviewImage->property("status").toInt(),1);
    const auto previewUrl=firstPreviewImage->property("source").toUrl();
    QVERIFY(previewUrl.toString().startsWith("data:image/png;base64,"));
    const auto previewBytes=QByteArray::fromBase64(previewUrl.toString().section(',',1).toLatin1());
    const auto thumbnail=QImage::fromData(previewBytes);
    QVERIFY(!thumbnail.isNull());
    QVERIFY(thumbnail.width()<=256 && thumbnail.height()<=256);
    QCOMPARE(thumbnail.pixelColor(thumbnail.width()/2,thumbnail.height()/2),colors[0]);
    // Horizontal gestures retain the active page's camera and never navigate its neighbors.
    const auto fittedZoom=workspaceCanvas->zoom(),fittedPanX=workspaceCanvas->panX();
    QWheelEvent horizontalWheel(QPointF(30,30),QPointF(30,30),QPoint(-120,0),QPoint(),Qt::NoButton,Qt::NoModifier,Qt::ScrollUpdate,false);
    QCoreApplication::sendEvent(workspaceCanvas,&horizontalWheel);
    QVERIFY(horizontalWheel.isAccepted());
    QCOMPARE(project->currentIndex(),0); QCOMPARE(workspaceCanvas->zoom(),fittedZoom);
    QCOMPARE(workspaceCanvas->panX(),fittedPanX);
    workspaceCanvas->setZoom(fittedZoom*2);
    const auto enlargedPanX=workspaceCanvas->panX();
    QCoreApplication::sendEvent(workspaceCanvas,&horizontalWheel);
    QVERIFY(workspaceCanvas->panX()<enlargedPanX);
    QCOMPARE(project->currentIndex(),0); QCOMPARE(workspaceCanvas->zoom(),fittedZoom*2);
    workspaceCanvas->fitToView();
    QTRY_VERIFY(QLineF(activeCenter(),surface->mapToScene(surface->boundingRect().center())).length()<1);
    const auto backgroundPoint=surface->mapToScene({4,4});
    QVERIFY(!activeClip->contains(activeClip->mapFromScene(backgroundPoint)));
    const auto beforeBackgroundZoom=workspaceCanvas->zoom();
    QWheelEvent backgroundWheel(backgroundPoint,window->mapToGlobal(backgroundPoint.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QCoreApplication::sendEvent(window,&backgroundWheel);
    QTRY_VERIFY(workspaceCanvas->zoom()!=beforeBackgroundZoom);
    QCOMPARE(project->currentIndex(),0);
    workspaceCanvas->fitToView();
    QTRY_VERIFY(QLineF(activeCenter(),surface->mapToScene(surface->boundingRect().center())).length()<1);
    QTRY_VERIFY(qAbs(nearCard->width()-nearCard->property("baseWidth").toDouble()*nearCard->property("depthScale").toDouble())<0.5);
    QTRY_VERIFY(qAbs(nearCard->height()-nearCard->property("baseHeight").toDouble()*nearCard->property("depthScale").toDouble())<0.5);
    QTRY_VERIFY(qAbs(nearCard->x()+nearCard->width()
        -(activeClip->x()+activeClip->width()+(surface->width()-activeClip->x()-activeClip->width())*0.62))<0.5);
    QTRY_VERIFY(qAbs(nearCard->y()+nearCard->height()/2-activeClip->y()-activeClip->height()/2)<0.5);
    QTRY_VERIFY(nearCard->x()+nearCard->width()>activeClip->x()+activeClip->width());
    QTRY_VERIFY(middleCard->x()+middleCard->width()>nearCard->x()+nearCard->width());
    QTRY_VERIFY(farCard->x()+farCard->width()>middleCard->x()+middleCard->width());
    QTRY_VERIFY(farCard->x()+farCard->width()<=surface->width()+0.5);
    QQuickItem *nearImage=nullptr;
    QTRY_VERIFY((nearImage=visualItem(deck,"editorPageCardImage1")));
    QTRY_COMPARE(nearImage->property("status").toInt(),1);
    const auto rowCapture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (window->rendererInterface()->graphicsApi()!=QSGRendererInterface::Software) {
        QQmlProperty layerEnabled(nearImage,"layer.enabled",&engine);
        QVERIFY(layerEnabled.isValid() && layerEnabled.read().toBool());
        const auto pixelsBeforeEffectCheck=iiSharedCanvas::encodeIisc(*workspaceCanvas->document()).bytes;
        const auto imageRect=nearImage->mapRectToScene(nearImage->boundingRect());
        const auto clipRect=activeClip->mapRectToScene(activeClip->boundingRect());
        const auto canvasRect=surface->mapRectToScene(surface->boundingRect());
        const auto stripeY=imageRect.top()+imageRect.height()/6.0;
        auto stripeLeft=qMax(imageRect.left()+4,canvasRect.left()+4);
        if (stripeY>=clipRect.top() && stripeY<=clipRect.bottom())
            stripeLeft=qMax(stripeLeft,clipRect.right()+4);
        const auto stripeRight=qMin(imageRect.right()-4,canvasRect.right()-4);
        QVERIFY(stripeRight-stripeLeft>12);
        const auto gradientEnergy=[&](const QImage &capture) {
            const auto ratio=window->devicePixelRatio();
            const auto left=qCeil(stripeLeft*ratio),right=qFloor(stripeRight*ratio);
            const auto top=qRound(stripeY*ratio);
            double energy=0;
            for (int y=top-1;y<=top+1;++y)
                for (int x=left+1;x<=right;++x) {
                    const auto previous=capture.pixelColor(x-1,y),current=capture.pixelColor(x,y);
                    const auto red=current.red()-previous.red(),green=current.green()-previous.green(),blue=current.blue()-previous.blue();
                    energy+=red*red+green*green+blue*blue;
                }
            return energy/(3*(right-left));
        };
        QSignalSpy effectFrames(window,&QQuickWindow::frameSwapped);
        window->update(); QTRY_VERIFY(effectFrames.count()>0);
        const auto blurredCapture=window->grabWindow();
        QVERIFY(!blurredCapture.isNull());
        qInfo()<<"Page blur sample geometry"<<imageRect<<"active"<<clipRect<<"surface"<<canvasRect
               <<"range"<<stripeLeft<<stripeRight<<stripeY<<"ratio"<<window->devicePixelRatio()<<"capture"<<blurredCapture.size();
        const auto blurredEnergy=gradientEnergy(blurredCapture);
        QVERIFY(layerEnabled.write(false));
        effectFrames.clear(); window->update(); QTRY_VERIFY(effectFrames.count()>0);
        const auto sharpCapture=window->grabWindow();
        QVERIFY(!sharpCapture.isNull());
        const auto sharpEnergy=gradientEnergy(sharpCapture);
        if (!rowCapture.isEmpty()) {
            QVERIFY(QDir().mkpath(rowCapture));
            QVERIFY(blurredCapture.save(rowCapture+"/"+QTest::currentDataTag()+"-blur-on.png"));
            QVERIFY(sharpCapture.save(rowCapture+"/"+QTest::currentDataTag()+"-blur-off.png"));
        }
        qInfo().nospace()<<"Page preview blur edge energy: blurred="<<blurredEnergy<<" sharp="<<sharpEnergy;
        QVERIFY2(sharpEnergy>1,qPrintable(QString("The stripe fixture needs visible edges; sharp energy=%1").arg(sharpEnergy)));
        QVERIFY2(blurredEnergy<sharpEnergy*0.98,qPrintable(QString("Native blur must reduce squared RGB edge energy: blurred=%1 sharp=%2").arg(blurredEnergy).arg(sharpEnergy)));
        // The unstriped lower section is solid blue, isolating Darken from the blur comparison.
        const auto blueY=imageRect.top()+imageRect.height()*0.75;
        auto blueLeft=qMax(imageRect.left()+4,canvasRect.left()+4);
        if (blueY>=clipRect.top() && blueY<=clipRect.bottom())
            blueLeft=qMax(blueLeft,clipRect.right()+4);
        const auto blueRight=qMin(imageRect.right()-4,canvasRect.right()-4);
        QVERIFY(blueRight>blueLeft);
        const auto darkened=sharpCapture.pixelColor((QPointF((blueLeft+blueRight)/2,blueY)*window->devicePixelRatio()).toPoint());
        const auto expectedBlue=qRound(255*(1-nearCard->property("darkenAmount").toDouble()));
        QVERIFY2(qAbs(darkened.blue()-expectedBlue)<=12 && darkened.red()<=12 && darkened.green()<=12,
            qPrintable(QString("Native Darken output=%1 expected blue=%2").arg(darkened.name()).arg(expectedBlue)));
        qInfo().nospace()<<"Page preview Darken: actual="<<darkened.name()<<" expected blue="<<expectedBlue;
        QVERIFY(layerEnabled.write(true));
        QCOMPARE(iiSharedCanvas::encodeIisc(*workspaceCanvas->document()).bytes,pixelsBeforeEffectCheck);
    }
    if (!rowCapture.isEmpty()) { QVERIFY(QDir().mkpath(rowCapture)); QVERIFY(window->grabWindow().save(rowCapture+"/"+QTest::currentDataTag()+"-pages.png")); }
    // A visible neighboring page is a navigation target; only the active page edits pixels.
    const auto exposedCard=nearCard->mapRectToItem(surface,nearCard->boundingRect()).intersected(surface->boundingRect());
    QVERIFY(!exposedCard.isEmpty());
    const auto neighborPoint=surface->mapToScene({exposedCard.right()-qMin(8.0,exposedCard.width()/2),exposedCard.center().y()});
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,neighborPoint.toPoint());
    QTRY_COMPARE(project->currentIndex(),1);
    QCOMPARE(project->currentCanvas(),workspaceCanvas);
    QVERIFY(QMetaObject::invokeMethod(previewList,"positionViewAtIndex",Q_ARG(int,3),Q_ARG(int,4)));
    QQuickItem *lastPreview=nullptr;
    QTRY_VERIFY((lastPreview=visualItem(previewStrip,"editorPagePreview3")));
    QTRY_VERIFY(previewList->contains(lastPreview->mapToItem(previewList,lastPreview->boundingRect().center())));
    click(window,lastPreview); QTRY_COMPARE(project->currentIndex(),3);
    // Hidden editor routes retain the chosen page and camera while dropping preview delegates.
    workspaceCanvas->setZoom(workspaceCanvas->zoom()*1.7);
    const auto retainedZoom=workspaceCanvas->zoom(),retainedPanX=workspaceCanvas->panX(),retainedPanY=workspaceCanvas->panY();
    const auto retainedViewport=workspaceCanvas->size();
    const auto retainedDocument=iiSharedCanvas::encodeIisc(*workspaceCanvas->document()).bytes;
    for (int cycle=0;cycle<2;++cycle) {
        QVERIFY(window->setProperty("editorVisible",false));
        QTRY_VERIFY(!editor->isVisible());
        QTRY_COMPARE(previewList->property("count").toInt(),0);
        QCOMPARE(workspaceCanvas->size(),retainedViewport);
        QCOMPARE(workspaceCanvas->zoom(),retainedZoom);
        QCOMPARE(workspaceCanvas->panX(),retainedPanX); QCOMPARE(workspaceCanvas->panY(),retainedPanY);
        QCOMPARE(project->currentIndex(),3);
        QVERIFY(window->setProperty("editorVisible",true));
        QTRY_VERIFY(editor->isVisible());
        QTRY_COMPARE(previewList->property("count").toInt(),4);
        QTRY_COMPARE(previewList->property("currentIndex").toInt(),3);
        QTRY_VERIFY((lastPreview=visualItem(previewStrip,"editorPagePreview3")));
        QTRY_VERIFY(previewList->contains(lastPreview->mapToItem(previewList,lastPreview->boundingRect().center())));
        QCOMPARE(workspaceCanvas->size(),retainedViewport);
        QCOMPARE(workspaceCanvas->zoom(),retainedZoom);
        QCOMPARE(workspaceCanvas->panX(),retainedPanX); QCOMPARE(workspaceCanvas->panY(),retainedPanY);
    }
    QCOMPARE(iiSharedCanvas::encodeIisc(*workspaceCanvas->document()).bytes,retainedDocument);
    QVERIFY(project->setCurrentIndex(0));
    QCOMPARE(project->currentCanvas(),workspaceCanvas);
    auto *first=project->currentCanvas(); QCOMPARE(first->editingWidth(),64); QCOMPARE(first->editingHeight(),40);
    click(window,item(window,"editorNextCanvas")); QTRY_COMPARE(project->currentIndex(),1);
    auto *second=project->currentCanvas(); QCOMPARE(second->editingWidth(),32); QCOMPARE(second->editingHeight(),60);
    QQuickItem *secondPreviewImage=nullptr;
    QTRY_VERIFY((secondPreviewImage=visualItem(previewStrip,"editorPagePreviewImage1")));
    QTRY_COMPARE(secondPreviewImage->property("status").toInt(),1);
    const auto unpaintedPreview=secondPreviewImage->property("source").toUrl();
    second->configureTool("brush",{{"field-4",6},{"field-5",100},{"field-8",100},{"field-9",100}});
    second->setBrushColor(Qt::white); QVERIFY(second->beginStrokeAt({16,30})); QVERIFY(second->endStrokeAt({16,30}));
    QTRY_VERIFY((secondPreviewImage=visualItem(previewStrip,"editorPagePreviewImage1")) && secondPreviewImage->property("source").toUrl()!=unpaintedPreview);
    QTRY_COMPARE(secondPreviewImage->property("status").toInt(),1);
    const auto painted=iiSharedCanvas::renderArtboard(*second->document(),0,second->document()->artboards[1].id).pixels.pixels;
    QVERIFY(second->canUndo());
    item(window,"editorNextCanvas")->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_Right,Qt::AltModifier);
    QCOMPARE(project->currentIndex(),1); // Focus outside the canvas keeps navigation local to the control.
    second->forceActiveFocus();
    QTest::keyClick(window,Qt::Key_Right,Qt::AltModifier); QTRY_COMPARE(project->currentIndex(),2);
    QCOMPARE(project->currentCanvas()->editingWidth(),48);
    QTest::keyClick(window,Qt::Key_PageDown); QTRY_COMPARE(project->currentIndex(),3);
    QVERIFY(!item(window,"editorNextCanvas")->isEnabled());
    QTest::keyClick(window,Qt::Key_PageUp); QTRY_COMPARE(project->currentIndex(),2);
    click(window,item(window,"editorPreviousCanvas")); QTRY_COMPARE(project->currentIndex(),1);
    QCOMPARE(project->currentCanvas(),second); QVERIFY(second->canUndo());
    QCOMPARE(iiSharedCanvas::renderArtboard(*second->document(),0,second->document()->artboards[1].id).pixels.pixels,painted);
    QTest::keySequence(window, QKeySequence(QKeySequence::Save));
    auto *dialog=window->findChild<QObject *>("editorDocumentSaveDialog"); QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool()); QCOMPARE(dialog->property("defaultSuffix").toString(),"iisc");
    QCOMPARE(dialog->property("currentFolder").toUrl().toLocalFile(), storage.filePath("Files"));
    const auto path=storage.filePath("Files/whole project %20 #.iisc");
    QVERIFY(dialog->setProperty("selectedFile",QUrl::fromLocalFile(path)));
    QVERIFY(QMetaObject::invokeMethod(dialog,"accepted")); QVERIFY(QMetaObject::invokeMethod(dialog,"close"));
    QCOMPARE(project->filePath(),path); QVERIFY(!project->modified());
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));
        const auto artifact=capture+"/"+QTest::currentDataTag()+".iisc";
        QFile::remove(artifact);
        QVERIFY(project->saveDocumentAs(QUrl::fromLocalFile(artifact)));
        QTRY_VERIFY(!second->rendering() && second->residentTileCount()>0);
        const auto point=second->mapToScene({second->panX()+second->zoom()*(second->editingOrigin().x()-second->canvasOriginX()+16),second->panY()+second->zoom()*(second->editingOrigin().y()-second->canvasOriginY()+30)});
        QTRY_COMPARE(window->grabWindow().pixelColor((point*window->devicePixelRatio()).toPoint()),QColor::fromRgba(painted[30*32+16]));
        QVERIFY(window->grabWindow().save(capture+"/"+QTest::currentDataTag()+".png"));
    }
    QVERIFY(QMetaObject::invokeMethod(editor,"openDocumentSource",Q_ARG(QVariant,QUrl::fromLocalFile(path)),Q_ARG(QVariant,false)));
    QCOMPARE(project->canvasCount(),4); QCOMPARE(project->currentIndex(),0);
    QVERIFY(project->setCurrentIndex(1));
    QCOMPARE(iiSharedCanvas::renderArtboard(*project->currentCanvas()->document(),0,project->currentCanvas()->document()->artboards[1].id).pixels.pixels,painted);
    auto *attachDialog=window->findChild<QObject *>("editorAttachImagesDialog"); QVERIFY(attachDialog);
    QVERIFY(QMetaObject::invokeMethod(attachDialog,"open")); QTRY_VERIFY(attachDialog->property("visible").toBool());
    QCOMPARE(attachDialog->property("fileMode").toInt(),1); // FileDialog.OpenFiles.
    // selectedFile is the writable public API; selectedFiles is a read-only list.
    QVERIFY(attachDialog->setProperty("selectedFile",results[0].toMap().value("imageSource")));
    QVERIFY(QMetaObject::invokeMethod(attachDialog,"accepted")); QVERIFY(QMetaObject::invokeMethod(attachDialog,"close"));
    QVERIFY2(project->canvasCount()==5,qPrintable(project->error()+" selected="+QString::number(attachDialog->property("selectedFiles").value<QList<QUrl>>().size()))); QCOMPARE(project->currentIndex(),4); QCOMPARE(project->currentCanvas()->document()->artboards.size(),5u);
    QCOMPARE(project->filePath(),path);
    EditorProject durable; QVERIFY(durable.openDocumentSource(QUrl::fromLocalFile(path))); QCOMPARE(durable.canvasCount(),5);
    // Native documents may persist scattered or overlapping artboards. Presentation follows
    // page order and isolates the active board without rewriting their serialized content.
    auto scattered=*project->currentCanvas()->document();
    const QList<QPoint> origins{{900,900},{-600,-250},{400,-600},{10,800},{-600,-250}};
    for (int i=0;i<origins.size();++i)
        scattered.artboards[i].region.origin={origins[i].x(),origins[i].y()};
    iiSharedCanvas::recordDocumentChange(scattered);
    QVERIFY(project->currentCanvas()->loadProjectPage(std::move(scattered),"Scattered page fixture"));
    QVERIFY(project->setCurrentIndex(1));
    project->currentCanvas()->fitToView();
    const auto scatteredBytes=iiSharedCanvas::encodeIisc(*project->currentCanvas()->document()).bytes;
    QQuickItem *leftCard=nullptr,*rightCard=nullptr;
    QTRY_VERIFY((leftCard=visualItem(deck,"editorPageCard0")));
    QTRY_VERIFY((rightCard=visualItem(deck,"editorPageCard2")));
    QTRY_VERIFY(leftCard->mapToScene(leftCard->boundingRect().center()).x()<activeClip->mapToScene(activeClip->boundingRect().center()).x());
    QTRY_VERIFY(rightCard->mapToScene(rightCard->boundingRect().center()).x()>activeClip->mapToScene(activeClip->boundingRect().center()).x());
    QTRY_VERIFY(qAbs(leftCard->mapToScene(leftCard->boundingRect().center()).y()-rightCard->mapToScene(rightCard->boundingRect().center()).y())<1);
    QTRY_VERIFY(QLineF(activeCenter(),surface->mapToScene(surface->boundingRect().center())).length()<1);
    QTRY_VERIFY(!project->currentCanvas()->rendering() && project->currentCanvas()->residentTileCount()>0);
    const auto isolated=iiSharedCanvas::renderArtboard(*project->currentCanvas()->document(),0,project->currentCanvas()->document()->artboards[1].id);
    const auto *isolatedCanvas=project->currentCanvas();
    const auto isolatedCenter=QColor::fromRgba(isolated.pixels.pixels[(isolatedCanvas->editingHeight()/2)*isolatedCanvas->editingWidth()+isolatedCanvas->editingWidth()/2]);
    QTRY_COMPARE(window->grabWindow().pixelColor((activeCenter()*window->devicePixelRatio()).toPoint()),isolatedCenter);
    QVERIFY(project->setCurrentIndex(2));
    QTRY_VERIFY(QLineF(activeCenter(),surface->mapToScene(surface->boundingRect().center())).length()<1);
    QCOMPARE(iiSharedCanvas::encodeIisc(*project->currentCanvas()->document()).bytes,scatteredBytes);
    for (int i=0;i<origins.size();++i) {
        QCOMPARE(project->currentCanvas()->document()->artboards[i].region.origin.x,origins[i].x());
        QCOMPARE(project->currentCanvas()->document()->artboards[i].region.origin.y,origins[i].y());
    }
    click(window,editorBackControl(window)); QTRY_VERIFY(result->isVisible());
    QCOMPARE(listProperty(selection,"selectedImages").size(),4);
}

void GuiTests::homeEditorRoutes()
{
    QFETCH(QString, target);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/home-editor-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    EditorCanvas source;
    QVERIFY(source.createCanvas({{"width", 72}, {"height", 48}, {"unit", "px"}, {"background", "White"}}));
    const auto nativePath = storage.filePath("Files/native %20 #.iisc");
    QVERIFY(source.saveDocumentAs(QUrl::fromLocalFile(nativePath)));
    QImage red(64, 40, QImage::Format_ARGB32); red.fill(Qt::red);
    QImage blue(32, 60, QImage::Format_ARGB32); blue.fill(Qt::blue);
    const auto generatedPng = storage.filePath("Generation History/red %20 #.png");
    QVERIFY(red.save(generatedPng));
    QVERIFY(QDir(storage.filePath("Generation History")).entryList({"*.iisc"}, QDir::Files).isEmpty());
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
    QVERIFY(quick);
    QVERIFY(!editor && !canvas);
    QVERIFY(quick->setProperty("prompt", "Keep the Home draft"));
    auto *openButton = item(window, target == "macos" ? "desktopRecentFilesOpenFile" : "mobileHomeOpenFile");
    QVERIFY(openButton);
    auto *dialog = window->findChild<QObject *>("homeOpenFileDialog");
    QVERIFY(dialog);
    click(window, openButton);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QVERIFY(!item(window, "canvasEditor"));
    QVERIFY(QMetaObject::invokeMethod(window, "ensureEditor"));
    editor = item(window, "canvasEditor");
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QVERIFY(editor && canvas);
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
    QCOMPARE(QDir(storage.filePath("Generation History")).entryList({"*.iisc"}, QDir::Files).size(), 0);
    QCOMPARE(QDir(storage.filePath("Files")).entryList({"red %20 #.png.*.iisc", "blue.png.*.iisc"}, QDir::Files).size(), 2);
    QCOMPARE(canvas->document()->layers.size(), 2u);
    QCOMPARE(canvas->document()->artboards.size(), 2u);
    QCOMPARE(listProperty(editor, "generationResults").size(), 2);
    QVERIFY(canvas->filePath().isEmpty());
    const auto saved = storage.filePath("Files/selected.iisc");
    QVERIFY(project->saveDocumentAs(QUrl::fromLocalFile(saved)));
    EditorProject observer;
    QVERIFY(observer.openDocumentSource(QUrl::fromLocalFile(saved)));
    QCOMPARE(observer.canvasCount(), 2);
    const auto capture = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));
        const auto artifact = capture + "/" + QTest::currentDataTag() + ".iisc";
        QFile::remove(artifact);
        QVERIFY(project->saveDocumentAs(QUrl::fromLocalFile(artifact)));
        QTRY_VERIFY(!canvas->rendering() && canvas->residentTileCount() > 0);
        const int x=canvas->editingWidth()/2,y=canvas->editingHeight()/2;
        const auto expected = QColor::fromRgba(iiSharedCanvas::renderArtboard(*canvas->document(), 0, canvas->document()->artboards[canvas->activeArtboardIndex()].id).pixels.pixels[y*canvas->editingWidth()+x]);
        const auto position = canvas->mapToScene({canvas->panX() + canvas->zoom() * (x + canvas->editingOrigin().x() - canvas->canvasOriginX()), canvas->panY() + canvas->zoom() * (y + canvas->editingOrigin().y() - canvas->canvasOriginY())});
        QTRY_COMPARE(window->grabWindow().pixelColor((position * window->devicePixelRatio()).toPoint()), expected);
        QVERIFY(window->grabWindow().save(capture + "/" + QTest::currentDataTag() + ".png"));
    }
    click(window, editorBackControl(window));
    QCOMPARE(listProperty(selection, "selectedImages").size(), 2);
    QCOMPARE(quick->property("prompt").toString(), "Keep the Home draft");
    QTRY_COMPARE(listProperty(selection, "images").size(), 2);
    QTRY_VERIFY((first = visualItem(historyCards, target == "macos" ? "generationHistoryCard0" : "mobileGenerationHistoryCard0")));
    click(window, first);
    QCOMPARE(listProperty(selection, "selectedImages").size(), 1);
    click(window, item(selection, target == "macos" ? "generationHistorySelectionToggle" : "mobileGenerationHistorySelectionToggle"));
    QTRY_VERIFY((second = visualItem(historyCards, target == "macos" ? "generationHistoryCard1" : "mobileGenerationHistoryCard1")));
    click(window, second);
    QTRY_VERIFY(editor->isVisible());
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    QCOMPARE(canvas->document()->layers.size(), 1u);
}

void GuiTests::generatedImageSelectionRoutes_data() { homeEditorRoutes_data(); }
void GuiTests::generatedImageSelectionRoutes()
{
    QFETCH(QString, target);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/result-editor-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QVariantList results;
    for (int index = 0; index < 3; ++index) {
        QImage image(index == 0 ? QSize(64, 40) : QSize(32, 60), QImage::Format_ARGB32);
        image.fill(index == 0 ? Qt::red : index == 1 ? Qt::blue : Qt::green);
        const auto path = storage.filePath(QString("image-%1.png").arg(index));
        QVERIFY(image.save(path));
        QString error;
        const auto native = dreamscapes::persistImageCanvas(path, &error);
        QVERIFY2(!native.isEmpty(), qPrintable(error));
        const auto preview = dreamscapes::imageCanvasPreview(native, storage.filePath("previews"), &error);
        QVERIFY(!preview.isEmpty()); QVERIFY(QFile::remove(path));
        results.append(QVariantMap{{"id", "one-job-with-several-outputs"}, {"imageSource", QUrl::fromLocalFile(preview)},
            {"documentSource", QUrl::fromLocalFile(native)}, {"mediaType", "Image"}, {"prompt", "Generated batch"}});
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
    QVERIFY(result && quick);
    QVERIFY(!editor && !canvas);
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
    editor = item(window, "canvasEditor");
    QVERIFY(editor);
    QTRY_VERIFY(editor->isVisible());
    canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas"));
    auto *project = window->findChild<EditorProject *>("editorProject");
    QCOMPARE(project->canvasCount(),2);
    QCOMPARE(canvas->document()->layers.size(), 2u);
    QCOMPARE(canvas->editingWidth(), 64);
    QCOMPARE(canvas->editingHeight(), 40);
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/toolbar-gui-XXXXXX.societycontainer");
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
    QCOMPARE(list->property("count").toInt(), 18);
    const auto activeTools = listProperty(toolbar, "tools");
    QCOMPARE(activeTools[2].toMap()["key"].toString(), "camera-photo");
    QCOMPARE(activeTools[3].toMap()["key"].toString(), "file");
    for (const auto &entry : activeTools)
        QVERIFY(entry.toMap()["key"].toString() != "asset");
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
    QCOMPARE(assets.size(), 18);
    QSet<QRgb> renderedAccentColors;
    for (int index = 0; index < assets.size(); ++index) {
        const auto asset = assets[index].toObject();
        const auto file = asset["file"].toString();
        const auto key = file.chopped(4);
        const auto path = sourceUrl("Views/Editor/Assets/" + file).toLocalFile();
        QVERIFY(QFileInfo(path).size() > 0);
        const QColor expectedAccent(asset["strokeColor"].toString());
        QColor beforeSelection, selectedAccent, afterSelection;
        QString colorError;
        // Expose the icon without selecting it. ListView may recreate delegates
        // when wrapping between the first and last mobile tools.
        QVERIFY(QMetaObject::invokeMethod(toolbar, "syncTool", Q_ARG(QVariant, (index + 1) % assets.size())));
        QQmlExpression expose(QQmlEngine::contextForObject(list), list,
            QString("positionViewAtIndex(%1, ListView.Contain)").arg(index));
        expose.evaluate();
        QVERIFY2(!expose.hasError(), qPrintable(expose.error().toString()));
        QTRY_VERIFY(toolFor(key) && !toolFor(key)->property("selected").toBool());
        QTRY_COMPARE(item(toolFor(key), "editorToolIcon")->property("status").toInt(), 1);
        QVERIFY2(captureToolbarIconColor(item(toolFor(key), "editorToolIcon"), expectedAccent,
                                        &beforeSelection, &colorError),
                 qPrintable(key + " before selection: " + colorError));
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index)));
        QTRY_VERIFY(toolFor(key));
        auto *tool = toolFor(key);
        QCOMPARE(tool->width(), 86.0);
        QCOMPARE(tool->height(), 68.0);
        QCOMPARE(tool->property("text").toString(), key == "auto-enhance" ? QString("Adjust") : asset["label"].toString());
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
        QVERIFY2(captureToolbarIconColor(icon, expectedAccent, &selectedAccent, &colorError),
                 qPrintable(key + " selected: " + colorError));
        QVERIFY(iconColorsNear(beforeSelection, selectedAccent));
        QVERIFY2(!renderedAccentColors.contains(selectedAccent.rgb()), qPrintable("Repeated toolbar accent: " + key));
        renderedAccentColors.insert(selectedAccent.rgb());
        QVERIFY(QMetaObject::invokeMethod(toolSheet, "dismiss"));
        QTRY_VERIFY(!toolSheet->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(toolbar, "syncTool", Q_ARG(QVariant, (index + 1) % assets.size())));
        expose.evaluate();
        QVERIFY2(!expose.hasError(), qPrintable(expose.error().toString()));
        QTRY_VERIFY(toolFor(key) && !toolFor(key)->property("selected").toBool());
        QTRY_COMPARE(item(toolFor(key), "editorToolIcon")->property("status").toInt(), 1);
        QVERIFY2(captureToolbarIconColor(item(toolFor(key), "editorToolIcon"), expectedAccent,
                                        &afterSelection, &colorError),
                 qPrintable(key + " after deselection: " + colorError));
        QVERIFY(iconColorsNear(selectedAccent, afterSelection));
        QVERIFY(QMetaObject::invokeMethod(toolbar, "syncTool", Q_ARG(QVariant, index)));
    }
    QCOMPARE(renderedAccentColors.size(), 18);
    QCOMPARE(editor->property("selectedTool").toString(), "eraser");
    auto *last = toolFor("eraser");
    QVERIFY(bounds(last, list).right() <= list->width() + 1);
    QTest::keyClick(window, Qt::Key_Home);
    QTRY_COMPARE(editor->property("selectedTool").toString(), "elements");
    QVERIFY(QMetaObject::invokeMethod(toolSheet, "dismiss"));
    QTRY_VERIFY(!toolSheet->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 2)));
    QTRY_COMPARE(editor->property("selectedTool").toString(), "camera-photo");
    QVERIFY(QMetaObject::invokeMethod(toolSheet, "dismiss"));
    QTRY_VERIFY(!toolSheet->property("visible").toBool());
    toolFor("camera-photo")->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Right);
    QTRY_COMPARE(editor->property("selectedTool").toString(), "file");
    QTRY_COMPARE(toolSheet->property("definition").value<QJSValue>().toVariant().toMap()["key"].toString(), "file");
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
    QTemporaryDir files(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/editor-quality-XXXXXX.societycontainer");
    QVERIFY(files.isValid());
    QVERIFY(iiSocietyContainer::SocietyDrive::create(files.path()));
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
    QString nativeError;
    const auto nativeInput = dreamscapes::persistImageCanvas(input, &nativeError, files.path());
    QVERIFY2(!nativeInput.isEmpty(), qPrintable(nativeError));
    QVERIFY(prepared.openDocumentSource(QUrl::fromLocalFile(nativeInput)));
    const auto originalPixels = std::get<RasterAsset>(prepared.document()->assets.front()).pixels.pixels;
    const auto working = files.filePath("Files/working.iisc");
    QVERIFY(prepared.saveDocumentAs(QUrl::fromLocalFile(working)));
    const auto encoded = encodeIisc(*prepared.document());
    QVERIFY(encoded.ok());
    const auto snapshot = files.filePath("Files/snapshot.iisc");
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
    const auto activeTools = listProperty(toolbar, "tools");
    const auto tool = [&](const QString &key) {
        for (int index = 0; index < activeTools.size(); ++index)
            if (activeTools[index].toMap()["key"].toString() == key)
                return QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index));
        return false;
    };
    const auto edit = [&](const char *key, QVariant value) { return QMetaObject::invokeMethod(sheet, "setValue", Q_ARG(QVariant, QString::fromLatin1(key)), Q_ARG(QVariant, value)); };
    const auto point = [&](int x, int y) { return canvas->mapToScene({canvas->panX() + x * canvas->zoom(), canvas->panY() + y * canvas->zoom()}).toPoint(); };
    QVERIFY(tool("text")); QVERIFY(edit("selector", "Free Text")); QVERIFY(edit("field-0", "Hello")); QVERIFY(edit("field-2", 12));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point(2, 22));
    QVERIFY(!canvas->selectedLayerId().isEmpty()); const auto before = renderFrame(*canvas->document(), 0).pixels.pixels;
    QVERIFY(std::ranges::any_of(before, [](auto p) { return p >> 24; }));
    QVERIFY(edit("field-0", "Changed")); QVERIFY(renderFrame(*canvas->document(), 0).pixels.pixels != before);
    canvas->forceActiveFocus();
    QTest::keySequence(&window, QKeySequence(QKeySequence::Undo));
    QCOMPARE(renderFrame(*canvas->document(), 0).pixels.pixels, before);
    QTRY_COMPARE(sheet->property("values").value<QJSValue>().toVariant().toMap().value("field-0").toString(), QString("Hello"));
    QVERIFY(tool("select")); QVERIFY(edit("selector", "Rectangle")); QVERIFY(edit("field-2", 0)); QVERIFY(edit("field-5", false));
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, point(4, 4)); QTest::mouseMove(&window, point(16, 16), 30); QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, point(16, 16));
    QVERIFY(canvas->toolState().value("selectionActive").toBool());
    QVERIFY(tool("fill")); QVERIFY(edit("selector", "Solid")); QVERIFY(edit("field-1", "#804020")); QVERIFY(edit("field-2", 100));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point(8, 8));
    QVERIFY(canvas->rasterLayerSelected()); QCOMPARE(canvas->selectedRasterPixels()->pixels[8 * 64 + 8], 0xff804020U);
    QVERIFY(canvas->setLayerOpacity(canvas->selectedLayerId(), 0.5));
    QVERIFY(tool("layers")); QTRY_COMPARE(sheet->property("values").value<QJSValue>().toVariant().toMap().value("field-10").toDouble(), 50.0);
    QVERIFY(canvas->setLayerOpacity(canvas->selectedLayerId(), 1.0));
    QVERIFY(tool("color")); QVERIFY(edit("field-1", 1.0)); QVERIFY(qRed(canvas->selectedRasterPixels()->pixels[8 * 64 + 8]) > 128);
    auto *dock = item(editor.data(), "editorDesktopPanel"); QVERIFY(dock && dock->isVisible());
    auto *graphic = item(dock, "editorGraphicPanel"); QVERIFY(graphic && graphic->isVisible());
    auto *viewport = item(dock, "editorDesktopPanelViewport"); QVERIFY(viewport);
    QCOMPARE(graphic->property("toolId").toString(), QString("color"));
    QString previewName;
    for (const auto &entry : graphicRows(mapProperty(graphic,"selectedView")["rows"].toList())) {
        const auto row=entry.toMap(); const auto options=row["options"].toList();
        for (int index=0;index<options.size();++index)
            if (options[index].toMap()["value"].toString()=="preview")
                previewName="graphicAction-"+row["id"].toString()+"-"+QString::number(index);
    }
    QVERIFY(!previewName.isEmpty());
    auto *preview=visualItem(graphic,qPrintable(previewName)); QVERIFY(preview && preview->isEnabled());
    const auto previewRect=bounds(preview,viewport->property("contentItem").value<QQuickItem *>());
    QVERIFY(viewport->setProperty("contentY",qBound(0.0,previewRect.top()-16,
        qMax(0.0,viewport->property("contentHeight").toReal()-viewport->height()))));
    QTRY_VERIFY(preview->isVisible() && bounds(preview,viewport).top()>=0
        && bounds(preview,viewport).bottom()<=viewport->height());
    const auto beforePreview=encodeIisc(*canvas->document()).bytes;
    click(&window,preview);
    auto *surface=editor->findChild<QObject *>("editorGraphicPreviewSheet"); QVERIFY(surface);
    QTRY_VERIFY(surface->property("opened").toBool());
    QTRY_COMPARE(graphic->property("operationStatus").toString(),QString("preview"));
    QTRY_VERIFY(!graphic->property("busy").toBool());
    QCOMPARE(mapProperty(graphic,"statusView")["status"].toString(),QString("preview"));
    QVERIFY(graphic->property("previewImage").toUrl().toString().startsWith("data:image/png;base64,"));
    QVERIFY(editor->property("modalActive").toBool());
    QCOMPARE(encodeIisc(*canvas->document()).bytes,beforePreview);
    QVERIFY(QMetaObject::invokeMethod(surface,"close")); QTRY_VERIFY(!surface->property("visible").toBool());
    QTRY_VERIFY(!editor->property("modalActive").toBool());
    QVERIFY(tool("camera-photo")); QVERIFY(!visualItem(editor.data(), "editorAction-field-15"));
    QVERIFY(!sheet->property("definition").value<QJSValue>().toVariant().toMap()["selector"].toMap()["options"].toStringList().contains("RAW Capture"));
    QVERIFY(tool("elements"));
    QQuickItem *elements = nullptr;
    QTRY_VERIFY((elements = item(dock, "editorElementsPanel")) && elements->isVisible());
    QVERIFY(QMetaObject::invokeMethod(elements, "selectView", Q_ARG(QVariant, QString("rectangle"))));
    QTRY_VERIFY(visualItem(elements, "elementsControl-field-1"));
    QVERIFY(!visualItem(elements, "elementsControl-field-4"));
    QVERIFY(edit("selector", "Ellipse"));
    QVERIFY(QMetaObject::invokeMethod(elements, "selectView", Q_ARG(QVariant, QString("ellipse"))));
    QTRY_VERIFY(visualItem(elements, "elementsControl-field-4"));
    QVERIFY(!visualItem(elements, "elementsControl-field-1"));
    QVERIFY(canvas->createCanvas({{"width", 64}, {"height", 64}, {"unit", "px"}, {"background", "Transparent"}}));
    for (const auto &entry : activeTools) {
        QVERIFY(tool(entry.toMap()["key"].toString()));
        // Selecting the current desktop tool toggles its panel. This sweep
        // validates the expanded presenter for every tool.
        QVERIFY(editor->setProperty("desktopPanelExpanded", true));
        const auto definition = sheet->property("definition").value<QJSValue>().toVariant().toMap();
        const auto key = definition["key"].toString();
        if (key == "elements") {
            QQuickItem *panel = nullptr;
            QTRY_VERIFY((panel = item(dock, "editorElementsPanel")) && panel->isVisible());
            const auto fields = listProperty(panel, "presentedFields");
            const auto values = sheet->property("values").value<QJSValue>().toVariant().toMap();
            QVERIFY(!fields.isEmpty());
            for (const auto &field : fields) {
                QVERIFY(visualItem(panel, qPrintable("elementsControl-" + field.toString())));
                QVERIFY(canvas->toolControlState(key, field.toString(), values)["supported"].toBool());
            }
            continue;
        }
        if (key == "camera-photo") {
            QQuickItem *panel = nullptr;
            QTRY_VERIFY((panel = visualItem(dock, "editorCameraPhotoPanel")) && panel->isVisible());
            QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("Camera"))));
            QVERIFY(visualItem(panel, "photoCapturePhoto"));
            QCOMPARE(panel->property("currentViewId").toString(), QString("Camera"));
            continue;
        }
        if (key == "text") {
            // Detailed fields are covered by EditorTextPanels; this test checks
            // that the toolbar presents the current text editor.
            QQuickItem *panel = nullptr;
            QTRY_VERIFY((panel = item(dock, "editorTextPanel")) && panel->isVisible());
            QVERIFY(visualItem(panel, "textContentInput"));
            QCOMPARE(listProperty(panel, "tabs").size(), 4);
            continue;
        }
        if (key == "layers") {
            // EditorLayers covers the four detailed panel states.
            QQuickItem *panel = nullptr;
            QTRY_VERIFY((panel = item(dock, "editorLayersPanel")) && panel->isVisible());
            QVERIFY(visualItem(panel, "layerPanelSegments"));
            QVERIFY(visualItem(panel, "layerDuplicate"));
            QVERIFY(!mapProperty(panel, "info").isEmpty());
            continue;
        }
        if (key == "background") {
            QQuickItem *panel=nullptr;
            QTRY_VERIFY((panel=visualItem(dock,"editorBackgroundPanel")) && panel->isVisible());
            QVERIFY(visualItem(panel,"backgroundNavigation"));
            QVERIFY(visualItem(panel,"maskTarget"));
            QVERIFY(visualItem(panel,"maskNew"));
            for(const auto &view:{"Color","Remove","Mask","Replace"}) {
                QVERIFY(edit("backgroundView",QString::fromLatin1(view)));
                QTRY_COMPARE(panel->property("currentView").toString(),QString::fromLatin1(view));
                const auto bodyName=QString("background%1Body").arg(QString::fromLatin1(view));
                QTRY_VERIFY(visualItem(panel,qPrintable(bodyName)) && visualItem(panel,qPrintable(bodyName))->isVisible());
                if(QString(view)=="Color")QVERIFY(visualItem(panel,"background-pickerMode"));
                if(QString(view)=="Remove")QVERIFY(!visualItem(panel,"background-applyRemoval")->isEnabled());
                if(QString(view)=="Replace")QVERIFY(visualItem(panel,"background-applyReplacement"));
            }
            continue;
        }
        if (QStringList{"color","auto-enhance","masking","effects"}.contains(key)) {
            // The detailed views are covered by EditorGraphicPanels. This route
            // verifies the active presenter and its real SDK-backed controls.
            QQuickItem *panel=nullptr;
            QTRY_VERIFY((panel=item(dock,"editorGraphicPanel")) && panel->isVisible());
            QTRY_COMPARE(panel->property("toolId").toString(),key);
            QVERIFY(!listProperty(panel,"viewCatalog").isEmpty());
            const auto view=mapProperty(panel,"selectedView");
            QVERIFY(!view["id"].toString().isEmpty() && !view["operation"].toString().isEmpty());
            QVERIFY2(canvas->graphicOperationState(view["operation"].toString())["supported"].toBool(),qPrintable(key+'/'+view["operation"].toString()));
            const auto rows=graphicRows(view["rows"].toList()); QVERIFY(!rows.isEmpty());
            for (const auto &entry : rows) {
                const auto row=entry.toMap(); const auto id=row["id"].toString();
                auto *control=visualItem(panel,qPrintable("graphicControl-"+id));
                QVERIFY2(control && control->isVisible(),qPrintable(key+'/'+id));
                if ((row.contains("supported") && !row["supported"].toBool())
                    || (row.contains("enabled") && !row["enabled"].toBool())) {
                    QVERIFY2(!control->property("editable").toBool(),qPrintable(key+'/'+id));
                } else if (!row["uiOnly"].toBool() && !row["api"].toString().isEmpty()) {
                    QVERIFY2(canvas->graphicOperationState(row["api"].toString())["supported"].toBool(),qPrintable(key+'/'+id+": unsupported graphic control rendered"));
                }
            }
            continue;
        }
        if (key == "file") {
            auto *filePanel = visualItem(dock, "editorFilePanel"); QVERIFY(filePanel && filePanel->isVisible());
            QCOMPARE(filePanel->property("segmentCount").toInt(), 3);
            for (const auto &view : {"information", "contents", "storage"}) {
                QVERIFY(QMetaObject::invokeMethod(filePanel, "selectView", Q_ARG(QVariant, QString::fromLatin1(view))));
                QTRY_COMPARE(filePanel->property("currentViewId").toString(), QString::fromLatin1(view));
            }
            continue;
        }
        if (isCompactEditorTool(key)) {
            QQuickItem *panel = nullptr;
            QTRY_VERIFY((panel = visualItem(dock, "editorCompactPanel")) && panel->isVisible());
            const auto fields = listProperty(panel, "presentedFields"); QVERIFY(!fields.isEmpty());
            const auto values = mapProperty(sheet, "values");
            for (const auto &entry : fields) {
                const auto field = entry.toMap();
                const auto name = (field["type"].toString() == "Action" ? "editorAction-" : "editorControl-") + field["id"].toString();
                auto *control = visualItem(panel, qPrintable(name)); QVERIFY(control);
                if (!canvas->toolControlState(key, field["id"].toString(), values)["enabled"].toBool()) QVERIFY(!control->isEnabled());
            }
            continue;
        }
        auto modes = definition["selector"].toMap()["options"].toStringList();
        if (modes.isEmpty()) modes.append(QString());
        for (const auto &mode : modes) {
            if (!mode.isEmpty()) QVERIFY(edit("selector", mode));
            QCoreApplication::processEvents();
            QQuickItem *panel = nullptr;
            // A closed layer adjustment sheet also owns a generic tool panel.
            // Inspect the visible desktop presenter for the selected tool.
            QTRY_VERIFY((panel = visualItem(dock, "editorToolPanel")) && panel->isVisible());
            QTRY_COMPARE(mapProperty(panel, "definition")["key"].toString(), key);
            const auto fields = panel->property("presentedFields").value<QJSValue>().toVariant().toList(); QVERIFY(!fields.isEmpty());
            const auto values = sheet->property("values").value<QJSValue>().toVariant().toMap();
            for (const auto &field : fields) QVERIFY2(canvas->toolControlState(key, field.toMap()["id"].toString(), values).value("supported").toBool(), qPrintable(key + '/' + mode + '/' + field.toMap()["id"].toString()));
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
    const auto tools = listProperty(toolbar, "tools");
    auto selectTool = [&](const QString &key) {
        for (int index = 0; index < tools.size(); ++index)
            if (tools[index].toMap()["key"].toString() == key)
                return QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index));
        return false;
    };
    auto setField = [&](const char *field, QVariant value) {
        return QMetaObject::invokeMethod(sheet, "setValue", Q_ARG(QVariant, QString::fromLatin1(field)), Q_ARG(QVariant, value));
    };
    QVERIFY(selectTool("brush")); // Through the same toolbar and panel as the user.
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
    QVERIFY(selectTool("eraser")); // Pixel eraser.
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
    QVERIFY(selectTool("elements")); // Native vector creation by dragging on the canvas.
    QVERIFY(setField("selector", "Rectangle"));
    QVERIFY(setField("field-2", "Solid"));
    QVERIFY(setField("field-10", 0));
    QVERIFY(setField("field-16", "#8B7CFF"));
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, position(8, 8));
    QTest::mouseMove(&window, position(28, 24), 25);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, position(28, 24));
    QCOMPARE(canvas->document()->layers.size(), 2u);
    QVERIFY(std::holds_alternative<StaticVectorLayer>(canvas->document()->layers.back()));
    const auto vectorId = canvas->selectedLayerId();
    QVERIFY(observer.open(path.toStdString()).ok());
    QCOMPARE(observer.document()->layers.size(), 2u);
    QCOMPARE(layerProperties(observer.document()->layers.front()).name, std::string("Red paint"));
    QCOMPARE(layerProperties(observer.document()->layers.front()).opacity, 0.5);
    QVERIFY(std::holds_alternative<StaticVectorLayer>(observer.document()->layers.back()));
    observer.close();
    QVERIFY(selectTool("select")); // Object selection resolves the native rendered layer.
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

void GuiTests::videoTimelineTracks()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Video/VideoTimelineState.qml"));
    QScopedPointer<QObject> state(component.create());
    QVERIFY2(state, qPrintable(component.errorString()));
    QCOMPARE(listProperty(state.data(), "tracks").size(), 0);
    QVERIFY(!state->property("canUndo").toBool());
    auto addTrack = [&](const QString &type, const QString &name, int position = -1) {
        QVariant result;
        if (!QMetaObject::invokeMethod(state.data(), "addTrack", Q_RETURN_ARG(QVariant, result),
                Q_ARG(QVariant, type), Q_ARG(QVariant, name), Q_ARG(QVariant, position))) return QString{};
        return result.toString();
    };
    auto editTrack = [&](const char *method, const QString &id, const QVariant &value) {
        QVariant result;
        return QMetaObject::invokeMethod(state.data(), method, Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, id), Q_ARG(QVariant, value)) && result.toBool();
    };
    auto removeTrack = [&](const QString &id) {
        QVariant result;
        return QMetaObject::invokeMethod(state.data(), "removeTrack", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, id)) && result.toBool();
    };
    auto addKey = [&](const QString &source, int frame, const QString &trackId) {
        QVariant result;
        return QMetaObject::invokeMethod(state.data(), "addKeyToTrack", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, source), Q_ARG(QVariant, frame), Q_ARG(QVariant, trackId)) && result.toBool();
    };
    const auto audio1 = addTrack("Audio", "Dialogue"); QVERIFY(!audio1.isEmpty());
    const auto audio2 = addTrack("Audio", "Music"); QVERIFY(!audio2.isEmpty());
    const auto caption = addTrack("Caption", "English"); QVERIFY(!caption.isEmpty());
    const auto contents1 = addTrack("Contents", "Reference"); QVERIFY(!contents1.isEmpty());
    const auto contents2 = addTrack("Contents", "Overlay", 0); QVERIFY(!contents2.isEmpty());
    const auto shots = addTrack("Shots", "Sequence"); QVERIFY(!shots.isEmpty());
    const QStringList expectedTypes{"Shots", "Contents", "Contents", "Audio", "Audio", "Caption"};
    const QStringList expectedIds{shots, contents2, contents1, audio1, audio2, caption};
    const auto tracks = listProperty(state.data(), "tracks");
    QCOMPARE(tracks.size(), expectedTypes.size());
    for (qsizetype i = 0; i < tracks.size(); ++i) {
        QCOMPARE(tracks[i].toMap().value("type").toString(), expectedTypes[i]);
        QCOMPARE(tracks[i].toMap().value("id").toString(), expectedIds[i]);
    }
    QVERIFY(addTrack("Camera", "Unsupported").isEmpty());
    QVERIFY(addTrack("Contents", "Out of range", 99).isEmpty());
    QCOMPARE(listProperty(state.data(), "tracks").size(), 6);
    QVERIFY(editTrack("renameTrack", contents1, "First image"));
    QVERIFY(!editTrack("renameTrack", contents1, "   "));
    QVERIFY(editTrack("moveTrack", audio2, 0));
    QCOMPARE(listProperty(state.data(), "tracks")[3].toMap().value("id").toString(), audio2);
    QVERIFY(!editTrack("moveTrack", audio2, -1));
    QVERIFY(addKey("file:///reference.png", 8, contents1));
    QVERIFY(addKey("file:///overlay.png", 16, contents2));
    const auto keys = listProperty(state.data(), "keys");
    QCOMPARE(keys.size(), 2);
    QCOMPARE(keys[0].toMap().value("trackId").toString(), contents1);
    QCOMPARE(keys[1].toMap().value("trackId").toString(), contents2);
    QVERIFY(!keys[0].toMap().contains("lane"));
    QVERIFY(!addKey("file:///conflict.png", 8, contents2));
    QCOMPARE(listProperty(state.data(), "keys"), keys);
    QVERIFY(!addKey("file:///invalid.png", 24, audio1));
    QVERIFY(!addKey("file:///missing.png", 24, "missing-track"));
    QVERIFY(editTrack("editKey", "trackId", contents1));
    QCOMPARE(mapProperty(state.data(), "selectedKey").value("trackId").toString(), contents1);
    QVERIFY(!editTrack("editKey", "trackId", audio1));
    QVERIFY(editTrack("editKey", "trackId", contents2));
    QCOMPARE(listProperty(state.data(), "keys"), keys);
    QVERIFY(editTrack("moveTrack", contents1, 0));
    QCOMPARE(listProperty(state.data(), "keys"), keys);
    QVariant added;
    QVERIFY(QMetaObject::invokeMethod(state.data(), "addShotToTrack", Q_RETURN_ARG(QVariant, added), Q_ARG(QVariant, shots)));
    QVERIFY(added.toBool());
    QCOMPARE(listProperty(state.data(), "shots").first().toMap().value("trackId").toString(), shots);
    const auto alternate = addTrack("Shots", "Alternate", 0); QVERIFY(!alternate.isEmpty());
    const auto shotId = listProperty(state.data(), "shots").first().toMap().value("id").toString();
    QVERIFY(editTrack("assignShotTrack", shotId, alternate));
    QCOMPARE(listProperty(state.data(), "shots").first().toMap().value("trackId").toString(), alternate);
    QVERIFY(editTrack("assignShotTrack", shotId, shots));
    QVERIFY(removeTrack(alternate));
    QVERIFY(removeTrack(contents1));
    QCOMPARE(listProperty(state.data(), "keys").size(), 1);
    QCOMPARE(listProperty(state.data(), "shots").size(), 1);
    QVERIFY(QMetaObject::invokeMethod(state.data(), "undo"));
    QCOMPARE(listProperty(state.data(), "tracks").size(), 6);
    QCOMPARE(listProperty(state.data(), "keys"), keys);
    QVERIFY(QMetaObject::invokeMethod(state.data(), "redo"));
    QCOMPARE(listProperty(state.data(), "keys").size(), 1);
    QVERIFY(removeTrack(shots));
    QCOMPARE(listProperty(state.data(), "shots").size(), 0);
    QCOMPARE(state->property("totalFrames").toInt(), 120);
    QVERIFY(QMetaObject::invokeMethod(state.data(), "reset", Q_ARG(QVariant, 48), Q_ARG(QVariant, 24)));
    QCOMPARE(listProperty(state.data(), "tracks").size(), 0);
    QCOMPARE(listProperty(state.data(), "keys").size(), 0);
    QCOMPARE(state->property("selectedTrackId").toString(), QString{});
    QVERIFY(!state->property("canUndo").toBool());
    // Explicit image insertion creates one destination, never alternating placeholder lanes.
    QVERIFY(addKey("file:///first.png", 0, ""));
    QVERIFY(addKey("file:///second.png", 12, ""));
    QCOMPARE(listProperty(state.data(), "tracks").size(), 1);
    const auto imageKeys = listProperty(state.data(), "keys");
    QCOMPARE(imageKeys[0].toMap().value("trackId"), imageKeys[1].toMap().value("trackId"));
}

void GuiTests::mainStartupPerformance()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/startup-performance-XXXXXX.societycontainer");QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    for(int attempt=0;attempt<3;++attempt){
        QQmlApplicationEngine engine;engine.setInitialProperties({{"initialContainerPath",storage.path()}});QElapsedTimer timer;timer.start();engine.load(sourceUrl("Main.qml"));
        const auto milliseconds=timer.nsecsElapsed()/1000000.0;QCOMPARE(engine.rootObjects().size(),1);auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);
        qInfo().noquote()<<QString("PERF main_startup attempt=%1 qml_load_ms=%2 objects=%3 editor_created=%4").arg(attempt).arg(milliseconds,0,'f',3).arg(window->findChildren<QObject *>().size()).arg(bool(item(window,"canvasEditor")));
        window->hide();
    }
}
void GuiTests::homeDefersEditorUntilNeeded()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/lazy-editor-XXXXXX.societycontainer");QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QQmlApplicationEngine engine;engine.setInitialProperties({{"initialContainerPath",storage.path()}});engine.load(sourceUrl("Main.qml"));QCOMPARE(engine.rootObjects().size(),1);
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);QVERIFY(!item(window,"canvasEditor"));
    window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
    QVariant opened;
    QVERIFY(QMetaObject::invokeMethod(window,"openCanvas",Q_RETURN_ARG(QVariant,opened),Q_ARG(QVariant,QString()),Q_ARG(QVariant,QVariantMap{})));
    QVERIFY2(opened.toBool(),qPrintable(window->property("generationRequestError").toString()));
    auto *editor=item(window,"canvasEditor");QVERIFY(editor);QVERIFY(editor->isVisible());auto *canvas=qobject_cast<EditorCanvas *>(item(editor,"editorBlankCanvas"));QVERIFY(canvas && canvas->documentReady());
    const auto snapshot=iiSharedCanvas::encodeIisc(*canvas->document()).bytes;QVERIFY(QMetaObject::invokeMethod(window,"closeCanvas"));QVERIFY(!editor->isVisible());
    QCOMPARE(item(window,"canvasEditor"),editor);QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes,snapshot);
}
void GuiTests::editorTimelineTrackManagement()
{
    QQmlEngine engine;
    auto *theme=engine.singletonInstance<QObject *>("LVRS","Theme");
    QVERIFY(theme && theme->setProperty("targetOverride","macos"));
    QQmlComponent component(&engine,sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor,qPrintable(component.errorString()));
    QQuickWindow window;window.resize(2267,1316);editor->setParentItem(window.contentItem());editor->setSize(window.size());
    const auto detach=qScopeGuard([&]{editor->setParentItem(nullptr);});
    window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *timeline=item(editor.data(),"editorTimeline");QVERIFY(timeline);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);
    auto *canvas=qobject_cast<EditorCanvas *>(item(editor.data(),"editorBlankCanvas"));QVERIFY(canvas);
    iiSharedCanvas::Document document;document.extent={128,96};document.timeline.frameCount=48;
    QVERIFY(canvas->loadProjectPage(document,"Native timeline"));QCOMPARE(model->tracks().size(),0);
    auto *add=item(timeline,"editorTimelineAddLayer");QVERIFY(add);
    QTRY_VERIFY(bounds(add,timeline).right()<=timeline->width());click(&window,add);
    auto *menu=timeline->findChild<QObject *>("editorTimelineAddMenu");QVERIFY(menu);QTRY_VERIFY(menu->property("visible").toBool());
    auto *content=menu->property("contentItem").value<QQuickItem *>();QVERIFY(content);
    QQuickItem *choice=nullptr;QList<QQuickItem *> pending{content};
    while(!pending.isEmpty()) {auto *candidate=pending.takeLast();pending.append(candidate->childItems());if(candidate->property("label").toString()=="Dynamic bitmap"){choice=candidate;break;}}
    QVERIFY(choice);click(&window,choice);QTRY_VERIFY(!menu->property("visible").toBool());
    QTRY_COMPARE(canvas->document()->layers.size(),std::size_t(1));
    QVERIFY(std::holds_alternative<iiSharedCanvas::DynamicBitmapLayer>(canvas->document()->layers.back()));
    const auto id=model->selectedTrackId();QVERIFY(!id.isEmpty());
    QVERIFY(model->renameTrack(id,"Animation"));QCOMPARE(iiSharedCanvas::layerProperties(canvas->document()->layers.back()).name,std::string("Animation"));
    QVERIFY(model->addLayer("Static bitmap","Background"));QVERIFY(model->addAudioTrack());
    QCOMPARE(model->tracks().size(),3);QCOMPARE(model->tracks().back().toMap()["type"].toString(),QString("Audio"));
    QVERIFY(model->selectTrack(id));QVERIFY(model->moveTrack(id,-1));
    QCOMPARE(iiSharedCanvas::layerProperties(canvas->document()->layers.back()).id,id.toStdString());
    QVERIFY(model->removeTrack(id));QCOMPARE(canvas->document()->layers.size(),std::size_t(1));
    click(&window,item(timeline,"editorTimelineUndo"));QTRY_COMPARE(canvas->document()->layers.size(),std::size_t(2));
    QVERIFY(model->selectTrack(id));
    QVERIFY(QMetaObject::invokeMethod(timeline,"chooseFrame",Q_ARG(QVariant,id)));
    auto *dialog=timeline->findChild<QObject *>("editorTimelineImageDialog");QVERIFY(dialog);QTRY_VERIFY(dialog->property("visible").toBool());
    QCOMPARE(dialog->property("targetLayer").toString(),id);QVERIFY(QMetaObject::invokeMethod(dialog,"close"));
    QVERIFY(canvas->loadProjectPage(document,"Empty"));QTRY_COMPARE(model->tracks().size(),0);
    QVERIFY(item(timeline,"editorTimelineEmptyState")->isVisible());
}

void GuiTests::editorTimelineLayout_data()
{
    QTest::addColumn<QSize>("windowSize");
    QTest::addColumn<bool>("mobile");
    QTest::newRow("figma") << QSize(2267, 1316) << false;
    QTest::newRow("desktop") << QSize(1280, 800) << false;
    QTest::newRow("compact") << QSize(900, 700) << false;
    QTest::newRow("tablet") << QSize(768, 1024) << true;
    QTest::newRow("mobile") << QSize(430, 932) << true;
    QTest::newRow("small-mobile") << QSize(320, 932) << true;
}

void GuiTests::editorTimelineLayout()
{
    QFETCH(QSize, windowSize);
    QFETCH(bool, mobile);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(windowSize);
    editor->setParentItem(window.contentItem()); editor->setSize(windowSize);
    const auto detach = qScopeGuard([&] { editor->setParentItem(nullptr); });
    QVERIFY(editor->setProperty("mobileLayout", mobile));
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *timeline = item(editor.data(), "editorTimeline");
    auto *dock = item(editor.data(), "editorTimelineDock");
    auto *surface = item(editor.data(), "editorCanvasSurface");
    auto *workspace = item(editor.data(), "editorWorkspace");
    QVERIFY(timeline && dock && surface && workspace);
    QVERIFY(timeline->isVisible());
    QVERIFY(!item(editor.data(), "editorFrameControls"));
    QVERIFY(timeline->height() >= 160);
    QVERIFY(surface->height() >= 120);
    QVERIFY(bounds(surface, editor.data()).bottom() <= bounds(dock, editor.data()).top());
    QVERIFY(bounds(workspace, editor.data()).bottom() <= bounds(dock, editor.data()).top());
    QVERIFY(bounds(timeline, editor.data()).right() <= editor->width());
    auto *viewport = item(timeline, "editorTimelineViewport"); QVERIFY(viewport);
    QTRY_COMPARE(timeline->width(), dock->width());
    QCOMPARE(viewport->width(), timeline->width());
    QCOMPARE(viewport->property("contentHeight").toReal(), timeline->implicitHeight());
    QCOMPARE(viewport->property("contentWidth").toReal(), viewport->width());
    auto *lanes = item(timeline, "editorTimelineLanes"); QVERIFY(lanes);
    QTRY_COMPARE(lanes->property("contentWidth").toReal(), lanes->width());
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);
    auto *canvas=qobject_cast<EditorCanvas *>(item(editor.data(),"editorBlankCanvas"));QVERIFY(canvas);
    QCOMPARE(model->tracks().size(),canvas->layers().size()+qsizetype(canvas->document()->audioTracks.size()));
    iiSharedCanvas::Document document;document.extent={32,32};document.timeline.frameCount=48;
    QVERIFY(canvas->loadProjectPage(document,"Empty timeline"));
    QCOMPARE(model->tracks().size(),0);QVERIFY(item(timeline,"editorTimelineEmptyState")->isVisible());
    auto verifyControlRowsClip=[&] {
        for(const auto *name:{"editorTimelineHeader","editorTimelineTools","editorTimelinePrecision","editorTimelineFooter"}) {
            auto *row=item(timeline,name);QVERIFY(row);QVERIFY(row->clip());QList<QQuickItem *> controls;qreal tallest=0;
            const auto rowRect=bounds(row,timeline);QVERIFY(rowRect.left()>=0 && rowRect.right()<=timeline->width()+.01);
            for(auto *child:row->childItems()) {
                if(!child->isVisible()||child->width()<=0||child->height()<=0)continue;
                QCOMPARE(child->y(),0.0);tallest=qMax(tallest,child->height());
                for(auto *other:controls)QVERIFY(!bounds(child,row).intersects(bounds(other,row)));
                controls.append(child);
            }
            QCOMPARE(row->height(),tallest);
        }
    };
    QTest::qWait(40);verifyControlRowsClip();
    if(!mobile) {
        QVERIFY(editor->setProperty("desktopPanelExpanded",false));QTRY_COMPARE(timeline->width(),dock->width());
        QTest::qWait(30);verifyControlRowsClip();QVERIFY(editor->setProperty("desktopPanelExpanded",true));
    }
    QVERIFY(canvas->loadProjectPage(document,"Layout"));QVERIFY(model->addLayer("Static bitmap","Actual layer"));
    QTRY_COMPARE(model->tracks().size(),1);QTest::qWait(40);verifyControlRowsClip();
    const auto layer=model->selectedTrackId();auto *card=visualItem(timeline,qPrintable("editorTimelineClip-"+layer));QVERIFY(card);
    const auto clipRect=bounds(card,lanes);QVERIFY(clipRect.left()>=0 && clipRect.right()<=lanes->width());
    auto *footer=item(timeline,"editorTimelineFooter");QVERIFY(footer);
    const auto bottom=qMax(0.0,viewport->property("contentHeight").toReal()-viewport->height());
    QVERIFY(viewport->setProperty("contentY",bottom));QTRY_VERIFY(bounds(footer,viewport).bottom()<=viewport->height()+1);
    QVERIFY(timeline->setProperty("timelineScale",2));QTRY_COMPARE(lanes->property("contentWidth").toReal(),lanes->width()*2);
    QVERIFY(QMetaObject::invokeMethod(timeline,"fitTimeline"));QTRY_COMPARE(lanes->property("contentX").toReal(),0.0);
    QCOMPARE(viewport->property("contentWidth").toReal(),viewport->width());
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if(!capture.isEmpty()){QTest::qWait(60);QVERIFY(window.grabWindow().save(capture+QString("/editor-native-timeline-%1.png").arg(windowSize.width())));}
}

void GuiTests::editorTimelineControlsAndPlayback()
{
    QQmlEngine engine;auto *theme=engine.singletonInstance<QObject *>("LVRS","Theme");
    QVERIFY(theme && theme->setProperty("targetOverride","macos"));
    QQmlComponent component(&engine,sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));QVERIFY2(editor,qPrintable(component.errorString()));
    QQuickWindow window;window.resize(2267,1316);editor->setParentItem(window.contentItem());editor->setSize(window.size());
    const auto detach=qScopeGuard([&]{editor->setParentItem(nullptr);});window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *timeline=item(editor.data(),"editorTimeline");QVERIFY(timeline);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);
    auto *canvas=qobject_cast<EditorCanvas *>(item(editor.data(),"editorBlankCanvas"));QVERIFY(canvas);
    iiSharedCanvas::Document document;document.extent={32,32};document.timeline={{24,1},48};
    QVERIFY(canvas->loadProjectPage(document,"Playback"));QVERIFY(model->addLayer("Static bitmap","Still"));
    const auto id=model->selectedTrackId();model->seek(12);QTRY_COMPARE(canvas->frame(),12U);
    canvas->setFrame(30);QTRY_COMPARE(model->playhead(),30);model->seek(12);
    auto *play=item(timeline,"editorTimelinePlay");QVERIFY(play && play->isEnabled());
    click(&window,play);QTRY_VERIFY(timeline->property("playing").toBool());QTRY_VERIFY(canvas->frame()>12);
    click(&window,play);QVERIFY(!timeline->property("playing").toBool());const auto paused=canvas->frame();QTest::qWait(90);QCOMPARE(canvas->frame(),paused);
    for(const auto *mode:{"Keyframes","Curves","Layers"}) {
        auto *button=visualItem(timeline,qPrintable(QString("editorTimelineMode-")+mode));QVERIFY(button);
        const auto tone=button->property("tone").toInt();click(&window,button);QCOMPARE(model->mode(),QString(mode));
        QCOMPARE(button->property("tone").toInt(),tone);QVERIFY(button->property("checked").toBool());
        auto *background=button->property("background").value<QObject *>();QVERIFY(background);
        QTRY_COMPARE(background->property("color").value<QColor>(),button->property("backgroundColorPressed").value<QColor>());
    }
    model->seek(24);QVERIFY(model->split());QCOMPARE(canvas->document()->layers.size(),std::size_t(2));
    click(&window,item(timeline,"editorTimelineUndo"));QTRY_COMPARE(canvas->document()->layers.size(),std::size_t(1));
    QVERIFY(model->selectClip(id));auto *duration=item(timeline,"editorTimelineDuration");QVERIFY(duration);
    QVERIFY(QMetaObject::invokeMethod(duration,"accepted",Q_ARG(QString,"72f")));
    QTRY_COMPARE(canvas->document()->timeline.frameCount,iiSharedCanvas::FrameIndex(72));
    model->seek(8);timeline->forceActiveFocus();QTest::keyClick(&window,Qt::Key_Right);QTRY_COMPARE(canvas->frame(),9U);
    QVERIFY(timeline->setProperty("playing",true));editor->setVisible(false);QTRY_VERIFY(!timeline->property("playing").toBool());editor->setVisible(true);
    QVERIFY(canvas->loadProjectPage(document,"Reload"));QTRY_COMPARE(model->tracks().size(),0);
    iiSharedCanvas::Document fractional;fractional.extent={32,32};fractional.timeline={{30000,1001},60};
    QVERIFY(canvas->loadProjectPage(fractional,"Fractional"));QTRY_VERIFY(qAbs(model->fps()-30000.0/1001)<.000001);
    QVERIFY(QMetaObject::invokeMethod(timeline,"seekFrame",Q_ARG(QVariant,24)));QCOMPARE(canvas->frame(),24U);
    QVERIFY(timeline->setProperty("playing",true));QVERIFY(timeline->setProperty("enabled",false));QVERIFY(!timeline->property("playing").toBool());
}

void GuiTests::desktopEditorLayoutCentersCanvas()
{
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QVERIFY(!item(editor.data(), "editorToolStatus"));
    QQuickWindow window;
    window.resize(2319, 1316);
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
    QCOMPARE(toolbar->property("radius").toReal(), 18.0);
    QCOMPARE(QQmlProperty(toolbar, "border.width").read().toReal(), 0.0);
    QCOMPARE(toolbar->property("color").value<QColor>().alpha(), 0);
    QCOMPARE(bounds(toolbar, editor.data()), QRectF(1925, 0, 52, 732));
    QCOMPARE(bounds(workspace, editor.data()), QRectF(181, 0, 1744, bounds(item(editor.data(), "editorTimelineDock"), editor.data()).top()));
    QCOMPARE(bounds(toolbar, editor.data()).right(), bounds(dock, editor.data()).left());
    QCOMPARE(list->property("orientation").toInt(), int(Qt::Vertical));
    QCOMPARE(bounds(surface, workspace), workspace->boundingRect());
    QVERIFY(!item(editor.data(), "editorDocumentOpen")->isVisible());
    QCOMPARE(item(editor.data(), "editorDocumentControls")->height(), 0.0);
    QCOMPARE(list->property("count").toInt(), 18);
    QVERIFY(!visualItem(editor.data(), "editorTool-asset"));
    auto *content = list->property("contentItem").value<QQuickItem *>();
    QVERIFY(content);
    QFile manifest(sourceUrl("Views/Editor/Assets/manifest.json").toLocalFile());
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    const auto icons = QJsonDocument::fromJson(manifest.readAll()).object()["icons"].toArray();
    QCOMPARE(icons.size(), 18);
    QSet<QRgb> renderedAccentColors;
    for (int index = 0; index < icons.size(); ++index) {
        const auto asset = icons[index].toObject();
        const auto key = asset["file"].toString().chopped(4);
        QQuickItem *tool = nullptr;
        QTRY_VERIFY((tool = visualItem(content, qPrintable("editorTool-" + key))));
        QCOMPARE(bounds(tool, toolbar), QRectF(8, 8 + index * 40, 36, 36));
        QCOMPARE(tool->property("text").toString(), key == "auto-enhance" ? QString("Adjust") : asset["label"].toString());
        auto *slot = item(tool, "editorToolIconSlot");
        auto *icon = item(tool, "editorToolIcon");
        QVERIFY(slot && icon);
        QCOMPARE(bounds(slot, tool), QRectF(7, 8, 22, 22));
        QVERIFY(!item(tool, "editorToolLabel")->isVisible());
        QTRY_COMPARE(icon->property("status").toInt(), 1);
        QCOMPARE(icon->size(), QSizeF(asset["width"].toString().toDouble(), asset["height"].toString().toDouble()));
        QCOMPARE(icon->position(), QPointF(asset["x"].toDouble(), asset["y"].toDouble()));
        const QColor expectedAccent(asset["strokeColor"].toString());
        QColor beforeSelection, selectedAccent, afterSelection;
        QString colorError;
        QVERIFY(QMetaObject::invokeMethod(toolbar, "syncTool", Q_ARG(QVariant, (index + 1) % icons.size())));
        QVERIFY(!tool->property("selected").toBool());
        QVERIFY2(captureToolbarIconColor(icon, expectedAccent, &beforeSelection, &colorError),
                 qPrintable(key + " before selection: " + colorError));
        click(&window, tool);
        QCOMPARE(editor->property("selectedTool").toString(), key);
        QVERIFY(tool->property("selected").toBool());
        auto *background = tool->property("background").value<QQuickItem *>();
        QVERIFY(background);
        QCOMPARE(QQmlProperty(background, "border.width").read().toReal(), 1.0);
        QVERIFY2(captureToolbarIconColor(icon, expectedAccent, &selectedAccent, &colorError),
                 qPrintable(key + " selected: " + colorError));
        QVERIFY(iconColorsNear(beforeSelection, selectedAccent));
        QVERIFY2(!renderedAccentColors.contains(selectedAccent.rgb()), qPrintable("Repeated toolbar accent: " + key));
        renderedAccentColors.insert(selectedAccent.rgb());
        QVERIFY(QMetaObject::invokeMethod(toolbar, "syncTool", Q_ARG(QVariant, (index + 1) % icons.size())));
        QVERIFY(!tool->property("selected").toBool());
        QVERIFY2(captureToolbarIconColor(icon, expectedAccent, &afterSelection, &colorError),
                 qPrintable(key + " after deselection: " + colorError));
        QVERIFY(iconColorsNear(selectedAccent, afterSelection));
        QVERIFY(QMetaObject::invokeMethod(toolbar, "syncTool", Q_ARG(QVariant, index)));
    }
    QCOMPARE(renderedAccentColors.size(), 18);
    const auto toolbarCapture = toolbar->grabToImage();
    QVERIFY(toolbarCapture);
    QTRY_VERIFY(!toolbarCapture->image().isNull());
    const auto railImage = toolbarCapture->image();
    for (const QPoint corner : {QPoint(0, 0), QPoint(railImage.width() - 1, 0),
                                QPoint(0, railImage.height() - 1),
                                QPoint(railImage.width() - 1, railImage.height() - 1)})
        QCOMPARE(railImage.pixelColor(corner).alpha(), 0);
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
    QVERIFY(centered());
    canvas->zoomAt(3, canvas->boundingRect().center());
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
        QVERIFY(toolbarCapture->image().save(captureDirectory + "/desktop-vertical-toolbar.png"));
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

void GuiTests::desktopEditorToolbarScrollsAndNavigates()
{
    QTest::failOnWarning(QRegularExpression(".*(Binding loop|Cannot assign|Unable to assign).*"));
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window;
    editor->setParentItem(window.contentItem());
    const auto detach = qScopeGuard([&] { editor->setParentItem(nullptr); });
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *list = item(toolbar, "editorToolList");
    auto *workspace = item(editor.data(), "editorWorkspace");
    auto *timeline = item(editor.data(), "editorTimelineDock");
    auto *dock = item(editor.data(), "editorDesktopPanel");
    QVERIFY(toolbar && list && workspace && timeline && dock);
    QElapsedTimer inputClock;
    inputClock.start();
    for (const auto size : {QSize(1280, 480), QSize(900, 700), QSize(720, 480), QSize(2319, 1316)}) {
        window.resize(size); editor->setSize(size); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
        QVERIFY(editor->setProperty("desktopPanelExpanded", true));
        QTRY_COMPARE(toolbar->width(), 52.0);
        QCOMPARE(toolbar->height(), qreal(qMin(732, size.height())));
        QCOMPARE(bounds(toolbar, editor.data()).top(), 0.0);
        QCOMPARE(bounds(workspace, editor.data()).top(), 0.0);
        QCOMPARE(bounds(workspace, editor.data()).right(), bounds(toolbar, editor.data()).left());
        QCOMPARE(bounds(timeline, editor.data()).right(), bounds(toolbar, editor.data()).left());
        QCOMPARE(bounds(toolbar, editor.data()).right(), bounds(dock, editor.data()).left());
        const auto available = size.width() - item(editor.data(), "editorSidebar")->width() - toolbar->width();
        QVERIFY(workspace->width() >= qMin(320.0, qMax(0.0, available - 280.0)));
        QCOMPARE(list->property("orientation").toInt(), int(Qt::Vertical));
        QCOMPARE(list->property("contentX").toReal(), 0.0);
        if (list->property("contentHeight").toReal() > list->height()) {
            const auto center = list->mapToScene(list->boundingRect().center());
            list->forceActiveFocus(Qt::MouseFocusReason);
            QTest::mouseMove(&window, center.toPoint());
            QTest::qWait(60);
            QWheelEvent wheel(center, window.mapToGlobal(center.toPoint()), QPoint(), QPoint(0, -120),
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            // Native events carry increasing timestamps. Reusing zero makes
            // Qt ignore later wheels when no pointer drag has started its timer.
            wheel.setTimestamp(quint64(inputClock.elapsed()) + 1);
            QCoreApplication::sendEvent(&window, &wheel);
            QTRY_VERIFY(list->property("contentY").toReal() > 0);
        }
        QVERIFY(QMetaObject::invokeMethod(toolbar, "syncTool", Q_ARG(QVariant, 0)));
        auto *first = visualItem(toolbar, "editorTool-elements"); QVERIFY(first);
        first->forceActiveFocus(Qt::TabFocusReason);
        for (const auto &key : QStringList{"text", "camera-photo", "file"}) {
            QTest::keyClick(&window, Qt::Key_Down);
            QTRY_COMPARE(editor->property("selectedTool").toString(), key);
        }
        QTest::keyClick(&window, Qt::Key_Up);
        QTRY_COMPARE(editor->property("selectedTool").toString(), QString("camera-photo"));
        QTest::keyClick(&window, Qt::Key_Home);
        QTRY_COMPARE(editor->property("selectedTool").toString(), QString("elements"));
        QTest::keyClick(&window, Qt::Key_End);
        QTRY_COMPARE(editor->property("selectedTool").toString(), QString("eraser"));
        auto *last = visualItem(toolbar, "editorTool-eraser"); QVERIFY(last);
        QTRY_VERIFY(bounds(last, list).top() >= 0 && bounds(last, list).bottom() <= list->height() + 1);
        const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory)); QTest::qWait(40);
            QVERIFY(window.grabWindow().save(captureDirectory + QString("/vertical-toolbar-%1x%2.png").arg(size.width()).arg(size.height())));
        }
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 17)));
        QTRY_VERIFY(!dock->isVisible());
        QCOMPARE(bounds(toolbar, editor.data()).right(), qreal(size.width()));
        QCOMPARE(bounds(workspace, editor.data()).right(), bounds(toolbar, editor.data()).left());
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 17)));
        QTRY_VERIFY(dock->isVisible());
        QCOMPARE(bounds(toolbar, editor.data()).right(), bounds(dock, editor.data()).left());
    }
}

void GuiTests::editorCanvasPanAndZoomStayInsideViewport()
{
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QVERIFY(!item(editor.data(), "editorToolStatus"));
    QQuickWindow window; window.resize(1280, 800);
    editor->setParentItem(window.contentItem()); editor->setSize(window.size());
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas")); QVERIFY(canvas);
    auto *surface = item(editor.data(), "editorCanvasSurface"); QVERIFY(surface);
    QVERIFY(canvas->createCanvas({{"width", 500}, {"height", 300}, {"unit", "px"}, {"background", "White"}}));
    QTRY_COMPARE(canvas->size(), surface->size());
    const auto centered = [&] {
        return qAbs(canvas->panX() - (canvas->width() - canvas->canvasWidth() * canvas->zoom()) / 2) < 0.01
            && qAbs(canvas->panY() - (canvas->height() - canvas->canvasHeight() * canvas->zoom()) / 2) < 0.01;
    };
    const auto drag = [&] {
        const auto start = surface->mapToScene(surface->boundingRect().center()).toPoint();
        const auto end = start + QPoint(40, 30);
        QTest::mousePress(&window, Qt::MiddleButton, Qt::NoModifier, start);
        QTest::mouseMove(&window, end, 20);
        QTest::mouseRelease(&window, Qt::MiddleButton, Qt::NoModifier, end);
    };
    const auto wheel = [&](int delta, const QPointF &position) {
        const auto scene = surface->mapToScene(position);
        QWheelEvent event(scene, window.mapToGlobal(scene.toPoint()), QPoint{}, QPoint(0, delta),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&window, &event);
    };
    QTRY_VERIFY(centered());
    drag(); QVERIFY(centered());
    const auto fittedZoom = canvas->zoom();
    wheel(1200, surface->boundingRect().center());
    QTRY_VERIFY(canvas->zoom() > fittedZoom);
    const auto x = canvas->panX(), y = canvas->panY();
    drag();
    QTRY_VERIFY(qAbs(canvas->panX() - x - 40) < 0.01);
    QTRY_VERIFY(qAbs(canvas->panY() - y - 30) < 0.01);
    canvas->panBy(1000000, -1000000);
    QCOMPARE(canvas->panX(), 0.0);
    QVERIFY(qAbs(canvas->panY() - (canvas->height() - canvas->canvasHeight() * canvas->zoom())) < 0.01);
    wheel(-1200, {surface->width() - 20, 20});
    QTRY_VERIFY(centered());
    drag(); QVERIFY(centered());
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
    auto *panel = item(dock, "editorElementsPanel");
    QVERIFY(panel);
    click(&window, visualItem(panel, "elementsShapeChoice-1"));
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

    // Dedicated Elements views must remain bounded at every dock width.
    const QStringList elementViews{"rectangle", "ellipse", "polygon", "line", "fill", "stroke", "selection", "inspect", "multiple", "clear"};
    for (const qreal width : {280.0, 342.0, 432.0, 720.0}) {
        QVERIFY(editor->setProperty("desktopPanelWidth", width));
        QTRY_COMPARE(dock->width(), width);
        for (const auto &view : elementViews) {
            QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, view)));
            QTRY_COMPARE(panel->property("currentViewId").toString(), view);
            QTest::qWait(25);
            QCOMPARE(panel->width(), width - 34);
            for (auto *control : panel->findChildren<QQuickItem *>()) {
                if (!control->isVisible() || !control->objectName().startsWith("elements")) continue;
                const auto rect = bounds(control, panel);
                QVERIFY2(rect.left() >= -0.01 && rect.right() <= panel->width() + 0.01,
                         qPrintable(view + '/' + control->objectName() + QString(" width %1").arg(width)));
            }
        }
    }

    // Exercise all design controls, including long labels, at narrow/default/wide widths.
    QVERIFY(state->setProperty("engine", QVariant::fromValue<QObject *>(nullptr)));
    const auto tools = listProperty(toolbar, "tools");
    for (int toolIndex = 0; toolIndex < tools.size(); ++toolIndex) {
        const auto key = tools[toolIndex].toMap()["key"].toString();
        // Dedicated views have their own sizing checks and do not expose the
        // generic editorToolPanel used by this catalog-control loop.
        if (QStringList{"elements", "text", "camera-photo", "file", "background", "color", "adjust", "auto-enhance", "effects", "mask", "masking", "audio-track", "canvas", "generative", "select", "retouch", "fill", "brush"}.contains(key)) continue;
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex)));
        QVERIFY(editor->setProperty("desktopPanelExpanded", true));
        panel = item(dock, "editorToolPanel");
        QTRY_VERIFY(panel && panel->isVisible());
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

void GuiTests::editorElementsTransformAndInspectorStayInSync()
{
    QTest::failOnWarning(QRegularExpression(".*Binding loop.*"));
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    editor->setProperty("desktopPanelWidth", 432);
    QQuickWindow window; window.resize(1440, 960);
    editor->setParentItem(window.contentItem()); editor->setSize(window.size()); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    if (QGuiApplication::platformName() == "cocoa") { window.requestActivate(); QVERIFY(QTest::qWaitForWindowActive(&window)); }
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    auto *panel = item(editor.data(), "editorElementsPanel");
    auto *state = editor->findChild<QObject *>("editorToolSheet");
    auto *overlay = item(editor.data(), "editorElementTransform");
    QVERIFY(canvas && panel && state && overlay);
    QVERIFY(canvas->createCanvas({{"width", 256}, {"height", 192}, {"unit", "px"}, {"background", "White"}}));
    auto draft = mapProperty(state, "values");
    draft["field-1"] = 0; draft["field-10"] = 0;
    draft["field-16"] = "#8B7CFF";
    canvas->configureTool("elements", draft);
    QVERIFY(canvas->createElement({28, 40, 88, 64}));
    const auto rectangle = canvas->selectedLayerId();
    QTRY_VERIFY(overlay->isVisible());
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("rectangle"));
    auto *width = visualItem(panel, "elementsInput-field-0-0");
    auto *height = visualItem(panel, "elementsInput-field-0-1");
    QVERIFY(width && height);
    QTRY_COMPARE(width->property("text").toString(), QString("88 px"));
    QTRY_COMPARE(height->property("text").toString(), QString("64 px"));
    auto *corner = visualItem(overlay, "elementTransformHandle-se"); QVERIFY(corner && corner->isVisible());
    QCOMPARE(corner->size(), QSizeF(9, 9));
    const auto capture = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!capture.isEmpty()) { QVERIFY(QDir().mkpath(capture)); QVERIFY(window.grabWindow().save(capture + "/elements-transform-before.png")); }
    // Actual pointer input passes through the overlay into native transform input.
    const auto before = iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
    const auto start = canvas->elementTransform().value("handles").toList()[4].toMap().value("point").toPointF();
    const auto finish = start + QPointF(24, 16) * canvas->zoom();
    QTest::mouseMove(&window, canvas->mapToScene(start).toPoint(), 50);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, canvas->mapToScene(start).toPoint());
    QTest::mouseMove(&window, canvas->mapToScene(finish).toPoint(), 50);
    if (!capture.isEmpty()) QVERIFY(window.grabWindow().save(capture + "/elements-transform-after-move.png"));
    QTRY_VERIFY(canvas->elementTransform().value("dragging").toBool());
    QTRY_VERIFY(std::abs(canvas->selectedElementSettings().value("field-0").toList()[0].toDouble() - 112) < 0.6);
    QTRY_VERIFY(width->property("text").toString() != QString("88 px"));
    // The native asynchronous renderer must also display the enlarged shape,
    // not merely move the overlay and inspector ahead of its painted content.
    const auto expandedPoint = canvas->mapToScene(QPointF(canvas->panX() + 130 * canvas->zoom(), canvas->panY() + 90 * canvas->zoom()));
    const auto expandedPixel = [&] {
        const auto image = window.grabWindow();
        const auto x = qBound(0, qRound(expandedPoint.x() * image.width() / window.width()), image.width() - 1);
        const auto y = qBound(0, qRound(expandedPoint.y() * image.height() / window.height()), image.height() - 1);
        return image.pixelColor(x, y);
    };
    QTRY_VERIFY(expandedPixel().red() < 180 && expandedPixel().blue() > 200);
    if (!capture.isEmpty()) QVERIFY(window.grabWindow().save(capture + "/elements-transform-drag.png"));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, canvas->mapToScene(finish).toPoint());
    QTRY_VERIFY(!canvas->elementTransform().value("dragging").toBool());
    const auto after = iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
    QVERIFY(after != before); QVERIFY(canvas->undo());
    QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, before);
    QTRY_COMPARE(width->property("text").toString(), QString("88 px"));
    QVERIFY(canvas->redo());
    QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, after);
    // A typed decimal is reflected by geometry and the accepted text binding.
    width->setProperty("text", "123.5 px"); QVERIFY(QMetaObject::invokeMethod(width, "commit"));
    QTRY_COMPARE(canvas->selectedElementSettings().value("field-0").toList()[0].toDouble(), 123.5);
    QTRY_COMPARE(width->property("text").toString(), QString("123.5 px"));
    // A type change keeps the same layer ID; Undo must restore its matching
    // shape inspector as well as the geometry and generation parameters.
    const QVariant ellipseChoice = QVariantMap{{"id", "ellipse"}, {"name", "Ellipse"}};
    QVERIFY(QMetaObject::invokeMethod(panel, "chooseShape", Q_ARG(QVariant, ellipseChoice)));
    QTRY_COMPARE(canvas->selectedElementSettings().value("selector").toString(), QString("Ellipse"));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("ellipse"));
    QVERIFY(canvas->undo());
    QTRY_COMPARE(canvas->selectedElementSettings().value("selector").toString(), QString("Rectangle"));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("rectangle"));
    QVERIFY(canvas->redo()); QTRY_COMPARE(panel->property("currentViewId").toString(), QString("ellipse"));
    QVERIFY(canvas->undo()); QTRY_COMPARE(panel->property("currentViewId").toString(), QString("rectangle"));
    auto ellipseDraft = canvas->toolValues("elements"); ellipseDraft["selector"] = "Ellipse";
    ellipseDraft["field-16"] = "#32A89E";
    canvas->configureTool("elements", ellipseDraft); QVERIFY(canvas->createElement({164, 56, 52, 72}));
    const auto ellipse = canvas->selectedLayerId();
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("ellipse"));
    QTRY_COMPARE(visualItem(panel, "elementsInput-field-0-0")->property("text").toString(), QString("52 px"));
    QVERIFY(canvas->selectElement(rectangle));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("rectangle"));
    QTRY_COMPARE(visualItem(panel, "elementsInput-field-0-0")->property("text").toString(), QString("123.5 px"));
    QVERIFY(canvas->selectElement(ellipse)); QTRY_COMPARE(panel->property("currentViewId").toString(), QString("ellipse"));
    const auto handleBefore = canvas->elementTransform().value("handles").toList()[4].toMap().value("point").toPointF();
    canvas->setZoom(canvas->zoom() * 1.25);
    QTRY_VERIFY(visualItem(overlay, "elementTransformHandle-se"));
    QCOMPARE(visualItem(overlay, "elementTransformHandle-se")->size(), QSizeF(9, 9));
    QVERIFY(canvas->elementTransform().value("handles").toList()[4].toMap().value("point").toPointF() != handleBefore);
    canvas->fitToView();
    if (!capture.isEmpty()) { QTest::qWait(150); QVERIFY(window.grabWindow().save(capture + "/elements-transform-selection.png")); }
    // The same native state drives the mobile sheet; there is one geometry source.
    QVERIFY(canvas->clearElementSelection()); QTRY_VERIFY(!overlay->isVisible());
    canvas->configureTool("brush", {}); QTRY_VERIFY(!overlay->isVisible());
}

void GuiTests::desktopEditorElementsMatchesFigma()
{
    QTest::failOnWarning(QRegularExpression(".*Binding loop.*"));
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QVERIFY(editor->setProperty("desktopPanelWidth", 432));
    QQuickWindow window;
    window.resize(1400, 559);
    editor->setParentItem(window.contentItem());
    editor->setSize(window.size());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    if (QGuiApplication::platformName() == "cocoa") {
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
    }
    auto *dock = item(editor.data(), "editorDesktopPanel");
    auto *panel = item(dock, "editorElementsPanel");
    auto *viewport = item(dock, "editorDesktopPanelViewport");
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *state = editor->findChild<QObject *>("editorToolSheet");
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    QVERIFY(dock && panel && viewport && toolbar && state && canvas);
    // Panel geometry uses the Figma shape dimensions, independently of the
    // clipped document extent needed for this UI test's native render work.
    QVERIFY(canvas->createCanvas({{"width", 128}, {"height", 128}, {"unit", "px"}, {"background", "Transparent"}}));
    QCOMPARE(panel->width(), 398.0);
    QCOMPARE(canvas->property("elementSelectionCount").toInt(), 0);
    QVERIFY(listProperty(canvas, "elementCandidates").isEmpty());
    // Selection rows must be native document data, never permanent Figma samples.
    QVERIFY(!visualItem(panel, "elementsSelectionRow-0"));
    const auto values = [state] { return mapProperty(state, "values"); };
    auto defaults = values();
    QStringList ids;
    for (const auto *shape : {"Polygon", "Ellipse", "Rectangle"}) {
        auto configuration = defaults;
        configuration["selector"] = shape;
        canvas->configureTool("elements", configuration);
        QVERIFY2(canvas->createElement(QRectF(8, 8, 1080, 1080)), qPrintable(canvas->error()));
        ids.prepend(canvas->selectedLayerId());
    }
    QCOMPARE(listProperty(canvas, "elementCandidates").size(), 3);
    const auto selectElement = [&](const QString &id) {
        bool selected = false;
        QVERIFY(QMetaObject::invokeMethod(canvas, "selectElement", Q_RETURN_ARG(bool, selected), Q_ARG(QString, id)));
        QVERIFY(selected);
    };
    selectElement(ids.first());
    QTRY_COMPARE(canvas->property("elementSelectionCount").toInt(), 1);
    const auto chooseView = [&](const QString &id) {
        QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, id)));
        QTRY_COMPARE(panel->property("currentViewId").toString(), id);
        viewport->setProperty("contentY", 0);
        QTest::qWait(40);
    };
    QFile fixtureFile(sourceUrl("Views/Editor/fixtures/ElementsPanelsFigma.json").toLocalFile());
    QVERIFY(fixtureFile.open(QIODevice::ReadOnly));
    const auto fixture = QJsonDocument::fromJson(fixtureFile.readAll()).object();
    const auto views = fixture["views"].toArray();
    QCOMPARE(views.size(), 10);
    const QStringList tabAssets{"direct-select.svg", "rectangle.svg", "fill.svg", "stroke.svg"};
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) QVERIFY(QDir().mkpath(captureDirectory));
    for (const auto &entry : views) {
        const auto spec = entry.toObject();
        const auto id = spec["id"].toString();
        const auto revision = canvas->revision();
        const auto encoded = iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
        if (id == "clear") QVERIFY(QMetaObject::invokeMethod(canvas, "clearElementSelection"));
        chooseView(id);
        QCOMPARE(canvas->revision(), revision);
        QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, encoded);
        auto *header = visualItem(panel, "elementsHeader");
        QVERIFY(header);
        QCOMPARE(bounds(header, dock), QRectF(17, 17, 398, 30));
        for (int tab = 0; tab < 4; ++tab) {
            auto *button = visualItem(panel, qPrintable(QString("elementsViewTab%1").arg(tab)));
            QVERIFY(button && button->isVisible());
            auto *icon = visualItem(button, qPrintable(button->objectName() + "_icon"));
            QVERIFY(icon);
            QTRY_COMPARE(icon->property("status").toInt(), 1);
            QCOMPARE(icon->size(), QSizeF(18, 18));
            QCOMPARE(icon->property("source").toUrl().fileName(), tabAssets[tab]);
            QVERIFY(QFileInfo(icon->property("source").toUrl().toLocalFile()).size() > 0);
            QQmlExpression checked(qmlContext(button), button, "Accessible.checked");
            QTRY_COMPARE(checked.evaluate().toBool(), tab == spec["selectedTab"].toInt());
        }
        auto *headerAction = visualItem(panel, "elementsHeaderAction");
        QVERIFY(headerAction);
        auto *headerIcon = visualItem(headerAction, "elementsHeaderAction_icon");
        QVERIFY(headerIcon);
        const auto selecting = spec["selectedTab"].toInt() == 0;
        QTRY_COMPARE(headerIcon->property("status").toInt(), 1);
        QCOMPARE(headerIcon->size(), selecting ? QSizeF(18, 18) : QSizeF(16, 16));
        QCOMPARE(headerIcon->property("source").toUrl().fileName(), selecting ? QString("unselect-all.svg") : QString("auto-refresh.svg"));
        if (spec.contains("heading")) {
            QVERIFY(visualText(panel, spec["heading"].toString()));
            auto *heading = visualItem(panel, selecting ? "elementsSelectionHeading" : "elementsHeading");
            QVERIFY(heading && heading->isVisible());
            QCOMPARE(bounds(heading, dock).top(), selecting ? 177.0 : 59.0);
            QCOMPARE(heading->height(), 17.0);
        }
        QStringList expectedFields;
        for (const auto &controlEntry : spec["controls"].toArray()) {
            const auto control = controlEntry.toObject();
            const auto field = control["fieldId"].toString();
            const auto kind = control["kind"].toString();
            expectedFields.append(field);
            auto *wrapper = visualItem(panel, qPrintable("elementsControl-" + field));
            QVERIFY2(wrapper && wrapper->isVisible(), qPrintable(id + '/' + field));
            const auto rect = control["rect"].toArray();
            QCOMPARE(bounds(wrapper, dock), QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble()));
            QVERIFY(visualText(wrapper, control["label"].toString()));
            if (kind == "dimensions") {
                for (int dimension = 0; dimension < 2; ++dimension) {
                    auto *input = visualItem(wrapper, qPrintable(QString("elementsInput-field-0-%1").arg(dimension)));
                    QVERIFY(input && input->isVisible());
                    QCOMPARE(input->height(), 22.0);
                    QCOMPARE(input->property("text").toString(), QString("1080 px"));
                    QVERIFY(bounds(input, wrapper).right() <= wrapper->width());
                }
            } else if (kind == "slider") {
                auto *input = visualItem(wrapper, qPrintable("elementsInput-" + field));
                auto *slider = visualItem(wrapper, qPrintable("elementsSlider-" + field));
                QVERIFY(input && slider);
                QCOMPARE(input->size(), QSizeF(206, 22));
                QCOMPARE(slider->size(), QSizeF(398, 22));
                QCOMPARE(bounds(slider, wrapper), QRectF(0, 30, 398, 22));
                QCOMPARE(slider->property("value").toDouble(), control["initial"].toDouble());
                auto *thumb = visualItem(slider, qPrintable("elementsThumb-" + field));
                QVERIFY(thumb && thumb->isVisible());
                QTRY_COMPARE(thumb->property("status").toInt(), 1);
                QCOMPARE(thumb->size(), QSizeF(18, 18));
                const auto thumbAsset = id == "polygon" ? "thumb-650-15793.svg" : id == "fill" ? "thumb-650-15795.svg" : id == "stroke" ? "thumb-650-15796.svg" : "thumb.svg";
                QCOMPARE(thumb->property("source").toUrl().fileName(), QString(thumbAsset));
            } else if (kind == "toggle") {
                auto *toggle = visualItem(wrapper, qPrintable("elementsToggle-" + field));
                QVERIFY(toggle);
                QCOMPARE(toggle->size(), QSizeF(38, 22));
                QCOMPARE(toggle->property("checked").toBool(), control["initial"].toBool());
            } else if (kind == "color") {
                auto *input = visualItem(wrapper, qPrintable("elementsInput-" + field));
                auto *picker = visualItem(wrapper, qPrintable("elementsColorPicker-" + field));
                QVERIFY(input && picker);
                QCOMPARE(input->property("text").toString(), control["initial"].toString());
                QCOMPARE(input->height(), 22.0);
                QCOMPARE(picker->size(), QSizeF(28, 28));
                QCOMPARE(bounds(picker, wrapper).right(), 398.0);
            } else if (kind == "choices") {
                const auto options = spec["options"].toObject()[field].toArray();
                for (int option = 0; option < options.size(); ++option) {
                    auto *button = visualItem(wrapper, qPrintable(QString("elementsChoice-%1-%2").arg(field).arg(option)));
                    QVERIFY(button);
                    QCOMPARE(button->height(), 22.0);
                    QCOMPARE(button->property("text").toString(), options[option].toString());
                    QVERIFY(bounds(button, wrapper).right() <= wrapper->width());
                }
            }
        }
        QStringList actualFields;
        for (const auto &field : listProperty(panel, "presentedFields")) {
            const auto object = field.toMap();
            actualFields.append(object.isEmpty() ? field.toString() : object.value("id", object.value("fieldId")).toString());
        }
        QCOMPARE(actualFields, expectedFields);
        if (spec["shapePalette"].toBool()) {
            auto *palette = visualItem(panel, "elementsShapePalette");
            QVERIFY(palette);
            QCOMPARE(bounds(palette, dock), QRectF(17, 88, 398, 72));
            const QStringList assets{"rectangle.svg", "ellipse.svg", "polygon.svg", "line.svg"};
            for (int shape = 0; shape < 4; ++shape) {
                auto *button = visualItem(palette, qPrintable(QString("elementsShapeChoice-%1").arg(shape)));
                QVERIFY(button);
                QCOMPARE(button->size(), QSizeF(34, 34));
                auto *icon = visualItem(button, qPrintable(button->objectName() + "_icon"));
                QVERIFY(icon);
                QTRY_COMPARE(icon->property("status").toInt(), 1);
                QCOMPARE(icon->size(), QSizeF(18, 18));
                QCOMPARE(icon->property("source").toUrl().fileName(), assets[shape]);
            }
        }
        if (spec["selectionActions"].toBool()) {
            auto *palette = visualItem(panel, "elementsSelectionPalette");
            QVERIFY(palette);
            QCOMPARE(bounds(palette, dock), QRectF(17, 93, 398, 72));
            QCOMPARE(canvas->property("elementSelectionCount").toInt(), spec["selectionCount"].toInt());
            for (const auto *action : {"inspect", "multiple", "clear"}) {
                auto *button = visualItem(palette, qPrintable(QString("elementsSelectionAction-") + action));
                QVERIFY(button);
                QCOMPARE(button->size(), QSizeF(34, 34));
                auto *icon = visualItem(button, qPrintable(button->objectName() + "_icon"));
                QVERIFY(icon);
                QTRY_COMPARE(icon->property("status").toInt(), 1);
                QCOMPARE(icon->size(), QSizeF(18, 18));
            }
        }
        if (spec.contains("scope")) {
            const auto scopeId = id == "fill" ? "apply" : id == "inspect" ? "active" : "selection";
            auto *combo = visualItem(panel, qPrintable(QString("elementsScope-") + scopeId));
            QVERIFY(combo);
            auto *arrow = visualItem(combo, qPrintable(QString("elementsScopeArrow-") + scopeId));
            QVERIFY(arrow);
            QCOMPARE(combo->size(), QSizeF(206, 20));
            QCOMPARE(combo->property("text").toString(), spec["scope"].toObject()["initial"].toString());
            QTRY_COMPARE(arrow->property("status").toInt(), 1);
            QCOMPARE(arrow->size(), QSizeF(18, 18));
            QCOMPARE(arrow->property("source").toUrl().fileName(), QString("arrow-down-tone-primary.svg"));
        }
        for (const auto &label : spec["labels"].toArray()) QVERIFY2(visualText(panel, label.toString()), qPrintable(label.toString()));
        if (spec.contains("selectionRows")) {
            const auto rows = spec["selectionRows"].toObject();
            const auto names = rows["names"].toArray();
            const auto selected = rows["selected"].toArray();
            const auto top = rows["rect"].toArray()[1].toDouble();
            for (int row = 0; row < names.size(); ++row) {
                auto *entryItem = visualItem(panel, qPrintable(QString("elementsSelectionRow-%1").arg(row)));
                auto *check = visualItem(panel, qPrintable(QString("elementsSelectionCheck-%1").arg(row)));
                QVERIFY(entryItem && check);
                QCOMPARE(bounds(entryItem, dock), QRectF(17, top + row * 44, 398, 36));
                QVERIFY(visualText(entryItem, names[row].toString()));
                QCOMPARE(check->property("checked").toBool(), selected[row].toBool());
                QCOMPARE(check->property("checkedAssetSourceEnabled").toUrl().fileName(), QString("checkbox.svg"));
            }
        }
        if (!captureDirectory.isEmpty()) {
            // Capture the settled LVRS state, after the button color transition.
            QTest::qWait(300);
            const auto capture = dock->grabToImage(QSize(432, 559));
            QVERIFY(capture);
            QTRY_VERIFY(!capture->image().isNull());
            QVERIFY(capture->image().save(captureDirectory + "/elements-" + id + ".png"));
        }
    }
    // Real click-driven selection changes retain document bytes and history.
    const auto encoded = iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
    const auto revision = canvas->revision();
    click(&window, visualItem(panel, "elementsClearSelectAll"));
    QTRY_COMPARE(canvas->property("elementSelectionCount").toInt(), 3);
    chooseView("clear");
    click(&window, visualItem(panel, "elementsInspectFirst"));
    QTRY_COMPARE(canvas->property("elementSelectionCount").toInt(), 1);
    QTRY_COMPARE(panel->property("currentViewId").toString(), "inspect");
    click(&window, visualItem(panel, "elementsSelectionAction-multiple"));
    QTRY_COMPARE(panel->property("currentViewId").toString(), "multiple");
    click(&window, visualItem(panel, "elementsSelectionCheck-1"));
    QTRY_COMPARE(canvas->property("elementSelectionCount").toInt(), 2);
    click(&window, visualItem(panel, "elementsInvert"));
    QTRY_COMPARE(canvas->property("elementSelectionCount").toInt(), 1);
    click(&window, visualItem(panel, "elementsSelectAll"));
    QTRY_COMPARE(canvas->property("elementSelectionCount").toInt(), 3);
    click(&window, visualItem(panel, "elementsSelectionAction-clear"));
    QTRY_COMPARE(canvas->property("elementSelectionCount").toInt(), 0);
    QCOMPARE(canvas->revision(), revision);
    QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, encoded);
    // Figma dimensions and real three-element selection are checked above.
    // Use a small document for pixel and input contracts, avoiding repeated
    // rasterization of the unrelated 1080 px ellipse behind the edited shape.
    QVERIFY(canvas->createCanvas({{"width", 128}, {"height", 128}, {"unit", "px"}, {"background", "Transparent"}}));
    canvas->configureTool("elements", defaults);
    QVERIFY(canvas->createElement(QRectF(8, 8, 80, 80)));
    const auto editRevision = canvas->revision();
    chooseView("rectangle");
    auto *numeric = visualItem(panel, "elementsInput-field-1");
    QVERIFY(numeric);
    const auto before = iiSharedCanvas::renderFrameRegion(*canvas->document(), 0, {{0, 0}, {64, 64}}, {64, 64});
    QVERIFY(before.ok());
    click(&window, numeric);
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    for (const char character : QByteArrayLiteral("32 px")) QTest::keyClick(&window, character);
    QTest::keyClick(&window, Qt::Key_Return);
    QTRY_COMPARE(values()["field-1"].toInt(), 32);
    QVERIFY(canvas->revision() > editRevision);
    const auto after = iiSharedCanvas::renderFrameRegion(*canvas->document(), 0, {{0, 0}, {64, 64}}, {64, 64});
    QVERIFY(after.ok());
    QVERIFY(after.pixels.pixels != before.pixels.pixels);
    numeric->setProperty("text", "NaN");
    QVERIFY(QMetaObject::invokeMethod(numeric, "commit"));
    QCOMPARE(values()["field-1"].toInt(), 32);
    QCOMPARE(numeric->property("text").toString(), QString("32 px"));
    auto *slider = visualItem(panel, "elementsSlider-field-1");
    slider->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_End);
    QTRY_COMPARE(values()["field-1"].toInt(), 256);
    auto *widthInput = visualItem(panel, "elementsInput-field-0-0");
    auto *heightInput = visualItem(panel, "elementsInput-field-0-1");
    auto *constrain = visualItem(panel, "elementsToggle-field-3");
    QVERIFY(widthInput && heightInput && constrain);
    const auto dimension = [&](QQuickItem *input, const QString &text) {
        QVERIFY(input->setProperty("text", text));
        QVERIFY(QMetaObject::invokeMethod(input, "commit"));
    };
    dimension(widthInput, "65536 px");
    QCOMPARE(values()["field-0"].toList(), QVariantList({65536, 65536}));
    const auto maximumRevision = canvas->revision();
    dimension(widthInput, "65537 px");
    QCOMPARE(values()["field-0"].toList(), QVariantList({65536, 65536}));
    QCOMPARE(canvas->revision(), maximumRevision);
    click(&window, constrain);
    dimension(widthInput, "100 px"); dimension(heightInput, "200 px");
    click(&window, constrain);
    const auto ratioDraft = values();
    const auto ratioRevision = canvas->revision();
    const auto ratioDocument = iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
    dimension(widthInput, "40000 px"); // Derived height 80000 exceeds the other-axis limit.
    QCOMPARE(values(), ratioDraft);
    QCOMPARE(canvas->revision(), ratioRevision);
    QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, ratioDocument);
    QCOMPARE(widthInput->property("text").toString(), QString("100 px"));
    click(&window, constrain);
    dimension(widthInput, "1080 px"); dimension(heightInput, "1080 px");
    click(&window, constrain);
    chooseView("fill");
    // A rejected Image edit can restore Solid before the source dialog opens.
    // The picker must still receive the requested Image settings, while cancel
    // retains the last accepted native shape and its draft.
    const auto acceptedDraft = values();
    const auto acceptedRevision = canvas->revision();
    const auto acceptedDocument = iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
    QCOMPARE(acceptedDraft["field-2"].toString(), QString("Solid"));
    click(&window, visualItem(panel, "elementsChoice-field-2-3"));
    auto *sourceDialog = editor->findChild<QObject *>("editorSourceDialog");
    QTRY_VERIFY(sourceDialog && sourceDialog->property("visible").toBool());
    QCOMPARE(sourceDialog->property("targetTool").toString(), QString("elements"));
    QCOMPARE(mapProperty(sourceDialog, "settings")["field-2"].toString(), QString("Image"));
    QVERIFY(QMetaObject::invokeMethod(sourceDialog, "reject"));
    QTRY_VERIFY(!sourceDialog->property("visible").toBool());
    QCOMPARE(values(), acceptedDraft);
    QCOMPARE(canvas->revision(), acceptedRevision);
    QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, acceptedDocument);
    click(&window, visualItem(panel, "elementsChoice-field-2-1"));
    QTRY_COMPARE(values()["field-2"].toString(), "Gradient");
    auto *color = visualItem(panel, "elementsColorPicker-field-16");
    click(&window, color);
    auto *colorSheet = state->findChild<QObject *>("editorColorSheet");
    QTRY_VERIFY(colorSheet && colorSheet->property("opened").toBool());
    auto *picker = item(colorSheet, "editorColorPicker");
    QVERIFY(picker);
    QVERIFY(QMetaObject::invokeMethod(picker, "setColor", Q_ARG(QColor, QColor("#123456"))));
    QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
    QTRY_VERIFY(!colorSheet->property("visible").toBool());
    QTRY_COMPARE(values()["field-16"].toString(), "#123456");
    click(&window, color);
    QTRY_VERIFY(colorSheet->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(picker, "setColor", Q_ARG(QColor, QColor("#ABCDEF"))));
    QVERIFY(QMetaObject::invokeMethod(picker, "cancel"));
    QTRY_VERIFY(!colorSheet->property("visible").toBool());
    QCOMPARE(values()["field-16"].toString(), QString("#123456"));
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(dock->isVisible());
    QCOMPARE(panel->property("currentViewId").toString(), QString("fill"));
    QCOMPARE(values()["field-16"].toString(), QString("#123456"));
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 1)));
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTest::qWait(40); // Flush the previous loader item's deferred deletion.
    QTRY_VERIFY((panel = visualItem(dock, "editorElementsPanel")));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("fill"));
    chooseView("fill"); // Settle the recreated panel before clicking its header.
    click(&window, visualItem(panel, "elementsHeaderAction"));
    QTRY_COMPARE(values()["field-2"].toString(), "Solid");
    QTRY_COMPARE(values()["field-16"].toString(), "#8B7CFF");
    QCOMPARE(values()["field-1"].toInt(), 256); // Fill reset retains geometry.
    chooseView("rectangle");
    click(&window, visualItem(panel, "elementsHeaderAction"));
    QTRY_COMPARE(values()["field-1"].toInt(), 24);
    // The ordinary editor entry point remains reachable with the new panel.
    QSignalSpy back(editor.data(), SIGNAL(backRequested()));
    auto *sidebar = item(editor.data(), "editorSidebar");
    click(&window, visualItem(sidebar, "desktopAction_home"));
    QCOMPARE(back.size(), 1);
}

void GuiTests::editorButtonInteractionStates_data()
{
    QTest::addColumn<bool>("icon");
    QTest::addColumn<int>("kind");
    for (bool icon : {false, true})
        for (int kind : {0, 1, 2})
            QTest::newRow(qPrintable(QString("%1-kind-%2").arg(icon ? "icon" : "label").arg(kind))) << icon << kind;
}

void GuiTests::editorButtonInteractionStates()
{
    QFETCH(bool, icon);
    QFETCH(int, kind);
    QTest::failOnWarning(QRegularExpression(".*(Binding loop|Cannot assign|Unable to assign).*"));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    const auto source = QString(R"QML(
import QtQuick
import LVRS 1.0 as LV
Item {
    id: fixture
    width: 360; height: 140
    property bool selected: false
    property bool acceptSelection: true
    property int activations: 0
    LV.%1 {
        id: control
        objectName: "stateButton"
        x: 32; y: 32; width: 160; height: 40
        text: "Select"
        tone: %2
        motionEnabled: false
        enterKeyActivation: true
        Accessible.name: "Select"
        Accessible.onToggleAction: control.click()
        onClicked: { fixture.activations++; if (fixture.acceptSelection) fixture.selected = true }
        EditorButtonState { button: control; selected: fixture.selected }
    }
})QML").arg(icon ? "IconButton" : "LabelButton").arg(kind).toUtf8();
    component.setData(source, sourceUrl("Views/Editor/ButtonStateFixture.qml"));
    QScopedPointer<QQuickItem> fixture(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(fixture, qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(360, 140);
    fixture->setParentItem(window.contentItem());
    const auto detach = qScopeGuard([&] { fixture->setParentItem(nullptr); });
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    if (QGuiApplication::platformName() == "cocoa") {
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
    }
    auto *button = visualItem(fixture.data(), "stateButton");
    QVERIFY(button);
    auto *surface = button->property("background").value<QQuickItem *>();
    QVERIFY(surface);
    QSignalSpy kindChanges(button, SIGNAL(toneChanged()));
    const auto size = button->size();
    const auto color = [&] { return surface->property("color").value<QColor>(); };
    const auto buttonColor = [&](const char *property) { return button->property(property).value<QColor>(); };
    const auto center = button->mapToScene(button->boundingRect().center()).toPoint();
    const QPoint outside(8, 125);
    QTest::mouseMove(&window, outside);
    QTRY_COMPARE(color(), buttonColor("backgroundColor"));
    QTest::mouseMove(&window, center);
    QTRY_VERIFY(button->property("hovered").toBool());
    QTRY_COMPARE(color(), buttonColor("backgroundColorHover"));
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, center);
    QTRY_COMPARE(button->property("interactionPhase").toString(), QString("press"));
    QTRY_COMPARE(color(), buttonColor("backgroundColorPressed"));
    QCOMPARE(fixture->property("activations").toInt(), 0);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, center);
    QTRY_COMPARE(fixture->property("activations").toInt(), 1);
    QTRY_VERIFY(button->property("checked").toBool());
    QTest::mouseMove(&window, outside);
    QTRY_COMPARE(color(), buttonColor("backgroundColorPressed"));

    // Repeated selection, Space, Enter and the accessibility path share the
    // accepted model. They must neither change Kind nor clear a required choice.
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);
    QTRY_VERIFY(button->property("checked").toBool());
    button->forceActiveFocus(Qt::TabFocusReason);
    for (auto key : {Qt::Key_Space, Qt::Key_Return}) {
        const int count = fixture->property("activations").toInt();
        QTest::keyPress(&window, key);
        QTRY_VERIFY(button->property("down").toBool());
        QCOMPARE(fixture->property("activations").toInt(), count);
        QTest::keyRelease(&window, key);
        QTRY_COMPARE(fixture->property("activations").toInt(), count + 1);
        QTRY_VERIFY(button->property("checked").toBool());
    }
    const int count = fixture->property("activations").toInt();
    QQmlExpression accessibility(qmlContext(button), button, "Accessible.toggleAction()");
    accessibility.evaluate();
    QVERIFY2(!accessibility.hasError(), qPrintable(accessibility.error().toString()));
    QTRY_COMPARE(fixture->property("activations").toInt(), count + 1);
    QTRY_VERIFY(button->property("checked").toBool());
    button->setEnabled(false);
    QTRY_COMPARE(color(), buttonColor("backgroundColorDisabled"));
    button->setEnabled(true);
    QTest::mouseMove(&window, outside);
    QTRY_COMPARE(color(), buttonColor("backgroundColorPressed"));

    fixture->setProperty("selected", false);
    QTRY_VERIFY(!button->property("checked").toBool());
    QTRY_COMPARE(color(), buttonColor("backgroundColor"));
    fixture->setProperty("acceptSelection", false);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);
    QTRY_VERIFY(!button->property("checked").toBool());
    QTRY_COMPARE(color(), buttonColor("backgroundColorHover"));
    QTest::mouseMove(&window, outside);
    QTRY_COMPARE(color(), buttonColor("backgroundColor"));
    QCOMPARE(button->property("tone").toInt(), kind);
    QCOMPARE(kindChanges.size(), 0);
    QCOMPARE(button->size(), size);
}

void GuiTests::editorPanelButtonKindsStayFixed()
{
    QTest::failOnWarning(QRegularExpression(".*(Binding loop|Cannot assign|Unable to assign).*"));
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    editor->setProperty("desktopPanelWidth", 432);
    QQuickWindow window;
    window.resize(1400, 950);
    editor->setParentItem(window.contentItem());
    editor->setSize(window.size());
    const auto detach = qScopeGuard([&] { editor->setParentItem(nullptr); });
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    if (QGuiApplication::platformName() == "cocoa") {
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
    }
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *dock = item(editor.data(), "editorDesktopPanel");
    auto *state = editor->findChild<QObject *>("editorToolSheet");
    QVERIFY(canvas && toolbar && dock && state);
    QVERIFY(canvas->createCanvas({{"width", 128}, {"height", 128}, {"unit", "px"}, {"background", "Transparent"}}));
    const auto tools = listProperty(toolbar, "tools");
    int checkedButtons = 0;
    int checkedSelections = 0;
    QSet<QString> visited;
    const auto inspect = [&] {
        QTest::mouseMove(&window, QPoint(2, 2));
        QCoreApplication::processEvents();
        QList<QQuickItem *> controls;
        const auto collect = [&](const auto &self, QQuickItem *parent) -> void {
            if (parent->isVisible() && parent->property("tone").isValid()) controls.append(parent);
            for (auto *child : parent->childItems()) self(self, child);
        };
        collect(collect, dock);
        for (auto *control : controls) {
            auto *binding = control->findChild<QObject *>("editorButtonState", Qt::FindDirectChildrenOnly);
            const bool elements = control->property("selectionChecked").isValid() && control->property("checkable").toBool();
            if (control->objectName().startsWith("photoChoice-") || control->objectName().startsWith("photoTimer-"))
                QVERIFY2(binding, qPrintable(control->objectName() + " must bind accepted selection to the fixed button's pressed state"));
            if (!binding && !elements) continue;
            control->setProperty("motionEnabled", false);
            const int kind = control->property("tone").toInt();
            QVERIFY2(kind == 1 || kind == 2, qPrintable(control->objectName()));
            const bool selected = binding ? binding->property("selected").toBool() : control->property("selectionChecked").toBool();
            QTRY_COMPARE(control->property("checked").toBool(), selected);
            auto *surface = control->property("background").value<QQuickItem *>();
            QVERIFY(surface);
            const auto expected = control->property(!control->isEnabled() ? "backgroundColorDisabled" : selected ? "backgroundColorPressed" : "backgroundColor").value<QColor>();
            if (surface->property("color").value<QColor>() != expected)
                qInfo() << "Button surface diagnostic" << control->objectName() << state->property("toolId")
                        << "selected" << selected << "checked" << control->property("checked")
                        << "phase" << control->property("interactionPhase") << "binding" << binding
                        << "actual" << surface->property("color") << "expected" << expected;
            QTRY_COMPARE(surface->property("color").value<QColor>(), expected);
            QQmlExpression accessible(qmlContext(control), control, "Accessible.checked");
            if (accessible.evaluate().toBool() != selected)
                qInfo() << "Button accessibility diagnostic" << control->objectName() << state->property("toolId")
                        << "selected" << selected << "error" << accessible.error();
            QCOMPARE(accessible.evaluate().toBool(), selected);
            ++checkedButtons;
            if (selected) ++checkedSelections;
        }
    };
    const QStringList safePrefixes{"elementsViewTab", "textTab-", "layerTab-", "editorFileView-", "photoTab-", "backgroundTab-", "maskTool-", "maskMode-"};
    for (int index = 0; index < tools.size(); ++index) {
        const auto tool = tools[index].toMap().value("key").toString();
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index)));
        QTRY_COMPARE(state->property("toolId").toString(), tool);
        QTest::qWait(40);
        visited.insert(tool);
        inspect();
        QStringList names;
        const auto collectNames = [&](const auto &self, QQuickItem *parent) -> void {
            if (parent->isVisible())
                for (const auto &prefix : safePrefixes)
                    if (parent->objectName().startsWith(prefix)) { names.append(parent->objectName()); break; }
            for (auto *child : parent->childItems()) self(self, child);
        };
        collectNames(collectNames, dock);
        // Background Mask has a second set of tool/mode selections. Appending
        // these names also exercises the newly created view in this fixture.
        if (tool == "background")
            names << "backgroundTab-Mask" << "maskTool-Lasso" << "maskTool-Brush" << "maskMode-Subtract" << "maskMode-Add" << "backgroundTab-Replace";
        if (tool == "camera-photo")
            names << "photoTab-Camera" << "photoTimer-cameraTimer-3" << "photoTimer-cameraTimer-0"
                  << "photoChoice-cameraDrive-1" << "photoChoice-cameraDrive-0"
                  << "photoTab-Capture" << "photoTimer-captureDelay-5" << "photoTimer-captureDelay-0";
        for (const auto &name : names) {
            auto *button = visualItem(dock, qPrintable(name));
            if (!button || !button->isVisible() || !button->isEnabled()) continue;
            QPointer<QQuickItem> current(button);
            const int kind = button->property("tone").toInt();
            QSignalSpy changes(button, SIGNAL(toneChanged()));
            QVERIFY(QMetaObject::invokeMethod(button, "click"));
            QCoreApplication::processEvents();
            if (current) QCOMPARE(current->property("tone").toInt(), kind);
            QCOMPARE(changes.size(), 0);
            inspect();
        }
    }
    QCOMPARE(visited.size(), 18);
    QVERIFY(checkedButtons > 80);
    QVERIFY(checkedSelections > 20);
    qInfo() << "Fixed-kind editor checks:" << visited.size() << "tools," << checkedButtons << "button states," << checkedSelections << "selected surfaces";
}

void GuiTests::panelRowsKeepLabelsPassive_data()
{
    QTest::addColumn<int>("type");
    QTest::newRow("heading") << 0;
    QTest::newRow("navigation") << 2;
    QTest::newRow("toggle") << 3;
    QTest::newRow("action") << 5;
    QTest::newRow("action-group") << 6;
    QTest::newRow("select") << 8;
    QTest::newRow("input") << 9;
}

void GuiTests::panelRowsKeepLabelsPassive()
{
    QFETCH(int, type);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/PanelRow.qml"));
    QScopedPointer<QQuickItem> row(qobject_cast<QQuickItem *>(component.createWithInitialProperties({
        {"type", type}, {"label", "Parameter"}, {"description", "Supporting text"},
        {"showLeadingIcon", false}, {"showDescription", false}, {"value", "View"},
        {"inputText1", "12"}, {"showPrimaryAction", type != 9},
        {"selector", QVariantMap{{"items", QStringList{"First", "Second"}}}}
    })));
    QVERIFY2(row, qPrintable(component.errorString()));
    QQuickWindow window;
    window.setColor(QColor("#1e1e1e")); window.resize(430, 160);
    row->setParentItem(window.contentItem()); row->setX(16); row->setY(16);
    row->setWidth(398); row->setHeight(row->implicitHeight());
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(row->acceptedMouseButtons(), Qt::NoButton);
    QVERIFY(!row->acceptHoverEvents());
    QVERIFY(!row->activeFocusOnTab());
    auto *label = visualItem(row.data(), "panelRow_label"); QVERIFY(label);
    QSignalSpy edits(row.data(), SIGNAL(edited(QString,QVariant)));
    QSignalSpy actions(row.data(), SIGNAL(actionTriggered(QString,QVariant)));
    QSignalSpy navigation(row.data(), SIGNAL(clicked()));
    QTest::mouseMove(&window, QPoint(1, 150)); QTest::qWait(100);
    const auto before = window.grabWindow(); QVERIFY(!before.isNull());
    const auto labelPoint = label->mapToScene(label->boundingRect().center()).toPoint();
    QTest::mouseMove(&window, labelPoint); QTest::qWait(100);
    QCOMPARE(window.grabWindow(), before);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, labelPoint); QTest::qWait(100);
    QCOMPARE(window.grabWindow(), before);
    QCOMPARE(edits.size(), 0); QCOMPARE(actions.size(), 0); QCOMPARE(navigation.size(), 0);
    QVERIFY(!row->hasActiveFocus());

    // The passive surface must not disable the native controls within it.
    if (type == 2) {
        click(&window, visualItem(row.data(), "panelRow_navigation")); QCOMPARE(navigation.size(), 1);
    } else if (type == 3) {
        click(&window, visualItem(row.data(), "panelRow_toggle"));
        QTRY_VERIFY(row->property("checked").toBool()); QCOMPARE(edits.size(), 1);
    } else if (type == 5 || type == 6) {
        click(&window, visualItem(row.data(), "panelRow_primaryAction")); QCOMPARE(actions.size(), 1);
        if (type == 6) {
            click(&window, visualItem(row.data(), "panelRow_secondaryAction")); QCOMPARE(actions.size(), 2);
        }
    } else if (type == 8) {
        auto *selector = visualItem(row.data(), "panelRow_selector"); QVERIFY(selector);
        selector->forceActiveFocus(Qt::TabFocusReason);
        QTest::keyClick(&window, Qt::Key_Down);
        QTRY_COMPARE(row->property("selectorIndex").toInt(), 1); QCOMPARE(edits.size(), 1);
    } else if (type == 9) {
        auto *input = visualItem(row.data(), "panelRow_input1"); QVERIFY(input);
        click(&window, input); QTest::keyClick(&window, Qt::Key_3);
        QTRY_VERIFY(row->property("inputText1").toString().contains('3')); QVERIFY(!edits.isEmpty());
    }
}

void GuiTests::editorLayerPanelFourStates()
{
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(1280, 559);
    editor->setParentItem(window.contentItem()); editor->setSize(window.size());
    QVERIFY(editor->setProperty("desktopPanelWidth", 432));
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    QVERIFY(canvas);
    QVERIFY(canvas->createCanvas({{"width", 32}, {"height", 24}, {"unit", "px"}, {"background", "Transparent"}}));
    QImage pixels(32, 24, QImage::Format_ARGB32);
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 32; ++x) pixels.setPixel(x, y, qRgb(x * 8, y * 10, 128));
    QVERIFY(canvas->insertPixels(pixels, "Subject"));
    auto *toolbar = item(editor.data(), "editorToolbar"); QVERIFY(toolbar);
    const auto tools = toolbar->property("tools").value<QJSValue>().toVariant().toList();
    int layerIndex = -1;
    for (int index = 0; index < tools.size(); ++index) if (tools[index].toMap()["key"].toString() == "layers") layerIndex = index;
    QVERIFY(layerIndex >= 0);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, layerIndex)));
    auto *dock = item(editor.data(), "editorDesktopPanel"); QVERIFY(dock);
    QTRY_VERIFY(item(dock, "editorLayersPanel"));
    auto *panel = item(dock, "editorLayersPanel");
    QTRY_COMPARE(panel->width(), 398.0);
    const auto layerItem = [&](const char *name) -> QQuickItem * {
        if (auto *found = visualItem(panel, name)) return found;
        if (auto *found = item(panel, name)) return found;
        return visualItem(window.contentItem(), name);
    };
    const auto layerClick = [&](QQuickItem *control) {
        QVERIFY(control);
        if (QGuiApplication::platformName() == "cocoa") {
            window.requestActivate(); QVERIFY(QTest::qWaitForWindowActive(&window));
        }
        // Generated hierarchy/action delegates can be replaced during hover.
        const auto position = control->mapToScene(control->boundingRect().center()).toPoint();
        QTest::mouseMove(&window, position);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, position);
    };
    auto *hierarchy = layerItem("layerHierarchy"); QVERIFY(hierarchy);
    QCOMPARE(hierarchy->property("generatedRowHeight").toInt(), 17);
    const auto sourceId = canvas->selectedLayerId();
    QTRY_COMPARE(hierarchy->property("activeItemKey").toString(), sourceId);
    const auto initialCount = canvas->document()->layers.size();
    layerClick(layerItem("layerDuplicate"));
    QTRY_COMPARE(canvas->document()->layers.size(), initialCount + 1);
    const auto copiedId = canvas->selectedLayerId();
    QTRY_COMPARE(hierarchy->property("activeItemKey").toString(), copiedId);
    QTRY_VERIFY(layerItem(("layerRow-" + copiedId).toUtf8().constData()));
    QVERIFY(layerItem(("layerRow-" + copiedId).toUtf8().constData())->property("resolvedSelected").toBool());
    QVERIFY(!layerItem(("layerRow-" + sourceId).toUtf8().constData())->property("resolvedSelected").toBool());
    QTest::qWait(80);
    layerClick(layerItem(("layerRow-" + sourceId).toUtf8().constData()));
    QTRY_COMPARE(canvas->selectedLayerId(), sourceId);
    QTRY_COMPARE(hierarchy->property("activeItemKey").toString(), sourceId);
    QTest::qWait(80);
    layerClick(layerItem(("layerRow-" + copiedId).toUtf8().constData()));
    QTRY_COMPARE(canvas->selectedLayerId(), copiedId);
    QTRY_COMPARE(hierarchy->property("activeItemKey").toString(), copiedId);
    const auto checkFooter = [&] {
        QTest::mouseMove(&window, {10, window.height() - 5});
        const QStringList actions{"layerNew", "layerGroup", "layerMerge", "layerDuplicate", "layerMask", "layerLock", "layerDelete"};
        const QStringList assets{"NewLayer.svg", "Group.svg", "Merge.svg", "DuplicateLayer.svg", "LayerMask.svg", "Lock.svg", "Generaldelete.svg"};
        for (int index = 0; index < actions.size(); ++index) {
            auto *button = layerItem(actions[index].toUtf8().constData()); QVERIFY(button);
            const auto destructive = actions[index] == "layerDelete";
            QCOMPARE(button->property("tone").toInt(), destructive ? 3 : 1);
            auto *background = button->property("background").value<QQuickItem *>(); QVERIFY(background);
            const auto expected = button->property("toneBackgroundColor").value<QColor>();
            QTRY_COMPARE(background->property("color").value<QColor>(), expected);
            auto *icon = visualItem(button, index == 0 ? "iconMenuButton_icon" : "iconButton_icon"); QVERIFY(icon);
            QTRY_COMPARE(icon->property("status").toInt(), 1);
            QCOMPARE(icon->size(), QSizeF(18, 18));
            QCOMPARE(button->property("iconSource").toUrl().fileName(), assets[index]);
            const QFileInfo asset(button->property("iconSource").toUrl().toLocalFile());
            QVERIFY(asset.isFile() && asset.size() > 0);
            if (destructive) {
                const auto capture = icon->grabToImage(QSize(72, 72)); QVERIFY(capture);
                QTRY_VERIFY(!capture->image().isNull());
                bool foregroundVisible = false;
                for (int y = 0; y < capture->image().height(); ++y)
                    for (int x = 0; x < capture->image().width(); ++x) {
                        const auto pixel = capture->image().pixelColor(x, y);
                        foregroundVisible |= pixel.alpha() > 40 && iconColorsNear(pixel, QColor("#CED0D6"));
                    }
                QVERIFY2(foregroundVisible, "Figma's light delete glyph must remain visible on the Destructive surface");
            }
        }
    };
    QVERIFY(!layerItem("layerMask")->isEnabled());
    checkFooter();
    layerClick(layerItem("layerLock"));
    QTRY_VERIFY(canvas->toolState()["pixelLocked"].toBool());
    QVERIFY(canvas->toolState()["positionLocked"].toBool());
    QVERIFY(!layerItem("layerDelete")->isEnabled());
    checkFooter();
    layerClick(layerItem("layerLock"));
    QTRY_COMPARE(canvas->selectedLayer()["lock"].toString(), QString("None"));
    checkFooter();
    QTRY_VERIFY(layerItem(("layerVisible-" + copiedId).toUtf8().constData()));
    QTest::qWait(80); // Finish native hierarchy delegate replacement and layout.
    auto *visibilityIcon = visualItem(layerItem(("layerVisible-" + copiedId).toUtf8().constData()), "iconButton_icon");
    QVERIFY(visibilityIcon);
    QTRY_COMPARE(visibilityIcon->property("status").toInt(), 1);
    QCOMPARE(visibilityIcon->size(), QSizeF(16, 16));
    QCOMPARE(visibilityIcon->property("source").toUrl().fileName(), QString("Visibility.svg"));
    // Check the exported eye's axes in scene coordinates, including parent transforms.
    const auto leftEye = visibilityIcon->mapToScene(QPointF(0, 8));
    const auto rightEye = visibilityIcon->mapToScene(QPointF(16, 8));
    const auto topEye = visibilityIcon->mapToScene(QPointF(8, 2.5));
    const auto bottomEye = visibilityIcon->mapToScene(QPointF(8, 13.5));
    QVERIFY2(rightEye.x() - leftEye.x() > bottomEye.y() - topEye.y(), "The visibility eye must be wider than it is tall.");
    QVERIFY(qAbs(leftEye.y() - rightEye.y()) < 0.01);
    QVERIFY(qAbs(topEye.x() - bottomEye.x()) < 0.01);
    const auto eyeCapture = visibilityIcon->grabToImage(QSize(64, 64));
    QVERIFY(eyeCapture);
    QTRY_VERIFY(!eyeCapture->image().isNull());
    QRect eyeInk;
    const auto eyeImage = eyeCapture->image();
    for (int y = 0; y < eyeImage.height(); ++y)
        for (int x = 0; x < eyeImage.width(); ++x)
            if (qAlpha(eyeImage.pixel(x, y)) > 16) eyeInk = eyeInk.united(QRect(x, y, 1, 1));
    QVERIFY2(eyeInk.width() > eyeInk.height(), "The rendered visibility glyph must be a horizontal eye.");
    layerClick(layerItem(("layerVisible-" + copiedId).toUtf8().constData()));
    QTRY_VERIFY(!canvas->selectedLayer()["visible"].toBool());
    QTRY_VERIFY(layerItem(("layerVisible-" + copiedId).toUtf8().constData()));
    QTest::qWait(80);
    layerClick(layerItem(("layerVisible-" + copiedId).toUtf8().constData()));
    QTRY_VERIFY(canvas->selectedLayer()["visible"].toBool());
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    const QStringList tabs{"layer", "adjust", "histogram", "information"};
    for (int index = 0; index < tabs.size(); ++index) {
        auto *tab = layerItem(("layerTab-" + tabs[index]).toUtf8().constData()); QVERIFY(tab);
        layerClick(tab); QTRY_COMPARE(panel->property("panelIndex").toInt(), index);
        QCOMPARE(tab->property("tone").toInt(), 2); // Fixed Borderless; selection uses its pressed state.
        QTest::qWait(80); // Instantiate the new view and polish its action layout.
        if (index == 1) {
            QTRY_VERIFY(layerItem("layerAction-exposure"));
            auto *exposure = layerItem("layerAction-exposure"); QVERIFY(exposure && exposure->isEnabled());
            auto *gradient = layerItem("layerAction-gradient"); QVERIFY(gradient && !gradient->isEnabled());
            const auto original = canvas->selectedRasterPixels()->pixels;
            layerClick(exposure);
            QTRY_VERIFY(panel->property("modalActive").toBool());
            auto *adjustmentSheet = panel->findChild<QObject *>("layerAdjustmentSheet");
            QVERIFY(adjustmentSheet);
            QTRY_VERIFY(adjustmentSheet->property("opened").toBool());
            QTRY_VERIFY(layerItem("editorNumeric-field-1"));
            auto *numeric = layerItem("editorNumeric-field-1");
            numeric->setProperty("text", "1.0 EV");
            QVERIFY(QMetaObject::invokeMethod(numeric, "accepted", Q_ARG(QString, "1.0 EV")));
            QTRY_VERIFY(canvas->selectedRasterPixels()->pixels != original);
            auto *done = layerItem("layerAdjustmentDone"); QVERIFY(done);
            QVERIFY(window.contentItem()->boundingRect().contains(bounds(done, window.contentItem())));
            layerClick(done);
            QTRY_VERIFY(!panel->property("modalActive").toBool());
            layerClick(layerItem("layerResetAdjustments"));
            QTRY_COMPARE(canvas->selectedRasterPixels()->pixels, original);
        } else if (index == 2) {
            QTRY_VERIFY(panel->property("histogram").value<QJSValue>().toVariant().toMap()["pixelCount"].toLongLong() > 0);
            layerClick(layerItem("layerAction-Red"));
            QTRY_COMPARE(panel->property("channel").toString(), QString("Red"));
            layerClick(layerItem("layerAction-shadow"));
            QTRY_VERIFY(item(editor.data(), "editorClippingOverlay")->isVisible());
            QVERIFY(!canvas->toolState()["clippingOverlay"].toUrl().isEmpty());
            QTest::qWait(80);
            layerClick(layerItem("layerAction-shadow"));
            QTRY_VERIFY(!panel->property("shadowClipping").toBool());
        } else if (index == 3) {
            layerClick(layerItem("layerAction-sample"));
            QVERIFY(canvas->layerColorSample()["picking"].toBool());
            QVERIFY(canvas->sampleLayerColor({4, 4}));
            QTest::qWait(80); // Sampling enables and recreates the footer delegates.
            layerClick(layerItem("layerAction-copyColor"));
            QCOMPARE(QGuiApplication::clipboard()->text(), QString("#202880"));
            layerClick(layerItem("layerAction-copyDetails"));
            QVERIFY(QGuiApplication::clipboard()->text().contains("32 × 24 px"));
        }
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory)); QTest::qWait(80);
            const auto capture = dock->grabToImage(QSize(432, 559)); QVERIFY(capture);
            QTRY_VERIFY(!capture->image().isNull());
            QVERIFY(capture->saveToFile(captureDirectory + "/layers-" + tabs[index] + ".png"));
        }
    }
    QVERIFY(editor->setProperty("desktopPanelWidth", 280));
    QTRY_COMPARE(panel->width(), 246.0);
    QVERIFY(panel->implicitHeight() >= 472);
}

void GuiTests::editorTextPanelFourStates_data()
{
    QTest::addColumn<int>("viewIndex");
    QTest::addColumn<int>("referenceHeight");
    QTest::newRow("content") << 0 << 653;
    QTest::newRow("typography") << 1 << 718;
    QTest::newRow("layout") << 2 << 949;
    QTest::newRow("appearance") << 3 << 1636;
}

void GuiTests::editorTextPanelFourStates()
{
    QFETCH(int, viewIndex); QFETCH(int, referenceHeight);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QVERIFY(editor->setProperty("desktopPanelWidth", 432));
    QQuickWindow window; window.resize(1280, referenceHeight);
    editor->setParentItem(window.contentItem()); editor->setSize(window.size());
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    QVERIFY(canvas && canvas->createCanvas({{"width", 256}, {"height", 144}, {"unit", "px"}, {"background", "Transparent"}}));
    auto *toolbar = item(editor.data(), "editorToolbar"); QVERIFY(toolbar);
    const auto tools = listProperty(toolbar, "tools");
    int index = -1;
    for (int i = 0; i < tools.size(); ++i) if (tools[i].toMap()["key"].toString() == "text") index = i;
    QVERIFY(index >= 0); QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index)));
    auto *dock = item(editor.data(), "editorDesktopPanel"); QVERIFY(dock);
    QTRY_VERIFY(item(dock, "editorTextPanel"));
    auto *panel = item(dock, "editorTextPanel");
    QTRY_COMPARE(panel->width(), 398.0);
    QVERIFY(panel->setProperty("panelIndex", viewIndex));
    QTRY_VERIFY(panel->height() > 0);
    QTRY_COMPARE(qRound(panel->height()), referenceHeight - 34);
    const QStringList names{"textTab-content", "textTab-typography", "textTab-layout", "textTab-appearance"};
    for (int i = 0; i < names.size(); ++i) {
        auto *tab = visualItem(panel, qPrintable(names[i])); QVERIFY(tab);
        QCOMPARE(tab->size(), QSizeF(22, 22));
        auto *icon = visualItem(tab, "iconButton_icon"); QVERIFY(icon);
        QTRY_COMPARE(icon->property("status").toInt(), 1);
        QCOMPARE(icon->size(), QSizeF(18, 18));
    }
    // Every original Figma icon must be a loaded, proportionate local asset.
    const auto images = panel->findChildren<QQuickItem *>();
    for (auto *image : images) {
        const auto source = image->property("source").toUrl();
        if (!source.toString().contains("Assets/Text/")) continue;
        QTRY_COMPARE(image->property("status").toInt(), 1);
        QVERIFY(!source.scheme().startsWith("http"));
        const QStringList insetAssets{"AutoRefresh.svg", "LeftAlign.svg", "CenterAlign.svg", "RightAlign.svg"};
        const qreal artworkSize = insetAssets.contains(source.fileName()) ? 16 : 18;
        QCOMPARE(image->size(), QSizeF(artworkSize, artworkSize));
    }
    QVERIFY(canvas->createText({24, 30}));
    const auto before = iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels;
    if (viewIndex == 0) {
        auto *input = visualItem(panel, "textContentInput"); QVERIFY(input);
        QVERIFY(input->setProperty("text", QString("Two lines\n한글 text")));
        QTRY_COMPARE(canvas->toolValues("text")["field-0"].toString(), QString("Two lines\n한글 text"));
        auto *role = visualItem(panel, "textRole-Title"); QVERIFY(role);
        QVERIFY(QMetaObject::invokeMethod(role, "clicked"));
        QTRY_COMPARE(canvas->toolValues("text")["field-2"].toInt(), 26);
    } else if (viewIndex == 1) {
        auto *weight = visualItem(panel, "textChoice-textWeight-3"); QVERIFY(weight);
        QVERIFY(QMetaObject::invokeMethod(weight, "clicked"));
        QTRY_COMPARE(canvas->toolValues("text")["textWeight"].toString(), QString("Bold"));
    } else if (viewIndex == 2) {
        auto *anchor = visualItem(panel, "textChoice-field-5-0"); QVERIFY(anchor);
        QVERIFY(QMetaObject::invokeMethod(anchor, "clicked"));
        QTRY_VERIFY(canvas->toolValues("text")["textLayoutEnabled"].toBool());
    } else {
        auto *opacity = visualItem(panel, "textInput-textOpacity"); QVERIFY(opacity);
        QVERIFY(opacity->setProperty("text", QString("40%")));
        QVERIFY(QMetaObject::invokeMethod(opacity, "commit"));
        QTRY_COMPARE(canvas->toolValues("text")["textOpacity"].toInt(), 40);
    }
    QVERIFY(iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels != before);
    const auto text = canvas->toolValues("text")["field-0"];
    QVERIFY(panel->setProperty("panelIndex", (viewIndex + 1) % 4));
    QVERIFY(panel->setProperty("panelIndex", viewIndex));
    QCOMPARE(canvas->toolValues("text")["field-0"], text);
    auto *state = editor->findChild<QObject *>("editorToolSheet"); QVERIFY(state);
    QVERIFY(QMetaObject::invokeMethod(state, "resetTool"));
    QTRY_COMPARE(canvas->toolValues("text")["field-2"].toInt(), 13);
    // Allow the shared LVRS release animation to settle before visual capture.
    QTest::qWait(250);
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_TEXT_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QDir().mkpath(captureDirectory);
        auto capture = dock->grabToImage(QSize(432, qRound(dock->height()))); QVERIFY(capture);
        QSignalSpy ready(capture.data(), &QQuickItemGrabResult::ready); QVERIFY(ready.wait(5000));
        QVERIFY(capture->image().save(captureDirectory + '/' + QTest::currentDataTag() + ".png"));
    }
    // Resizing keeps all four tabs usable and numeric controls in the panel.
    QVERIFY(editor->setProperty("desktopPanelWidth", 320));
    QTRY_COMPARE(panel->width(), 286.0);
    for (const auto &name : names) QVERIFY(bounds(visualItem(panel, qPrintable(name)), panel).right() <= panel->width());
}


void GuiTests::editorCameraPhotoViews_data()
{
    QTest::addColumn<QSize>("size"); QTest::addColumn<bool>("mobile"); QTest::addColumn<int>("panelWidth");
    QTest::newRow("desktop-432") << QSize(1440, 1040) << false << 432;
    QTest::newRow("desktop-280") << QSize(1280, 900) << false << 280;
    QTest::newRow("mobile-402") << QSize(402, 874) << true << 0;
    QTest::newRow("mobile-320") << QSize(320, 640) << true << 0;
}
void GuiTests::storageBrowserRoutes_data()
{
    QTest::addColumn<QString>("target");
    QTest::newRow("desktop") << QString("macos");
    QTest::newRow("mobile") << QString("ios");
}

void GuiTests::storageBrowserRoutes()
{
    QFETCH(QString, target);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/storage-browser-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::cyan);
    for (const auto &area : {QString("Files"), QString("Generation History")})
        for (int i = 0; i < 25; ++i)
            QVERIFY(image.save(storage.filePath(area + QString("/Study-%1.png").arg(i, 2, 10, QChar('0')))));
    for (int i = 0; i < 6; ++i)
        QVERIFY(image.save(storage.filePath(QString("Published/Published-%1.png").arg(i))));
    QVERIFY(QDir().mkpath(storage.filePath("Files/Folder #1")));
    const auto nested = storage.filePath("Files/Folder #1/inner % #.png");
    QVERIFY(image.save(nested));
    const auto poster = storage.filePath("Generation History/clip-poster.png");
    QVERIFY(image.save(poster));
    QFile video(storage.filePath("Generation History/clip-0001.mp4"));
    QVERIFY(video.open(QIODevice::WriteOnly)); video.write("metadata fixture"); video.close();

    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme"); QVERIFY(theme);
    QVERIFY(theme->setProperty("targetOverride", target));
    const auto resetTheme = qScopeGuard([&] { theme->setProperty("targetOverride", ""); });
    QDesktopServices::setUrlHandler("society", this, "captureSocietyUrl");
    m_societyUrl.clear();
    const auto resetHandler = qScopeGuard([] { QDesktopServices::unsetUrlHandler("society"); });
    engine.setInitialProperties({{"initialContainerPath", storage.path()}});
    engine.load(sourceUrl("Main.qml")); QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->resize(target == "ios" ? 430 : 1374, 900);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *home = item(window, target == "ios" ? "mobileHome" : "desktopHome"); QVERIFY(home);
    auto *quick = item(window, "quickGenerate"); QVERIFY(quick);
    quick->setProperty("prompt", "Keep this draft while browsing");
    auto *history = window->findChild<QObject *>("generationHistoryModel"); QVERIFY(history);
    QTRY_COMPARE(history->property("recentFiles").toList().size(), 20);
    QVERIFY(!item(window, "storageBrowser"));
    auto *allFiles = item(home, target == "ios" ? "recentFilesViewAll" : "desktopRecentFilesViewAll");
    QVERIFY(allFiles); click(window, allFiles);
    QTRY_COMPARE(window->property("storageBrowserSection").toString(), "files");
    auto *browser = item(window, "storageBrowser"); QVERIFY(browser && browser->isVisible());
    QTRY_COMPARE(listProperty(browser, "sectionFiles").size(), 26);
    QCOMPARE(browser->property("container").value<QObject *>(), window->property("societyContainer").value<QObject *>());
    QVERIFY(!window->findChild<QObject *>("canvasEditorLoader")->property("active").toBool());
    auto *grid = item(browser, "storageBrowserGrid"); QVERIFY(grid);
    auto *search = item(browser, "storageBrowserSearch"); QVERIFY(search);
    QVERIFY(search->setProperty("text", "sTuDy-00"));
    QTRY_COMPARE(listProperty(browser, "entries").size(), 1);
    QVERIFY(search->setProperty("text", ""));
    QTRY_COMPARE(listProperty(browser, "entries").size(), 26);
    QVERIFY(browser->setProperty("sortOrder", "name"));
    QCOMPARE(listProperty(browser, "entries")[1].toMap()["name"].toString(), "Study-00.png");
    const auto folder = listProperty(browser, "entries").first().toMap();
    QVERIFY(folder["isDirectory"].toBool());
    QCOMPARE(folder["name"].toString(), "Folder #1");
    auto *folderCard = visualItem(grid, "storageBrowserCard0"); QVERIFY(folderCard);
    auto *folderOpen = visualItem(folderCard, "card_action"); QVERIFY(folderOpen);
    click(window, folderOpen);
    QTRY_COMPARE(browser->property("directory").toString(), "Folder #1");
    QCOMPARE(listProperty(browser, "entries").size(), 1);
    const auto nestedEntry = listProperty(browser, "entries").first().toMap();
    QCOMPARE(nestedEntry["source"].toUrl(), QUrl::fromLocalFile(nested));
    auto *nestedCard = visualItem(grid, "storageBrowserCard0"); QVERIFY(nestedCard);
    click(window, nestedCard); click(window, item(browser, "storageBrowserOpen"));
    QTRY_VERIFY(window->property("editorVisible").toBool());
    auto *canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas")); QVERIFY(canvas);
    QCOMPARE(canvas->canvasWidth(), 32); QCOMPARE(canvas->canvasHeight(), 24);
    QVERIFY(QMetaObject::invokeMethod(window, "closeCanvas"));
    QTRY_VERIFY(browser->isVisible());
    QCOMPARE(browser->property("directory").toString(), "Folder #1");
    QCOMPARE(quick->property("prompt").toString(), "Keep this draft while browsing");
    click(window, item(browser, "storageBrowserUp"));
    QTRY_COMPARE(browser->property("directory").toString(), "");
    QVERIFY(search->setProperty("text", "inner % #"));
    // Opening a PNG preserves the original and adds its editable IISC document.
    // Both files must remain discoverable by the same nested-name query.
    QTRY_COMPARE(listProperty(browser, "entries").size(), 2);
    QSet<QString> matchingPaths;
    for (const auto &entry : listProperty(browser, "entries")) matchingPaths.insert(entry.toMap()["path"].toString());
    QVERIFY(matchingPaths.contains(nested));
    matchingPaths.remove(nested);
    QCOMPARE(matchingPaths.size(), 1);
    QVERIFY(QFileInfo(*matchingPaths.begin()).isFile());
    QVERIFY(matchingPaths.begin()->endsWith(".iisc"));
    click(window, item(browser, "storageBrowserBack"));
    QTRY_VERIFY(!browser->isVisible());

    auto *allHistory = item(home, "viewAllGenerationHistory");
    if (target == "ios") allHistory = item(home, "mobileGenerationHistoryViewAll");
    QVERIFY(allHistory); click(window, allHistory);
    QTRY_COMPARE(window->property("storageBrowserSection").toString(), "generation-history");
    QTRY_COMPARE(listProperty(browser, "sectionFiles").size(), 26); // 25 PNGs + video, with one poster.
    QCOMPARE(listProperty(browser, "entries").size(), 26);
    const auto files = listProperty(browser, "sectionFiles");
    int videos = 0;
    for (const auto &entry : files) if (entry.toMap()["mediaType"].toString() == "Video") {
        ++videos;
        QCOMPARE(entry.toMap()["mediaSource"].toUrl(), QUrl::fromLocalFile(video.fileName()));
        QCOMPARE(entry.toMap()["previewSource"].toUrl().toLocalFile(), poster);
        QCOMPARE(entry.toMap()["imageSource"].toUrl(), entry.toMap()["previewSource"].toUrl());
    }
    QCOMPARE(videos, 1);
    QVERIFY(search->setProperty("text", "new-arrival"));
    QTRY_COMPARE(listProperty(browser, "entries").size(), 0);
    const auto added = storage.filePath("Generation History/new-arrival.png");
    QVERIFY(image.save(added));
    click(window, item(browser, "storageBrowserRefresh"));
    QTRY_COMPARE_WITH_TIMEOUT(listProperty(browser, "entries").size(), 1, 15000);
    QVERIFY(QFile::remove(added));
    click(window, item(browser, "storageBrowserRefresh"));
    QTRY_COMPARE_WITH_TIMEOUT(listProperty(browser, "entries").size(), 0, 15000);
    auto *publishedSection = visualItem(browser, "storageBrowserSection-published"); QVERIFY(publishedSection);
    click(window, publishedSection);
    QTRY_COMPARE(listProperty(browser, "sectionFiles").size(), 6);
    QCOMPARE(browser->property("query").toString(), "");
    for (int width : {target == "ios" ? 430 : 960, 320}) {
        window->resize(width, 700); QTest::qWait(50);
        QTRY_VERIFY_WITH_TIMEOUT(grid->width() > 180 && grid->height() > 100, 10000);
        for (const auto *name : {"storageBrowserBack", "storageBrowserSearch", "storageBrowserOpen"}) {
            const auto area = bounds(item(browser, name), browser);
            QVERIFY(area.left() >= 0 && area.right() <= browser->width());
        }
    }
    QVERIFY(m_societyUrl.isEmpty());
    const auto evidence = qEnvironmentVariable("DREAMSCAPES_STORAGE_EVIDENCE");
    if (!evidence.isEmpty()) {
        QVERIFY(QDir().mkpath(evidence));
        window->resize(target == "ios" ? 430 : 1374, 900);
        for (int index = 0; index < 6; ++index) {
            QQuickItem *card = nullptr;
            QTRY_VERIFY((card = visualItem(grid, qPrintable("storageBrowserCard" + QString::number(index)))));
            QTRY_COMPARE_WITH_TIMEOUT(card->property("previewStatus").toInt(), 1, 15000);
        }
        QTest::qWait(100);
        const auto capture = window->grabWindow();
        if (window->rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
            for (int index = 0; index < 6; ++index) {
                const auto *card = visualItem(grid, qPrintable("storageBrowserCard" + QString::number(index)));
                const auto point = card->mapToScene({card->width() / 2, 24}) * window->devicePixelRatio();
                const auto pixel = capture.pixelColor(point.toPoint());
                QVERIFY2(pixel.red() < 80 && pixel.green() > 180 && pixel.blue() > 180,
                    "The native gallery must paint each real preview after a compact-window reflow.");
            }
            qInfo() << "STORAGE_NATIVE_PREVIEWS" << window->rendererInterface()->graphicsApi() << "count" << 6;
        }
        QVERIFY(capture.save(evidence + "/storage-" + target + ".png"));
    }
}

void GuiTests::storageBrowserDownloadOpensFreshAlias_data()
{
    QTest::addColumn<QString>("target"); QTest::addColumn<QString>("section");
    QTest::newRow("desktop-files") << QString("macos") << QString("files");
    QTest::newRow("desktop-history") << QString("macos") << QString("generation-history");
    QTest::newRow("mobile-files") << QString("ios") << QString("files");
    QTest::newRow("mobile-history") << QString("ios") << QString("generation-history");
}
void GuiTests::storageBrowserDownloadOpensFreshAlias()
{
    QFETCH(QString, target); QFETCH(QString, section);
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/download-open-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    const auto relative = section == "files" ? QString("Files/Download % #.png") : QString("Generation History/Download % #.png");
    const auto original = storage.filePath(relative);
    QImage image(37, 29, QImage::Format_RGB32); image.fill(Qt::cyan); QVERIFY(image.save(original));
    QQmlApplicationEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme"); QVERIFY(theme);
    QVERIFY(theme->setProperty("targetOverride", target));
    engine.setInitialProperties({{"initialContainerPath", storage.path()}}); engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()); QVERIFY(window);
    window->resize(target == "ios" ? 402 : 1400, target == "ios" ? 874 : 900);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *generation = qobject_cast<GenerationController *>(window->findChild<QObject *>("generationController")); QVERIFY(generation);
    const auto key = section + "/Download % #.png";
    auto stale = generation->container()->alias(key); QVERIFY(!stale.isEmpty());
    stale["available"] = false; stale["source"] = QUrl{}; stale["path"] = "";
    QSignalSpy ready(generation->fileDownload(), &iiSocietyHelper::FileDownload::ready);
    // The bytes may arrive after a card snapshot. Opening must resolve a fresh
    // native Alias, and leaving the collection must retire the open intent.
    QVERIFY(QMetaObject::invokeMethod(window, "openHomeFile", Q_ARG(QVariant, stale)));
    QCOMPARE(window->property("storageBrowserSection").toString(), section);
    QVERIFY(generation->fileDownload()->active());
    QVERIFY(window->setProperty("storageBrowserSection", QString{}));
    QTest::qWait(100); QCOMPARE(ready.size(), 0); QVERIFY(!window->property("editorVisible").toBool());
    QVERIFY(!generation->fileDownload()->active());
    QVERIFY(QMetaObject::invokeMethod(window, "openHomeFile", Q_ARG(QVariant, stale)));
    QTRY_VERIFY(window->property("editorVisible").toBool()); QCOMPARE(ready.size(), 1);
    const auto file = ready.first().first().toMap(); QCOMPARE(file.value("source").toUrl(), QUrl::fromLocalFile(original));
    auto *canvas = qobject_cast<EditorCanvas *>(item(window, "editorBlankCanvas")); QVERIFY(canvas);
    QCOMPARE(canvas->canvasWidth(), 37); QCOMPARE(canvas->canvasHeight(), 29);
    QCOMPARE(QImage(original).size(), QSize(37, 29));
    QVERIFY(window->property("pendingFileKey").toString().isEmpty());
    QVERIFY(generation->jobs().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(window, "closeCanvas"));
    QTRY_VERIFY(window->property("storageBrowserVisible").toBool());
    QCOMPARE(window->property("storageBrowserSection").toString(), section);
    window->hide();
}

void GuiTests::storageBrowserUnavailableEntriesAndContainerChanges()
{
    iiSocietyContainer::Container mirror;
    const QVariantMap remote{{"key", "files/constructor/missing.png"}, {"containerId", "first"},
        {"section", "files"}, {"relativePath", "constructor/missing.png"}, {"name", "missing.png"},
        {"exists", true}, {"available", false}, {"isImage", true}};
    mirror.synchronize("first", "/mirror-one", {remote}, true);
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Home/StorageBrowser.qml"));
    QScopedPointer<QQuickItem> browser(qobject_cast<QQuickItem *>(component.createWithInitialProperties({
        {"container", QVariant::fromValue<QObject *>(&mirror)}, {"section", "files"}})));
    QVERIFY2(browser, qPrintable(component.errorString()));
    QQuickWindow window; window.resize(430, 700);
    browser->setParentItem(window.contentItem()); browser->setSize(window.size()); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(listProperty(browser.get(), "sectionFiles").size(), 1);
    QCOMPARE(listProperty(browser.get(), "entries").first().toMap()["name"].toString(), "constructor");
    QSignalSpy snapshots(browser.get(), SIGNAL(mirroredAliasesChanged())); QVERIFY(snapshots.isValid());
    mirror.setQuery("Home-only search");
    QCOMPARE(snapshots.size(), 0);
    browser->setVisible(false);
    auto second = remote; second["key"] = "files/other.png"; second["relativePath"] = "other.png"; second["name"] = "other.png";
    auto text = remote; text["key"] = "files/document.txt"; text["relativePath"] = "document.txt"; text["name"] = "document.txt";
    text["isImage"] = false; text["available"] = true; text["source"] = QUrl::fromLocalFile("/mirror-one/Files/document.txt");
    mirror.synchronize("first", "/mirror-one", {remote, second, text}, true);
    QCOMPARE(snapshots.size(), 0);
    QCOMPARE(listProperty(browser.get(), "sectionFiles").size(), 1);
    browser->setVisible(true);
    QTRY_COMPARE(listProperty(browser.get(), "sectionFiles").size(), 3);
    QCOMPARE(listProperty(item(browser.get(), "storageBrowserSelection"), "images").size(), 0);
    QVERIFY(browser->setProperty("directory", "constructor"));
    QQuickItem *missingCard = nullptr;
    QTRY_VERIFY((missingCard = visualItem(item(browser.get(), "storageBrowserGrid"), "storageBrowserCard0")));
    QTRY_COMPARE(missingCard->property("metadata").toString(), "Not downloaded");
    QSignalSpy requested(browser.get(), SIGNAL(fileRequested(QVariant)));
    QVERIFY(QMetaObject::invokeMethod(browser.get(), "activate", Q_ARG(QVariant, remote)));
    QCOMPARE(requested.size(), 1);
    const auto requestedValue = requested.first().first();
    const auto requestedFile = requestedValue.metaType() == QMetaType::fromType<QJSValue>()
        ? requestedValue.value<QJSValue>().toVariant().toMap() : requestedValue.toMap();
    QCOMPARE(requestedFile.value("key"), remote.value("key"));
    QVERIFY(browser->property("notice").toString().isEmpty());
    QVERIFY(browser->setProperty("selectedKey", remote["key"]));
    QVERIFY(item(browser.get(), "storageBrowserOpen")->isEnabled());
    QCOMPARE(item(browser.get(), "storageBrowserOpen")->property("text").toString(), "Download and open");
    QQmlComponent transferComponent(&engine);
    transferComponent.setData(R"(import QtQml
        QtObject {
            property var state: ({state: "downloading", key: "files/constructor/missing.png", containerId: "first", name: "missing.png", completed: 50, total: 100})
            property bool active: true
            function cancel() { active = false; state = Object.assign({}, state, {state: "cancelled"}) }
        })", QUrl{});
    QScopedPointer<QObject> transfer(transferComponent.create()); QVERIFY2(transfer, qPrintable(transferComponent.errorString()));
    QVERIFY(browser->setProperty("download", QVariant::fromValue(transfer.get())));
    QTRY_VERIFY(item(browser.get(), "storageBrowserDownload")->isVisible());
    QVERIFY(item(browser.get(), "storageBrowserDownloadStatus")->property("text").toString().contains("50%"));
    QCOMPARE(item(browser.get(), "storageBrowserDownloadProgress")->property("currentValue").toInt(), 50);
    QVERIFY(!item(browser.get(), "storageBrowserOpen")->isEnabled());
    click(&window, item(browser.get(), "storageBrowserDownloadCancel"));
    QTRY_VERIFY(!transfer->property("active").toBool());
    QVERIFY(item(browser.get(), "storageBrowserDownloadRetry")->isVisible());
    click(&window, item(browser.get(), "storageBrowserDownloadRetry")); QCOMPARE(requested.size(), 2);
    QVERIFY(item(browser.get(), "storageBrowserSearch")->setProperty("text", "missing"));
    QTRY_COMPARE(browser->property("query").toString(), "missing");
    mirror.synchronize("second", "/mirror-two", {}, true);
    QTRY_COMPARE(browser->property("directory").toString(), "");
    QCOMPARE(browser->property("query").toString(), "");
    QCOMPARE(browser->property("selectedKey").toString(), "");
    QVERIFY(listProperty(browser.get(), "entries").isEmpty());
    mirror.synchronize({}, {}, {}, false);
    QTRY_VERIFY(item(browser.get(), "storageBrowserEmpty")->isVisible());
    QVERIFY(item(browser.get(), "storageBrowserEmpty")->property("text").toString().contains("No mirrored drive"));
}

void GuiTests::societyContainerMirrorIsOwnerProvided()
{
    QQmlEngine engine;
    QVERIFY(qmlTypeId("Dreamscapes.Storage", 1, 0, "SocietyContainer") >= 0);
    QQmlComponent component(&engine);
    component.setData("import Dreamscapes.Storage 1.0\nSocietyContainer {}", QUrl());
    QVERIFY(component.isError());
    QVERIFY(component.errorString().contains("Provided by iiSocietyHelper"));

    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/alias-binding-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QImage image(16, 12, QImage::Format_ARGB32); image.fill(Qt::cyan);
    QVERIFY(image.save(storage.filePath("Generation History/alias.png")));
    QQmlApplicationEngine application;
    application.setInitialProperties({{"initialContainerPath", storage.path()}});
    application.load(sourceUrl("Main.qml"));
    QCOMPARE(application.rootObjects().size(), 1);
    auto *window = application.rootObjects().first();
    auto *generation = qobject_cast<GenerationController *>(window->findChild<QObject *>("generationController"));
    auto *history = window->findChild<QObject *>("generationHistoryModel");
    QVERIFY(generation && history);
    QCOMPARE(window->property("societyContainer").value<QObject *>(), generation->container());
    QCOMPARE(history->property("snapshot").value<QObject *>(), generation->container());
    const auto alias = generation->container()->alias("generation-history/alias.png");
    QVERIFY(alias["exists"].toBool()); QVERIFY(alias["available"].toBool());
    QCOMPARE(alias["previewSource"].toUrl().toLocalFile(), storage.filePath("Generation History/alias.png"));
    QVERIFY(QFile::remove(storage.filePath("Generation History/alias.png")));
    QVERIFY(generation->container()->alias("generation-history/alias.png")["exists"].toBool());
    generation->container()->refresh();
    QVERIFY(generation->container()->alias("generation-history/alias.png").isEmpty());
}
void GuiTests::editorCameraPhotoViews()
{
    QFETCH(QSize, size); QFETCH(bool, mobile); QFETCH(int, panelWidth);
    QQmlEngine engine; auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", mobile ? "ios" : "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"mobileLayout", mobile}, {"desktopPanelWidth", panelWidth ? panelWidth : 432}})));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window; window.resize(size); editor->setParentItem(window.contentItem()); editor->setSize(size); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    auto *state = editor->findChild<QObject *>("editorToolSheet"); auto *toolbar = item(editor.data(), "editorToolbar");
    QVERIFY(canvas && state && toolbar);
    auto *sources = qobject_cast<EditorCameraPhoto *>(canvas->photoSources()); QVERIFY(sources);
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/photo-ui-XXXXXX.societycontainer"); QVERIFY(directory.isValid());
    QVERIFY(iiSocietyContainer::SocietyDrive::create(directory.path()));
    for (int index = 0; index < 4; ++index) {
        QImage image(80, 48, QImage::Format_ARGB32); image.fill(QColor::fromHsv(index * 80, 180, 210));
        QVERIFY(image.save(directory.filePath(QString("Photo_%1.png").arg(index))));
    }
    QVERIFY(canvas->createCanvas({{"width", 160}, {"height", 100}, {"unit", "px"}, {"background", "Transparent"}}));
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 2)));
    auto *parent = mobile ? window.contentItem() : item(editor.data(), "editorDesktopPanel"); QVERIFY(parent);
    QQuickItem *panel = nullptr; QTRY_VERIFY((panel = mobile ? item(state, "editorCameraPhotoPanel") : visualItem(parent, "editorCameraPhotoPanel")) && panel->isVisible());
    const auto edit = [&](const QString &id, QVariant value) { return QMetaObject::invokeMethod(state, "setValue", Q_ARG(QVariant, id), Q_ARG(QVariant, value)); };
    QVERIFY(edit("galleryLibrary", "Pictures")); QVERIFY(edit("galleryFolder", QUrl::fromLocalFile(directory.path())));
    const QStringList views{"Camera", "Capture", "Gallery"}; const QList<int> heights{810, 738, 549};
    for (int index = 0; index < views.size(); ++index) {
        QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, views[index])));
        QTRY_COMPARE(panel->property("currentViewId").toString(), views[index]);
        QTest::qWait(250);
        auto *navigation = visualItem(panel, "photoNavigation"); QVERIFY(navigation);
        QCOMPARE(navigation->width(), 78.0); QCOMPARE(navigation->height(), 30.0);
        auto *tab = visualItem(panel, qPrintable("photoTab-" + views[index])); QVERIFY(tab);
        QCOMPARE(tab->property("tone").toInt(), 2); // Fixed Borderless through selection and interaction.
        QCOMPARE(tab->width(), 22.0); QCOMPARE(tab->property("iconSize").toInt(), 18);
        for (auto *control : panel->findChildren<QQuickItem *>()) {
            if (!control->isVisible() || !control->objectName().startsWith("photo") || control->objectName() == "photoStatus") continue;
            const auto rect = bounds(control, panel);
            QVERIFY2(rect.left() >= -1 && rect.right() <= panel->width() + 1, qPrintable(views[index] + '/' + control->objectName()));
        }
        if (!mobile && panelWidth == 432) QCOMPARE(qRound(panel->implicitHeight()), heights[index]);
        if (index == 0) {
            auto *preview = visualItem(panel, "photoCameraPreview"); QVERIFY(preview); QCOMPARE(preview->height(), 144.0);
            auto *aspect = visualItem(panel, "photoOption-cameraAspect"); QVERIFY(aspect && aspect->activeFocusOnTab());
            QSignalSpy dropdownOpened(aspect, SIGNAL(clicked()));
            aspect->forceActiveFocus(); QTest::keyClick(&window, Qt::Key_Return);
            QTRY_COMPARE(dropdownOpened.size(), 1);
            auto *options = panel->findChild<QObject *>("photoOptions"); QVERIFY(options);
            QTRY_VERIFY(options->property("visible").toBool()); QVERIFY(QMetaObject::invokeMethod(options, "close"));
            auto *timer = visualItem(panel, "photoTimer-cameraTimer-3"); QVERIFY(timer);
            QVERIFY(QMetaObject::invokeMethod(timer, "clicked")); QTRY_COMPARE(canvas->toolValues("camera-photo")["cameraTimer"].toInt(), 3);
            QVERIFY(edit("cameraAspect", "4:3")); QVERIFY(edit("cameraAngle", "90°"));
            QCOMPARE(panel->property("previewAspectRatio").toDouble(), 0.75);
            QVERIFY(edit("cameraAspect", "16:9")); QVERIFY(edit("cameraAngle", "0°"));
        }
        if (index == 2) {
            QTRY_VERIFY(!sources->galleryBusy()); QCOMPARE(sources->gallery().size(), 4);
            sources->setSelected(sources->gallery().first().toMap()["id"].toString(), false);
            const auto count = canvas->layers().size(); auto *import = visualItem(panel, "photoImport-Gallery"); QVERIFY(import && import->isEnabled());
            QSignalSpy ready(sources, &EditorCameraPhoto::importReady);
            QVERIFY(QMetaObject::invokeMethod(import, "clicked")); QTRY_COMPARE(ready.size(), 1);
            QVERIFY2(canvas->layers().size() == count + 1, qPrintable(canvas->error() + " / " + sources->error()));
            QVERIFY(canvas->canUndo()); QVERIFY(canvas->undo()); QCOMPARE(canvas->layers().size(), count);
            sources->filterGallery("Photo_1", "Name"); QCOMPARE(sources->gallery().size(), 1);
        }
        const auto capture = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QVERIFY(QDir().mkpath(capture));
            if (!mobile) {
                const auto rendered = parent->grabToImage(QSize(panelWidth, qRound(parent->height()))); QVERIFY(rendered);
                QTRY_VERIFY(!rendered->image().isNull()); QVERIFY(rendered->image().save(capture + '/' + QTest::currentDataTag() + '-' + views[index] + ".png"));
            } else QVERIFY(window.grabWindow().save(capture + '/' + QTest::currentDataTag() + '-' + views[index] + ".png"));
        }
    }
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("Camera"))));
    QCOMPARE(canvas->toolValues("camera-photo")["cameraTimer"].toInt(), 3);
    editor.reset(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void GuiTests::editorTextPanelMobile_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("text-402") << QSize(402, 874);
    QTest::newRow("text-320") << QSize(320, 640);
}

void GuiTests::editorTextPanelMobile()
{
    QFETCH(QSize, size);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "ios"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"mobileLayout", true}})));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window; window.resize(size);
    editor->setParentItem(window.contentItem()); editor->setSize(size);
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas")); QVERIFY(canvas);
    QVERIFY(canvas->createCanvas({{"width", 256}, {"height", 144}, {"unit", "px"}, {"background", "Transparent"}}));
    auto *toolbar = item(editor.data(), "editorToolbar"); QVERIFY(toolbar);
    auto *sheet = editor->findChild<QObject *>("editorToolSheet"); QVERIFY(sheet);
    const auto tools = listProperty(toolbar, "tools"); int index = -1;
    for (int i = 0; i < tools.size(); ++i) if (tools[i].toMap()["key"].toString() == "text") index = i;
    QVERIFY(index >= 0); QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index)));
    QTRY_VERIFY(sheet->property("opened").toBool());
    auto *panel = item(sheet, "editorTextPanel"); QVERIFY(panel);
    auto *viewport = item(sheet, "sheet_viewport"); QVERIFY(viewport);
    QTRY_COMPARE(panel->width(), sheet->property("availableContentWidth").toReal());
    const QStringList tabs{"content", "typography", "layout", "appearance"};
    for (int view = 0; view < tabs.size(); ++view) {
        QTest::qWait(250); viewport->setProperty("contentY", 0);
        auto *tab = visualItem(panel, qPrintable("textTab-" + tabs[view])); QVERIFY(tab);
        click(&window, tab); QTRY_COMPARE(panel->property("panelIndex").toInt(), view);
        QTest::qWait(250);
        QVERIFY(panel->width() <= sheet->property("width").toReal());
        if (view == 3) QVERIFY(viewport->property("contentHeight").toReal() > viewport->height());
        const qreal end = qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height());
        viewport->setProperty("contentY", end); QCOMPARE(viewport->property("contentY").toReal(), end);
    }
    QVERIFY(canvas->createText({20, 30}));
    auto *opacity = visualItem(panel, "textInput-textOpacity"); QVERIFY(opacity);
    QVERIFY(opacity->setProperty("text", QString("50%"))); QVERIFY(QMetaObject::invokeMethod(opacity, "commit"));
    QTRY_COMPARE(canvas->toolValues("text")["textOpacity"].toInt(), 50);
    QTest::keyClick(&window, Qt::Key_Escape); QTRY_VERIFY(!sheet->property("visible").toBool());
    QCOMPARE(canvas->toolValues("text")["textOpacity"].toInt(), 50);
}

void GuiTests::editorCompactPanelsMatchFigma_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<int>("panelWidth");
    QTest::newRow("desktop-reference") << QString("macos") << 432;
    QTest::newRow("desktop-default") << QString("macos") << 342;
    QTest::newRow("desktop-narrow") << QString("macos") << 280;
    QTest::newRow("ios") << QString("ios") << 402;
    QTest::newRow("android-narrow") << QString("android") << 320;
}

void GuiTests::editorCompactPanelsMatchFigma()
{
    QFETCH(QString, target);
    QFETCH(int, panelWidth);
    const bool mobile = target != "macos";
    QFile fixture(sourceUrl("Views/Editor/fixtures/CompactPanelsFigma.json").toLocalFile());
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const auto states = QJsonDocument::fromJson(fixture.readAll()).object()["states"].toArray();
    QCOMPARE(states.size(), 38);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", target));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"mobileLayout", mobile}, {"desktopPanelWidth", panelWidth}})));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window;
    window.resize(mobile ? QSize(panelWidth, 874) : QSize(1440, 900));
    editor->setParentItem(window.contentItem()); editor->setSize(window.size());
    const auto detach = qScopeGuard([&] { editor->setParentItem(nullptr); });
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    if (QGuiApplication::platformName() == "cocoa") { window.requestActivate(); QVERIFY(QTest::qWaitForWindowActive(&window)); }
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *state = editor->findChild<QObject *>("editorToolSheet");
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    QVERIFY(toolbar && state && canvas);
    QVERIFY(canvas->createCanvas({{"width", 64}, {"height", 64}, {"unit", "px"}, {"background", "Transparent"}}));
    QSignalSpy actions(state, SIGNAL(actionRequested(QString,QString,QVariant)));
    const auto tools = listProperty(toolbar, "tools");
    const QStringList keys{"audio-track", "canvas", "generative", "select", "retouch", "fill", "brush"};
    const QList<int> counts{4, 5, 8, 5, 6, 4, 7};
    int checkedStates = 0, checkedRows = 0;
    for (int tool = 0; tool < keys.size(); ++tool) {
        const auto key = keys[tool];
        int index = -1;
        for (int i = 0; i < tools.size(); ++i) if (tools[i].toMap()["key"].toString() == key) index = i;
        QVERIFY(index >= 0);
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index)));
        if (mobile) QTRY_VERIFY(state->property("opened").toBool());
        else QVERIFY(editor->setProperty("desktopPanelExpanded", true));
        QObject *presenter = mobile ? state : item(editor.data(), "editorDesktopPanel");
        QQuickItem *panel = nullptr;
        QTRY_VERIFY((panel = item(presenter, "editorCompactPanel")) && panel->isVisible());
        const auto views = listProperty(panel, "viewCatalog");
        QCOMPARE(views.size(), counts[tool]);
        auto *viewport = item(presenter, mobile ? "sheet_viewport" : "editorDesktopPanelViewport");
        QVERIFY(viewport);
        for (const auto &entry : views) {
            const auto view = entry.toMap(); const auto id = view["id"].toString();
            QVERIFY(viewport->setProperty("contentY", 0));
            if (mobile) QTest::qWait(250);
            auto *tab = visualItem(panel, qPrintable("compactTab-" + id)); QVERIFY(tab);
            const int tone = tab->property("tone").toInt();
            click(&window, tab);
            QTRY_COMPARE(panel->property("currentViewId").toString(), id);
            QCOMPARE(tab->property("tone").toInt(), tone);
            QCOMPARE(tab->property("checked").toBool(), true);
            QCOMPARE(tab->size(), QSizeF(22, 22));
            const auto fields = listProperty(panel, "presentedFields");
            QVERIFY(!fields.isEmpty());
            QVariantMap expected;
            for (const auto &s : states) if (s.toObject()["tool"].toString() == key && s.toObject()["view"].toString() == id) expected = s.toObject().toVariantMap();
            if (!expected.isEmpty()) {
                const auto expectedFields = expected["fields"].toList();
                QCOMPARE(fields.size(), expectedFields.size());
                for (int f = 0; f < fields.size(); ++f) {
                    const auto field = fields[f].toMap(), spec = expectedFields[f].toMap();
                    QCOMPARE(field["id"].toString(), spec["id"].toString());
                    QCOMPARE(field["label"].toString(), spec["label"].toString());
                    const auto name = (field["type"].toString() == "Action" ? "editorAction-" : "editorControl-") + field["id"].toString();
                    auto *control = visualItem(panel, qPrintable(name)); QVERIFY2(control, qPrintable(key + '/' + id + '/' + name));
                    const auto rect = bounds(control, panel);
                    QVERIFY2(rect.left() >= -0.01 && rect.right() <= panel->width() + 0.01, qPrintable(key + '/' + id + '/' + name));
                    if (panelWidth == 432) {
                        QTRY_VERIFY2(qAbs(bounds(control, panel).top() - (74 + spec["y"].toReal())) <= 1,
                            qPrintable(key + '/' + id + '/' + name + QString(" y=%1 expected=%2").arg(bounds(control, panel).top()).arg(74 + spec["y"].toReal())));
                        QCOMPARE(control->height(), spec["type"].toString() == "Action" ? 22. : spec["height"].toReal());
                    }
                    const auto capability = canvas->toolControlState(key, field["id"].toString(), mapProperty(state, "values"));
                    if (!capability["enabled"].toBool() || field["unsupported"].toBool()) QVERIFY(!control->isEnabled());
                    if (field["type"].toString() == "Choices" || field["type"].toString() == "Segmented") {
                        const auto options = field["options"].toStringList();
                        for (int option = 0; option < options.size(); ++option) {
                            auto *button = visualItem(control, qPrintable(QString("editorChoice-%1-%2").arg(field["id"].toString()).arg(option))); QVERIFY(button);
                            QVERIFY(bounds(button, panel).right() <= panel->width() + 0.01);
                            if (field["disabledOptions"].toStringList().contains(options[option])) QVERIFY(!button->isEnabled());
                        }
                    }
                    ++checkedRows;
                }
                ++checkedStates;
            }
            if (panelWidth == 432 && !qEnvironmentVariableIsEmpty("DREAMSCAPES_CAPTURE_DIR")) {
                const auto directory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR"); QVERIFY(QDir().mkpath(directory));
                const auto grab = panel->grabToImage(); QVERIFY(grab);
                QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
                if (grab->image().isNull()) QVERIFY(ready.wait(5000));
                QVERIFY(grab->image().save(directory + '/' + key + '-' + id + ".png"));
            }
        }
        const auto last = views.last().toMap()["id"].toString();
        const auto first = listProperty(panel, "presentedFields").first().toMap();
        const auto fieldId = first["id"].toString();
        QVERIFY(QMetaObject::invokeMethod(state, "setValue", Q_ARG(QVariant, fieldId), Q_ARG(QVariant, QVariant("retained draft"))));
        QVERIFY(QMetaObject::invokeMethod(panel, "resetCurrentView"));
        QTRY_COMPARE(panel->property("currentViewId").toString(), last);
        QVERIFY(mapProperty(state, "values")[fieldId].toString() != "retained draft");
        // Selecting another view never discards values belonging to this one.
        QVERIFY(QMetaObject::invokeMethod(state, "setValue", Q_ARG(QVariant, QString("compactDraftProbe")), Q_ARG(QVariant, QVariant(key))));
        QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, views.first().toMap()["id"])));
        QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, last)));
        QCOMPARE(mapProperty(state, "values")["compactDraftProbe"].toString(), key);
        if (key == "retouch" && panelWidth == 432) {
            QImage pixels(64, 64, QImage::Format_ARGB32); pixels.fill(Qt::blue);
            QVERIFY(canvas->insertPixels(pixels, "Preview target"));
            canvas->configureTool("select", {{"selector", "Rectangle"}, {"field-0", "New"}, {"field-2", 0}});
            QVERIFY(canvas->selectArea({QPointF(8, 8), QPointF(20, 20)}));
            QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("content-aware"))));
            auto *preview = visualItem(panel, "editorAction-field-18"); QVERIFY(preview && preview->isEnabled());
            const auto before = iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
            const auto selection = canvas->selectionOverlay(); const auto revision = canvas->revision();
            viewport->setProperty("contentY", 0); click(&window, preview);
            auto *sheet = editor->findChild<QObject *>("editorToolPreviewSheet"); QVERIFY(sheet);
            QTRY_VERIFY(sheet->property("opened").toBool());
            const auto data = mapProperty(editor.data(), "previewData");
            QCOMPARE(data["title"].toString(), QString("Preview fill"));
            QVERIFY(!data["imageSource"].toUrl().isEmpty());
            QCOMPARE(data["items"].toList().first().toMap()["action"].toString(), QString("applyRepair"));
            QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, before);
            QCOMPARE(canvas->selectionOverlay(), selection); QCOMPARE(canvas->revision(), revision);
            QTest::keyClick(&window, Qt::Key_Escape); QTRY_VERIFY(!sheet->property("visible").toBool());
            QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, before);
            QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, last)));
        }
        if (mobile) {
            QTest::keyClick(&window, Qt::Key_Escape); QTRY_VERIFY(!state->property("visible").toBool());
        }
    }
    QCOMPARE(checkedStates, 38); QCOMPARE(checkedRows, 167);
    // Reopening a tool uses the same saved view and drafts.
    int brush = -1; for (int i = 0; i < tools.size(); ++i) if (tools[i].toMap()["key"].toString() == "brush") brush = i;
    if (!mobile) QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, brush)));
    if (mobile) QTRY_VERIFY(state->property("opened").toBool());
    else QVERIFY(editor->setProperty("desktopPanelExpanded", true));
    QObject *presenter = mobile ? state : item(editor.data(), "editorDesktopPanel");
    QQuickItem *panel = nullptr; QTRY_VERIFY((panel = item(presenter, "editorCompactPanel")) && panel->isVisible());
    QCOMPARE(panel->property("currentViewId").toString(), QString("stabilizer"));
    QCOMPARE(mapProperty(state, "values")["compactDraftProbe"].toString(), QString("brush"));
    QCOMPARE(actions.count(), panelWidth == 432 ? 1 : 0);
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
        // Asset is removed from the app; retain the original Figma fixture.
        // Dedicated view catalogs have their own rendering/interaction tests.
        if (QStringList{"asset", "file", "elements", "text", "camera-photo", "color", "auto-enhance", "effects", "masking", "audio-track", "canvas", "generative", "select", "retouch", "fill", "brush"}.contains(spec["key"].toString())) continue;
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
    if(key=="background") {
        auto *background=visualItem(dock,"editorBackgroundPanel");
        QVERIFY(dock&&background&&viewport&&dock->isVisible());QCOMPARE(background->width(),398.);
        QCOMPARE(background->property("currentView").toString(),QString("Color"));
        QCOMPARE(state->property("definition").value<QJSValue>().toVariant().toMap()["figmaNode"].toString(),QString("777:82068"));
        for(const auto &view:QStringList{"Color","Remove","Mask","Replace"}) {
            QVERIFY(QMetaObject::invokeMethod(state,"setValue",Q_ARG(QVariant,QVariant("backgroundView")),Q_ARG(QVariant,QVariant(view))));
            QTRY_COMPARE(background->property("currentView").toString(),view);
            const auto name=QString("background%1Body").arg(view);
            QTRY_VERIFY(visualItem(background,qPrintable(name))&&visualItem(background,qPrintable(name))->isVisible());
            QVERIFY(visualItem(background,qPrintable(name))->width()<=398.);
        }
        QVERIFY(!panel);editor->setParentItem(nullptr);return;
    }
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
            if (type == "Visual") button = visualItem(button, "editorPreviewChevron");
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
    // Dedicated loaders retire the generic presenter while visiting a neighbor.
    QTRY_VERIFY((panel = visualItem(dock, "editorToolPanel")) && panel->isVisible());
    auto *reset = visualItem(panel, "editorToolReset"); QVERIFY(reset);
    const auto resetRect = bounds(reset, viewport->property("contentItem").value<QQuickItem *>());
    viewport->setProperty("contentY", qBound(0.0, resetRect.top() - 16, qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height())));
    QTest::qWait(250);
    click(&window, reset);
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

void GuiTests::editorGraphicPanelAllFigmaViews_data()
{
    QTest::addColumn<QString>("toolId");
    QTest::addColumn<int>("expectedCount");
    QTest::newRow("graphic-color") << QString("color") << 31;
    QTest::newRow("graphic-adjust") << QString("auto-enhance") << 43;
    QTest::newRow("graphic-mask") << QString("masking") << 18;
    QTest::newRow("graphic-effects") << QString("effects") << 289;
}

void GuiTests::editorGraphicPanelAllFigmaViews()
{
    QFETCH(QString, toolId);
    QFETCH(int, expectedCount);
    QFile fixtureFile(sourceUrl("Views/Editor/fixtures/GraphicPanelsFigma.json").toLocalFile());
    QVERIFY(fixtureFile.open(QIODevice::ReadOnly));
    const auto fixture = QJsonDocument::fromJson(fixtureFile.readAll()).object();
    QCOMPARE(fixture["frameCount"].toInt(), 381);
    QCOMPARE(fixture["fieldCount"].toInt(), 1197);
    QMap<QString, QVariantMap> originalFrames;
    for (const auto &entry : fixture["panels"].toArray()) {
        const auto frame = entry.toObject().toVariantMap();
        originalFrames.insert(frame["id"].toString(), frame);
    }
    QCOMPARE(originalFrames.size(), 381);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QVERIFY(editor->setProperty("desktopPanelWidth", 432));
    QQuickWindow window; window.resize(1200, 1000);
    editor->setParentItem(window.contentItem()); editor->setSize(window.size());
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *toolbar = item(editor.data(), "editorToolbar"); QVERIFY(toolbar);
    const auto tools = listProperty(toolbar, "tools");
    int toolIndex = -1;
    for (int index = 0; index < tools.size(); ++index)
        if (tools[index].toMap()["key"].toString() == toolId) toolIndex = index;
    QVERIFY(toolIndex >= 0);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex)));
    QVERIFY(editor->setProperty("desktopPanelExpanded", true));
    auto *dock = item(editor.data(), "editorDesktopPanel"); QVERIFY(dock && dock->isVisible());
    auto *panel = item(dock, "editorGraphicPanel"); QVERIFY(panel);
    auto *viewport = item(dock, "editorDesktopPanelViewport"); QVERIFY(viewport);
    QTRY_COMPARE(panel->width(), 398.0);
    const auto views = listProperty(panel, "viewCatalog");
    QCOMPARE(views.size(), expectedCount);
    QSet<QString> ids;
    for (const auto &entry : views) {
        const auto view = entry.toMap();
        const auto id = view["id"].toString();
        QVERIFY(!id.isEmpty() && !ids.contains(id)); ids.insert(id);
    }
    int checkedControls = 0, checkedNavigation = 0, checkedSubNavigation = 0;
    int renderedViews = 0;
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    const bool captureOnly = qEnvironmentVariableIsSet("DREAMSCAPES_GRAPHIC_CAPTURE_ONLY");
    QVERIFY(!captureOnly || !captureDirectory.isEmpty());
    const QSet<QString> captureIds{"710:19322", "710:26956", "711:26624", "722:80722", "711:38008", "711:34931",
        "712:34939", "712:35772", "712:36558", "718:57579", "718:59238"};
    for (const auto &entry : views) {
        const auto view = entry.toMap();
        const auto id = view["id"].toString();
        if (captureOnly && !captureIds.contains(id)) continue;
        ++renderedViews;
        QVERIFY2(originalFrames.contains(id), qPrintable(id));
        const auto original = originalFrames[id];
        QCOMPARE(view["name"].toString(), original["name"].toString());
        QCOMPARE(view["width"].toReal(), original["width"].toReal());
        QCOMPARE(view["height"].toReal(), original["height"].toReal());
        QSignalSpy renderedFrame(&window, &QQuickWindow::frameSwapped);
        QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, id)));
        QTRY_COMPARE(panel->property("currentViewId").toString(), id);
        QTRY_COMPARE(mapProperty(panel, "selectedView")["id"].toString(), id);
        window.update();
        QTRY_VERIFY2(!renderedFrame.isEmpty(), qPrintable(id + ": no rendered frame after loading the view"));
        viewport->setProperty("contentY", 0);
        QTRY_VERIFY(panel->implicitHeight() > 0);
        const auto rows = graphicRows(mapProperty(panel, "selectedView")["rows"].toList());
        for (const auto &rowEntry : rows) {
            const auto row = rowEntry.toMap();
            const auto rowId = row["id"].toString();
            auto *control = visualItem(panel, qPrintable("graphicControl-" + rowId));
            QVERIFY2(control, qPrintable(id + '/' + rowId));
            QTRY_VERIFY(control->width() > 0 && control->height() > 0);
            const auto rect = bounds(control, panel);
            QVERIFY2(rect.left() >= -0.01 && rect.right() <= 398.01,
                qPrintable(id + '/' + rowId + ": row exceeds the 398px content"));
            QTRY_VERIFY2(bounds(control, panel).top() >= -0.01
                && bounds(control, panel).bottom() <= panel->implicitHeight() + 1,
                qPrintable(id + '/' + rowId + ": row exceeds the scrollable content"));
            if (row["type"].toString() == "field") {
                const auto controlType = row["control"].toString();
                if (controlType == "slider" || controlType == "number" || controlType == "text") {
                    auto *input = visualItem(control, qPrintable("graphicInput-" + rowId));
                    QVERIFY2(input, qPrintable(id + '/' + rowId + ": missing input"));
                    QVERIFY(bounds(input, control).left() >= -0.01);
                    QVERIFY(bounds(input, control).right() <= control->width() + 0.01);
                }
                if (controlType == "slider") {
                    auto *slider = visualItem(control, qPrintable("graphicSlider-" + rowId)); QVERIFY(slider);
                    QCOMPARE(slider->property("from").toReal(), row["min"].toReal());
                    QCOMPARE(slider->property("to").toReal(), row["max"].toReal());
                    QCOMPARE(slider->property("stepSize").toReal(), row["step"].toReal());
                    QVERIFY(bounds(slider, control).right() <= control->width() + 0.01);
                } else if (controlType == "toggle") {
                    auto *toggle = visualItem(control, qPrintable("graphicToggle-" + rowId)); QVERIFY(toggle);
                    QVERIFY(bounds(toggle, control).right() <= control->width() + 0.01);
                }
            }
            ++checkedControls;
        }
        for (const auto &expectedEntry : original["fields"].toList()) {
            const auto expected = expectedEntry.toMap();
            QVariantMap actual;
            for (const auto &rowEntry : rows)
                if (rowEntry.toMap()["id"].toString() == expected["id"].toString()) actual = rowEntry.toMap();
            QVERIFY2(!actual.isEmpty(), qPrintable(id + ": missing original field " + expected["id"].toString()));
            for (const auto *attribute : {"label", "parameter", "control", "unit"})
                QCOMPARE(actual[attribute].toString(), expected[attribute].toString());
            if (expected["initial"].isNull()) QVERIFY(actual["initial"].isNull());
            else if (expected["control"].toString() == "toggle") QCOMPARE(actual["initial"].toBool(), expected["initial"].toBool());
            else QCOMPARE(actual["initial"].toReal(), expected["initial"].toReal());
            if (expected.contains("min")) QCOMPARE(actual["min"].toReal(), expected["min"].toReal());
            if (expected.contains("max")) QCOMPARE(actual["max"].toReal(), expected["max"].toReal());
            auto *control = visualItem(panel, qPrintable("graphicControl-" + expected["id"].toString())); QVERIFY(control);
            QVERIFY2(visualText(control, expected["label"].toString()), qPrintable(id + ": original field label is absent"));
            if (expected["initial"].isNull()) {
                auto *input = visualItem(control, qPrintable("graphicInput-" + expected["id"].toString())); QVERIFY(input);
                QCOMPARE(input->property("text").toString(), QString("Default"));
            }
        }
        for (const auto &expectedTarget : original["navigationTargets"].toStringList()) {
            if (!ids.contains(expectedTarget)) continue;
            QVERIFY2(visualItem(panel, qPrintable("graphicNavigate-" + expectedTarget)),
                qPrintable(id + ": original navigation is absent: " + expectedTarget));
        }
        for (const auto &representative : fixture["representatives"].toArray()) {
            const auto sample = representative.toObject();
            if (sample["id"].toString() != id) continue;
            for (const auto &expectedEntry : sample["fields"].toArray()) {
                const auto expected = expectedEntry.toObject();
                QVariantMap actual;
                for (const auto &rowEntry : rows)
                    if (rowEntry.toMap()["id"].toString() == expected["id"].toString()) actual = rowEntry.toMap();
                QVERIFY2(!actual.isEmpty(), qPrintable(expected["id"].toString()));
                QCOMPARE(actual["label"].toString(), expected["label"].toString());
                QCOMPARE(actual["parameter"].toString(), expected["parameter"].toString());
                if (expected.contains("min")) QCOMPARE(actual["min"].toReal(), expected["min"].toDouble());
                if (expected.contains("max")) QCOMPARE(actual["max"].toReal(), expected["max"].toDouble());
                if (expected.contains("height")) QCOMPARE(visualItem(panel, qPrintable("graphicControl-" + expected["id"].toString()))->height(), expected["height"].toDouble());
            }
        }
        if (!captureDirectory.isEmpty() && captureIds.contains(id)) {
            QVERIFY(QDir().mkpath(captureDirectory));
            auto capture = dock->grabToImage(QSize(432, 1000)); QVERIFY(capture);
            QTRY_VERIFY(!capture->image().isNull());
            QVERIFY(capture->image().save(captureDirectory + '/' + view["name"].toString() + ".png"));
        }
        if (captureOnly) continue;
        // Follow a real row disclosure, including point editors and effect galleries.
        // Top tabs alone would not prove that their nested views are reachable.
        QString nestedTarget;
        for (const auto &rowEntry : rows) {
            const auto row = rowEntry.toMap();
            const auto target = row["target"].toString();
            const QStringList commands{"preview", "apply", "reset", "overview"};
            if (target != id && ids.contains(target) && !commands.contains(row["action"].toString())) nestedTarget = target;
            for (const auto &optionEntry : row["options"].toList()) {
                const auto option = optionEntry.toMap();
                const auto optionTarget = option["target"].toString();
                const auto action = option.contains("action") ? option["action"].toString()
                    : option.contains("value") ? option["value"].toString() : row["action"].toString();
                if (optionTarget != id && ids.contains(optionTarget) && !commands.contains(action)) { nestedTarget = optionTarget; break; }
            }
            if (!nestedTarget.isEmpty()) break;
        }
        if (!nestedTarget.isEmpty()) {
            auto *navigation = visualItem(panel, qPrintable("graphicNavigate-" + nestedTarget));
            QVERIFY2(navigation, qPrintable(id + ": missing nested navigation to " + nestedTarget));
            QTRY_VERIFY(viewport->property("contentHeight").toReal() + 1 >= panel->implicitHeight());
            const auto navigationRect = bounds(navigation, viewport->property("contentItem").value<QQuickItem *>());
            const auto maximum = qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height());
            QVERIFY(viewport->setProperty("contentY", qBound(0.0, navigationRect.top() - 16, maximum)));
            QTRY_VERIFY2(bounds(navigation, viewport).top() >= -0.01
                && bounds(navigation, viewport).bottom() <= viewport->height() + 0.01,
                qPrintable(id + ": nested navigation cannot be reached by scrolling"));
            click(&window, navigation);
            QTRY_COMPARE(panel->property("currentViewId").toString(), nestedTarget);
            ++checkedSubNavigation;
            QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, id)));
            QTRY_COMPARE(panel->property("currentViewId").toString(), id);
        }
        for (const auto &navigationEntry : view["navigation"].toList()) {
            const auto target = navigationEntry.toMap()["target"].toString();
            if (target == id || !ids.contains(target)) continue;
            viewport->setProperty("contentY", 0);
            QSignalSpy navigationFrame(&window, &QQuickWindow::frameSwapped);
            window.update(); QTRY_VERIFY(!navigationFrame.isEmpty());
            auto *navigation = visualItem(panel, qPrintable("graphicNavigate-" + target));
            QVERIFY2(navigation, qPrintable(id + ": missing navigation to " + target));
            if (auto *icon = visualItem(navigation, "iconButton_icon"))
                QTRY_COMPARE(icon->property("status").toInt(), 1);
            QTRY_VERIFY(navigation->isVisible() && navigation->isEnabled());
            QTRY_VERIFY(bounds(navigation, viewport).top() >= 0
                && bounds(navigation, viewport).bottom() <= viewport->height());
            const auto clickRect = bounds(navigation, window.contentItem());
            QPointer<QQuickItem> checkedNavigationItem(navigation);
            QSignalSpy navigationClicked(navigation, SIGNAL(clicked()));
            QVERIFY(navigationClicked.isValid());
            QSignalSpy navigationPressed(navigation, SIGNAL(pressed()));
            QSignalSpy navigationReleased(navigation, SIGNAL(released()));
            QSignalSpy navigationCanceled(navigation, SIGNAL(canceled()));
            MouseInputRecorder inputRecorder; window.installEventFilter(&inputRecorder);
            const auto hitPath = visualHitPath(window.contentItem(), clickRect.center());
            click(&window, navigation);
            if (panel->property("currentViewId").toString() != target)
                qInfo() << "Navigation input diagnostic" << id << target << clickRect << "active" << window.isActive()
                    << "frame" << navigationFrame.count() << "clicked" << navigationClicked.count() << "destroyed" << checkedNavigationItem.isNull()
                    << "pressed" << navigationPressed.count() << "events" << inputRecorder.presses << "hit" << hitPath
                    << "released" << navigationReleased.count() << "canceled" << navigationCanceled.count()
                    << "releaseEvents" << inputRecorder.releases << "moves" << inputRecorder.moves
                    << "window" << window.geometry() << "frameGeometry" << window.frameGeometry();
            window.removeEventFilter(&inputRecorder);
            QTRY_COMPARE(panel->property("currentViewId").toString(), target);
            ++checkedNavigation;
            break;
        }
    }
    QVERIFY(checkedControls > 0);
    if (captureOnly) {
        const QMap<QString, int> representativeCounts{{"color", 2}, {"auto-enhance", 4}, {"masking", 2}, {"effects", 3}};
        QCOMPARE(renderedViews, representativeCounts[toolId]);
    } else {
        QCOMPARE(renderedViews, expectedCount);
        QVERIFY(checkedNavigation > 0 && checkedSubNavigation > 0);
    }
    const auto currentId = panel->property("currentViewId").toString();
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("unknown-view"))));
    QCOMPARE(panel->property("currentViewId").toString(), currentId);
    qInfo() << toolId << "loaded original Figma views" << renderedViews << "catalog views" << views.size()
            << "checked controls" << checkedControls << "navigation transitions" << checkedNavigation
            << "nested navigation transitions" << checkedSubNavigation;
}

void GuiTests::editorColorToolSelectionDoesNotEditDocument_data()
{
    QTest::addColumn<bool>("mobile");
    QTest::addColumn<QString>("sourceKind");
    QTest::newRow("color-selection-desktop") << false << QString("bitmap");
    QTest::newRow("color-selection-mobile") << true << QString("bitmap");
    QTest::newRow("color-fractional-elements-desktop") << false << QString("elements");
    QTest::newRow("color-fractional-elements-mobile") << true << QString("elements");
    QTest::newRow("color-fractional-text-desktop") << false << QString("text");
    QTest::newRow("color-fractional-text-mobile") << true << QString("text");
}

void GuiTests::editorColorToolSelectionDoesNotEditDocument()
{
    QFETCH(bool, mobile);
    QFETCH(QString, sourceKind);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", mobile ? "ios" : "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(
        component.createWithInitialProperties({{"mobileLayout", mobile}})));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QVERIFY(editor->setProperty("desktopPanelWidth", 432));
    QQuickWindow window;
    window.resize(mobile ? QSize(402, 874) : QSize(1200, 1000));
    editor->setParentItem(window.contentItem()); editor->setSize(window.size());
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas"));
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *sheet = editor->findChild<QObject *>("editorToolSheet");
    auto *dock = item(editor.data(), "editorDesktopPanel");
    QVERIFY(canvas && toolbar && sheet);
    if (!mobile) QVERIFY(dock);
    QVERIFY(canvas->createCanvas({{"width", 48}, {"height", 32}, {"unit", "px"}, {"background", "Transparent"}}));
    QImage pixels(48, 32, QImage::Format_ARGB32);
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x)
            pixels.setPixel(x, y, qRgba(24 + x * 4, 32 + y * 5, 80 + (x + y) % 90, 192));
    QVERIFY(canvas->insertPixels(pixels, "Color selection source"));
    const auto tools = listProperty(toolbar, "tools");
    const auto toolIndex = [&tools](const QString &key) {
        for (int index = 0; index < tools.size(); ++index)
            if (tools[index].toMap()["key"].toString() == key) return index;
        return -1;
    };
    const auto colorIndex = toolIndex("color"), brushIndex = toolIndex("brush"), canvasIndex = toolIndex("canvas");
    QVERIFY(colorIndex >= 0 && brushIndex >= 0 && canvasIndex >= 0);
    QObject *presenter = mobile ? sheet : dock;
    if (sourceKind != "bitmap") {
        const auto sourceIndex = toolIndex(sourceKind); QVERIFY(sourceIndex >= 0);
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, sourceIndex)));
        QTRY_COMPARE(sheet->property("toolId").toString(), sourceKind);
        if (mobile) QTRY_VERIFY(sheet->property("opened").toBool());
        else {
            QVERIFY(editor->setProperty("desktopPanelExpanded", true));
            QTRY_VERIFY(dock->isVisible());
        }
        if (sourceKind == "elements") {
            QTRY_VERIFY(item(presenter, "editorElementsPanel"));
            canvas->configureTool("elements", mapProperty(sheet, "values"));
            QVERIFY2(canvas->createElement(QRectF(3, 3, 24.5, 16.5)), qPrintable(canvas->error()));
            QTRY_COMPARE(mapProperty(sheet, "values")["field-0"].toList(), (QVariantList{24.5, 16.5}));
        } else {
            QQuickItem *textPanel = nullptr;
            QTRY_VERIFY((textPanel = item(presenter, "editorTextPanel")));
            QVERIFY(QMetaObject::invokeMethod(sheet, "setValue", Q_ARG(QVariant, QString("field-0")),
                Q_ARG(QVariant, QString("A\nB"))));
            canvas->configureTool("text", mapProperty(sheet, "values"));
            QVERIFY2(canvas->createText({3, 3}), qPrintable(canvas->error()));
            QVERIFY(textPanel->setProperty("panelIndex", 1));
            QVERIFY(QMetaObject::invokeMethod(textPanel, "linePreset", Q_ARG(QVariant, QString("120%"))));
            QTRY_COMPARE(canvas->toolValues("text")["textLineHeight"].toReal(), 15.6);
            QTRY_COMPARE(canvas->toolValues("text")["textLinePreset"].toString(), QString("120%"));
        }
    }
    const auto selected = canvas->selectedLayerId();
    // A retained redo edit exposes accidental commits even when the UI keeps
    // canUndo true both before and after an unwanted initialization edit.
    QVERIFY(canvas->setLayerOpacity(selected, 0.5));
    const auto redoDocument = iiSharedCanvas::encodeIisc(*canvas->document());
    QVERIFY(redoDocument.ok());
    QVERIFY(canvas->undo());
    QVERIFY(canvas->canUndo() && canvas->canRedo());
    QTest::qWait(50);
    const auto originalDocument = iiSharedCanvas::encodeIisc(*canvas->document());
    const auto originalFrame = iiSharedCanvas::renderFrame(*canvas->document(), 0);
    QVERIFY(originalDocument.ok() && originalFrame.ok());
    const bool originalRaster = canvas->selectedRasterPixels() != nullptr;
    QCOMPARE(originalRaster, sourceKind == "bitmap");
    if (sourceKind == "elements")
        QCOMPARE(canvas->selectedElementSettings()["field-0"].toList(), (QVariantList{24.5, 16.5}));
    const auto originalPixels = originalRaster ? canvas->selectedRasterPixels()->pixels : std::vector<std::uint32_t>{};
    const auto originalRevision = canvas->revision();
    const auto originalCanUndo = canvas->canUndo(), originalCanRedo = canvas->canRedo();
    QSignalSpy revisions(canvas, &iiSharedCanvas::CanvasItem::revisionChanged);
    QSignalSpy history(canvas, &EditorCanvas::undoRedoChanged);
    QVERIFY(revisions.isValid() && history.isValid());
    const auto verifyUnchanged = [&] {
        QSignalSpy frame(&window, &QQuickWindow::frameSwapped);
        window.update(); QTRY_VERIFY(!frame.isEmpty());
        QTest::qWait(50);
        const auto document = iiSharedCanvas::encodeIisc(*canvas->document());
        const auto rendered = iiSharedCanvas::renderFrame(*canvas->document(), 0);
        QVERIFY(document.ok() && rendered.ok());
        // IISC bytes include the native vector geometry. Elements' UI selection
        // projection is intentionally cleared when another tool becomes active.
        QCOMPARE(document.bytes, originalDocument.bytes);
        QCOMPARE(canvas->selectedRasterPixels() != nullptr, originalRaster);
        if (originalRaster) QCOMPARE(canvas->selectedRasterPixels()->pixels, originalPixels);
        QCOMPARE(rendered.pixels.pixels, originalFrame.pixels.pixels);
        QCOMPARE(canvas->revision(), originalRevision);
        QCOMPARE(canvas->selectedLayerId(), selected);
        QCOMPARE(canvas->canUndo(), originalCanUndo);
        QCOMPARE(canvas->canRedo(), originalCanRedo);
        QCOMPARE(revisions.count(), 0);
        QCOMPARE(history.count(), 0);
        if (sourceKind == "text") {
            QCOMPARE(canvas->toolValues("text")["textLineHeight"].toReal(), 15.6);
            QCOMPARE(canvas->toolValues("text")["textLinePreset"].toString(), QString("120%"));
        }
    };
    QPointer<QQuickItem> panel;
    QQuickItem *passiveInput = nullptr;
    if (sourceKind == "bitmap") {
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, canvasIndex)));
        QTRY_COMPARE(sheet->property("toolId").toString(), QString("canvas"));
        if (mobile) QTRY_VERIFY(sheet->property("opened").toBool());
        else QTRY_VERIFY(dock->isVisible());
        QQuickItem *canvasPanel = nullptr;
        QTRY_VERIFY((canvasPanel = item(presenter, "editorCompactPanel")) && canvasPanel->isVisible());
        passiveInput = visualItem(canvasPanel, "editorInput-field-1");
    } else if (sourceKind == "elements") {
        auto *elementsPanel = item(presenter, "editorElementsPanel"); QVERIFY(elementsPanel);
        passiveInput = visualItem(elementsPanel, "elementsInput-field-0-0");
        QVERIFY(passiveInput);
        QTRY_COMPARE(passiveInput->property("text").toString(), QString("25 px"));
    } else {
        auto *textPanel = item(presenter, "editorTextPanel"); QVERIFY(textPanel);
        passiveInput = visualItem(textPanel, "textInput-textLineHeight");
        QVERIFY(passiveInput);
        QTRY_COMPARE(passiveInput->property("text").toString(), QString("16 px"));
    }
    QVERIFY(passiveInput && passiveInput->isVisible());
    passiveInput->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(passiveInput->hasActiveFocus());
    verifyUnchanged();
    // Switching tools must not accept an unchanged PPI field or round the
    // displayed Elements/Text fraction, nor apply newly presented defaults.
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, colorIndex)));
    QTRY_COMPARE(sheet->property("toolId").toString(), QString("color"));
    if (mobile) QTRY_VERIFY(sheet->property("opened").toBool());
    else QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY((panel = item(presenter, "editorGraphicPanel")) && panel->isVisible());
    QTRY_COMPARE(panel->property("toolId").toString(), QString("color"));
    verifyUnchanged();

    // The same toolbar selection closes and reopens the mobile sheet, while
    // the desktop toolbar toggles its dock without accepting any color value.
    if (mobile) {
        QSignalSpy closed(sheet, SIGNAL(closed())); QVERIFY(closed.isValid());
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, colorIndex)));
        QTRY_VERIFY(!closed.isEmpty());
        QTRY_VERIFY(sheet->property("opened").toBool());
    } else {
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, colorIndex)));
        QTRY_VERIFY(!dock->isVisible());
        verifyUnchanged();
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, colorIndex)));
        QTRY_VERIFY(dock->isVisible());
    }
    QTRY_VERIFY((panel = item(presenter, "editorGraphicPanel")) && panel->isVisible());
    verifyUnchanged();
    for (const auto &view : {QString("710:19322"), QString("710:19795")}) {
        QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, view)));
        QTRY_COMPARE(panel->property("currentViewId").toString(), view);
        verifyUnchanged();
    }
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, brushIndex)));
    QTRY_COMPARE(sheet->property("toolId").toString(), QString("brush"));
    if (mobile) QTRY_VERIFY(sheet->property("opened").toBool());
    else QTRY_VERIFY(dock->isVisible());
    verifyUnchanged();
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, colorIndex)));
    QTRY_COMPARE(sheet->property("toolId").toString(), QString("color"));
    if (mobile) QTRY_VERIFY(sheet->property("opened").toBool());
    else QTRY_VERIFY(dock->isVisible());
    QTRY_VERIFY((panel = item(presenter, "editorGraphicPanel")) && panel->isVisible());
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("710:19795"));
    verifyUnchanged();
    QVERIFY(canvas->redo());
    QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, redoDocument.bytes);
    QCOMPARE(canvas->selectedLayerId(), selected);
    QCOMPARE(canvas->selectedLayer()["opacity"].toReal(), 0.5);
}

void GuiTests::editorGraphicPanelEditsPersistAndReset()
{
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QVERIFY(editor->setProperty("desktopPanelWidth", 432));
    QQuickWindow window; window.resize(1200, 1000);
    editor->setParentItem(window.contentItem()); editor->setSize(window.size());
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas")); QVERIFY(canvas);
    QVERIFY(canvas->createCanvas({{"width", 48}, {"height", 32}, {"unit", "px"}, {"background", "Transparent"}}));
    QImage pixels(48, 32, QImage::Format_RGBA8888); pixels.fill(QColor("#587a9a"));
    QVERIFY(canvas->insertPixels(pixels, "Graphic parameter test"));
    auto *toolbar = item(editor.data(), "editorToolbar"); QVERIFY(toolbar);
    auto *state = editor->findChild<QObject *>("editorToolSheet"); QVERIFY(state);
    auto *dock = item(editor.data(), "editorDesktopPanel"); QVERIFY(dock);
    const auto tools = listProperty(toolbar, "tools");
    const auto toolIndex = [&tools](const QString &key) {
        for (int index = 0; index < tools.size(); ++index)
            if (tools[index].toMap()["key"].toString() == key) return index;
        return -1;
    };
    QVERIFY(toolIndex("color") >= 0 && toolIndex("auto-enhance") >= 0);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex("color"))));
    QVERIFY(editor->setProperty("desktopPanelExpanded", true));
    auto *panel = item(dock, "editorGraphicPanel"); QVERIFY(panel);
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("710:19322"))));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("710:19322"));
    const auto light = mapProperty(panel, "selectedView");
    QVariantMap exposureRow;
    for (const auto &entry : graphicRows(light["rows"].toList()))
        if (entry.toMap()["parameter"].toString() == "exposure") exposureRow = entry.toMap();
    QVERIFY(!exposureRow.isEmpty());
    QCOMPARE(exposureRow["label"].toString(), QString("Exposure"));
    QCOMPARE(exposureRow["min"].toReal(), -5.0);
    QCOMPARE(exposureRow["max"].toReal(), 5.0);
    auto *input = visualItem(panel, qPrintable("graphicInput-" + exposureRow["id"].toString())); QVERIFY(input);
    QVERIFY(input->setProperty("text", QString("1.25 EV")));
    QVERIFY(QMetaObject::invokeMethod(input, "commit"));
    const auto draftValue = [state](const QString &operation, const QString &parameter) {
        return mapProperty(state, "values")["graphicDrafts"].toMap()[operation].toMap()[parameter];
    };
    const auto lightOperation = light["operation"].toString(); QVERIFY(!lightOperation.isEmpty());
    QTRY_COMPARE(draftValue(lightOperation, "exposure").toReal(), 1.25);
    QTRY_COMPARE(canvas->toolValues("color")["graphicDrafts"].toMap()[lightOperation].toMap()["exposure"].toReal(), 1.25);
    const auto beforeProcessing = iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels;
    auto *preview = visualItem(panel, "graphicAction-710:19782-1"); QVERIFY(preview);
    click(&window, preview);
    auto *previewSheet = editor->findChild<QObject *>("editorGraphicPreviewSheet"); QVERIFY(previewSheet);
    QTRY_VERIFY(previewSheet->property("opened").toBool());
    QTRY_VERIFY(!panel->property("busy").toBool());
    QCOMPARE(panel->property("previewImage").toUrl().scheme(), QString("data"));
    QCOMPARE(iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels, beforeProcessing);
    QVERIFY(QMetaObject::invokeMethod(previewSheet, "close"));
    QTRY_VERIFY(!previewSheet->property("visible").toBool());
    auto *apply = visualItem(panel, "graphicAction-710:19782-2"); QVERIFY(apply);
    click(&window, apply);
    QTRY_VERIFY(!panel->property("busy").toBool());
    QTRY_VERIFY(iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels != beforeProcessing);
    auto *operationSheet = editor->findChild<QObject *>("editorGraphicOperationSheet"); QVERIFY(operationSheet);
    QTRY_VERIFY(operationSheet->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(operationSheet, "close"));
    QTRY_VERIFY(!operationSheet->property("visible").toBool());
    QVERIFY(canvas->undo());
    QCOMPARE(iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels, beforeProcessing);
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("710:19795"))));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("710:19795"));
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("710:19322"))));
    QTRY_COMPARE(draftValue(lightOperation, "exposure").toReal(), 1.25);
    auto *returnedInput = visualItem(panel, qPrintable("graphicInput-" + exposureRow["id"].toString())); QVERIFY(returnedInput);
    QCOMPARE(returnedInput->property("text").toString(), QString("1.25 EV"));
    QVERIFY(returnedInput->setProperty("text", QString("NaN")));
    QVERIFY(QMetaObject::invokeMethod(returnedInput, "commit"));
    QCOMPARE(draftValue(lightOperation, "exposure").toReal(), 1.25);
    QCOMPARE(returnedInput->property("text").toString(), QString("1.25 EV"));
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex("auto-enhance"))));
    panel = item(dock, "editorGraphicPanel"); QVERIFY(panel);
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("710:26956"))));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("710:26956"));
    const auto detail = mapProperty(panel, "selectedView");
    QVariantMap sharpeningRow;
    for (const auto &entry : graphicRows(detail["rows"].toList()))
        if (entry.toMap()["parameter"].toString() == "sharpenAmount") sharpeningRow = entry.toMap();
    QVERIFY(!sharpeningRow.isEmpty());
    const auto detailOperation = detail["operation"].toString(); QVERIFY(!detailOperation.isEmpty());
    QVERIFY(QMetaObject::invokeMethod(panel, "edit", Q_ARG(QVariant, sharpeningRow), Q_ARG(QVariant, 40)));
    QTRY_COMPARE(draftValue(detailOperation, "sharpenAmount").toInt(), 40);
    QTRY_COMPARE(canvas->toolValues("auto-enhance")["graphicDrafts"].toMap()[detailOperation].toMap()["sharpenAmount"].toInt(), 40);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex("color"))));
    panel = item(dock, "editorGraphicPanel"); QVERIFY(panel);
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("710:19322"));
    QCOMPARE(draftValue(lightOperation, "exposure").toReal(), 1.25);
    QTRY_VERIFY(!panel->property("modalActive").toBool());
    QSignalSpy resetFrame(&window, &QQuickWindow::frameSwapped);
    window.update(); QTRY_VERIFY(!resetFrame.isEmpty());
    auto *reset = visualItem(panel, "graphicResetView"); QVERIFY(reset);
    QTRY_VERIFY(bounds(reset, window.contentItem()).top() >= 0
        && bounds(reset, window.contentItem()).bottom() <= window.height());
    click(&window, reset);
    QTRY_COMPARE(draftValue(lightOperation, "exposure").toReal(), 0.0);
    QTRY_COMPARE(canvas->toolValues("color")["graphicDrafts"].toMap()[lightOperation].toMap()["exposure"].toReal(), 0.0);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex("auto-enhance"))));
    panel = item(dock, "editorGraphicPanel"); QVERIFY(panel);
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("710:26956"));
    QCOMPARE(draftValue(detailOperation, "sharpenAmount").toInt(), 40);
    window.resize(1400, 1000); editor->setSize(window.size());
    for (const qreal width : {320.0, 432.0, 720.0}) {
        QVERIFY(editor->setProperty("desktopPanelWidth", width));
        QTRY_COMPARE(panel->width(), width - 34);
        for (const auto &entry : graphicRows(mapProperty(panel, "selectedView")["rows"].toList())) {
            auto *control = visualItem(panel, qPrintable("graphicControl-" + entry.toMap()["id"].toString())); QVERIFY(control);
            QTRY_VERIFY(bounds(control, panel).right() <= panel->width() + 0.01);
        }
    }
}

void GuiTests::editorGraphicPanelCollectionsAndPickers()
{
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.create()));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QVERIFY(editor->setProperty("desktopPanelWidth", 432));
    QQuickWindow window; window.resize(1200, 1000);
    editor->setParentItem(window.contentItem()); editor->setSize(window.size());
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *canvas = qobject_cast<EditorCanvas *>(item(editor.data(), "editorBlankCanvas")); QVERIFY(canvas);
    QVERIFY(canvas->createCanvas({{"width", 48}, {"height", 32}, {"unit", "px"}, {"background", "Transparent"}}));
    QImage pixels(48, 32, QImage::Format_RGBA8888);
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x) pixels.setPixelColor(x, y, QColor(x * 255 / 47, y * 255 / 31, 100));
    QVERIFY(canvas->insertPixels(pixels, "Graphic collection test"));
    auto *toolbar = item(editor.data(), "editorToolbar"); QVERIFY(toolbar);
    auto *state = editor->findChild<QObject *>("editorToolSheet"); QVERIFY(state);
    auto *dock = item(editor.data(), "editorDesktopPanel"); QVERIFY(dock);
    const auto tools = listProperty(toolbar, "tools");
    int effectsIndex = -1;
    for (int index = 0; index < tools.size(); ++index)
        if (tools[index].toMap()["key"].toString() == "effects") effectsIndex = index;
    QVERIFY(effectsIndex >= 0);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, effectsIndex)));
    QVERIFY(editor->setProperty("desktopPanelExpanded", true));
    auto *panel = item(dock, "editorGraphicPanel"); QVERIFY(panel);
    const auto converted = [](QVariant value) {
        return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
    };
    const auto collectionItems = [&] {
        QVariant result;
        QMetaObject::invokeMethod(panel, "collectionItems", Q_RETURN_ARG(QVariant, result));
        return converted(result).toList();
    };
    const auto parameters = [&](const QString &operation) {
        QVariant result;
        QMetaObject::invokeMethod(panel, "parameters", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, operation));
        return converted(result).toMap();
    };

    // The empty SDK curve displays two identity endpoints; Add persists a sorted midpoint.
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("718:57579"))));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("718:57579"));
    QTRY_COMPARE(collectionItems().size(), 2);
    QCOMPARE(collectionItems().first().toList(), (QVariantList{0, 0}));
    QCOMPARE(collectionItems().last().toList(), (QVariantList{255, 255}));
    QVERIFY(!mapProperty(state, "values")["graphicDrafts"].toMap().contains("cameraRawFilter"));
    auto *manage = visualItem(panel, "graphicManageCollection"); QVERIFY(manage);
    click(&window, manage);
    auto *collectionSheet = editor->findChild<QObject *>("editorGraphicCollectionSheet"); QVERIFY(collectionSheet);
    QTRY_VERIFY(collectionSheet->property("opened").toBool());
    auto *add = item(collectionSheet, "graphicAddItem"); QVERIFY(add);
    click(&window, add);
    QTRY_COMPARE(collectionItems().size(), 3);
    QCOMPARE(collectionItems()[1].toList(), (QVariantList{127, 128}));
    QCOMPARE(mapProperty(panel, "collectionIndices")["pointCurve"].toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(collectionSheet, "close"));
    QTRY_VERIFY(!collectionSheet->property("visible").toBool());
    auto *output = visualItem(panel, "graphicInput-718:57838"); QVERIFY(output);
    QVERIFY(output->setProperty("text", QString("160")));
    QVERIFY(QMetaObject::invokeMethod(output, "commit"));
    QTRY_COMPARE(collectionItems()[1].toList()[1].toInt(), 160);
    QCOMPARE(collectionItems().first().toList(), (QVariantList{0, 0}));
    QCOMPARE(collectionItems().last().toList(), (QVariantList{255, 255}));
    const auto original = iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels;
    auto *preview = visualItem(panel, "graphicAction-718:57579/footer-1"); QVERIFY(preview);
    click(&window, preview);
    auto *previewSheet = editor->findChild<QObject *>("editorGraphicPreviewSheet"); QVERIFY(previewSheet);
    QTRY_VERIFY(previewSheet->property("opened").toBool());
    QTRY_VERIFY(!panel->property("busy").toBool());
    QCOMPARE(panel->property("operationStatus").toString(), QString("preview"));
    QCOMPARE(panel->property("previewImage").toUrl().scheme(), QString("data"));
    QCOMPARE(iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels, original);
    QVERIFY(QMetaObject::invokeMethod(previewSheet, "close"));
    QTRY_VERIFY(!previewSheet->property("visible").toBool());
    auto *apply = visualItem(panel, "graphicAction-718:57579/footer-2"); QVERIFY(apply);
    click(&window, apply);
    QTRY_COMPARE(panel->property("operationStatus").toString(), QString("applied"));
    QVERIFY(iiSharedCanvas::renderFrame(*canvas->document(), 0).pixels.pixels != original);
    auto *operationSheet = editor->findChild<QObject *>("editorGraphicOperationSheet"); QVERIFY(operationSheet);
    QVERIFY(QMetaObject::invokeMethod(operationSheet, "close"));
    QTRY_VERIFY(!operationSheet->property("visible").toBool());
    click(&window, manage);
    QTRY_VERIFY(collectionSheet->property("opened").toBool());
    auto *remove = item(collectionSheet, "graphicRemoveItem"); QVERIFY(remove);
    click(&window, remove);
    QTRY_COMPARE(collectionItems().size(), 2);
    click(&window, remove);
    QCOMPARE(collectionItems().size(), 2);
    QCOMPARE(collectionItems().first().toList(), (QVariantList{0, 0}));
    QCOMPARE(collectionItems().last().toList(), (QVariantList{255, 255}));
    window.resize(320, 640); editor->setSize(window.size());
    auto *collectionViewport = item(collectionSheet, "sheet_viewport"); QVERIFY(collectionViewport);
    QTRY_VERIFY(collectionViewport->width() <= 320);
    QTRY_VERIFY(bounds(add, collectionViewport).right() <= collectionViewport->width() + 0.01);
    QTRY_VERIFY(bounds(remove, collectionViewport).right() <= collectionViewport->width() + 0.01);
    window.resize(1200, 1000); editor->setSize(window.size());
    QVERIFY(QMetaObject::invokeMethod(collectionSheet, "close"));
    QTRY_VERIFY(!collectionSheet->property("visible").toBool());

    // Point Color permits exactly eight samples, independent of repeated Add requests.
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("712:34939"))));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("712:34939"));
    for (int count = 0; count < 10; ++count) QVERIFY(QMetaObject::invokeMethod(panel, "addCollectionItem"));
    QCOMPARE(collectionItems().size(), 8);
    for (const auto &sample : collectionItems()) {
        QCOMPARE(sample.toMap()["lightness"].toReal(), 0.5);
        QCOMPARE(sample.toMap()["chroma"].toReal(), 0.1);
    }
    QVERIFY(QMetaObject::invokeMethod(panel, "addCollectionItem"));
    QCOMPARE(collectionItems().size(), 8);

    // The child picker reads the current RGB draft and commits an RGB array.
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("718:67286"))));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("718:67286"));
    QVERIFY(QMetaObject::invokeMethod(state, "setGraphicValue", Q_ARG(QVariant, QString("neonGlow")),
        Q_ARG(QVariant, QString("glowColor")), Q_ARG(QVariant, (QVariantList{0.2, 0.3, 0.4}))));
    QSignalSpy colorFrame(&window, &QQuickWindow::frameSwapped);
    window.update(); QTRY_VERIFY(!colorFrame.isEmpty());
    auto *pick = visualItem(panel, "graphicAction-718:67748"); QVERIFY(pick);
    QTRY_VERIFY(pick->isVisible());
    click(&window, pick);
    auto *colorSheet = editor->findChild<QObject *>("editorGraphicColorSheet"); QVERIFY(colorSheet);
    QTRY_VERIFY(colorSheet->property("opened").toBool());
    auto *picker = colorSheet->property("loadedContent").value<QObject *>(); QVERIFY(picker);
    const auto current = picker->property("previousColor").value<QColor>();
    QVERIFY(qAbs(current.redF() - 0.2) < 0.001 && qAbs(current.greenF() - 0.3) < 0.001 && qAbs(current.blueF() - 0.4) < 0.001);
    window.resize(320, 640); editor->setSize(window.size());
    auto *colorViewport = item(colorSheet, "sheet_viewport"); QVERIFY(colorViewport);
    auto *pickerItem = qobject_cast<QQuickItem *>(picker); QVERIFY(pickerItem);
    QTRY_VERIFY(bounds(pickerItem, colorViewport).right() <= colorViewport->width() + 0.01);
    window.resize(1200, 1000); editor->setSize(window.size());
    const QColor edited("#123456");
    QVERIFY(QMetaObject::invokeMethod(picker, "setColor", Q_ARG(QColor, edited)));
    QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
    QTRY_VERIFY(!colorSheet->property("visible").toBool());
    auto accepted = parameters("neonGlow")["glowColor"].toList();
    QCOMPARE(accepted.size(), 3);
    QVERIFY(qAbs(accepted[0].toReal() - edited.redF()) < 0.001);
    QVERIFY(qAbs(accepted[1].toReal() - edited.greenF()) < 0.001);
    QVERIFY(qAbs(accepted[2].toReal() - edited.blueF()) < 0.001);
    QTRY_VERIFY(!panel->property("modalActive").toBool());
    QSignalSpy reopenColorFrame(&window, &QQuickWindow::frameSwapped);
    window.update(); QTRY_VERIFY(!reopenColorFrame.isEmpty());
    pick = visualItem(panel, "graphicAction-718:67748"); QVERIFY(pick && pick->isVisible() && pick->isEnabled());
    const auto pickerClickRect = bounds(pick, window.contentItem());
    QSignalSpy pickerClicked(pick, SIGNAL(clicked())); QVERIFY(pickerClicked.isValid());
    QSignalSpy pickerPressed(pick, SIGNAL(pressed()));
    QSignalSpy pickerReleased(pick, SIGNAL(released()));
    QSignalSpy pickerCanceled(pick, SIGNAL(canceled()));
    MouseInputRecorder pickerInput; window.installEventFilter(&pickerInput);
    const auto pickerHit = visualHitPath(window.contentItem(), pickerClickRect.center());
    click(&window, pick);
    if (!colorSheet->property("opened").toBool() && pickerClicked.isEmpty())
        qInfo() << "Color picker input diagnostic" << pickerClickRect << "active" << window.isActive()
            << "frame" << reopenColorFrame.count() << "clicked" << pickerClicked.count()
            << "pressed" << pickerPressed.count() << "events" << pickerInput.presses << "hit" << pickerHit
            << "released" << pickerReleased.count() << "canceled" << pickerCanceled.count()
            << "releaseEvents" << pickerInput.releases << "moves" << pickerInput.moves
            << "window" << window.geometry() << "frameGeometry" << window.frameGeometry();
    window.removeEventFilter(&pickerInput);
    QTRY_VERIFY(colorSheet->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(picker, "setColor", Q_ARG(QColor, QColor("#abcdef"))));
    QVERIFY(QMetaObject::invokeMethod(picker, "cancel"));
    QTRY_VERIFY(!colorSheet->property("visible").toBool());
    QCOMPARE(parameters("neonGlow")["glowColor"].toList(), accepted);
    auto *reset = visualItem(panel, "graphicResetView"); QVERIFY(reset);
    click(&window, reset);
    accepted = parameters("neonGlow")["glowColor"].toList();
    QCOMPARE(accepted, (QVariantList{0, 0.62, 1}));

    // Opening and canceling the native source dialog leaves its draft untouched.
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("718:59027"))));
    QTRY_COMPARE(panel->property("currentViewId").toString(), QString("718:59027"));
    const auto draftsBeforeDialog = mapProperty(state, "values")["graphicDrafts"];
    QTRY_VERIFY(!panel->property("modalActive").toBool());
    QSignalSpy sourceFrame(&window, &QQuickWindow::frameSwapped);
    window.update(); QTRY_VERIFY(!sourceFrame.isEmpty());
    auto *choose = visualItem(panel, "graphicAction-718:59219"); QVERIFY(choose);
    QTRY_VERIFY(choose->isVisible() && choose->isEnabled());
    QTRY_VERIFY(bounds(choose, window.contentItem()).top() >= 0
        && bounds(choose, window.contentItem()).bottom() <= window.height());
    click(&window, choose);
    auto *sourcePicker = editor->findChild<QObject *>("editorGraphicSourcePicker"); QVERIFY(sourcePicker);
    QTRY_VERIFY(sourcePicker->property("visible").toBool());
    QCOMPARE(panel->property("sourceParameter").toString(), QString("map"));
    QVERIFY(QMetaObject::invokeMethod(sourcePicker, "reject"));
    QTRY_VERIFY(!sourcePicker->property("visible").toBool());
    QCOMPARE(mapProperty(state, "values")["graphicDrafts"], draftsBeforeDialog);
}

void GuiTests::editorGraphicPanelMobileRoutes_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("graphic-mobile-402") << QSize(402, 874);
    QTest::newRow("graphic-mobile-320") << QSize(320, 640);
}

void GuiTests::editorGraphicPanelMobileRoutes()
{
    QFETCH(QSize, size);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "ios"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"mobileLayout", true}})));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QQuickWindow window; window.resize(size);
    editor->setParentItem(window.contentItem()); editor->setSize(size);
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *toolbar = item(editor.data(), "editorToolbar"); QVERIFY(toolbar);
    auto *sheet = editor->findChild<QObject *>("editorToolSheet"); QVERIFY(sheet);
    const auto tools = listProperty(toolbar, "tools");
    const QMap<QString, QString> representatives{{"color", "710:19322"}, {"auto-enhance", "710:26956"},
        {"masking", "711:26624"}, {"effects", "722:80722"}};
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    for (auto it = representatives.cbegin(); it != representatives.cend(); ++it) {
        int toolIndex = -1;
        for (int index = 0; index < tools.size(); ++index)
            if (tools[index].toMap()["key"].toString() == it.key()) toolIndex = index;
        QVERIFY(toolIndex >= 0);
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex)));
        QTRY_VERIFY(sheet->property("opened").toBool());
        auto *panel = item(sheet, "editorGraphicPanel"); QVERIFY(panel);
        auto *viewport = item(sheet, "sheet_viewport"); QVERIFY(viewport);
        QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, it.value())));
        QTRY_COMPARE(panel->property("currentViewId").toString(), it.value());
        QTest::qWait(250); viewport->setProperty("contentY", 0);
        QTRY_COMPARE(panel->width(), sheet->property("availableContentWidth").toReal());
        QVERIFY(panel->width() <= size.width());
        const auto view = mapProperty(panel, "selectedView");
        const auto rows = graphicRows(view["rows"].toList());
        QVERIFY(!rows.isEmpty());
        for (const auto &entry : rows) {
            auto *control = visualItem(panel, qPrintable("graphicControl-" + entry.toMap()["id"].toString())); QVERIFY(control);
            const auto rect = bounds(control, panel);
            QVERIFY2(rect.left() >= -0.01 && rect.right() <= panel->width() + 0.01,
                qPrintable(view["name"].toString() + '/' + control->objectName()));
        }
        for (auto *control : panel->findChildren<QQuickItem *>()) {
            if (!control->objectName().startsWith("graphic") || !control->isVisible()) continue;
            bool clippedHorizontalNavigation = false;
            for (auto *parent = control->parentItem(); parent && parent != panel; parent = parent->parentItem()) {
                if (parent->property("contentWidth").toReal() <= parent->width()
                    || parent->property("contentHeight").toReal() > parent->height() + 0.01) continue;
                if (!parent->property("clip").toBool()) continue;
                const auto viewportRect = bounds(parent, panel);
                QVERIFY(viewportRect.left() >= -0.01 && viewportRect.right() <= panel->width() + 0.01);
                clippedHorizontalNavigation = true;
                break;
            }
            if (clippedHorizontalNavigation) continue;
            const auto rect = bounds(control, panel);
            QVERIFY2(rect.left() >= -0.01 && rect.right() <= panel->width() + 0.01,
                qPrintable(view["name"].toString() + '/' + control->objectName()));
        }
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory));
            QVERIFY(window.grabWindow().save(captureDirectory + '/' + QTest::currentDataTag() + '-' + view["name"].toString() + ".png"));
        }
        const auto maximum = qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height());
        if (maximum > 0) {
            QVERIFY(viewport->setProperty("contentY", maximum));
            auto *last = visualItem(panel, qPrintable("graphicControl-" + rows.last().toMap()["id"].toString())); QVERIFY(last);
            QTRY_VERIFY(bounds(last, viewport).bottom() <= viewport->height() + 1);
        }
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY(!sheet->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, toolIndex)));
        QTRY_VERIFY(sheet->property("opened").toBool());
        panel = item(sheet, "editorGraphicPanel"); QVERIFY(panel);
        QTRY_COMPARE(panel->property("currentViewId").toString(), it.value());
        QVERIFY(QMetaObject::invokeMethod(sheet, "dismiss"));
        QTRY_VERIFY(!sheet->property("visible").toBool());
    }
}

void GuiTests::editorToolControlsDoNotCommitUneditedFocus_data()
{
    QTest::addColumn<QVariantMap>("field");
    QTest::addColumn<QVariant>("initialValue");
    QTest::addColumn<QVariant>("reboundValue");
    QTest::addColumn<QStringList>("inputNames");
    QTest::addColumn<QStringList>("reboundTexts");
    QTest::addColumn<QString>("typedText");
    QTest::addColumn<QVariant>("acceptedValue");
    QTest::newRow("unedited-slider-focus")
        << QVariantMap{{"id", "field-1"}, {"type", "Slider"}, {"label", "Exposure"},
            {"initial", 0.7}, {"minimum", -5.0}, {"maximum", 5.0}, {"step", 0.1}, {"unit", "EV"}, {"decimals", 1}}
        << QVariant(0.7) << QVariant(1.2)
        << QStringList{"editorNumeric-field-1"} << QStringList{"1.2 EV"}
        << QString("1.4 EV") << QVariant(1.4);
    QTest::newRow("unedited-dimensions-focus")
        << QVariantMap{{"id", "field-0"}, {"type", "Dimensions"}, {"label", "Dimensions"},
            {"initial", QVariantList{48, 32}}}
        << QVariant(QVariantList{48, 32}) << QVariant(QVariantList{64, 40})
        << QStringList{"editorDimension-field-0-0", "editorDimension-field-0-1"}
        << QStringList{"64 px", "40 px"}
        << QString("96 px") << QVariant(QVariantList{96, 40});
    QTest::newRow("unedited-color-focus")
        << QVariantMap{{"id", "field-16"}, {"type", "Color"}, {"label", "Fill color"}, {"initial", "#587A9A"}}
        << QVariant(QString("#587A9A")) << QVariant(QString("#123456"))
        << QStringList{"editorColorInput-field-16"} << QStringList{"#123456"}
        << QString("#ABCDEF") << QVariant(QString("#ABCDEF"));
}

void GuiTests::editorToolControlsDoNotCommitUneditedFocus()
{
    QFETCH(QVariantMap, field);
    QFETCH(QVariant, initialValue);
    QFETCH(QVariant, reboundValue);
    QFETCH(QStringList, inputNames);
    QFETCH(QStringList, reboundTexts);
    QFETCH(QString, typedText);
    QFETCH(QVariant, acceptedValue);
    QCOMPARE(inputNames.size(), reboundTexts.size());
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/EditorToolControl.qml"));
    QScopedPointer<QQuickItem> control(qobject_cast<QQuickItem *>(component.createWithInitialProperties(
        {{"field", field}, {"value", initialValue}, {"sampleLayout", true}, {"width", 398}})));
    QVERIFY2(control, qPrintable(component.errorString()));
    QQuickWindow window; window.resize(432, 240);
    control->setParentItem(window.contentItem()); control->setPosition({17, 17});
    QQuickItem focusSink(window.contentItem());
    focusSink.setPosition({17, 180}); focusSink.setSize({100, 30});
    QSignalSpy edits(control.data(), SIGNAL(edited(QVariant))); QVERIFY(edits.isValid());
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(50);
    QCOMPARE(edits.count(), 0);
    for (int cycle = 0; cycle < 2; ++cycle) {
        if (cycle == 1) {
            QVERIFY(control->setProperty("value", reboundValue));
            for (int index = 0; index < inputNames.size(); ++index) {
                auto *input = visualItem(control.data(), qPrintable(inputNames[index])); QVERIFY(input);
                QTRY_COMPARE(input->property("text").toString(), reboundTexts[index]);
            }
            QCOMPARE(edits.count(), 0);
        }
        for (const auto &name : inputNames) {
            auto *input = visualItem(control.data(), qPrintable(name));
            QVERIFY(input && input->isVisible());
            QSignalSpy focus(input, &QQuickItem::activeFocusChanged); QVERIFY(focus.isValid());
            input->forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(input->hasActiveFocus());
            QCOMPARE(edits.count(), 0);
            focusSink.forceActiveFocus(Qt::TabFocusReason);
            QTRY_VERIFY(focusSink.hasActiveFocus() && !input->hasActiveFocus());
            QTRY_VERIFY(focus.count() >= 2);
            QTest::qWait(50);
            QCOMPARE(edits.count(), 0);
        }
    }
    // A real keyboard edit must still be accepted once on focus loss. The
    // active item is the LVRS field's inner TextInput, not a synthetic signal.
    auto *input = visualItem(control.data(), qPrintable(inputNames.first())); QVERIFY(input);
    auto *textInput = input->property("inputItem").value<QQuickItem *>();
    QVERIFY(textInput && textInput->property("text").isValid());
    textInput->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(input->hasActiveFocus());
    QTRY_COMPARE(window.activeFocusItem(), textInput);
    QTest::keySequence(&window, QKeySequence(QKeySequence::SelectAll));
    for (const char character : typedText.toLatin1()) QTest::keyClick(&window, character);
    QTRY_COMPARE(input->property("text").toString(), typedText);
    QCOMPARE(edits.count(), 0);
    focusSink.forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(focusSink.hasActiveFocus() && !input->hasActiveFocus());
    QTRY_COMPARE(edits.count(), 1);
    const auto emitted = edits.first().first();
    const auto accepted = emitted.metaType() == QMetaType::fromType<QJSValue>()
        ? emitted.value<QJSValue>().toVariant() : emitted;
    QCOMPARE(accepted, acceptedValue);
    input->forceActiveFocus(Qt::TabFocusReason); QTRY_VERIFY(input->hasActiveFocus());
    focusSink.forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY(focusSink.hasActiveFocus() && !input->hasActiveFocus());
    QTest::qWait(50);
    QCOMPARE(edits.count(), 1);
}

void GuiTests::editorToolNumericContracts()
{
    QFile file(sourceUrl("Views/Editor/EditorToolDefinitions.js").toLocalFile());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString script = QString::fromUtf8(file.readAll());
    script.remove(".pragma library");
    script.remove(".import \"EditorTextDefinitions.js\" as TextDefinitions");
    script.remove(".import \"EditorCameraPhotoDefinitions.js\" as PhotoDefinitions");
    QFile photoFile(sourceUrl("Views/Editor/EditorCameraPhotoDefinitions.js").toLocalFile());
    QVERIFY(photoFile.open(QIODevice::ReadOnly));
    QString photoScript = QString::fromUtf8(photoFile.readAll()); photoScript.remove(".pragma library");
    script.prepend("var PhotoDefinitions = (function() {" + photoScript + "\nreturn {definition: definition}; })();\n");
    script.remove(".import \"EditorGraphicDefinitions.js\" as GraphicDefinitions");
    QFile textFile(sourceUrl("Views/Editor/EditorTextDefinitions.js").toLocalFile());
    QVERIFY(textFile.open(QIODevice::ReadOnly));
    QString textScript = QString::fromUtf8(textFile.readAll()); textScript.remove(".pragma library");
    textScript = "var TextDefinitions = (function() {" + textScript + "\nreturn {definition: definition}; })();\n";
    script.prepend(textScript);
    QFile graphicFile(sourceUrl("Views/Editor/EditorGraphicDefinitions.js").toLocalFile());
    QVERIFY(graphicFile.open(QIODevice::ReadOnly));
    QString graphicScript = QString::fromUtf8(graphicFile.readAll());
    graphicScript.remove(".pragma library");
    script.prepend("var GraphicDefinitions = (function() {" + graphicScript
        + "\nreturn {definition: definition, defaultsFor: defaultsFor}; })();\n");
    script.remove(".import \"EditorFileDefinitions.js\" as FileDefinitions");
    QFile fileDefinitions(sourceUrl("Views/Editor/EditorFileDefinitions.js").toLocalFile());
    QVERIFY(fileDefinitions.open(QIODevice::ReadOnly));
    QString fileScript = QString::fromUtf8(fileDefinitions.readAll()); fileScript.remove(".pragma library");
    script.prepend("var FileDefinitions = (function() {" + fileScript + "\nreturn {definition: definition}; })();\n");
    QJSEngine engine;
    const auto evaluated = engine.evaluate(script);
    QVERIFY2(!evaluated.isError(), qPrintable(evaluated.toString()));
    const auto validation = engine.evaluate(R"JS(
        var errors = [];
        tools.forEach(function(tool, index) {
            if (tool.number !== index + 1) errors.push('editor panel numbering: ' + tool.key);
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
        if (tool('asset') !== null || runtimeDefinition(tool('asset')) !== null) errors.push('removed Asset tool remains available');
        if (tools.length !== 18) errors.push('editor tool count');
        if (tool('camera-photo').selector.options.join(',') !== 'Camera,Capture,Gallery') errors.push('camera views diverged from Figma');
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
    QCOMPARE(tools.size(), 18);
    const QMap<QString, int> counts{{"elements", 18}, {"text", 65}, {"camera-photo", 19},
        {"file", 0}, {"background", 16}, {"audio-track", 16}, {"canvas", 20}, {"generative", 31},
        {"layers", 27}, {"select", 17}, {"color", 48}, {"effects", 48}, {"retouch", 23},
        {"fill", 16}, {"brush", 27}, {"auto-enhance", 20}, {"masking", 26}, {"eraser", 14}};
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    int total = 0;
    for (int index = 0; index < tools.size(); ++index) {
        const auto key = tools[index].toMap()["key"].toString();
        QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, index)));
        QTRY_VERIFY(sheet->property("opened").toBool());
        QCOMPARE(sheet->property("toolId").toString(), key);
        auto *panel = item(sheet, "editorToolPanel");
        auto *viewport = item(sheet, "sheet_viewport");
        QVERIFY(viewport);
        const auto catalog = sheet->property("definition").value<QJSValue>().toVariant().toMap()["fields"].toList();
        if (counts.contains(key)) { QCOMPARE(catalog.size(), counts[key]); total += catalog.size(); }
        if (key == "file") {
            auto *filePanel = item(sheet, "editorFilePanel"); QVERIFY(filePanel);
            QCOMPARE(filePanel->property("segmentCount").toInt(), 3);
            QCOMPARE(filePanel->property("fieldCount").toInt(), 108);
            QTest::keyClick(&window, Qt::Key_Escape);
            QTRY_VERIFY(!sheet->property("visible").toBool());
            continue;
        }
        if (key == "background") {
            auto *background=item(sheet,"editorBackgroundPanel");QVERIFY(background);
            QCOMPARE(background->property("currentView").toString(),QString("Color"));
            QVERIFY(visualItem(background,"backgroundNavigation"));QVERIFY(visualItem(background,"maskNew"));
            for(const auto &view:QStringList{"Color","Remove","Mask","Replace"}) {
                QVERIFY(QMetaObject::invokeMethod(sheet,"setValue",Q_ARG(QVariant,QVariant("backgroundView")),Q_ARG(QVariant,QVariant(view))));
                QTRY_COMPARE(background->property("currentView").toString(),view);
                const auto name=QString("background%1Body").arg(view);
                QTRY_VERIFY(visualItem(background,qPrintable(name)) && visualItem(background,qPrintable(name))->isVisible());
            }
            QVERIFY(background->width()<=viewport->width()+1);
            QTest::keyClick(&window,Qt::Key_Escape);QTRY_VERIFY(!sheet->property("visible").toBool());
            continue;
        }
        if (key == "elements") {
            auto *elements = item(sheet, "editorElementsPanel"); QVERIFY(elements);
            QTRY_COMPARE(elements->width(), sheet->property("availableContentWidth").toReal());
            const QStringList views{"rectangle", "ellipse", "polygon", "line", "fill", "stroke", "selection", "inspect", "multiple", "clear"};
            for (const auto &view : views) {
                QVERIFY(QMetaObject::invokeMethod(elements, "selectView", Q_ARG(QVariant, view)));
                QTRY_COMPARE(elements->property("currentViewId").toString(), view);
                QTest::qWait(250);
                viewport->setProperty("contentY", 0);
                QVERIFY(elements->width() <= sheet->property("width").toReal());
                auto *header = visualItem(elements, "elementsHeader"); QVERIFY(header);
                QVERIFY(bounds(header, elements).right() <= elements->width());
                for (auto *control : elements->findChildren<QQuickItem *>()) {
                    if (!control->isVisible() || !control->objectName().startsWith("elements")) continue;
                    const auto rect = bounds(control, elements);
                    QVERIFY2(rect.left() >= -1 && rect.right() <= elements->width() + 1,
                             qPrintable(view + '/' + control->objectName()));
                }
                const auto end = qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height());
                viewport->setProperty("contentY", end);
                if (!captureDirectory.isEmpty()) {
                    QVERIFY(QDir().mkpath(captureDirectory));
                    QVERIFY(window.grabWindow().save(captureDirectory + '/' + QTest::currentDataTag() + "-elements-" + view + ".png"));
                }
            }
            QTest::keyClick(&window, Qt::Key_Escape);
            QTRY_VERIFY(!sheet->property("visible").toBool());
            continue;
        }
        if (QStringList{"color", "adjust", "auto-enhance", "effects", "masking"}.contains(key)) {
            auto *graphic = item(sheet, "editorGraphicPanel"); QVERIFY(graphic);
            QTRY_COMPARE(graphic->width(), sheet->property("availableContentWidth").toReal());
            QVERIFY(!listProperty(graphic, "viewCatalog").isEmpty());
            QVERIFY(!graphic->property("currentViewId").toString().isEmpty());
            QVERIFY(graphic->width() <= sheet->property("width").toReal());
            QTest::keyClick(&window, Qt::Key_Escape);
            QTRY_VERIFY(!sheet->property("visible").toBool());
            continue;
        }
        if (key == "camera-photo") {
            auto *photo = item(sheet, "editorCameraPhotoPanel"); QVERIFY(photo);
            QTRY_COMPARE(photo->width(), sheet->property("availableContentWidth").toReal());
            QVERIFY(visualItem(photo, "photoCameraPreview"));
            QTest::keyClick(&window, Qt::Key_Escape);
            QTRY_VERIFY(!sheet->property("visible").toBool());
            continue;
        }
        if (key == "text") {
            auto *text = item(sheet, "editorTextPanel"); QVERIFY(text);
            QTRY_COMPARE(text->width(), sheet->property("availableContentWidth").toReal());
            const QStringList tabs{"content", "typography", "layout", "appearance"};
            for (int view = 0; view < tabs.size(); ++view) {
                // Fit sheets animate their height when switching text views.
                QTest::qWait(250);
                viewport->setProperty("contentY", 0);
                auto *tab = visualItem(text, qPrintable("textTab-" + tabs[view])); QVERIFY(tab);
                click(&window, tab); QTRY_COMPARE(text->property("panelIndex").toInt(), view);
                QVERIFY(text->width() <= sheet->property("width").toReal());
            }
            QVERIFY(viewport->property("contentHeight").toReal() > viewport->height());
            QTest::keyClick(&window, Qt::Key_Escape);
            QTRY_VERIFY(!sheet->property("visible").toBool());
            continue;
        }
        if (key == "layers") {
            auto *layers = item(sheet, "editorLayersPanel"); QVERIFY(layers);
            QTRY_COMPARE(layers->width(), sheet->property("availableContentWidth").toReal());
            const QStringList tabs{"layer", "adjust", "histogram", "information"};
            for (int view = 0; view < tabs.size(); ++view) {
                auto *tab = visualItem(layers, qPrintable("layerTab-" + tabs[view])); QVERIFY(tab);
                click(&window, tab); QTRY_COMPARE(layers->property("panelIndex").toInt(), view);
                QTest::qWait(40);
                QVERIFY(layers->width() <= sheet->property("width").toReal());
                if (view == 1) {
                    auto *exposure = visualItem(layers, "layerAction-exposure"); QVERIFY(exposure);
                    QVERIFY(bounds(exposure, layers).right() <= layers->width());
                }
            }
            QVERIFY(viewport->property("contentHeight").toReal() >= 472);
            QTest::keyClick(&window, Qt::Key_Escape);
            QTRY_VERIFY(!sheet->property("visible").toBool());
            continue;
        }
        if (isCompactEditorTool(key)) {
            auto *compact = item(sheet, "editorCompactPanel"); QVERIFY(compact && compact->isVisible());
            QTRY_COMPARE(compact->width(), sheet->property("availableContentWidth").toReal());
            QVERIFY(!listProperty(compact, "viewCatalog").isEmpty());
            QVERIFY(visualItem(compact, "compactCurrentContext"));
            QTest::keyClick(&window, Qt::Key_Escape);
            QTRY_VERIFY(!sheet->property("visible").toBool());
            continue;
        }
        QVERIFY(panel);
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
    QCOMPARE(total, 451);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(sheet->property("opened").toBool());
    auto *panel = item(sheet, "editorElementsPanel");
    auto *viewport = item(sheet, "sheet_viewport");
    QVERIFY(panel && viewport);
    const auto chooseElementView = [&](const QString &view) {
        QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, view)));
        QTRY_COMPARE(panel->property("currentViewId").toString(), view);
        QTest::qWait(250);
        viewport->setProperty("contentY", 0);
    };
    chooseElementView("rectangle");
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
    auto *dimension = visualItem(panel, "elementsInput-field-0-0");
    QVERIFY(dimension);
    reveal(dimension);
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    for (const char character : QByteArrayLiteral("1536 px")) QTest::keyClick(&window, character);
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(values()["field-0"].toList(), QVariantList({1536, 1536}));
    chooseElementView("fill");
    auto *choice = visualItem(panel, "elementsChoice-field-2-1");
    QVERIFY(choice);
    reveal(choice);
    QCOMPARE(values()["field-2"].toString(), "Gradient");
    chooseElementView("rectangle");
    auto *toggle = visualItem(panel, "elementsToggle-field-3");
    QVERIFY(toggle);
    reveal(toggle);
    QCOMPARE(values()["field-3"].toBool(), false);
    auto *slider = visualItem(panel, "elementsSlider-field-1");
    QVERIFY(slider);
    reveal(slider);
    slider->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_End);
    QCOMPARE(values()["field-1"].toInt(), 256);
    auto *numeric = visualItem(panel, "elementsInput-field-1");
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
    chooseElementView("fill");
    auto *color = visualItem(panel, "elementsColorPicker-field-16");
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
    auto *textPanel = item(sheet, "editorTextPanel"); QVERIFY(textPanel);
    auto *textInput = visualItem(textPanel, "textContentInput");
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
    panel = item(sheet, "editorElementsPanel"); QVERIFY(panel);
    chooseElementView("fill");
    click(&window, visualItem(panel, "elementsHeaderAction"));
    QCOMPARE(values()["field-1"].toInt(), 37);
    QCOMPARE(values()["field-2"].toString(), "Solid");
    QCOMPARE(values()["field-16"].toString(), "#8B7CFF");
    chooseElementView("rectangle");
    click(&window, visualItem(panel, "elementsHeaderAction"));
    QCOMPARE(values()["field-1"].toInt(), 24);
    QCOMPARE(values()["field-3"].toBool(), true);
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
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, 3)));
    QTRY_VERIFY(sheet->property("opened").toBool());
    panel = item(sheet, "editorFilePanel");
    QVERIFY(panel);
    QVERIFY(!visualItem(panel, "editorAction-field-12"));
    QCOMPARE(actions.size(), 0);
    auto *documentControls = item(editor, "editorDocumentControls"); QVERIFY(documentControls);
    QVERIFY(QMetaObject::invokeMethod(documentControls, "requestSave"));
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
    QCOMPARE(actions.size(), 0);
}

void GuiTests::resultCannotOpenProjectWithoutReadableImage()
{
    QQmlApplicationEngine engine;
    engine.load(sourceUrl("Main.qml"));
    QCOMPARE(engine.rootObjects().size(), 1);
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *result = item(window, "generationResult");
    auto *selection = item(result, "resultImageSelection");
    QVERIFY(selection);
    QVERIFY(!selection->isEnabled());
    QVERIFY(!item(window, "newProjectButton"));
    // Inject the unreadable image at the view boundary; accepted submissions only expose verified files.
    QVERIFY(item(window, "generationResult")->setProperty("result",
        QVariantMap{{"imageSource", sourceUrl("missing-result.png")}}));
    QVERIFY(window->setProperty("resultVisible", true));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE(item(window, "generatedImage")->property("status").toInt(), 3);
    QSignalSpy requests(result, SIGNAL(imagesRequested(QVariant)));
    QVERIFY(requests.isValid());
    click(window, item(selection, "resultImageSelectionToggle"));
    QVERIFY(QMetaObject::invokeMethod(selection, "selectAll"));
    click(window, item(selection, "resultImageSelectionOpen"));
    QCOMPARE(requests.size(), 1);
    QVERIFY(!window->property("editorVisible").toBool());
    QVERIFY(window->property("resultVisible").toBool());
    QVERIFY(!window->property("generationRequestError").toString().isEmpty());
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
        QCOMPARE(material->property("color").value<QColor>(), QColor("#141414"));
        QCOMPARE(material->property("tintOpacity").toReal(), 0.66);
        QCOMPARE(material->property("intenseOpacity").toReal(), 0.0);
        QCOMPARE(material->property("faintOpacity").toReal(), 0.0);
        QCOMPARE(material->property("blurRadius").toReal(), 64.0);
        auto *accentOverlay = material->findChild<QQuickItem *>("applicationWindowAccentOverlay");
        QVERIFY(accentOverlay);
        QCOMPARE(accentOverlay->opacity(), 0.04);
        for (const QColor accent : {QColor("#57965C"), QColor("#0A84FF")}) {
            QVERIFY(window->setProperty("primaryColor", accent));
            QTRY_COMPARE(material->property("primaryColor").value<QColor>(), accent);
            QTRY_COMPARE(material->property("displayedPrimaryColor").value<QColor>(), accent);
            QTRY_COMPARE(accentOverlay->property("color").value<QColor>(), accent);
        }
        QCOMPARE(material->property("color").value<QColor>(), QColor("#141414"));
        QCOMPARE(window->opacity(), 1.0);
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
        if (window->property("backgroundBlurSupported").toBool()) {
            QTRY_VERIFY(window->property("backgroundBlurActive").toBool());
            const QImage home = window->grabWindow();
            QVERIFY(!home.isNull());
            const qreal dpr = window->devicePixelRatio();
            const QColor background = home.pixelColor(qRound(24 * dpr),
                                                       qRound((window->height() - 24) * dpr));
            QVERIFY2(qAbs(background.alpha() - 172) <= 2,
                     qPrintable(QString("Home must expose the translucent window material, got %1")
                         .arg(background.name(QColor::HexArgb))));
            QVERIFY2(background.blue() > background.green() + 3,
                     qPrintable(QString("Home must show the 4% blue app accent overlay, got %1")
                         .arg(background.name(QColor::HexArgb))));
        }
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
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/preferences-drive-XXXXXX.societycontainer");
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/preferences-models-XXXXXX.societycontainer");
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
        if (category != "Account" && category != "Storage" && category != "Image" && category != "Video"
            && category != "Audio" && category != "Agents")
            QVERIFY(visualItem(preferences->contentItem(), "preferencesCategoryDetails")->isVisible());
        else if (category != "Account" && category != "Storage")
            QVERIFY(visualItem(preferences->contentItem(), "preferencesGenerateDetails")->isVisible());
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

void GuiTests::preferencesPromptsWrapAndPersist()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/preferences-prompts-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QFile model(storage.filePath("Models/krea2SnowlineStyle_v10.safetensors"));
    QVERIFY(model.open(QIODevice::WriteOnly)); QVERIFY(model.write("UI model fixture") > 0); model.close();
    GenerationRuntime runtime;
    runtime.modelPreferencesFile = storage.filePath("settings/generation.conf");
    GenerationController controller(runtime);
    QVERIFY(controller.connectStorage(storage.path()));
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Preferences/PreferencesWindow.qml"));
    std::unique_ptr<QObject> root(component.createWithInitialProperties({
        {"generation", QVariant::fromValue<QObject *>(&controller)}, {"currentCategory", "Image"}}));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get()); QVERIFY(window);
    window->show(); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *positive = visualItem(window->contentItem(), "defaultGenerationPrompt");
    auto *negative = visualItem(window->contentItem(), "defaultGenerationNegativePrompt");
    auto *negativeLabel = visualItem(window->contentItem(), "defaultNegativePromptLabel");
    auto *details = visualItem(window->contentItem(), "preferencesGenerateDetails");
    QVERIFY(positive && negative && negativeLabel && details);
    const auto paste = [&](QQuickItem *field, const QString &text) {
        auto *input = field->property("inputItem").value<QQuickItem *>();
        input->forceActiveFocus();
        QMetaObject::invokeMethod(input, "selectAll");
        if (text.isEmpty()) QTest::keyClick(window, Qt::Key_Backspace);
        else { QGuiApplication::clipboard()->setText(text); QMetaObject::invokeMethod(input, "paste"); }
    };
    for (const auto &category : {QString("Image"), QString("Video"), QString("Audio"), QString("Agents")}) {
        QVERIFY(window->setProperty("currentCategory", category));
        QVERIFY(controller.setDefaultGenerationPrompt(category, "saved " + category, false));
        QTRY_COMPARE(positive->property("text").toString(), "saved " + category);
        QVERIFY(positive->isVisible() && negative->isVisible());
        for (const int width : {720, 560}) {
            window->resize(width, 480);
            QTRY_COMPARE(positive->width(), qreal(width - 207 - 40));
            QTRY_COMPARE(negative->width(), positive->width());
            for (auto *field : {positive, negative}) {
                const QString text = category + QString::fromUtf8(" richly detailed landscape with soft light. 한글 프롬프트 줄바꿈 검증. ").repeated(30);
                paste(field, text);
                QTRY_COMPARE(field->property("text").toString(), text);
                QTRY_VERIFY(field->height() > 44);
                auto *input = field->property("inputItem").value<QQuickItem *>();
                QVERIFY(input);
                QTRY_VERIFY(input->height() >= input->property("contentHeight").toReal());
                QVERIFY(input->property("contentWidth").toReal() <= input->width() + 1);
                QVERIFY(bounds(negativeLabel, details).top() >= bounds(positive, details).bottom());
                QVERIFY(bounds(negative, details).top() >= bounds(negativeLabel, details).bottom());
                const auto key = field == positive ? "prompt" : "negativePrompt";
                QTRY_COMPARE(controller.defaultGenerationPrompts().value(category).toMap().value(key).toString(), text);
                auto *viewport = field->property("scrollViewport").value<QQuickItem *>();
                QVERIFY(viewport);
                const auto caret = [&] { return input->mapRectToItem(viewport, input->property("cursorRectangle").toRectF()); };
                QVERIFY(input->setProperty("cursorPosition", text.size()));
                QTRY_VERIFY(caret().top() >= 0 && caret().bottom() <= viewport->height());
                QVERIFY(input->setProperty("cursorPosition", 0));
                QTRY_VERIFY(caret().top() >= 0 && caret().bottom() <= viewport->height());
                paste(field, {});
                QTRY_COMPARE(field->height(), 22.0);
                QTRY_VERIFY(controller.defaultGenerationPrompts().value(category).toMap().value(key).toString().isEmpty());
            }
        }
        paste(positive, category + QString::fromUtf8(" · A cinematic landscape with soft afternoon light and carefully balanced colors. 부드러운 빛과 섬세한 질감을 표현한다. ").repeated(2));
        paste(negative, category + QString::fromUtf8(" · Blur, noise, distorted details, low quality. 흐릿한 표현과 왜곡을 제외한다. ").repeated(2));
    }
    const auto expected = controller.defaultGenerationPrompts();
    GenerationController restored(runtime);
    QCOMPARE(restored.defaultGenerationPrompts(), expected);
    QVERIFY(window->setProperty("currentCategory", "Image"));
    QTRY_COMPARE(positive->property("text").toString(), expected.value("Image").toMap().value("prompt").toString());
    window->resize(720, 560);
    positive->property("inputItem").value<QQuickItem *>()->forceActiveFocus();
    QVERIFY(positive->property("inputItem").value<QQuickItem *>()->setProperty("cursorPosition", 0));
    const auto capture = qEnvironmentVariable("DREAMSCAPES_PROMPT_PREFERENCES_SCREENSHOT");
    if (!capture.isEmpty()) { QTest::qWait(200); QVERIFY(window->grabWindow().save(capture)); }
    window->close();
}

void GuiTests::generationPromptDefaultsSeedNewDrafts()
{
    GenerationRuntime runtime;
    GenerationController controller(runtime);
    QVERIFY(controller.setDefaultGenerationPrompt("Image", "default image prompt", false));
    QVERIFY(controller.setDefaultGenerationPrompt("Image", "default image negative", true));
    QVERIFY(controller.setDefaultGenerationPrompt("Video", "default video prompt", false));
    QVERIFY(controller.setDefaultGenerationPrompt("Video", "default video negative", true));
    QQmlEngine engine;
    QQmlComponent quickComponent(&engine, sourceUrl("Views/Home/QuickGenerate.qml"));
    std::unique_ptr<QObject> quick(quickComponent.createWithInitialProperties({{"defaultPrompts", controller.defaultGenerationPrompts()}}));
    QVERIFY2(quick, qPrintable(quickComponent.errorString()));
    QCOMPARE(quick->property("prompt").toString(), QString("default image prompt"));
    QVERIFY(quick->setProperty("mediaType", "Video"));
    QCOMPARE(quick->property("prompt").toString(), QString("default video prompt"));
    QVERIFY(quick->setProperty("prompt", "my written prompt"));
    QVERIFY(controller.setDefaultGenerationPrompt("Video", "changed video default", false));
    QVERIFY(quick->setProperty("defaultPrompts", controller.defaultGenerationPrompts()));
    QCOMPARE(quick->property("prompt").toString(), QString("my written prompt"));
    QQmlComponent imageComponent(&engine, sourceUrl("Views/Home/AdvancedGenerate.qml"));
    std::unique_ptr<QObject> image(imageComponent.createWithInitialProperties({{"generation", QVariant::fromValue<QObject *>(&controller)}}));
    QVERIFY2(image, qPrintable(imageComponent.errorString()));
    QCOMPARE(image->property("prompt").toString(), QString("default image prompt"));
    QCOMPARE(image->property("negativePrompt").toString(), QString("default image negative"));
    QVERIFY(image->setProperty("prompt", "my image draft"));
    QVERIFY(controller.setDefaultGenerationPrompt("Image", "changed image default", false));
    QCOMPARE(image->property("prompt").toString(), QString("my image draft"));
    QVERIFY(QMetaObject::invokeMethod(image.get(), "resetDraft"));
    QCOMPARE(image->property("prompt").toString(), QString("changed image default"));
    QQmlComponent videoComponent(&engine, sourceUrl("Views/Video/VideoGenerationWorkspace.qml"));
    std::unique_ptr<QObject> video(videoComponent.createWithInitialProperties({{"generation", QVariant::fromValue<QObject *>(&controller)}}));
    QVERIFY2(video, qPrintable(videoComponent.errorString()));
    auto *panel = video->property("parameterPanel").value<QObject *>(); QVERIFY(panel);
    QCOMPARE(mapProperty(panel, "values").value("prompt").toString(), QString("changed video default"));
    QCOMPARE(mapProperty(panel, "values").value("negativePrompt").toString(), QString("default video negative"));
    QVERIFY(QMetaObject::invokeMethod(panel, "edit", Q_ARG(QVariant, "prompt"), Q_ARG(QVariant, "my video draft")));
    QVERIFY(controller.setDefaultGenerationPrompt("Video", "next video default", false));
    QCOMPARE(mapProperty(panel, "values").value("prompt").toString(), QString("my video draft"));
    QVERIFY(QMetaObject::invokeMethod(panel, "resetDraft"));
    QCOMPARE(mapProperty(panel, "values").value("prompt").toString(), QString("next video default"));
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

    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/preferences-folder-XXXXXX.societycontainer");
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/preferences-avatar-XXXXXX.societycontainer");
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
    QTemporaryDir storage(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/home-drag-drive-XXXXXX");
    QString storageError;
    const auto drive=iiSocietyContainer::SocietyDrive::createAt(storage.path(),&storageError);
    QVERIFY2(drive,qPrintable(storageError));
    const bool hadContainer=qEnvironmentVariableIsSet("SOCIETY_CONTAINER_PATH");
    const auto previousContainer=qgetenv("SOCIETY_CONTAINER_PATH");
    const auto restoreContainer=qScopeGuard([hadContainer,previousContainer] {
        if(hadContainer)qputenv("SOCIETY_CONTAINER_PATH",previousContainer);
        else qunsetenv("SOCIETY_CONTAINER_PATH");
    });
    qputenv("SOCIETY_CONTAINER_PATH",drive->rootPath().toUtf8());
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
    QVERIFY2(canvas->addAttachment(source),qPrintable(canvas->inputError()));
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
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-video-XXXXXX.societycontainer");
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
    auto *fpsMenu=quick->findChild<QObject *>("videoFpsMenu"); QVERIFY(fpsMenu);
    QCOMPARE(listProperty(fpsMenu,"items").size(),5);
    QVERIFY(QMetaObject::invokeMethod(fpsMenu,"itemTriggered",Q_ARG(int,3),Q_ARG(QVariant,listProperty(fpsMenu,"items")[3])));
    QCOMPARE(quick->property("videoFps").toInt(),60);
    QVERIFY(QMetaObject::invokeMethod(fpsMenu,"itemTriggered",Q_ARG(int,4),Q_ARG(QVariant,listProperty(fpsMenu,"items")[4])));
    QCOMPARE(quick->property("videoFps").toInt(),120);
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
    QVERIFY(!item(result,"newProjectButton"));
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

void GuiTests::videoWorkspaceStartsEmptyAndUsesGenerationDefaults()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-video-defaults-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QVERIFY(QDir().mkpath(storage.filePath("Models/Checkpoint/ltx")));
    QFile manifest(storage.filePath("Models/Checkpoint/ltx/model_index.json"));
    QVERIFY(manifest.open(QIODevice::WriteOnly)); manifest.write(R"({"_class_name":"LTXConditionPipeline"})"); manifest.close();
    auto runtime = GenerationRuntime{QStringLiteral(DREAMSCAPES_FAKE_GENERATOR), "cpu", 1, 64, {}};
    runtime.modelPreferencesFile = storage.filePath("settings/generation.conf");
    GenerationController controller(runtime);
    QVERIFY(controller.connectStorage(storage.path()));
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Video/VideoGenerationWorkspace.qml"));
    std::unique_ptr<QObject> workspace(component.createWithInitialProperties({
        {"generation", QVariant::fromValue<QObject *>(&controller)}, {"width", 1280}, {"height", 800}}));
    QVERIFY2(workspace, qPrintable(component.errorString()));
    auto *parameters = workspace->property("parameterPanel").value<QObject *>(); QVERIFY(parameters);
    auto *state = workspace->property("timelineState").value<QObject *>(); QVERIFY(state);
    auto *timeline = workspace->property("timelinePanel").value<QObject *>(); QVERIFY(timeline);
    auto *preview = item(workspace.get(), "videoReferencePreview"); QVERIFY(preview);
    QVERIFY(item(workspace.get(), "videoResetDefaults"));
    QVERIFY(!item(workspace.get(), "videoSaveRecipe"));
    QVERIFY(!item(workspace.get(), "videoSavePreset"));
    const auto verifyEmpty = [&] {
        QCOMPARE(listProperty(state, "shots").size(), 0);
        QCOMPARE(listProperty(state, "keys").size(), 0);
        QCOMPARE(listProperty(state, "selectedIds").size(), 0);
        QVERIFY(state->property("selectedKeyId").toString().isEmpty());
        QCOMPARE(state->property("playhead").toInt(), 0);
        QCOMPARE(state->property("markIn").toInt(), 0);
        QCOMPARE(state->property("markOut").toInt(), state->property("totalFrames").toInt()-1);
        QVERIFY(preview->property("source").toUrl().isEmpty());
        QVERIFY(!item(workspace.get(), "videoSplitShot")->isEnabled());
        QVERIFY(!item(workspace.get(), "videoExcludeShot")->isEnabled());
        QVERIFY(!item(workspace.get(), "videoRippleDelete")->isEnabled());
    };
    verifyEmpty();
    QCOMPARE(state->property("totalFrames").toInt(), 120);
    QCOMPARE(state->property("markOut").toInt(), 119);
    QVERIFY(mapProperty(parameters, "values").value("prompt").toString().isEmpty());
    QVERIFY(mapProperty(parameters, "values").value("negativePrompt").toString().isEmpty());
    QCOMPARE(mapProperty(parameters, "values").value("seed").toInt(), -1);
    QVERIFY(!workspace->property("canGenerate").toBool());
    auto *fpsRow=item(workspace.get(),"videoFps"); QVERIFY(fpsRow);
    QCOMPARE(listProperty(fpsRow,"optionValues"),QVariantList({12,24,30,60,120}));
    for (int fps : {60,120}) {
        QVERIFY(QMetaObject::invokeMethod(parameters,"edit",Q_ARG(QVariant,"fps"),Q_ARG(QVariant,fps)));
        QCOMPARE(mapProperty(parameters,"values").value("fps").toInt(),fps);
        QCOMPARE(state->property("totalFrames").toInt(),5*fps);
        verifyEmpty();
    }
    QVERIFY(QMetaObject::invokeMethod(parameters,"edit",Q_ARG(QVariant,"fps"),Q_ARG(QVariant,90)));
    QVERIFY(parameters->property("hasInvalidInputs").toBool());
    QCOMPARE(mapProperty(parameters,"values").value("fps").toInt(),120);
    QVERIFY(QMetaObject::invokeMethod(parameters,"resetDraft"));
    verifyEmpty();
    QVERIFY(QMetaObject::invokeMethod(parameters, "edit", Q_ARG(QVariant, "randomSeed"), Q_ARG(QVariant, false)));
    QVERIFY(parameters->property("hasInvalidInputs").toBool());
    QCOMPARE(mapProperty(parameters, "values").value("seed").toInt(), -1);
    QVERIFY(QMetaObject::invokeMethod(parameters, "edit", Q_ARG(QVariant, "seed"), Q_ARG(QVariant, 0)));
    QVERIFY(!parameters->property("hasInvalidInputs").toBool());
    QVERIFY(QMetaObject::invokeMethod(parameters, "edit", Q_ARG(QVariant, "randomSeed"), Q_ARG(QVariant, true)));
    QVERIFY(QMetaObject::invokeMethod(parameters, "edit", Q_ARG(QVariant, "duration"), Q_ARG(QVariant, 3)));
    QVERIFY(QMetaObject::invokeMethod(parameters, "edit", Q_ARG(QVariant, "fps"), Q_ARG(QVariant, 12)));
    verifyEmpty();
    QCOMPARE(state->property("totalFrames").toInt(), 36);
    QCOMPARE(state->property("markOut").toInt(), 35);
    QVERIFY(QMetaObject::invokeMethod(state, "undo"));
    QCOMPARE(mapProperty(parameters, "values").value("fps").toInt(), 24);
    QCOMPARE(mapProperty(parameters, "values").value("duration").toInt(), 3);
    QCOMPARE(state->property("totalFrames").toInt(), 72);
    QVERIFY(QMetaObject::invokeMethod(state, "undo"));
    QCOMPARE(mapProperty(parameters, "values").value("duration").toInt(), 5);
    QCOMPARE(state->property("totalFrames").toInt(), 120);
    verifyEmpty();
    QVERIFY(QMetaObject::invokeMethod(state, "redo"));
    QVERIFY(QMetaObject::invokeMethod(state, "redo"));
    QCOMPARE(mapProperty(parameters, "values").value("fps").toInt(), 12);
    QCOMPARE(mapProperty(parameters, "values").value("duration").toInt(), 3);
    QCOMPARE(state->property("totalFrames").toInt(), 36);
    QVERIFY(QMetaObject::invokeMethod(state, "seek", Q_ARG(QVariant, 100)));
    QCOMPARE(state->property("playhead").toInt(), 35);
    const auto keyUrl = QUrl::fromLocalFile(storage.filePath("Files/key.png"));
    QImage reference(64, 32, QImage::Format_RGB32); reference.fill(Qt::red); QVERIFY(reference.save(keyUrl.toLocalFile()));
    QVariant accepted;
    QVERIFY(QMetaObject::invokeMethod(parameters, "addImageKey", Q_RETURN_ARG(QVariant, accepted),
        Q_ARG(QVariant, keyUrl), Q_ARG(QVariant, 35)));
    QVERIFY(accepted.toBool());
    QCOMPARE(listProperty(state, "shots").size(), 0);
    QCOMPARE(listProperty(state, "keys").size(), 1);
    QCOMPARE(listProperty(state, "keys").first().toMap().value("value").toDouble(), 1.0);
    QVERIFY(QMetaObject::invokeMethod(parameters, "edit", Q_ARG(QVariant, "prompt"), Q_ARG(QVariant, "A video")));
    QVariant request;
    QVERIFY(QMetaObject::invokeMethod(parameters, "parameters", Q_RETURN_ARG(QVariant, request)));
    const auto recipe = request.metaType() == QMetaType::fromType<QJSValue>()
        ? request.value<QJSValue>().toVariant().toMap() : request.toMap();
    QCOMPARE(recipe.value("shots").toList().size(), 1);
    QCOMPARE(recipe.value("shots").toList().first().toMap().value("frames").toInt(), 36);
    const auto conditions = recipe.value("shots").toList().first().toMap().value("conditions").toList();
    QCOMPARE(conditions.size(), 1);
    QCOMPARE(conditions.first().toMap().value("frame").toInt(), 35);
    QVERIFY(QMetaObject::invokeMethod(state, "addShot"));
    QCOMPARE(listProperty(state, "shots").size(), 1);
    QCOMPARE(state->property("totalFrames").toInt(), 36);
    QVERIFY(QMetaObject::invokeMethod(state, "rippleDelete"));
    verifyEmpty();
    QVERIFY(QMetaObject::invokeMethod(state, "undo"));
    QCOMPARE(listProperty(state, "shots").size(), 1);
    QCOMPARE(listProperty(state, "keys").size(), 1);
    QVERIFY(QMetaObject::invokeMethod(state, "redo"));
    verifyEmpty();
    QVERIFY(timeline->setProperty("playing", true));
    QVERIFY(timeline->setProperty("timelineScale", 2));
    QVERIFY(QMetaObject::invokeMethod(parameters, "resetDraft"));
    verifyEmpty();
    QCOMPARE(state->property("totalFrames").toInt(), 120);
    QCOMPARE(state->property("nextId").toInt(), 1);
    QVERIFY(!state->property("canUndo").toBool());
    QVERIFY(!state->property("canRedo").toBool());
    QVERIFY(!timeline->property("playing").toBool());
    QCOMPARE(timeline->property("timelineScale").toReal(), 1.0);
    QVERIFY(QMetaObject::invokeMethod(parameters, "edit", Q_ARG(QVariant, "duration"), Q_ARG(QVariant, 3)));
    QVERIFY(QMetaObject::invokeMethod(parameters, "edit", Q_ARG(QVariant, "fps"), Q_ARG(QVariant, 12)));
    QVERIFY(QMetaObject::invokeMethod(state, "addShot"));
    const auto shotId = mapProperty(state, "selectedShot").value("id");
    QVERIFY(QMetaObject::invokeMethod(state, "trim", Q_ARG(QVariant, shotId), Q_ARG(QVariant, "out"),
        Q_ARG(QVariant, 24), Q_ARG(QVariant, "Trim")));
    QCOMPARE(state->property("totalFrames").toInt(), 60);
    // Matching the edited shot extent must also update the empty generation extent.
    QVERIFY(QMetaObject::invokeMethod(parameters, "edit", Q_ARG(QVariant, "duration"), Q_ARG(QVariant, 5)));
    QCOMPARE(state->property("compositionFrames").toInt(), 60);
    QVERIFY(state->setProperty("markIn", 10));
    QVERIFY(QMetaObject::invokeMethod(state, "rippleDelete"));
    verifyEmpty();
    QCOMPARE(state->property("totalFrames").toInt(), 60);
    QVERIFY(QMetaObject::invokeMethod(parameters, "resetDraft"));
    verifyEmpty();
    QVERIFY(QMetaObject::invokeMethod(parameters, "edit", Q_ARG(QVariant, "prompt"), Q_ARG(QVariant, "A video")));
    QVERIFY(workspace->property("canGenerate").toBool());
    QVERIFY(QMetaObject::invokeMethod(workspace.get(), "submit"));
    QCOMPARE(controller.jobs().size(), 1);
    auto actual = QJsonObject::fromVariantMap(controller.jobs().first().toMap().value("videoParameters").toMap());
    QJsonObject expected{{"prompt", "A video"}, {"negativePrompt", ""}, {"randomSeed", true}};
    QString error;
    QVERIFY2(dreamscapes::normalizeVideoRecipe(&expected, &error), qPrintable(error));
    // Queue ownership adds no draft content; the backend supplies its single generation segment.
    QCOMPARE(actual.value("shots"), expected.value("shots"));
    for (const auto &key : expected.keys()) if (key != "seed") QCOMPARE(actual.value(key), expected.value(key));
    verifyEmpty();
}

void GuiTests::videoWorkspaceRoutesEditsAndGenerates()
{
    QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/gui-video-workspace-XXXXXX.societycontainer");
    QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
    QVERIFY(QDir().mkpath(storage.filePath("Models/Checkpoint/ltx")));
    QFile manifest(storage.filePath("Models/Checkpoint/ltx/model_index.json"));
    QVERIFY(manifest.open(QIODevice::WriteOnly)); manifest.write(R"({"_class_name":"LTXConditionPipeline"})"); manifest.close();
    auto runtime=GenerationRuntime{QStringLiteral(DREAMSCAPES_FAKE_GENERATOR),"cpu",1,64,{}};
    runtime.nativeInference=true;
    runtime.modelPreferencesFile=storage.filePath("settings/generation.conf");
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
    QCOMPARE(listProperty(state,"shots").size(),0);
    QCOMPARE(listProperty(state,"keys").size(),0);
    QCOMPARE(state->property("playhead").toInt(),0);
    QVERIFY(item(workspace,"videoReferencePreview")->property("source").toUrl().isEmpty());
    QCOMPARE(item(workspace,"videoTimelineEmptyState")->property("text").toString(),QString("No tracks"));
    QCOMPARE(listProperty(state,"tracks").size(),0);
    auto *upper=item(workspace,"videoUpperWorkspace"); QVERIFY(upper);
    QTRY_COMPARE(parameters->width(),300.0);
    QTRY_COMPARE(timeline->height(),timeline->implicitHeight());
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
    click(window,item(workspace,"videoResetDefaults"));
    QVERIFY(!parameters->property("hasInvalidInputs").toBool());
    QTRY_VERIFY(!widthRow->property("invalidEdit").toBool());
    QTRY_COMPARE(widthRow->property("inputText1").toString(),QString("1024"));
    // Editing fixtures are explicitly added; opening/resetting the view never provides these shots.
    for (const int frames : {24,24,36,36}) {
        click(window,item(workspace,"videoAddShot"));
        const auto shot=mapProperty(state,"selectedShot");
        const int delta=frames-shot.value("frames").toInt();
        if(delta) QVERIFY(QMetaObject::invokeMethod(state,"trim",Q_ARG(QVariant,shot.value("id")),
            Q_ARG(QVariant,"out"),Q_ARG(QVariant,delta),Q_ARG(QVariant,"Trim")));
        QCOMPARE(mapProperty(state,"selectedShot").value("frames").toInt(),frames);
    }
    QVERIFY(QMetaObject::invokeMethod(state,"changeShot",Q_ARG(QVariant,mapProperty(state,"selectedShot").value("id")),
        Q_ARG(QVariant,"excluded"),Q_ARG(QVariant,true)));
    QVERIFY(QMetaObject::invokeMethod(state,"selectShot",Q_ARG(QVariant,listProperty(state,"shots")[2].toMap().value("id")),Q_ARG(QVariant,false)));
    QCOMPARE(listProperty(state,"shots").size(),4);
    QCOMPARE(state->property("keptFrames").toInt(),84);
    state->setProperty("playhead",60);
    click(window,item(workspace,"videoSplitShot"));
    QCOMPARE(listProperty(state,"shots").size(),5);
    click(window,item(workspace,"videoTimelineUndo"));
    QCOMPARE(listProperty(state,"shots").size(),4);
    const auto keyUrl=QUrl::fromLocalFile(storage.filePath("Files/key.png"));
    QImage reference(64,32,QImage::Format_RGB32); reference.fill(Qt::red); QVERIFY(reference.save(keyUrl.toLocalFile()));
    QVariant keyAccepted;
    QVERIFY(QMetaObject::invokeMethod(parameters,"addImageKey",Q_RETURN_ARG(QVariant,keyAccepted),Q_ARG(QVariant,keyUrl),Q_ARG(QVariant,60)));
    QVERIFY(keyAccepted.toBool());
    QCOMPARE(listProperty(state,"keys").size(),1);
    QVERIFY(QUrl(listProperty(state,"keys").first().toMap().value("source").toString()).toLocalFile().endsWith(".iisc"));
    QVERIFY(QFile::remove(keyUrl.toLocalFile()));
    const auto keyImageName = "videoKeyImage" + listProperty(state,"keys").first().toMap().value("id").toString();
    QQuickItem *keyImage = nullptr;
    QTRY_VERIFY((keyImage = visualItem(workspace,qPrintable(keyImageName))));
    QTRY_COMPARE(keyImage->property("status").toInt(), 1);
    QVERIFY(keyImage->property("source").toUrl().toLocalFile().endsWith(".png"));
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
        QCOMPARE(parameters->width(),300.0); QCOMPARE(timeline->height(),timeline->implicitHeight());
        auto *timelineViewport=item(timeline,"videoTimelineViewport"); QVERIFY(timelineViewport);
        QCOMPARE(timelineViewport->property("contentWidth").toReal(),timelineViewport->width());
        QVERIFY(item(workspace,"videoUpperWorkspace")->height()>=210);
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
    QVERIFY(!QFileInfo::exists(keyUrl.toLocalFile()));
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
    QString storageError;
    const auto drive = iiSocietyContainer::SocietyDrive::createAt(presence.path(), &storageError);
    QVERIFY2(drive, qPrintable(storageError));
    const auto containerPath = drive->rootPath();
    const auto storage = iiSocietyContainer::SharedStorage::open(containerPath, &storageError);
    QVERIFY2(storage, qPrintable(storageError));
    const auto helperDirectory = storage->ensureRuntimeDirectory("helpers/test", &storageError);
    QVERIFY2(!helperDirectory.isEmpty(), qPrintable(storageError));
    const auto endpointsDirectory = storage->ensureRuntimeDirectory("apps/test", &storageError);
    QVERIFY2(!endpointsDirectory.isEmpty(), qPrintable(storageError));
    iiSocietyHelper::Helper observer;
    QVERIFY(observer.start({"com.iisacc.dreamscapes.test", "Dreamscapes test", "1"},
                            {helperDirectory, 100, 5000}));
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("SOCIETY_CONTAINER_PATH", containerPath);
    environment.insert("SOCIETY_HELPER_DIRECTORY", helperDirectory);
    environment.insert("IILOCALLLM_APP_ENDPOINTS", endpointsDirectory);
    for (const auto *key : {"DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"})
        environment.remove(QString::fromLatin1(key));
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    // Verify the installed entry point too, without copying another build-tree app.
    const auto installedExecutable = qEnvironmentVariable("DREAMSCAPES_TEST_APP_PATH");
    process.start(installedExecutable.isEmpty() ? QStringLiteral(DREAMSCAPES_EXECUTABLE_PATH)
                                               : installedExecutable,
                  {"--society-container", containerPath});
    QVERIFY(process.waitForStarted());
    QByteArray output;
    QElapsedTimer timer;
    timer.start();
    while (!output.contains("LVRS bootstrap.entry.root-loaded")
           && !output.contains("LVRS bootstrap.entry.root-load-failed") && timer.elapsed() < 90000
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
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QVERIFY(QDir().mkpath(capture));
        QFile entryLog(capture+"/packaged-entry.log");
        QVERIFY(entryLog.open(QIODevice::WriteOnly));
        QCOMPARE(entryLog.write(output),qint64(output.size()));
    }
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
    // Qt Labs Platform menus require the Widgets fallback on offscreen surfaces.
    QApplication application(argc, argv);
    registerColorManagerTypes();
    QTemporaryDir settings(DREAMSCAPES_TEST_DIRECTORY "/gui-storage-XXXXXX");
    if (!settings.isValid()) return 1;
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settings.filePath("storage.json").toUtf8());
    qunsetenv("SOCIETY_CONTAINER_PATH");
    qputenv("IILD_GENERATOR_EXECUTABLE", DREAMSCAPES_FAKE_GENERATOR);
    qputenv("DREAMSCAPES_TEMP_DIRECTORY", DREAMSCAPES_TEST_DIRECTORY);
    qputenv("SOCIETY_DISABLE_SESSION_RESTORE", "1");
    qmlRegisterUncreatableType<iiSocietyContainer::Container>("Dreamscapes.Storage", 1, 0,
        "SocietyContainer", "Provided by iiSocietyHelper");
    qmlRegisterUncreatableType<iiSocietyHelper::FileDownload>("Dreamscapes.Storage", 1, 0,
        "SocietyFileDownload", "Provided by iiSocietyHelper");
    qmlRegisterType<GuiGenerationController>("Dreamscapes.Storage", 1, 0, "GenerationController");
    qputenv("DREAMSCAPES_TEST_PRESET_FILE", settings.filePath("advanced-presets.json").toUtf8());
    qmlRegisterType<GuiAdvancedImageParameters>("Dreamscapes.Storage", 1, 0, "AdvancedImageParameters");
    qmlRegisterType<iiSocietyContainer::DashboardFiles>("Dreamscapes.Storage", 1, 0, "DashboardFiles");
    qmlRegisterType<iiSocietyContainer::SocietyApplication>("Dreamscapes.Storage", 1, 0, "SocietyApplication");
    qmlRegisterType<HomeCanvas>("Dreamscapes.Storage", 1, 0, "HomeCanvas");
    qmlRegisterType<CanvasPresets>("Dreamscapes.Storage", 1, 0, "CanvasPresets");
    qmlRegisterType<CanvasPreview>("Dreamscapes.Storage", 1, 0, "CanvasPreview");
    qmlRegisterType<EditorCanvas>("Dreamscapes.Storage", 1, 0, "EditorCanvas");
    qmlRegisterType<EditorCanvasFeedback>("Dreamscapes.Storage", 1, 0, "EditorCanvasFeedback");
    qmlRegisterType<EditorTimelineModel>("Dreamscapes.Storage", 1, 0, "EditorTimelineModel");
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

void GuiTests::editorFileThreeViews_data()
{
    QTest::addColumn<bool>("mobile");
    QTest::addColumn<int>("panelWidth");
    QTest::newRow("desktop-432") << false << 432;
    QTest::newRow("desktop-280") << false << 280;
    QTest::newRow("desktop-720") << false << 720;
    QTest::newRow("mobile-402") << true << 402;
}

void GuiTests::editorFileThreeViews()
{
    QFETCH(bool, mobile); QFETCH(int, panelWidth);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme && theme->setProperty("targetOverride", mobile ? "ios" : "macos"));
    QQmlComponent component(&engine, sourceUrl("Views/Editor/CanvasEditor.qml"));
    QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"mobileLayout", mobile}})));
    QVERIFY2(editor, qPrintable(component.errorString()));
    QVERIFY(editor->setProperty("desktopPanelWidth", panelWidth));
    QQuickWindow window;
    window.resize(mobile ? QSize(panelWidth, 874) : QSize(1400, 900));
    editor->setParentItem(window.contentItem()); editor->setSize(window.size());
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *project = editor->findChild<EditorProject *>("editorProject"); QVERIFY(project);
    QVERIFY(project->createCanvas({{"name", "File panel runtime"}, {"width", 32}, {"height", 16}, {"unit", "px"}, {"background", "Transparent"}}));
    auto *canvas = project->currentCanvas();
    auto document = *canvas->document();
    document.stableDiffusionMetadata = iiSharedCanvas::StableDiffusionMetadata{};
    document.stableDiffusionMetadata->positivePrompt = "<b>Literal text</b> " + std::string(500, 'x');
    document.stableDiffusionMetadata->samplingPasses = {{"node", std::numeric_limits<std::uint64_t>::max(), 20, 7.0, "euler", "normal"}};
    QVERIFY(canvas->loadProjectPage(std::move(document), "File panel runtime"));
    const auto before = iiSharedCanvas::encodeIisc(*canvas->document()).bytes;
    const auto revision = canvas->revision();
    const auto authorship = canvas->document()->authorship.dump();
    auto *toolbar = item(editor.data(), "editorToolbar");
    auto *state = editor->findChild<QObject *>("editorToolSheet"); QVERIFY(toolbar && state);
    const auto tools = listProperty(toolbar, "tools");
    int fileIndex = -1;
    for (int i = 0; i < tools.size(); ++i) if (tools[i].toMap()["key"].toString() == "file") fileIndex = i;
    QVERIFY(fileIndex >= 0);
    QVERIFY(QMetaObject::invokeMethod(toolbar, "selectTool", Q_ARG(QVariant, fileIndex)));
    if (mobile) QTRY_VERIFY(state->property("opened").toBool());
    else QVERIFY(editor->setProperty("desktopPanelExpanded", true));
    QObject *presenter = mobile ? state : item(editor.data(), "editorDesktopPanel"); QVERIFY(presenter);
    QQuickItem *panel = nullptr;
    QTRY_VERIFY((panel = item(presenter, "editorFilePanel")) && panel->isVisible());
    QCOMPARE(panel->property("segmentCount").toInt(), 3);
    auto *segments = visualItem(panel, "editorFileViewSegments");
    auto *context = visualItem(panel, "editorFileContext");
    auto *viewport = visualItem(panel, "editorFileViewport"); QVERIFY(segments && context && viewport);
    QCOMPARE(segments->height(), 30.0); QCOMPARE(context->y(), 46.0); QCOMPARE(viewport->y(), 82.0);
    if (!mobile) QCOMPARE(panel->width(), qreal(panelWidth - 34));
    else QTRY_COMPARE(panel->height(), state->property("availableContentHeight").toReal());
    const QStringList ids{"information", "contents", "storage"};
    const QStringList nodes{"1000:72907", "1000:72178", "1000:71361"};
    const QStringList icons{"document-properties", "panel-preserve-layers", "panel-file-location"};
    const QList<int> counts{108, 94, 26};
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    for (int i = 0; i < ids.size(); ++i) {
        auto *button = visualItem(panel, qPrintable("editorFileView-" + ids[i])); QVERIFY(button);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_COMPARE(panel->property("currentViewId").toString(), ids[i]);
        QTRY_COMPARE(panel->property("fieldCount").toInt(), counts[i]);
        QCOMPARE(panel->property("figmaNode").toString(), nodes[i]);
        QCOMPARE(visualItem(panel, "editorFileContextTitle")->property("text").toString(), ids[i].left(1).toUpper() + ids[i].mid(1));
        QVERIFY(visualItem(panel, "editorFileContextIcon")->property("source").toUrl().toString().endsWith(icons[i] + ".svg"));
        QTRY_COMPARE(viewport->property("contentY").toReal(), 0.0);
        QList<QQuickItem *> pending{panel};
        int renderedRows = 0;
        while (!pending.isEmpty()) {
            auto *field = pending.takeFirst();
            pending.append(field->childItems());
            if (!field->objectName().startsWith("editorFileRow-")) continue;
            ++renderedRows;
            const auto key = field->property("bindingKey").toString();
            auto *label = visualItem(field, qPrintable("editorFileLabel-" + key));
            auto *value = visualItem(field, qPrintable("editorFileValue-" + key)); QVERIFY(label && value);
            QVERIFY(label->width() <= 152.0);
            QVERIFY(qAbs(value->x() - label->width() - 12.0) < 0.01);
            QVERIFY(value->x() + value->width() <= panel->width() + 0.01);
            QVERIFY(field->height() >= value->implicitHeight());
            QCOMPARE(value->property("textFormat").toInt(), int(Qt::PlainText));
            QVERIFY(!visualItem(field, "panelRow_input"));
        }
        QCOMPARE(renderedRows, counts[i]);
        if (ids[i] == "information") {
            auto *prompt = visualItem(panel, "editorFileValue-StableDiffusionMetadata.positivePrompt"); QVERIFY(prompt);
            QCOMPARE(prompt->property("text").toString(), QString("<b>Literal text</b> ") + QString(500, 'x'));
            QVERIFY(prompt->implicitHeight() > 22);
            QCOMPARE(visualItem(panel, "editorFileValue-StableDiffusionSamplingPass.seed")->property("text").toString(), "18446744073709551615");
        }
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory));
            QTest::qWait(80); // Let the new view finish its native glyph/layout update.
            auto *captureItem = mobile ? panel : qobject_cast<QQuickItem *>(presenter);
            const auto grab = captureItem->grabToImage(); QVERIFY(grab);
            QTRY_VERIFY(!grab->image().isNull());
            QVERIFY(grab->image().save(captureDirectory + "/file-" + QTest::currentDataTag() + '-' + ids[i] + ".png"));
        }
        const auto end = qMax(0.0, viewport->property("contentHeight").toReal() - viewport->height());
        QVERIFY(end > 0);
        viewport->setProperty("contentY", end);
        QCOMPARE(segments->y(), 0.0); QCOMPARE(context->y(), 46.0);
        QVERIFY(qAbs(viewport->property("contentY").toReal() + viewport->height() - viewport->property("contentHeight").toReal()) < 1);
    }
    QCOMPARE(iiSharedCanvas::encodeIisc(*canvas->document()).bytes, before);
    QCOMPARE(canvas->revision(), revision);
    QCOMPARE(canvas->document()->authorship.dump(), authorship);
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("contents"))));
    QTRY_COMPARE(panel->property("currentViewId").toString(), "contents");
    canvas->setPagePresentation(true);
    QTRY_COMPARE(visualItem(panel, "editorFileValue-EditorCanvas.pagePresentation")->property("text").toString(), "Yes");
    QVERIFY(QMetaObject::invokeMethod(panel, "selectView", Q_ARG(QVariant, QString("information"))));
    QVERIFY(project->createCanvas({{"name", "Replacement"}, {"width", 64}, {"height", 32}, {"unit", "px"}, {"background", "White"}}));
    QTRY_COMPARE(visualItem(panel, "editorFileValue-EditorCanvas.documentName")->property("text").toString(), "Replacement");
}
