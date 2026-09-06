#include <QDir>
#include <QGuiApplication>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
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

QRectF bounds(QQuickItem *control, QQuickItem *parent)
{
    return control->mapRectToItem(parent, control->boundingRect());
}

void click(QQuickWindow *window, QQuickItem *control)
{
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
    void mainSelectsOneWindow();
    void desktopKeepsItsContent();
    void mobilePanelLayout_data();
    void mobilePanelLayout();
    void mobileControlsSubmitCurrentSelection_data();
    void mobileControlsSubmitCurrentSelection();
    void loaderFailureRequestsExit();
    void packagedApplicationStarts();
};

void GuiTests::mainSelectsOneWindow()
{
    QPointer<QQuickWindow> selectedWindow;
    {
        QQmlApplicationEngine engine;
        engine.load(sourceUrl("Main.qml"));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *loader = engine.rootObjects().constFirst();
        const auto windows = loader->findChildren<QQuickWindow *>();
        QCOMPARE(windows.size(), 1);
        selectedWindow = windows.constFirst();
        auto *platform = engine.singletonInstance<QObject *>("LVRS", "Platform");
        QVERIFY(platform);
        const bool mobile = platform->property("mobile").toBool();
        QCOMPARE(selectedWindow->objectName(), mobile ? "mobileWindow" : "desktopWindow");
        QCOMPARE(sourceUrl("Main.qml").resolved(loader->property("source").toUrl()),
                 sourceUrl(mobile ? "Views/Mobile.qml" : "Views/Desktop.qml"));
        QVERIFY(selectedWindow->isVisible());
        QVERIFY(!selectedWindow->transientParent());
        QCOMPARE(selectedWindow->title(), "Dreamscapes");
        selectedWindow->resize(844, 540);
        QCoreApplication::processEvents();
        QCOMPARE(loader->findChildren<QQuickWindow *>().constFirst(), selectedWindow.data());
    }
    QVERIFY(selectedWindow.isNull());
}

void GuiTests::desktopKeepsItsContent()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, sourceUrl("Views/Desktop.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window);
    auto *label = item(root.get(), "helloLabel");
    QVERIFY(label);
    QCOMPARE(label->property("text").toString(), "Hello world!");
    QVERIFY(!item(root.get(), "quickGenerate"));
    for (const auto size : {QSize(960, 640), QSize(1200, 800)}) {
        window->resize(size);
        QCoreApplication::processEvents();
        const auto center = label->mapToScene(label->boundingRect().center());
        QVERIFY(qAbs(center.x() - size.width() / 2.0) <= 1);
        QVERIFY(qAbs(center.y() - size.height() / 2.0) <= 1);
    }
}

void GuiTests::mobilePanelLayout_data()
{
    QTest::addColumn<QString>("target");
    QTest::addColumn<int>("width");
    QTest::newRow("figma-402") << QString("macos") << 402;
    QTest::newRow("ios-320") << QString("ios") << 320;
    QTest::newRow("ios-390") << QString("ios") << 390;
    QTest::newRow("android-360") << QString("android") << 360;
    QTest::newRow("mobile-landscape") << QString("ios") << 844;
}

void GuiTests::mobilePanelLayout()
{
    QFETCH(QString, target);
    QFETCH(int, width);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme);
    QVERIFY(theme->setProperty("targetOverride", target));
    QQmlComponent component(&engine, sourceUrl("Views/Mobile.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window);
    window->resize(width, width > 500 ? 480 : 844);
    auto *panel = item(root.get(), "quickGenerate");
    QVERIFY2(panel, "The mobile window must contain the Figma QuickGenerate panel.");
    auto *prompt = item(panel, "promptField");
    auto *media = item(panel, "mediaTypeButton");
    auto *ratio = item(panel, "aspectRatioButton");
    auto *generate = item(panel, "generateButton");
    QVERIFY(prompt && media && ratio && generate);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE(panel->width(), width - root->property("mobileSystemSafeLeftInset").toReal()
                                      - root->property("mobileSystemSafeRightInset").toReal());

    const auto padding = theme->property("gap10").toReal();
    const auto gap = theme->property("gap8").toReal();
    QCOMPARE(prompt->height(), 19.0);
    QCOMPARE(generate->height(), 22.0);
    QCOMPARE(panel->height(), padding * 2 + prompt->height() + gap + generate->height());
    QCOMPARE(panel->mapToScene(QPointF()).y(), root->property("mobileSystemSafeTopInset").toReal());
    QCOMPARE(panel->width(), width - root->property("mobileSystemSafeLeftInset").toReal()
                                  - root->property("mobileSystemSafeRightInset").toReal());
    QCOMPARE(bounds(prompt, panel).left(), padding);
    QCOMPARE(bounds(prompt, panel).top(), padding);
    QCOMPARE(bounds(prompt, panel).right(), panel->width() - padding);
    QTRY_COMPARE(bounds(generate, panel).right(), panel->width() - padding);
    QCOMPARE(bounds(media, panel).top(), bounds(prompt, panel).bottom() + gap);
    QCOMPARE(prompt->property("placeholderText").toString(), "Prompt");
    QCOMPARE(media->property("text").toString(), "Image");
    QCOMPARE(ratio->property("text").toString(), "1:1");
    QCOMPARE(generate->property("text").toString(), "Generate");
    QVERIFY(!item(root.get(), "helloLabel"));

    const QString captureDirectory = qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if (!captureDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(captureDirectory));
        const auto image = window->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(captureDirectory + '/' + QTest::currentDataTag() + ".png"));
    }

    for (const auto &aspect : {QString("1:1"), QString("16:9"), QString("9:16")}) {
        QVERIFY(panel->setProperty("aspectRatio", aspect));
        QCoreApplication::processEvents();
        QVERIFY(bounds(media, panel).right() <= bounds(ratio, panel).left());
        QVERIFY(bounds(ratio, panel).right() <= bounds(generate, panel).left());
        QVERIFY(ratio->width() >= ratio->implicitWidth());
        QVERIFY(generate->width() >= generate->implicitWidth());
    }
}

void GuiTests::mobileControlsSubmitCurrentSelection_data()
{
    QTest::addColumn<QString>("target");
    QTest::newRow("reference") << QString("macos");
    QTest::newRow("ios") << QString("ios");
    QTest::newRow("android") << QString("android");
}

void GuiTests::mobileControlsSubmitCurrentSelection()
{
    QFETCH(QString, target);
    QQmlEngine engine;
    auto *theme = engine.singletonInstance<QObject *>("LVRS", "Theme");
    QVERIFY(theme);
    QVERIFY(theme->setProperty("targetOverride", target));
    QQmlComponent component(&engine, sourceUrl("Views/Mobile.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QVERIFY(window);
    window->resize(320, 844);
    auto *panel = item(root.get(), "quickGenerate");
    QVERIFY(panel);
    auto *prompt = item(panel, "promptField");
    auto *media = item(panel, "mediaTypeButton");
    auto *ratio = item(panel, "aspectRatioButton");
    auto *generate = item(panel, "generateButton");
    QVERIFY(prompt && media && ratio && generate);
    auto *mediaMenu = panel->findChild<QObject *>("mediaTypeMenu");
    auto *ratioMenu = panel->findChild<QObject *>("aspectRatioMenu");
    QVERIFY(mediaMenu && ratioMenu);
    QSignalSpy requests(root.get(), SIGNAL(generateRequested(QString,QString,QString)));
    QVERIFY(requests.isValid());
    QVERIFY(QTest::qWaitForWindowExposed(window));

    click(window, generate);
    QCOMPARE(requests.size(), 0);
    auto *input = prompt->property("inputItem").value<QQuickItem *>();
    QVERIFY(input);
    QTRY_VERIFY(input->hasActiveFocus());
    for (const char key : QByteArray("   a quiet forest   "))
        QTest::keyClick(window, key);
    QCOMPARE(prompt->property("text").toString(), "   a quiet forest   ");

    click(window, media);
    QTRY_VERIFY(mediaMenu->property("opened").toBool());
    QVERIFY(mediaMenu->property("x").toReal() >= 0);
    QVERIFY(mediaMenu->property("x").toReal() + mediaMenu->property("width").toReal() <= window->width());
    QVERIFY(QMetaObject::invokeMethod(mediaMenu, "triggerEntry", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(!mediaMenu->property("visible").toBool());
    click(window, ratio);
    QTRY_VERIFY(ratioMenu->property("opened").toBool());
    QVERIFY(ratioMenu->property("x").toReal() >= 0);
    QVERIFY(ratioMenu->property("x").toReal() + ratioMenu->property("width").toReal() <= window->width());
    auto *menuContent = ratioMenu->property("contentItem").value<QQuickItem *>();
    QVERIFY(menuContent);
    auto *wideRatio = menuEntry(menuContent, "16:9");
    QVERIFY(wideRatio);
    click(window, wideRatio);
    QTRY_VERIFY(!ratioMenu->property("visible").toBool());
    QCOMPARE(ratio->property("text").toString(), "16:9");

    click(window, generate);
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests.at(0), QVariantList({"a quiet forest", "Image", "16:9"}));
    click(window, prompt);
    QTest::keyClick(window, Qt::Key_Return);
    QCOMPARE(requests.size(), 2);
    QCOMPARE(requests.at(1), requests.at(0));
    prompt->setProperty("text", "   ");
    click(window, generate);
    QCOMPARE(requests.size(), 2);
}

void GuiTests::loaderFailureRequestsExit()
{
    QQmlEngine engine;
    QSignalSpy exit(&engine, &QQmlEngine::exit);
    QQmlComponent component(&engine, sourceUrl("Main.qml"));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    QVERIFY(root->setProperty("source", sourceUrl("Views/Missing.qml")));
    QTRY_COMPARE(exit.size(), 1);
    QCOMPARE(exit.at(0).at(0).toInt(), 1);
}

void GuiTests::packagedApplicationStarts()
{
    auto environment = QProcessEnvironment::systemEnvironment();
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
    while (!output.contains("LVRS bootstrap.entry.root-loaded") && timer.elapsed() < 5000
           && process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        output += process.readAll();
    }
    const bool running = process.state() == QProcess::Running;
    process.terminate();
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished();
    }
    output += process.readAll();
    QVERIFY2(running, output.constData());
    QVERIFY2(output.contains("LVRS bootstrap.entry.root-loaded"), output.constData());
    QVERIFY2(!output.contains("failed to load") && !output.contains("is not installed"), output.constData());
}

int main(int argc, char **argv)
{
    QQuickStyle::setStyle("Basic");
    QGuiApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    GuiTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_Gui.moc"
