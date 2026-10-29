#include "App/StorageViewTypes.h"
#include "App/Generation/GenerationController.h"
#include "App/Generation/AdvancedImageParameters.h"
#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorProject.h"
#include "App/Views/Editor/EditorTimelineModel.h"
#include <SocietyDrive.h>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QString settingsPath;
class PanelParameters : public AdvancedImageParameters {
public:
    explicit PanelParameters(QObject* parent = nullptr)
        : AdvancedImageParameters(settingsPath + "/presets.json", parent) {}
};
class PanelGeneration : public GenerationController {
public:
    explicit PanelGeneration(QObject* parent = nullptr)
        : GenerationController(makeRuntime(), parent) {}
private:
    static GenerationRuntime makeRuntime() {
        GenerationRuntime runtime;
        runtime.modelPreferencesFile = settingsPath + "/models.conf";
        return runtime;
    }
};
QUrl source(const QString& name) {
    return QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR) + '/' + name);
}
QQuickItem* visual(QQuickItem* root, const QString& name) {
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems())
        if (auto* found = visual(child, name)) return found;
    return nullptr;
}
QList<QQuickItem*> frames(QQuickItem* root) {
    QList<QQuickItem*> found;
    if (root->objectName().startsWith("panelFrame_")) found.append(root);
    for (auto* child : root->childItems()) found.append(frames(child));
    return found;
}
QObject* border(QQuickItem* item) { return item->property("border").value<QObject*>(); }
QString evidencePath() { return QString(DREAMSCAPES_TEST_DIRECTORY) + "/panel-frames"; }
}

class PanelFrameTests : public QObject {
    Q_OBJECT
private:
    void verifyFrame(QQuickItem* frame) {
        QVERIFY(frame);
        QCOMPARE(frame->property("radius").toReal(), 0.);
        QVERIFY(border(frame));
        QCOMPARE(frame->property("strokeWidth").toReal(), 1.);
        QCOMPARE(border(frame)->property("width").toReal(), frame->property("completeBorder").toBool() ? 1. : 0.);
        QCOMPARE(frame->property("panelBorderColor").value<QColor>(), QColor("#0c0c0c"));
        QCOMPARE(border(frame)->property("color").value<QColor>(),
                 frame->property("panelBorderColor").value<QColor>());
        QVERIFY(!frame->isEnabled());
        auto* target = frame->property("target").value<QQuickItem*>();
        QVERIFY(target);
        QCOMPARE(frame->size(), target->size());
        QCOMPARE(frame->position(), target->mapToItem(frame->parentItem(), QPointF()));
    }
private slots:
    void initTestCase() {
        QVERIFY(QDir().mkpath(evidencePath()));
        QTest::failOnWarning(QRegularExpression(".*(Binding loop detected|ReferenceError|TypeError|Cannot assign|Cannot anchor).*", QRegularExpression::DotMatchesEverythingOption));
    }
    void decorationPreservesLayoutAndInput() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        const auto imports = QString("import QtQuick\nimport QtQuick.Layouts\nimport 'Views' as Frames\n");
        component.setData((imports +
            "Item { id: root; width: 320; height: 240; property int clicks: 0\n"
            " ColumnLayout { x: 20; y: 20; width: 180; spacing: 12\n"
            "  Rectangle { id: panel; objectName: 'testPanel'; Layout.fillWidth: true; implicitHeight: 100; color: '#222222'\n"
            "   MouseArea { anchors.fill: parent; onClicked: root.clicks++ } }\n"
            "  Rectangle { objectName: 'followingPanel'; Layout.fillWidth: true; implicitHeight: 40 } }\n"
            " readonly property Item outline: Frames.PanelFrame { parent: root; target: panel; panelId: 'test' }\n"
            "}").toUtf8(), source("PanelFrameFixture.qml"));
        QScopedPointer<QQuickItem> view(qobject_cast<QQuickItem*>(component.create()));
        QVERIFY2(view, qPrintable(component.errorString()));
        QQuickWindow window; window.resize(320,240); window.setColor(Qt::black);
        view->setParentItem(window.contentItem()); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* panel = visual(view.data(), "testPanel");
        auto* frame = visual(view.data(), "panelFrame_test");
        QTRY_COMPARE(panel->height(), 100.);
        QTRY_COMPARE(frame->width(), 180.);
        verifyFrame(frame);
        QCOMPARE(visual(view.data(), "followingPanel")->y(), 112.);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(20,20));
        QCOMPARE(view->property("clicks").toInt(), 1);
        const auto image = window.grabWindow();
        QVERIFY(!image.isNull());
        const qreal dpr = image.devicePixelRatio();
        const auto pixel = [&](int x, int y) { return image.pixelColor(qRound(x*dpr),qRound(y*dpr)); };
        QCOMPARE(pixel(20,20), QColor("#0c0c0c")); // Square corner is actually painted.
        QCOMPARE(pixel(60,20), QColor("#0c0c0c"));
        QCOMPARE(pixel(60,21), QColor("#222222")); // One logical pixel, transparent interior.
        QCOMPARE(pixel(19,20), QColor(Qt::black));
        QVERIFY(panel->parentItem()->setProperty("width",220));
        QTRY_COMPARE(frame->width(),220.);
        verifyFrame(frame);
        panel->setVisible(false);
        QTRY_VERIFY(!frame->isVisible());
    }
    void sharedEdgesPaintOnce() {
        QQmlEngine engine; QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport 'Views' as Frames\n"
            "Item { width: 300; height: 160; Rectangle { id: left; x: 20; y: 20; width: 120; height: 100; color: '#222222' } "
            "Rectangle { id: right; x: 140; y: 20; width: 120; height: 100; color: '#333333' } "
            "Frames.PanelFrame { target: left; panelId: 'left' } "
            "Frames.PanelFrame { target: right; panelId: 'right'; leftEdge: false } }",source("SharedEdges.qml"));
        QScopedPointer<QQuickItem> view(qobject_cast<QQuickItem*>(component.create()));
        QVERIFY2(view,qPrintable(component.errorString()));
        QQuickWindow window;window.resize(300,160);view->setParentItem(window.contentItem());window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));QTest::qWait(40);
        const auto image=window.grabWindow();QVERIFY(!image.isNull());
        const auto sample=[&](int x){return image.pixelColor(qRound(x*image.devicePixelRatio()),qRound(70*image.devicePixelRatio()));};
        QCOMPARE(sample(138),QColor("#222222"));QCOMPARE(sample(139),QColor("#0c0c0c"));
        QCOMPARE(sample(140),QColor("#333333"));QCOMPARE(sample(141),QColor("#333333"));
    }
    void compactEditorKeepsCommandsAndDocumentState() {
        QQmlEngine engine;QQmlComponent component(&engine,source("Views/Editor/CanvasEditor.qml"));
        QScopedPointer<QQuickItem> editor(qobject_cast<QQuickItem*>(component.createWithInitialProperties({{"initializeBlankCanvas",false}})));
        QVERIFY2(editor,qPrintable(component.errorString()));
        QQuickWindow window;window.resize(1920,1200);window.setColor(QColor("#232424"));editor->setParentItem(window.contentItem());editor->setSize(window.size());
        const auto detach=qScopeGuard([&]{editor->setParentItem(nullptr);});
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* toolbar=visual(editor.data(),"editorToolbar");QVERIFY(toolbar);
        auto* timeline=visual(editor.data(),"editorTimeline");QVERIFY(timeline);
        QTRY_COMPARE(toolbar->width(),30.);QCOMPARE(timeline->height(),180.);
        QCOMPARE(timeline->property("gutter").toInt(),192);QCOMPARE(timeline->property("rulerHeight").toInt(),32);
        auto* project=editor->findChild<EditorProject*>("editorProject");QVERIFY(project);
        auto* canvas=project->currentCanvas();QVERIFY(canvas);
        iiSharedCanvas::Document document;document.extent={8,8};document.timeline={{24,1},48};
        QVERIFY(canvas->loadProjectPage(document,"Compact editor"));
        auto* model=qvariant_cast<EditorTimelineModel*>(timeline->property("stateModel"));QVERIFY(model);
        QCOMPARE(model->tracks().size(),0);
        QTest::qWait(40);
        const auto rendered=window.grabWindow();QVERIFY(!rendered.isNull());
        const auto colorAt=[&](QPointF point){return rendered.pixelColor(qRound(point.x()*rendered.devicePixelRatio()),qRound(point.y()*rendered.devicePixelRatio()));};
        const auto seam=[&](QPointF center,bool vertical){
            int gray=0;for(int offset=-2;offset<=2;++offset)
                if(colorAt(center+(vertical?QPointF(offset,0):QPointF(0,offset)))==QColor("#0c0c0c"))++gray;
            return gray;
        };
        const auto toolbarOrigin=toolbar->mapToScene(QPointF());
        QCOMPARE(seam({toolbarOrigin.x()-1,toolbarOrigin.y()+100},true),1);
        QCOMPARE(seam({toolbarOrigin.x()+toolbar->width()-1,toolbarOrigin.y()+100},true),1);
        const auto timelineOrigin=timeline->mapToScene(QPointF());
        QCOMPARE(seam({timelineOrigin.x()+500,timelineOrigin.y()},false),1);
        const auto revision=canvas->revision();const auto path=canvas->filePath();
        int toolIndex=0;
        for(const auto& tool:QStringList{"elements","text","camera-photo","file","background","audio-track","canvas","generative","layers","select","color","effects","retouch","fill","brush","auto-enhance","masking","eraser"}) {
            auto* button=visual(toolbar,"editorTool-"+tool);QVERIFY2(button,qPrintable(tool));
            QCOMPARE(button->size(),QSizeF(22,22));
            QCOMPARE(button->property("tone").toInt(),1); // Native Default, including selection.
            auto* icon=visual(button,"iconButton_icon");QVERIFY(icon);
            QTRY_COMPARE(icon->property("status").toInt(),1);
            QCOMPARE(icon->size(),QSizeF(18,18));
            QVERIFY(QMetaObject::invokeMethod(toolbar,"syncTool",Q_ARG(QVariant,toolIndex++)));
            auto* background=button->property("background").value<QObject*>();QVERIFY(background);
            QTRY_COMPARE(background->property("color").value<QColor>(),button->property("backgroundColorPressed").value<QColor>());
            QCOMPARE(button->property("tone").toInt(),1);
        }
        QVERIFY(QMetaObject::invokeMethod(toolbar,"syncTool",Q_ARG(QVariant,0)));
        QVERIFY(QMetaObject::invokeMethod(timeline,"openOptions"));
        auto* options=timeline->findChild<QObject*>("editorTimelineOptions");QVERIFY(options);
        QTRY_VERIFY(options->property("visible").toBool());
        for(const auto* name:{"editorTimelineHeader","editorTimelineTools","editorTimelinePrecision","editorTimelineFooter"}) {
            auto* row=visual(window.contentItem(),name);QVERIFY2(row,qPrintable(name));QVERIFY(row->isVisible());
        }
        auto* optionViewport=visual(window.contentItem(),"editorTimelineOptionsViewport");QVERIFY(optionViewport);
        const auto scrollLimit=qMax(0.,optionViewport->property("contentWidth").toReal()-optionViewport->width());
        QVERIFY(optionViewport->setProperty("contentX",scrollLimit));
        QTRY_COMPARE(optionViewport->property("contentX").toReal(),scrollLimit);
        QVERIFY(optionViewport->setProperty("contentX",0.));
        auto* keyMode=visual(window.contentItem(),"editorTimelineMode-Keyframes");QVERIFY(keyMode);
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,keyMode->mapToScene(keyMode->boundingRect().center()).toPoint());
        QTRY_COMPARE(model->mode(),QString("Keyframes"));
        QVERIFY(QMetaObject::invokeMethod(options,"close"));QTRY_VERIFY(!options->property("visible").toBool());
        QCOMPARE(canvas->revision(),revision);QCOMPARE(canvas->filePath(),path);
        QCOMPARE(model->tracks().size(),0);
        QVERIFY(model->addLayer("Static bitmap","User layer"));QCOMPARE(model->tracks().size(),1);
        QVERIFY(timeline->setProperty("playing",true));QTRY_VERIFY(canvas->frame()>0);
        QVERIFY(timeline->setProperty("playing",false));
        QVERIFY(window.grabWindow().save(evidencePath()+"/compact-editor.png"));
    }
    void workspaces_data() {
        QTest::addColumn<QString>("route"); QTest::addColumn<QStringList>("expected");
        QTest::newRow("home") << QString("home") << QStringList{};
        QTest::newRow("image") << QString("image") << QStringList{"image-parameters","image-preview"};
        QTest::newRow("video") << QString("video") << QStringList{"video-parameters","video-preview","video-timeline"};
        QTest::newRow("editor") << QString("editor") << QStringList{"editor-body","editor-canvas","tools","editor-inspector","editor-timeline"};
        QTest::newRow("files") << QString("files") << QStringList{"files"};
        QTest::newRow("review") << QString("review") << QStringList{"generation-queue","result-inspector","review-results","review-actions","review-header"};
        QTest::newRow("publish") << QString("publish") << QStringList{"publish-header","publish-document","publish-inspector"};
        QTest::newRow("preferences") << QString("preferences") << QStringList{"preferences","preferences-category"};
        QTest::newRow("mobile-home") << QString("mobile") << QStringList{"quick-generation","mobile-actions","mobile-recent-files","mobile-recent-generations","mobile-published"};
    }
    void workspaces() {
        QFETCH(QString,route); QFETCH(QStringList,expected);
        QTemporaryDir drive(QString(DREAMSCAPES_TEST_DIRECTORY) + "/panel-drive-XXXXXX.societycontainer");
        QVERIFY(drive.isValid()); QVERIFY(iiSocietyContainer::SocietyDrive::create(drive.path()));
        QQmlApplicationEngine engine;
        if (route == "mobile") {
            auto* theme=engine.singletonInstance<QObject*>("LVRS","Theme"); QVERIFY(theme);
            QVERIFY(theme->setProperty("targetOverride","ios"));
        }
        engine.setInitialProperties({{"initialContainerPath",drive.path()}});
        engine.load(source("Main.qml"));
        QCOMPARE(engine.rootObjects().size(),1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
        window->resize(route == "mobile" ? QSize(390,844) : QSize(1440,900));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto* home = window->findChild<QObject*>("desktopHome"); QVERIFY(home);
        if (route == "image" || route == "video")
            QVERIFY(QMetaObject::invokeMethod(home,"activate",Q_ARG(QVariant,route)));
        else if (route == "files")
            QVERIFY(QMetaObject::invokeMethod(window,"showStorage",Q_ARG(QVariant,"files")));
        else if (route == "editor" || route == "publish") {
            QVariant accepted;
            QVERIFY(QMetaObject::invokeMethod(window,"openCanvas",Q_RETURN_ARG(QVariant,accepted),Q_ARG(QVariant,""),Q_ARG(QVariant,QVariantMap{})));
            QVERIFY(accepted.toBool());
            if (route == "publish") { QVERIFY(QMetaObject::invokeMethod(window,"openPublish",Q_RETURN_ARG(QVariant,accepted))); QVERIFY(accepted.toBool()); }
        } else if (route == "review") {
            QVERIFY(QMetaObject::invokeMethod(window,"openGenerationReview"));
        } else if (route == "preferences") {
            QVERIFY(QMetaObject::invokeMethod(window,"openPreferences"));
            window=window->property("preferencesWindow").value<QQuickWindow*>(); QVERIFY(window);
            QVERIFY(QTest::qWaitForWindowExposed(window));
        }
        for (const auto& id : expected) {
            auto* frame = visual(window->contentItem(),"panelFrame_"+id);
            QVERIFY2(frame,qPrintable("Missing panel: "+id));
            QTRY_VERIFY2(frame->isVisible() && frame->width()>0 && frame->height()>0,qPrintable(id));
            verifyFrame(frame);
        }
        // Let the actual scene render; retain screenshots from the private empty drive.
        QTest::qWait(100);
        const auto image = window->grabWindow(); QVERIFY(!image.isNull());
        QVERIFY(image.save(evidencePath() + '/' + route + ".png"));
        if (route == "editor") {
            auto* toolbar = visual(window->contentItem(),"editorToolbar"); QVERIFY(toolbar);
            auto* inspector = visual(window->contentItem(),"panelFrame_editor-inspector"); QVERIFY(inspector);
            for (int index=0; index<18; ++index) {
                QVERIFY(QMetaObject::invokeMethod(toolbar,"selectTool",Q_ARG(QVariant,index)));
                QCoreApplication::processEvents(); verifyFrame(inspector);
            }
        } else if (route == "publish") {
            auto* publish=visual(window->contentItem(),"publishWorkspace"); QVERIFY(publish);
            auto* document=visual(publish,"publishDocument"); QVERIFY(document);
            QVERIFY(QMetaObject::invokeMethod(document,"positionViewAtEnd"));
            QTRY_VERIFY(visual(publish,"publishCreditsPanel"));
            auto* credits=visual(publish,"publishCreditsPanel"); QVERIFY(border(credits));
            auto* documentFrame=visual(publish,"panelFrame_publish-document"); QVERIFY(documentFrame);
            QCOMPARE(border(credits)->property("color").value<QColor>(),
                     documentFrame->property("panelBorderColor").value<QColor>());
            QVERIFY(QMetaObject::invokeMethod(publish,"reviewDestination",Q_ARG(QVariant,"x")));
            for (const auto* id : {"destination-review","destination-settings","destination-preview"}) {
                auto* frame=visual(window->contentItem(),QString("panelFrame_")+id);
                QTRY_VERIFY(frame && frame->isVisible() && frame->height()>0);
                verifyFrame(frame);
            }
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(evidencePath()+"/publish-review.png"));
        }
        // Geometry must follow a smaller viewport without becoming a layout child.
        window->resize(1000,700);
        QTest::qWait(100);
        for (auto* frame : frames(window->contentItem())) if (frame->isVisible()) verifyFrame(frame);
    }
    void sheetsRetainContentAndSquareBackground() {
        QQmlEngine engine; QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport 'Views' as Frames\nItem { width: 600; height: 600; Frames.PanelSheet { id: sheet; objectName: 'fixtureSheet'; width: 400; height: 300; Rectangle { objectName: 'sheetContents'; color: 'tomato'; width: 80; height: 80 } } }",source("PanelSheetFixture.qml"));
        QScopedPointer<QQuickItem> view(qobject_cast<QQuickItem*>(component.create()));
        QVERIFY2(view,qPrintable(component.errorString()));
        QQuickWindow window; window.resize(600,600); view->setParentItem(window.contentItem()); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* sheet = view->findChild<QObject*>("fixtureSheet"); QVERIFY(sheet);
        QVERIFY(QMetaObject::invokeMethod(sheet,"open"));
        QTRY_VERIFY(sheet->property("visible").toBool());
        QCOMPARE(sheet->property("cornerRadius").toReal(),0.);
        auto* background = sheet->property("background").value<QQuickItem*>();
        QTRY_VERIFY(background && background->isVisible()); verifyFrame(background);
        auto* contents = view->findChild<QQuickItem*>("sheetContents"); QVERIFY(contents && contents->isVisible());
        QCOMPARE(contents->size(),QSizeF(80,80));
    }
};
int main(int argc,char** argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs); QQuickStyle::setStyle("Basic");
    QTemporaryDir settings(QString(DREAMSCAPES_TEST_DIRECTORY)+"/panel-settings-XXXXXX");
    if(!settings.isValid()) return 2;
    settingsPath=settings.path();
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH",settings.filePath("storage.json").toUtf8());
    qputenv("DREAMSCAPES_TEMP_DIRECTORY",DREAMSCAPES_TEST_DIRECTORY);
    qputenv("SOCIETY_DISABLE_SESSION_RESTORE","1"); qputenv("IILOCALLLM_DISABLE_APP_MCP","1"); qunsetenv("SOCIETY_CONTAINER_PATH");
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false); registerStorageViewTypes();
    qmlRegisterType<PanelParameters>("Dreamscapes.Storage",1,0,"AdvancedImageParameters");
    qmlRegisterType<PanelGeneration>("Dreamscapes.Storage",1,0,"GenerationController");
    PanelFrameTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "tst_panelframes.moc"
