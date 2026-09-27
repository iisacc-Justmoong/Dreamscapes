#include <QDir>
#include <QDirIterator>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTimer>
#include <QTemporaryDir>
#include <QWheelEvent>
#include "Generation/GenerationController.h"
#include "../../tests/LocalRuntimeProbeReady.h"
#include "Views/Result/ImageFileExporter.h"
#include "Views/Result/PhotoLibraryExporter.h"
#include <iiSocietyHelper.h>
#include <iiSocietyContainer/DashboardFiles.h>
#include <iiSocietyContainer/SocietyApplication.h>
#include <QtTest>

#include <memory>

namespace {
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

void click(QQuickWindow *window, QQuickItem *control)
{
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
    void canvasRoutesPreserveSelectionAndDraft_data();
    void canvasRoutesPreserveSelectionAndDraft();
    void mobileEditorToolbarSlidesAndSelects_data();
    void mobileEditorToolbarSlidesAndSelects();
    void editorToolSheets_data();
    void editorToolSheets();
    void editorToolNumericContracts();
    void captureSocietyUrl(const QUrl &url) { m_societyUrl = url; }
    void foregroundApplicationPreparesBeforeGenerate();
    void generateOpensResultImmediatelyAndDisplaysEveryPreview();
    void resultCancelsTheActiveGeneration();
    void mainCreatesOneSharedWindow();
    void sharedContentSurvivesLayoutChanges();
    void sharedPanelLayout_data();
    void sharedPanelLayout();
    void sharedControlsSubmitCurrentSelection_data();
    void sharedControlsSubmitCurrentSelection();
    void advancedGenerationDefaultsAndDynamicCollections();
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
    QCOMPARE(view->property("controlNetCount").toInt(), 2);
    QCOMPARE(view->property("nextControlNetNumber").toInt(), 3);
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
    const auto firstCard = [cards]() -> QQuickItem * {
        auto *content = cards->property("contentItem").value<QQuickItem *>();
        if (content) for (auto *child : content->childItems())
            if (child->objectName() == "generationHistoryCard0") return child;
        return nullptr;
    };
    QTRY_VERIFY(firstCard());
    QCOMPARE(firstCard()->width(), 140.0);
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
    QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &wheel);
    QTRY_VERIFY(cards->property("contentX").toReal() > 0);
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
    QVERIFY2(quick->mapToScene(QPointF()).y() >= handleBottom(),
             "Home must leave the native traffic lights and LVRS move handle unobstructed.");
    auto *content = item(window, "appContent");
    QVERIFY(content && content->clip());
    for (const bool showResult : {false, true, false}) {
        QVERIFY(window->setProperty("resultVisible", showResult));
        QTRY_COMPARE(content->mapToScene(QPointF()).y(), handleBottom());
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
    QTRY_COMPARE(content->mapToScene(QPointF()).y(), 48.0);
    QVERIFY(window->setProperty("windowChromeInteractionsEnabled", false));
    QTRY_COMPARE(content->mapToScene(QPointF()).y(), window->property("mobileSystemSafeTopInset").toReal());
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
    QVERIFY(window->property("generationRequestError").toString().contains("Video generation is not available"));
    QVERIFY(!window->property("resultVisible").toBool());
    QVERIFY(quick->setProperty("mediaType", "Image"));
    // Restored draft/caller counts still use the existing queue contract.
    QVERIFY(quick->setProperty("generationCount", 3));
    QCOMPARE(quick->property("generationCount").toInt(), 3);
    quick->setProperty("prompt", "Three images from one submission");
    click(window, item(quick, "generateButton"));
    QTRY_COMPARE(controller->jobs().size(), 3);
    const auto allCompleted = [&] {
        for (const auto &entry : controller->jobs())
            if (entry.toMap().value("state") != "completed") return false;
        return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(allCompleted(), 10000);
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
    click(window, firstTile);
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
    click(window, item(window, "editorBackButton"));
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
    click(window, item(window, "editorBackButton"));
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
    if (size == QSize(402, 575)) QCOMPARE(upperGap, 69.0);
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
        QTRY_VERIFY(editor->isVisible());
        QVERIFY(item(window, "editorBlankCanvas")->isVisible());
        QVERIFY(editor->property("imageSource").toUrl().isEmpty());
        QVERIFY(!item(window, "mobileHome")->isVisible());
        QVERIFY(!quick->isVisible());
        click(window, item(window, "editorBackButton"));
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
    auto *canvasImage = item(window, "editorCanvasImage");
    QTRY_COMPARE(canvasImage->property("status").toInt(), 1);
    QCOMPARE(canvasImage->property("fillMode").toInt(), 1);
    const auto captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        QVERIFY(window->grabWindow().save(captureDirectory + '/' + QTest::currentDataTag() + ".png"));
    }
    click(window, item(window, "editorBackButton"));
    QTRY_VERIFY(result->isVisible());
    QVERIFY(quick->isVisible());
    QCOMPARE(quick->property("prompt").toString(), "unfinished draft");
    QCOMPARE(result->property("imageSource").toUrl(), QUrl::fromLocalFile(path));
    if (target == "ios") {
        click(window, item(window, "resultBackButton"));
        click(window, item(window, "newCanvasAction"));
        QTRY_VERIFY(editor->isVisible());
        QVERIFY(editor->property("imageSource").toUrl().isEmpty());
        QVERIFY(editor->property("generationResult").value<QJSValue>().toVariant().toMap().isEmpty());
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(item(window, "mobileHome")->isVisible());
    }
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
    QTRY_VERIFY(!toolbar->isVisible());
    QTRY_VERIFY(!toolSheet->property("visible").toBool());
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
        QCOMPARE(panel->property("fieldCount").toInt(), counts[index]);
        total += counts[index];
        const auto fields = sheet->property("definition").value<QJSValue>().toVariant().toMap()["fields"].toList();
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
    auto *actionSheet = sheet->findChild<QObject *>("editorActionSheet");
    QTRY_VERIFY(actionSheet && actionSheet->property("opened").toBool());
    QVERIFY(actionSheet->property("description").toString().contains("not connected"));
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY(!actionSheet->property("visible").toBool());
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
        QVERIFY(QMetaObject::invokeMethod(preferencesAction, "triggered"));
        auto *preferences = window->findChild<QQuickWindow *>("preferencesWindow");
        QVERIFY(preferences);
        QTRY_VERIFY(preferences->isVisible());
        QVERIFY(preferences->findChild<QQuickItem *>("preferencesDriveCategory"));
        QVERIFY(preferences->findChild<QQuickItem *>("preferencesDriveDetails"));
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/preferences-drive-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        auto *location = preferences->findChild<QQuickItem *>("preferencesDriveLocation");
        auto *apply = preferences->findChild<QQuickItem *>("applySocietyDrive");
        auto *current = preferences->findChild<QQuickItem *>("preferencesCurrentDrive");
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
        QTRY_COMPARE(panel->width(), size.width() - window->property("mobileSystemSafeLeftInset").toReal()
                                                  - window->property("mobileSystemSafeRightInset").toReal());
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

void GuiTests::sharedPanelLayout_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::newRow("desktop-960") << QString("macos") << 960 << 640;
    QTest::newRow("desktop-1440") << QString("macos") << 1440 << 900;
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
    auto *generate = item(panel, "generateButton");
    auto *composer = item(panel, "quickGenerateComposer");
    auto *chevron = item(panel, "mediaTypeChevron");
    QVERIFY(prompt && media && generate && composer && chevron);
    QVERIFY(!item(panel, "aspectRatioButton"));
    QVERIFY(!item(panel, "generationCountButton"));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    const bool mobileHomeLayout = target == "ios" || target == "android";
    const auto availableWidth = width - root->property("mobileSystemSafeLeftInset").toReal()
                                      - root->property("mobileSystemSafeRightInset").toReal();
    const auto expectedPanelWidth = mobileHomeLayout ? qMin(370.0, availableWidth - 32.0) : availableWidth;
    QTRY_COMPARE(panel->width(), expectedPanelWidth);
    const auto padding = mobileHomeLayout ? 0.0 : theme->property("gap10").toReal();
    QTRY_COMPARE(composer->height(), 126.0);
    QCOMPARE(prompt->height(), 44.0);
    QCOMPARE(media->height(), 44.0);
    QCOMPARE(generate->height(), 44.0);
    QCOMPARE(panel->height(), padding * 2 + composer->height());
    QCOMPARE(bounds(composer, panel).left(), padding);
    QCOMPARE(bounds(composer, panel).right(), panel->width() - padding);
    QCOMPARE(bounds(generate, composer).right(), composer->width() - 13.0);
    QCOMPARE(bounds(prompt, composer).top(), 13.0);
    QCOMPARE(bounds(media, composer).top(), bounds(prompt, composer).top());
    QVERIFY(bounds(media, composer).right() < bounds(prompt, composer).left());
    QVERIFY(bounds(prompt, composer).bottom() < bounds(generate, composer).top());
    QCOMPARE(media->property("text").toString(), "Image");
    QCOMPARE(generate->property("text").toString(), "Generate");
    QCOMPARE(prompt->property("placeholderText").toString(), composer->width() < 480
        ? "Describe your idea" : "Describe what you want to generate");
    QCOMPARE(chevron->size(), QSizeF(18, 18));
    QTRY_COMPARE(chevron->property("status").toInt(), 1);
    const QString captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(captureDirectory + '/' + QTest::currentDataTag() + ".png"));
        auto grab = composer->grabToImage();
        QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
        QVERIFY(ready.wait(3000));
        QVERIFY(grab->saveToFile(captureDirectory + "/composer-" + QTest::currentDataTag() + ".png"));
    }
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
    QVERIFY(prompt && media && generate);
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
    QVERIFY(notice->property("text").toString().contains("Video generation is not available"));
    QCOMPARE(prompt->property("text").toString(), "   a quiet forest   ");
    click(window, media);
    QTRY_VERIFY(mediaMenu->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(mediaMenu, "triggerEntry", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(!mediaMenu->property("visible").toBool());
    click(window, prompt);
    QTest::keyClick(window, Qt::Key_Return);
    QCOMPARE(requests.size(), 2);
    QCOMPARE(requests.at(1), QVariantList({"a quiet forest", "Image", "1:1", 1}));
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
    for (const auto *key : {"DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"})
        environment.remove(QString::fromLatin1(key));
    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QStringLiteral(DREAMSCAPES_EXECUTABLE_PATH), {});
    QVERIFY(process.waitForStarted());
    QByteArray output;
    QElapsedTimer timer;
    timer.start();
    while (!output.contains("LVRS bootstrap.entry.root-loaded") && timer.elapsed() < 30000
           && process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        output += process.readAll();
    }
    const bool running = process.state() == QProcess::Running;
    // Both participants must actually observe each other through the installed SDK.
    QElapsedTimer discovery;
    discovery.start();
    while (discovery.elapsed() < 15000 && (observer.peers().isEmpty()
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
    QVERIFY2(running, output.constData());
    QVERIFY2(output.contains("LVRS bootstrap.entry.root-loaded"), output.constData());
    QVERIFY2(peers.size() == 1, output.constData());
    QCOMPARE(peers.first().application.id, "com.iisacc.dreamscapes");
    QVERIFY2(output.contains("com.iisacc.dreamscapes observed com.iisacc.dreamscapes.test"), output.constData());
    QVERIFY2(output.contains("\"windowCount\":1"), output.constData());
    QVERIFY2(!output.contains("failed to load") && !output.contains("is not installed"), output.constData());
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
    qmlRegisterType<GenerationController>("Dreamscapes.Storage", 1, 0, "GenerationController");
    qmlRegisterType<iiSocietyContainer::DashboardFiles>("Dreamscapes.Storage", 1, 0, "DashboardFiles");
    qmlRegisterType<iiSocietyContainer::SocietyApplication>("Dreamscapes.Storage", 1, 0, "SocietyApplication");
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
