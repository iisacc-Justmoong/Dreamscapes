#include "App/Views/ColorManagerModel.h"
#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <cmath>
#include <memory>

namespace {
QQuickItem* visual(QQuickItem* root,const QString& name) {
    if(root->objectName()==name)return root;
    for(auto* child:root->childItems())if(auto* found=visual(child,name))return found;
    return nullptr;
}
QQuickItem* menuEntry(QQuickItem* root,const QString& label) {
    if(root->property("label").toString()==label)return root;
    for(auto* child:root->childItems())if(auto* found=menuEntry(child,label))return found;
    return nullptr;
}
void collect(QQuickItem* root,QList<QQuickItem*>& out) { out.push_back(root);for(auto* child:root->childItems())collect(child,out); }
}

class ColorManagerTests : public QObject {
    Q_OBJECT
private slots:
    void nativeBindingPreservesState();
    void pickerModes_data();
    void pickerModes();
    void menuListsNineModesAndCommits_data();
    void menuListsNineModesAndCommits();
};
void ColorManagerTests::nativeBindingPreservesState() {
    ColorManagerModel model;QSignalSpy edits(&model,&ColorManagerModel::colorEdited);
    const QColor original=QColor::fromRgbF(.2,.4,.6,.3);model.beginColor(original);QCOMPARE(model.color(),original);QCOMPARE(edits.size(),0);
    for(int i=0;i<9;i++){model.setMode(i);QCOMPARE(model.mode(),i);QCOMPARE(model.color(),original);QCOMPARE(edits.size(),0);QVERIFY(!model.channels().isEmpty());}
    QVERIFY(model.editChannel(3,255));QVERIFY(model.color().red()==255);QVERIFY(std::abs(model.color().alphaF()-.3)<.001);QCOMPARE(edits.size(),1);
    model.cancel();QCOMPARE(model.color(),original);model.setMode(7);QVERIFY(model.editChannel(13,127));QCOMPARE(model.channelValue(13),127.);QVERIFY(model.outOfGamut());
    QVERIFY(model.editHex("#8066FF"));QCOMPARE(model.hex(),QString("8066FF"));model.accept();const auto accepted=model.color();QVERIFY(model.editHex("#FFFFFF"));model.cancel();QCOMPARE(model.color(),accepted);
}
void ColorManagerTests::pickerModes_data() {
    QTest::addColumn<int>("mode");QTest::addColumn<int>("panelWidth");
    for(int mode=0;mode<9;mode++)for(int width:{280,402})QTest::newRow(qPrintable(QString("mode-%1-width-%2").arg(mode).arg(width)))<<mode<<width;
}
void ColorManagerTests::pickerModes() {
    QFETCH(int,mode);QFETCH(int,panelWidth);
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/ColorPicker.qml"));
    std::unique_ptr<QObject> object(component.create());QVERIFY2(object,qPrintable(component.errorString()));
    auto* root=qobject_cast<QQuickItem*>(object.get());QVERIFY(root);QQuickWindow window;window.resize(panelWidth,720);window.setColor(QColor("#202020"));root->setParentItem(window.contentItem());root->setWidth(panelWidth);
    root->setProperty("type",mode);root->setHeight(root->implicitHeight());
    auto* state=root->findChild<ColorManagerModel*>("colorPicker_state");QVERIFY(state);state->beginColor(QColor::fromRgbF(.7,.25,.45,.4));QCOMPARE(state->mode(),mode);
    root->setHeight(root->implicitHeight());window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTest::qWait(40);
    QVERIFY(root->implicitHeight()>100&&root->implicitHeight()<720);
    const auto* info=iiColorManager::modeInfo(iiColorManager::Mode(mode));QCOMPARE(state->channels().size(),qsizetype(info->channels.size()));
    QSignalSpy edits(state,&ColorManagerModel::colorEdited);const auto alpha=state->color().alphaF();
    if(mode<3){auto* field=root->findChild<ColorManagerSurface*>("colorPicker_field");QVERIFY(field&&field->isVisible());
        const auto from=field->mapToScene(QPointF(field->width()*.7,field->height()*.3)).toPoint();QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,from);QVERIFY(edits.size()>0);QVERIFY(std::abs(state->color().alphaF()-alpha)<.001);
        field->forceActiveFocus();const auto revision=state->revision();QTest::keyClick(&window,Qt::Key_Left);QVERIFY(state->revision()>revision);
    }else{const int channel=int(info->channels.front());auto* input=visual(root,QString("colorPicker_channelInput_%1").arg(channel));QVERIFY(input&&input->isVisible());
        const auto position=input->mapToScene(input->boundingRect().center()).toPoint();QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,position);
        QTRY_VERIFY(input->property("focused").toBool());
        QTest::qWait(40);
        QSignalSpy textEdits(input,SIGNAL(textEdited(QString)));
        QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);
        const QString entry=mode==8?"170":mode==7?"67":mode==4?"201":"73";
        for(const auto c:entry)QTest::keyClick(&window,c.toLatin1());QTest::keyClick(&window,Qt::Key_Return);
        QVERIFY2(edits.size()>0,qPrintable(QString("text=%1 edited=%2 focused=%3 active=%4 textEdits=%5").arg(input->property("text").toString()).arg(input->property("edited").toBool()).arg(input->property("focused").toBool()).arg(window.activeFocusItem()?window.activeFocusItem()->objectName():"none").arg(textEdits.size())));
        if(mode==8){QVERIFY(state->webSafe());QVERIFY(std::abs(state->channelValue(15)-153)<.01);}else QCOMPARE(qRound(state->channelValue(channel)),mode==7?67:mode==4?201:73);
    }
    // Verify every visible direct layout stays within its parent at narrow and mobile widths.
    QList<QQuickItem*> items;collect(root,items);
    for(auto* item:items)if(item->isVisible()&&item->parentItem()&&item->objectName().startsWith("colorPicker_")&&!item->objectName().startsWith("colorPicker_modeMenu")) {
        QVERIFY2(item->x()+item->width()<=item->parentItem()->width()+1,qPrintable(item->objectName()));
    }
    auto* modeButton=visual(root,"colorPicker_modeButton");QVERIFY(modeButton);
    auto* title=visual(root,"colorPicker_title");QVERIFY(title);
    QCOMPARE(title->property("text").toString(),QString("Color picker"));
    QVERIFY2(title->x()+title->width()<modeButton->x(),"Mode selector overlaps its label");
    QVERIFY2(std::abs(modeButton->x()+modeButton->width()-modeButton->parentItem()->width())<.1,"Mode selector must align to the header's right edge");
    QVERIFY2(modeButton->width()<modeButton->parentItem()->width()*.7,"Mode selector must remain compact");
    QCOMPARE(modeButton->property("text").toString(),state->modeName());
    QList<QQuickItem*> buttonParts;collect(modeButton,buttonParts);bool arrowFound=false;
    for(auto* part:buttonParts)if(part->property("resolvedIconAssetName").toString()=="StepperUpDownChevron") {
        const auto source=part->property("iconSource").toUrl();QCOMPARE(source.scheme(),QString("qrc"));
        QFile asset(":"+source.path());QVERIFY(asset.exists()&&asset.size()>0);
        QVERIFY(part->width()>0&&part->height()>0);arrowFound=true;
    }
    QVERIFY2(arrowFound,"The native UpDown indicator must have a local LVRS asset");
    auto* domain=visual(root,"colorPicker_domain");QVERIFY(domain);
    if(domain->isVisible())QVERIFY(modeButton->mapToScene({0,modeButton->height()}).y()<domain->mapToScene({0,0}).y());
    const auto capture=QString(DREAMSCAPES_TEST_DIRECTORY)+"/color-manager-verification";QVERIFY(QDir().mkpath(capture));
    if(panelWidth==402){QTest::qWait(300);const auto image=window.grabWindow();QVERIFY(!image.isNull());QVERIFY(image.save(capture+"/"+QString::fromUtf8(info->key.data(),qsizetype(info->key.size()))+".png"));}
}
void ColorManagerTests::menuListsNineModesAndCommits_data() {
    QTest::addColumn<int>("panelWidth");
    QTest::newRow("narrow-280")<<280;
    QTest::newRow("mobile-402")<<402;
}
void ColorManagerTests::menuListsNineModesAndCommits() {
    QFETCH(int,panelWidth);
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/ColorPicker.qml"));
    std::unique_ptr<QObject> object(component.create());QVERIFY2(object,qPrintable(component.errorString()));auto* root=qobject_cast<QQuickItem*>(object.get());QVERIFY(root);
    auto* state=root->findChild<ColorManagerModel*>("colorPicker_state");QCOMPARE(state->modeNames().size(),9);QCOMPARE(state->modeNames()[0],QString("Hue Cube"));QCOMPARE(state->modeNames()[8],QString("Web Color Sliders"));
    state->beginColor(QColor("#497EAC"));const auto original=state->color();QSignalSpy edits(state,&ColorManagerModel::colorEdited);
    QQuickWindow window;window.resize(panelWidth,720);root->setParentItem(window.contentItem());root->setWidth(panelWidth);root->setHeight(root->implicitHeight());window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* modeButton=visual(root,"colorPicker_modeButton");QVERIFY(modeButton);
    auto* menu=root->findChild<QObject*>("colorPicker_modeMenu");QVERIFY(menu);
    for(int mode=0;mode<9;mode++) {
        if(mode==1||mode==2) {
            modeButton->forceActiveFocus();QTest::keyClick(&window,mode==1?Qt::Key_Space:Qt::Key_Return);
        }else QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,modeButton->mapToScene(modeButton->boundingRect().center()).toPoint());
        QTRY_VERIFY(menu->property("visible").toBool());
        const auto label=(state->mode()==mode?QString("✓ "):QString())+state->modeNames()[mode];
        QTRY_VERIFY(menuEntry(window.contentItem(),label));auto* entry=menuEntry(window.contentItem(),label);
        QTest::qWait(350);
        QVERIFY(menu->property("x").toReal()>=0);
        QVERIFY(menu->property("x").toReal()+menu->property("width").toReal()<=window.width()+1);
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,entry->mapToScene(entry->boundingRect().center()).toPoint());
        QTRY_COMPARE(state->mode(),mode);QTRY_VERIFY(!menu->property("visible").toBool());
        QCOMPARE(modeButton->property("text").toString(),state->modeNames()[mode]);
        QCOMPARE(state->color(),original);QCOMPARE(edits.size(),0);
        QCOMPARE(root->property("hasPlane").toBool(),mode<3);
    }
    engine.rootContext()->setContextProperty("picker",root);
    QQmlExpression choose(engine.rootContext(),root,"picker.type = 7; picker.setHex('#FF0000'); picker.cancel()");choose.evaluate();QVERIFY2(!choose.hasError(),qPrintable(choose.error().toString()));QCOMPARE(state->color(),original);QCOMPARE(state->mode(),7);
    QQmlExpression apply(engine.rootContext(),root,"picker.setHex('#00FF00'); picker.accept()");apply.evaluate();QVERIFY2(!apply.hasError(),qPrintable(apply.error().toString()));QCOMPARE(state->previousColor(),QColor(Qt::green));
}
int main(int argc,char** argv) {
    QQuickStyle::setStyle("Basic");QApplication application(argc,argv);registerColorManagerTypes();ColorManagerTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "tst_colormanager.moc"
