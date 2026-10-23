#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/ColorManagerModel.h"
#include "App/StorageViewTypes.h"
#include <iiSharedCanvas.h>
#include <SocietyDrive.h>
#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QScopeGuard>
#include <QTabletEvent>
#include <QPointingDevice>
#include <limits>
using namespace iiSharedCanvas;
namespace {
Document fixture(int width=32,int height=24) {
    Document d;d.extent={width,height};d.assets.emplace_back(RasterAsset{"source",makeRasterLayer(width,height,0xff204080)});
    d.layers.emplace_back(StaticBitmapLayer{{"layer","Artwork"},StaticSource{"source"}});return d;
}
QVariantMap settings(QString tool="Brush",bool each=true) {
    return {{"backgroundView","Mask"},{"maskTool",tool},{"maskMode","Add"},{"maskSize",8},{"maskHardness",100},{"maskOpacity",100},{"maskFlow",100},{"maskPressure",false},{"maskNewEach",each},{"maskOverlay",true},{"maskOverlayColor","#FF5C77"},{"maskOverlayOpacity",50}};
}
const LayerAppearance &appearance(const EditorCanvas &canvas) {return layerProperties(canvas.document()->layers[0]).appearance;}
const RasterLayer &pixels(const EditorCanvas &canvas,int index=0) {return findRasterAsset(*canvas.document(),appearance(canvas).maskStack[index].mask.assetId)->pixels;}
QQuickItem *visual(QQuickItem *root,const QString &name) {if(root->objectName()==name)return root;for(auto *child:root->childItems())if(auto *item=visual(child,name))return item;return nullptr;}
QQuickItem *menuEntry(QQuickItem *root,const QString &label) {if(root->isVisible() && root->width()>0 && root->height()>0 && root->property("index").isValid() && root->property("label").toString()==label)return root;for(auto *child:root->childItems())if(auto *found=menuEntry(child,label))return found;return nullptr;}
void collect(QQuickItem *root,QList<QQuickItem *> &items) {items.append(root);for(auto *child:root->childItems())collect(child,items);}
bool shape(EditorCanvas &canvas,QString tool,QPointF a={4,4},QPointF b={14,14},bool each=true,QString mode="Add") {
    auto values=settings(tool,each);values["maskMode"]=mode;canvas.configureTool("background",values);
    return canvas.beginBackgroundMaskGesture(a)&&canvas.continueBackgroundMaskGesture((a+b)/2)&&canvas.endBackgroundMaskGesture(b);
}
}
class MaskPanelTests:public QObject {
    Q_OBJECT
    QTemporaryDir storage{DREAMSCAPES_TEST_DIRECTORY "/mask-documents-XXXXXX.societycontainer"};
    QByteArray oldContainer;
    QObject *panelState=nullptr;
    EditorCanvas *panelCanvas=nullptr;
private slots:
    void initTestCase() {oldContainer=qgetenv("SOCIETY_CONTAINER_PATH");QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));qputenv("SOCIETY_CONTAINER_PATH",storage.path().toUtf8());}
    void cleanupTestCase() {if(oldContainer.isNull())qunsetenv("SOCIETY_CONTAINER_PATH");else qputenv("SOCIETY_CONTAINER_PATH",oldContainer);}
    void selectionStatisticsFollowMaskOwnership() {
        EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(fixture(), "Mask statistics"));
        const auto selected = [&] { return canvas.toolState().value("selectedPixels").toLongLong(); };
        QVERIFY(shape(canvas, "Rectangle", {2, 2}, {14, 14}));
        QCOMPARE(selected(), 144);
        const auto first = QString::fromStdString(appearance(canvas).maskStack.front().id);
        QVERIFY(canvas.createBackgroundMask()); QCOMPARE(selected(), 0);
        QVERIFY(canvas.selectBackgroundMask(first)); QCOMPARE(selected(), 144);
        QVERIFY(canvas.deleteBackgroundMask()); QCOMPARE(selected(), 0);
    }
    void sourceAndMaskPersistenceAreIndependent() {
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Masks"));QVERIFY(shape(canvas,"Rectangle"));
        QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(1));QVERIFY(!appearance(canvas).maskStackEnabled);
        const auto original=std::get<RasterAsset>(canvas.document()->assets[0]).pixels;
        QCOMPARE(qGray(pixels(canvas).pixels[8*32+8]),255);QCOMPARE(qGray(pixels(canvas).pixels[0]),0);
        QVERIFY(canvas.setBackgroundMaskProperty("feather",0));QVERIFY(canvas.backgroundMaskAction("apply"));
        auto rendered=renderFrame(*canvas.document(),0);QVERIFY(rendered.ok());QCOMPARE(rendered.pixels.pixels[0]>>24,0U);QCOMPARE(rendered.pixels.pixels[8*32+8],0xff204080U);
        QVERIFY(canvas.setBackgroundMaskProperty("density",0));QCOMPARE(renderFrame(*canvas.document(),0).pixels.pixels,original.pixels);
        QVERIFY(canvas.setBackgroundMaskProperty("density",100));QVERIFY(canvas.backgroundMaskAction("invert"));QCOMPARE(renderFrame(*canvas.document(),0).pixels.pixels[0],0xff204080U);
        QVERIFY(canvas.setBackgroundMaskProperty("name","Subject detail"));QVERIFY(canvas.setBackgroundMaskProperty("feather",1.5));QVERIFY(canvas.duplicateBackgroundMask());
        const auto copied=appearance(canvas);QCOMPARE(copied.maskStack.size(),std::size_t(2));
        const auto path=QUrl::fromLocalFile(storage.filePath("Files/masks.iisc"));QVERIFY2(canvas.saveDocumentAs(path),qPrintable(canvas.error()));
        EditorCanvas reopened;QVERIFY2(reopened.openDocumentSource(path),qPrintable(reopened.error()));QVERIFY(appearance(reopened)==copied);
        QCOMPARE(std::get<RasterAsset>(reopened.document()->assets[0]).pixels.pixels,original.pixels);
        QCOMPARE(renderFrame(*reopened.document(),0).pixels.pixels,renderFrame(*canvas.document(),0).pixels.pixels);
        QVERIFY(reopened.selectBackgroundMask(QString::fromStdString(copied.maskStack[0].id)));QVERIFY(reopened.setBackgroundMaskProperty("visible",false));
        EditorCanvas durable;QVERIFY(durable.openDocumentSource(path));QVERIFY(!appearance(durable).maskStack[0].mask.enabled);
    }
    void collectionAndHistory() {
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Masks"));canvas.configureTool("background",settings());
        QVERIFY(canvas.createBackgroundMask());QVERIFY(canvas.createBackgroundMask());QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(2));
        QVERIFY(canvas.undo());QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(1));QVERIFY(canvas.redo());QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(2));
        QVERIFY(canvas.duplicateBackgroundMask());QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(3));QVERIFY(canvas.deleteBackgroundMask());QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(2));
        const auto before=encodeIisc(*canvas.document()).bytes;
        QVERIFY(!canvas.setBackgroundMaskProperty("density",101));QVERIFY(!canvas.setBackgroundMaskProperty("feather",std::numeric_limits<double>::quiet_NaN()));
        QCOMPARE(encodeIisc(*canvas.document()).bytes,before);
    }
    void selectionModesAndShapes() {
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Masks"));
        QVERIFY(shape(canvas,"Rectangle",{2,2},{14,14},false));QVERIFY(shape(canvas,"Rectangle",{8,2},{20,14},false,"Subtract"));
        QCOMPARE(qGray(pixels(canvas).pixels[6*32+5]),255);QCOMPARE(qGray(pixels(canvas).pixels[6*32+10]),0);
        QVERIFY(shape(canvas,"Rectangle",{4,4},{12,12},false,"Intersect"));QCOMPARE(qGray(pixels(canvas).pixels[3*32+5]),0);QCOMPARE(qGray(pixels(canvas).pixels[6*32+5]),255);
        QVERIFY(shape(canvas,"Ellipse",{16,4},{28,16},false,"Replace"));QCOMPARE(qGray(pixels(canvas).pixels[10*32+22]),255);QCOMPARE(qGray(pixels(canvas).pixels[4*32+16]),0);
        QVERIFY(shape(canvas,"Gradient",{0,0},{32,0}));QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(2));QVERIFY(qGray(pixels(canvas,1).pixels[0])<10);QVERIFY(qGray(pixels(canvas,1).pixels[31])>240);
        canvas.configureTool("background",settings("Lasso"));QVERIFY(canvas.beginBackgroundMaskGesture({2,2}));QVERIFY(canvas.continueBackgroundMaskGesture({12,2}));QVERIFY(canvas.continueBackgroundMaskGesture({12,12}));QVERIFY(canvas.endBackgroundMaskGesture({2,12}));
        QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(3));
        canvas.configureTool("background",settings("Polygon"));QVERIFY(canvas.beginBackgroundMaskGesture({2,2}));QVERIFY(canvas.endBackgroundMaskGesture({2,2}));QVERIFY(canvas.beginBackgroundMaskGesture({12,2}));QVERIFY(canvas.beginBackgroundMaskGesture({8,12}));
        QCOMPARE(canvas.maskPanelInfo()["polygonPoints"].toInt(),3);QVERIFY(canvas.backgroundMaskAction("closePath"));QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(4));
        QVERIFY(canvas.backgroundMaskAction("selectAll"));QVERIFY(canvas.backgroundMaskAction("invertSelection"));QVERIFY(std::all_of(pixels(canvas,4).pixels.begin(),pixels(canvas,4).pixels.end(),[](auto p){return qGray(p)==0;}));
    }
    void brushOpacityPressureAndCancellation() {
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Masks"));auto values=settings();values["maskOpacity"]=40;values["maskFlow"]=50;values["maskPressure"]=true;
        canvas.configureTool("background",values);QVERIFY(canvas.beginBackgroundMaskGesture({10,10},.5));
        // Repeated presentation of the same draft must preserve a live stroke.
        canvas.configureTool("background",values);QVERIFY(canvas.continueBackgroundMaskGesture({10,10},.5));QVERIFY(canvas.endBackgroundMaskGesture({10,10},.5));
        const auto peak=*std::max_element(pixels(canvas).pixels.begin(),pixels(canvas).pixels.end());QVERIFY(qGray(peak)>0&&qGray(peak)<=51);
        const auto source=std::get<RasterAsset>(canvas.document()->assets[0]).pixels.pixels;const auto previous=encodeIisc(*canvas.document()).bytes;
        QVERIFY(canvas.beginBackgroundMaskGesture({20,10},1));canvas.cancelBackgroundMaskGesture();QCOMPARE(encodeIisc(*canvas.document()).bytes,previous);
        QSignalSpy overlay(&canvas,&EditorCanvas::selectionOverlayChanged);
        values["maskTool"]="Eraser";values["maskOpacity"]=100;values["maskFlow"]=100;values["maskPressure"]=false;canvas.configureTool("background",values);
        QVERIFY(canvas.beginBackgroundMaskGesture({10,10}));const auto notifications=overlay.size();QVERIFY(canvas.continueBackgroundMaskGesture({10.5,10}));QVERIFY(overlay.size()>notifications);QVERIFY(canvas.endBackgroundMaskGesture({10,10}));QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(1));QCOMPARE(qGray(pixels(canvas).pixels[10*32+10]),0);
        QCOMPARE(std::get<RasterAsset>(canvas.document()->assets[0]).pixels.pixels,source);
    }
    void pressureControlsCoverageWithoutFlowReduction() {
        for(double pressure:{.25,.5,1.}) {
            EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Pressure"));auto values=settings();values["maskPressure"]=true;canvas.configureTool("background",values);
            QVERIFY(canvas.beginBackgroundMaskGesture({10,10},pressure));QVERIFY(canvas.endBackgroundMaskGesture({10,10},pressure));
            const auto peak=*std::max_element(pixels(canvas).pixels.begin(),pixels(canvas).pixels.end());
            QVERIFY2(qGray(peak)>0 && qGray(peak)<=qRound(pressure*255),qPrintable(QString("pressure=%1 coverage=%2").arg(pressure).arg(qGray(peak))));
        }
    }
    void colorRangeAndSemanticSelectionUseRealData() {
        auto doc=fixture(4,2);auto &source=std::get<RasterAsset>(doc.assets[0]).pixels;source.pixels={0xffff0000,0xffff0000,0xff0000ff,0xff0000ff,0xffff0000,0xffff0000,0xff0000ff,0xff0000ff};
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(doc,"Colors"));canvas.configureTool("background",settings("Color range"));QVERIFY(canvas.beginBackgroundMaskGesture({.5,.5}));
        QVERIFY(qGray(pixels(canvas).pixels[0])>240);QVERIFY(qGray(pixels(canvas).pixels[2])<10);QVERIFY(!canvas.maskPanelInfo()["canSelectSubject"].toBool());
        const auto prior=encodeIisc(*canvas.document()).bytes;QVERIFY(!canvas.backgroundMaskAction("subject"));QCOMPARE(encodeIisc(*canvas.document()).bytes,prior);
        doc.assets.emplace_back(RasterAsset{"regions",{4,2,{0xff00ff00,0xff00ff00,0xff0000ff,0xff0000ff,0xff00ff00,0xff00ff00,0xff0000ff,0xff0000ff}}});
        SemanticSegmentLayer segment;segment.properties={"semantic","Regions",false};segment.source=StaticSource{"regions"};segment.segmentation.taxonomy.id="fixture";
        segment.segmentation.taxonomy.classes={{.id=1,.key="subject",.name="Subject",.controlColor=0xff00ff00},{.id=2,.key="sky",.name="Sky",.controlColor=0xff0000ff}};
        segment.segmentation.regions={{.id=1,.classId=1,.maskColor=0xff00ff00,.name="Subject"},{.id=2,.classId=2,.maskColor=0xff0000ff,.name="Sky"}};doc.layers.emplace_back(segment);
        QVERIFY2(canvas.loadProjectPage(doc,"Regions"),qPrintable(canvas.error()));QVERIFY(canvas.selectLayer("layer"));canvas.configureTool("background",settings());
        QVERIFY(canvas.maskPanelInfo()["canSelectSky"].toBool());QVERIFY(canvas.backgroundMaskAction("sky"));QCOMPARE(qGray(pixels(canvas).pixels[0]),0);QCOMPARE(qGray(pixels(canvas).pixels[2]),255);
        QVERIFY(canvas.backgroundMaskAction("subject"));QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(2));QCOMPARE(qGray(pixels(canvas,1).pixels[0]),255);QCOMPARE(qGray(pixels(canvas,1).pixels[2]),0);
    }
    void importedMaskChannelsMatchNativeRendering() {
        auto doc=fixture(4,2);doc.assets.emplace_back(RasterAsset{"colorMask",makeRasterLayer(4,2,0x80ff0000)});
        layerProperties(doc.layers[0]).appearance.maskStack={{"imported","Imported",LayerMask{.assetId="colorMask",.featherRadius=0,.outsideCoverage=0}}};
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(doc,"Imported mask"));auto values=settings("Brush",false);values["maskFlow"]=0;canvas.configureTool("background",values);
        const auto before=renderFrame(*canvas.document(),0).pixels;QVERIFY(canvas.beginBackgroundMaskGesture({1,1}));QVERIFY(canvas.endBackgroundMaskGesture({1,1}));
        const auto after=renderFrame(*canvas.document(),0).pixels;for(std::size_t i=0;i<before.pixels.size();++i)QVERIFY(std::abs(qAlpha(before.pixels[i])-qAlpha(after.pixels[i]))<=1);
        QVERIFY(canvas.applyToolField("background","maskOverlayColor","#80FF0000"));QVERIFY(canvas.applyToolField("background","maskOverlayOpacity",50));
        const auto source=canvas.selectionOverlay().toString();const auto image=QImage::fromData(QByteArray::fromBase64(source.mid(source.indexOf(',')+1).toLatin1()));QVERIFY(!image.isNull());QVERIFY(qAlpha(image.pixel(1,1))<=8);
    }
    void transformedLayerAndOverlayPreservePixels() {
        auto doc=fixture(8,8);doc.extent={32,24};layerProperties(doc.layers[0]).transform={2,0,0,2,8,4};
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(doc,"Transform"));QVERIFY(shape(canvas,"Rectangle",{8,4},{16,12}));
        QCOMPARE(pixels(canvas).width,8);QCOMPARE(qGray(pixels(canvas).pixels[1*8+1]),255);QCOMPARE(qGray(pixels(canvas).pixels[7*8+7]),0);
        QVERIFY(canvas.setBackgroundMaskProperty("feather",0));QVERIFY(canvas.backgroundMaskAction("apply"));
        auto rendered=renderFrame(*canvas.document(),0).pixels;QVERIFY(qAlpha(rendered.pixels[7*32+11])>240);QCOMPARE(qAlpha(rendered.pixels[19*32+23]),0);
        const auto bytes=encodeIisc(*canvas.document()).bytes;QVERIFY(!canvas.selectionOverlay().isEmpty());
        QVERIFY(canvas.applyToolField("background","maskOverlay",false));QVERIFY(canvas.selectionOverlay().isEmpty());QCOMPARE(encodeIisc(*canvas.document()).bytes,bytes);QCOMPARE(renderFrame(*canvas.document(),0).pixels.pixels,rendered.pixels);
        QVERIFY(canvas.applyToolField("background","maskOverlayColor","#00FF00"));QVERIFY(canvas.applyToolField("background","maskOverlayOpacity",10));QCOMPARE(encodeIisc(*canvas.document()).bytes,bytes);
        QVERIFY(canvas.backgroundMaskAction("bypass"));QCOMPARE(renderFrame(*canvas.document(),0).pixels.pixels[19*32+23],0xff204080U);
    }
    void realMouseAndTabletEvents() {
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Input"));canvas.setWidth(320);canvas.setHeight(240);canvas.setZoom(10);canvas.setPanX(0);canvas.setPanY(0);canvas.configureTool("background",settings("Rectangle"));
        QQuickWindow window;window.resize(320,240);canvas.setParentItem(window.contentItem());window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,{40,40});QTest::mouseMove(&window,{140,140});QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,{140,140});
        QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(1));QCOMPARE(qGray(pixels(canvas).pixels[8*32+8]),255);
        auto values=settings();values["maskPressure"]=true;canvas.configureTool("background",values);
        const auto *device=QPointingDevice::primaryPointingDevice();
        QTabletEvent press(QEvent::TabletPress,device,{200,100},{200,100},.4,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::LeftButton);
        QApplication::sendEvent(&canvas,&press);QTabletEvent release(QEvent::TabletRelease,device,{200,100},{200,100},.4,0,0,0,0,0,Qt::NoModifier,Qt::LeftButton,Qt::NoButton);QApplication::sendEvent(&canvas,&release);
        QCOMPARE(appearance(canvas).maskStack.size(),std::size_t(2));const auto peak=*std::max_element(pixels(canvas,1).pixels.begin(),pixels(canvas,1).pixels.end());QVERIFY(qGray(peak)>0&&qGray(peak)<=102);
        canvas.setParentItem(nullptr);
    }
    void panelControls_data() {QTest::addColumn<int>("width");for(int width:{280,402,432})QTest::newRow(qPrintable(QString::number(width)))<<width;}
    void backgroundFillIsValidatedAndUndoable() {
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Background"));
        const auto original=encodeIisc(*canvas.document()).bytes;
        canvas.configureTool("background",{{"backgroundView","Color"}});
        QVERIFY(canvas.applyToolField("background","backgroundColor","#CC4422"));
        QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
        QVERIFY(canvas.applyBackgroundFill({{"backgroundColor","#CC4422"},{"backgroundAlpha",50}}));
        const auto &background=std::get<StaticBitmapLayer>(canvas.document()->layers.front());
        const auto *asset=findRasterAsset(*canvas.document(),background.content.assetId);
        QVERIFY(asset);QCOMPARE(asset->pixels.width,32);QCOMPARE(qRed(asset->pixels.pixels[0]),204);QCOMPARE(qAlpha(asset->pixels.pixels[0]),128);
        QCOMPARE(std::get<RasterAsset>(canvas.document()->assets[0]).pixels.pixels,makeRasterLayer(32,24,0xff204080).pixels);
        QVERIFY(canvas.undo());QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
        QVariantMap values{{"backgroundFill","Gradient"},{"backgroundInterpolation","Linear"},{"backgroundGradientStops",QVariantList{QVariantMap{{"position",0},{"color","#FF0000"}},QVariantMap{{"position",1},{"color","#0000FF"}}}}};
        QVERIFY(canvas.applyBackgroundFill(values));
        const auto &gradient=std::get<StaticBitmapLayer>(canvas.document()->layers.front());
        const auto &p=findRasterAsset(*canvas.document(),gradient.content.assetId)->pixels;
        QVERIFY(qRed(p.pixels.front())>240);QVERIFY(qBlue(p.pixels[31])>240);
        const auto prior=encodeIisc(*canvas.document()).bytes;
        values["backgroundOpacity"]=std::numeric_limits<double>::quiet_NaN();QVERIFY(!canvas.applyBackgroundFill(values));
        QCOMPARE(encodeIisc(*canvas.document()).bytes,prior);
    }
    void removalPreviewKeepsSourceAndRejectsStaleResults() {
        auto doc=fixture(4,2);EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(doc,"Removal"));canvas.configureTool("background",{{"backgroundView","Remove"}});
        const auto original=encodeIisc(*canvas.document()).bytes;
        QVERIFY(!canvas.previewBackgroundRemoval({})["ready"].toBool());QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
        QVERIFY(!canvas.applyBackgroundRemoval({}));
        doc.assets.emplace_back(RasterAsset{"regions",{4,2,{0xff00ff00,0xff00ff00,0xff0000ff,0xff0000ff,0xff00ff00,0xff00ff00,0xff0000ff,0xff0000ff}}});
        SemanticSegmentLayer segment;segment.properties={"semantic","Regions",false};segment.source=StaticSource{"regions"};segment.segmentation.taxonomy.id="fixture";
        segment.segmentation.taxonomy.classes={{.id=1,.key="subject",.name="Subject",.controlColor=0xff00ff00},{.id=2,.key="background",.name="Background",.controlColor=0xff0000ff}};
        segment.segmentation.regions={{.id=1,.classId=1,.maskColor=0xff00ff00,.name="Subject"},{.id=2,.classId=2,.maskColor=0xff0000ff,.name="Background"}};doc.layers.emplace_back(segment);
        QVERIFY(canvas.loadProjectPage(doc,"Removal regions"));QVERIFY(canvas.selectLayer("layer"));canvas.configureTool("background",{{"backgroundView","Remove"}});
        const auto supplied=encodeIisc(*canvas.document()).bytes;const QVariantMap edges{{"removeFeather",0},{"removeSmooth",0},{"removeRadius",0}};
        const auto preview=canvas.previewBackgroundRemoval(edges);QVERIFY2(preview["ready"].toBool(),qPrintable(canvas.error()));
        QVERIFY(!preview["after"].toUrl().isEmpty());QCOMPARE(encodeIisc(*canvas.document()).bytes,supplied);
        QVERIFY(canvas.applyBackgroundRemoval(edges));QCOMPARE(canvas.document()->layers.size(),std::size_t(3));
        const auto &source=layerProperties(canvas.document()->layers[0]);QVERIFY(!source.visible);
        const auto &result=layerProperties(canvas.document()->layers[1]);QVERIFY(result.appearance.maskStackEnabled);QCOMPARE(result.appearance.maskStack.size(),std::size_t(1));
        const auto &mask=findRasterAsset(*canvas.document(),result.appearance.maskStack[0].mask.assetId)->pixels;QCOMPARE(qGray(mask.pixels[0]),255);QCOMPARE(qGray(mask.pixels[2]),0);
        QCOMPARE(std::get<RasterAsset>(canvas.document()->assets[0]).pixels.pixels,std::get<RasterAsset>(doc.assets[0]).pixels.pixels);
        QVERIFY(canvas.undo());QVERIFY(canvas.selectLayer("layer"));QVERIFY(canvas.previewBackgroundRemoval(edges)["ready"].toBool());
        auto replaceEdges=edges;replaceEdges["removePreserve"]=false;QVERIFY(canvas.applyBackgroundRemoval(replaceEdges));QCOMPARE(canvas.document()->layers.size(),std::size_t(2));QVERIFY(!findLayer(*canvas.document(),"layer"));QCOMPARE(qAlpha(renderFrame(*canvas.document(),0).pixels.pixels[2]),0);
        QVERIFY(canvas.undo());QVERIFY(canvas.selectLayer("layer"));QVERIFY(canvas.previewBackgroundRemoval(edges)["ready"].toBool());
        QVERIFY(canvas.applyBackgroundFill({}));const auto changed=encodeIisc(*canvas.document()).bytes;QVERIFY(!canvas.applyBackgroundRemoval(edges));QCOMPARE(encodeIisc(*canvas.document()).bytes,changed);
    }
    void backgroundUsesActiveArtboardRegion() {
        auto doc=fixture(8,8);std::get<RasterAsset>(doc.assets[0]).pixels=makeRasterLayer(8,8,0);
        doc.artboards={{"first","First",{{0,0},{8,8}},0},{"second","Second",{{72,24},{8,8}},0}};
        auto &source=layerProperties(doc.layers[0]);source.artboardId="second";
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(doc,"Artboards"));QVERIFY(canvas.selectArtboard(1,false));
        QVERIFY(canvas.applyBackgroundFill({{"backgroundColor","#123456"}}));
        auto rendered=renderArtboard(*canvas.document(),0,"second");QVERIFY(rendered.ok());QCOMPARE(rendered.pixels.pixels[4*8+4],0xff123456U);
        const auto first=renderArtboard(*canvas.document(),0,"first");QVERIFY(first.ok());QVERIFY(std::all_of(first.pixels.pixels.begin(),first.pixels.pixels.end(),[](auto pixel){return qAlpha(pixel)==0;}));
        QImage image(8,8,QImage::Format_ARGB32);image.fill(QColor("#FFAA55"));const auto file=storage.filePath("Files/artboard-replacement.png");QVERIFY(image.save(file));
        QVERIFY(canvas.applyBackgroundReplacement(QUrl::fromLocalFile(file),{{"replaceResult","Replace background layer"},{"replaceLighting",false}}));
        rendered=renderArtboard(*canvas.document(),0,"second");QVERIFY(rendered.ok());QCOMPARE(rendered.pixels.pixels[4*8+4],0xffffaa55U);
        QCOMPARE(renderArtboard(*canvas.document(),0,"first").pixels.pixels,first.pixels.pixels);
    }
    void replacementsKeepPlacementAndGenerationParameters() {
        EditorCanvas empty;QVERIFY(empty.generationParameters("Background",{}).isEmpty());QVERIFY(!empty.error().isEmpty());
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Replacement"));canvas.configureTool("background",{{"backgroundView","Replace"}});
        QImage replacement(8,8,QImage::Format_ARGB32);replacement.fill(QColor("#FFAA55"));const auto path=storage.filePath("Files/replacement.png");QVERIFY(replacement.save(path));
        const auto original=encodeIisc(*canvas.document()).bytes;
        QVariantMap values{{"replaceFit","Fit"},{"replaceScale",50},{"replaceX",3},{"replaceY",2},{"replaceOpacity",40},{"replaceLighting",false},{"replacePreserve",true}};
        QVERIFY2(canvas.applyBackgroundReplacement(QUrl::fromLocalFile(path),values),qPrintable(canvas.error()));
        QCOMPARE(canvas.document()->layers.size(),std::size_t(2));const auto &p=layerProperties(canvas.document()->layers[0]);
        QCOMPARE(p.opacity,.4);QCOMPARE(p.transform.m11,1.5);QCOMPARE(p.transform.m22,1.5);QCOMPARE(p.transform.translationX,13.);QCOMPARE(p.transform.translationY,8.);
        QCOMPARE(layerProperties(canvas.document()->layers[1]).id,std::string("layer"));QVERIFY(canvas.undo());QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
        auto previousClipboard=std::make_unique<QMimeData>();const auto *clipboardData=QApplication::clipboard()->mimeData();
        if(clipboardData)for(const auto &format:clipboardData->formats())previousClipboard->setData(format,clipboardData->data(format));
        const auto restoreClipboard=qScopeGuard([&]{QApplication::clipboard()->setMimeData(previousClipboard.release());});
        QApplication::clipboard()->setImage(replacement);QVERIFY(canvas.captureBackgroundClipboard());QVERIFY(canvas.applyBackgroundReplacement(QUrl("clipboard:image"),values));QVERIFY(canvas.undo());
        values["replaceAngle"]=std::numeric_limits<double>::infinity();QVERIFY(!canvas.applyBackgroundReplacement(QUrl::fromLocalFile(path),values));QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
        const auto oldCache=qgetenv("DREAMSCAPES_EDITOR_CACHE");qputenv("DREAMSCAPES_EDITOR_CACHE",storage.filePath("GenerationInputs").toUtf8());const auto restore=qScopeGuard([&]{if(oldCache.isNull())qunsetenv("DREAMSCAPES_EDITOR_CACHE");else qputenv("DREAMSCAPES_EDITOR_CACHE",oldCache);});
        const auto params=canvas.generationParameters("Background",{{"backgroundView","Replace"},{"field-0","Studio"},{"field-2","test-model"},{"field-5",72},{"replaceVariations",3},{"replaceSeed",1234},{"replaceLighting",false}});
        QCOMPARE(params["prompt"].toString(),QString("Studio"));QCOMPARE(params["model"].toString(),QString("test-model"));QCOMPARE(params["outputCount"].toInt(),3);QCOMPARE(params["seed"].toLongLong(),1234);QCOMPARE(params["imageStrength"].toDouble(),.72);
        QVERIFY(canvas.placeGenerated(QUrl::fromLocalFile(path),"Background"));QCOMPARE(canvas.document()->layers.size(),std::size_t(2));QVERIFY(canvas.undo());
        QVERIFY(canvas.generationParameters("Background",{{"replaceVariations",0}}).isEmpty());
    }
    void backgroundViews_data() {QTest::addColumn<int>("width");for(int width:{280,402,432})QTest::newRow(qPrintable(QString::number(width)))<<width;}
    void backgroundViews() {
        QFETCH(int,width);EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Background views"));
        QQmlEngine engine;QStringList warnings;connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError>& errors){for(const auto &error:errors)warnings.append(error.toString());});
        const auto path=QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/";
        QQmlComponent stateComponent(&engine,QUrl::fromLocalFile(path+"EditorToolSheet.qml"));
        std::unique_ptr<QObject> state(stateComponent.createWithInitialProperties({{"toolId","background"},{"engine",QVariant::fromValue<QObject*>(&canvas)}}));QVERIFY2(state,qPrintable(stateComponent.errorString()));
        panelState=state.get();panelCanvas=&canvas;canvas.configureTool("background",state->property("values").toMap());
        QObject::connect(state.get(),SIGNAL(valueEdited(QString,QString,QVariant)),this,SLOT(panelDraftChanged(QString,QString,QVariant)));
        QQmlComponent component(&engine,QUrl::fromLocalFile(path+"EditorDesktopPanel.qml"));std::unique_ptr<QObject> object(component.createWithInitialProperties({{"toolState",QVariant::fromValue(state.get())}}));QVERIFY2(object,qPrintable(component.errorString()));
        auto *panel=qobject_cast<QQuickItem*>(object.get());QVERIFY(panel);QQuickWindow window;window.resize(width,720);window.setColor(QColor("#1d1e1e"));panel->setParentItem(window.contentItem());panel->setSize({qreal(width),720});
        state->setProperty("parent",QVariant::fromValue(window.contentItem()));window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTest::qWait(150);
        auto *body=visual(panel,"editorBackgroundPanel"),*viewport=visual(panel,"editorDesktopPanelViewport");QVERIFY(body&&viewport);QCOMPARE(body->property("currentView").toString(),QString("Color"));
        auto click=[&](const QString &name){auto *item=visual(panel,name);QVERIFY2(item&&item->isVisible(),qPrintable(name));const auto y=item->mapToItem(viewport,{0,0}).y()+viewport->property("contentY").toDouble();viewport->setProperty("contentY",std::clamp(y-100.,0.,std::max(0.,viewport->property("contentHeight").toDouble()-viewport->height())));QTest::qWait(40);QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(item->boundingRect().center()).toPoint());};
        const auto output=QString(DREAMSCAPES_TEST_DIRECTORY)+"/background-panel-verification";QVERIFY(QDir().mkpath(output));
        for(const auto &view:QStringList{"Color","Remove","Mask","Replace"}) {
            click("backgroundTab-"+view);QTRY_COMPARE(body->property("currentView").toString(),view);QTest::qWait(100);
            const auto required=view=="Color"?QStringList{"background-pickerMode","background-hex","backgroundChannel-3","backgroundGradientPreview","background-applyColor"}:view=="Remove"?QStringList{"background-removeModel","background-removeQuality-Balanced","backgroundRemovalPreview","background-removeFeather","background-applyRemoval"}:view=="Replace"?QStringList{"background-source-Generative","background-replacePrompt","background-replaceStrength","background-replaceFile","background-fit-Fill","background-applyReplacement"}:QStringList{"maskTool-Brush","maskNew","maskNumber-feather"};
            for(const auto &name:required){auto *item=visual(panel,name);QVERIFY2(item&&item->isVisible()&&item->width()>0,qPrintable(name));}
            // Figma 1023:70875 uses Default for every Background action,
            // including Apply removal before and after its preview is ready.
            const auto actions=view=="Color"?QStringList{"background-applyColor"}
                :view=="Remove"?QStringList{"background-remove","background-applyRemoval"}
                :view=="Mask"?QStringList{"maskNew","maskAction-apply"}
                :QStringList{"background-generate","background-applyReplacement"};
            for(const auto &name:actions) {
                auto *button=visual(panel,name);QVERIFY2(button,qPrintable(name));
                QCOMPARE(button->property("tone").toInt(),1); // LVRS AbstractButton.Default
            }
            if(view=="Remove") {
                auto *apply=visual(panel,"background-applyRemoval");QVERIFY(!apply->isEnabled());
                QSignalSpy toneChanges(apply,SIGNAL(toneChanged()));
                QVERIFY(QMetaObject::invokeMethod(state.get(),"setValue",Q_ARG(QVariant,QVariant("backgroundRemovalPreview")),Q_ARG(QVariant,QVariant(QVariantMap{{"ready",true}}))));
                QTRY_VERIFY(apply->isEnabled());QCOMPARE(apply->property("tone").toInt(),1);
                QVERIFY(QMetaObject::invokeMethod(state.get(),"setValue",Q_ARG(QVariant,QVariant("backgroundRemovalPreview")),Q_ARG(QVariant,QVariant(QVariantMap{}))));
                QTRY_VERIFY(!apply->isEnabled());QCOMPARE(apply->property("tone").toInt(),1);
                QCOMPARE(toneChanges.size(),0);
            }
            if(view=="Color") {
                bool alphaFound=false;
                for(auto *surface:body->findChildren<ColorManagerSurface*>())if(surface->channel()==10&&surface->height()==22&&surface->isVisible()){QVERIFY(!surface->checkerboard());QVERIFY(!surface->showMarker());alphaFound=true;}
                QVERIFY(alphaFound);
            }
            QVERIFY(viewport->property("contentHeight").toReal()>viewport->height());
            if(width==432)for(int section=0;section<3;++section){viewport->setProperty("contentY",section*(viewport->property("contentHeight").toDouble()-viewport->height())/2);QTest::qWait(140);const auto capture=window.grabWindow();QVERIFY(!capture.isNull());QVERIFY(capture.save(output+QString("/%1-%2.png").arg(view.toLower()).arg(section)));}
            if(width==432) {
                const auto full=body->grabToImage();QVERIFY(full);QSignalSpy ready(full.get(),&QQuickItemGrabResult::ready);QVERIFY(ready.wait(5000));
                QVERIFY(!full->image().isNull());QVERIFY(full->image().save(output+QString("/%1-full.png").arg(view.toLower())));
            }
            QList<QQuickItem*> items;collect(body,items);for(auto *item:items)if(item->isVisible()&&item->parentItem()) {
                if(item->objectName().startsWith("background-")&&item->width()>0)QVERIFY2(item->x()+item->width()<=item->parentItem()->width()+1,qPrintable(item->objectName()));
                const auto asset=item->property("resolvedIconSource").toUrl();
                if(!asset.isEmpty()) {
                    QFile file(asset.isLocalFile()?asset.toLocalFile():":"+asset.path());QVERIFY2(file.exists()&&file.size()>0,qPrintable(asset.toString()));
                    QCOMPARE(item->property("iconSize").toInt(),18);
                }
                if(item->objectName()=="iconButton_icon"){QCOMPARE(item->width(),18.);QCOMPARE(item->height(),18.);QTRY_COMPARE(item->property("status").toInt(),1);}
            }
            viewport->setProperty("contentY",0);
        }
        click("backgroundTab-Color");auto *colorModel=body->findChild<ColorManagerModel*>("backgroundColorModel");QVERIFY(colorModel);
        click("background-alpha");QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_5);QTest::keyClick(&window,Qt::Key_0);QTest::keyClick(&window,Qt::Key_Return);QTRY_COMPARE(qRound(state->property("values").toMap()["backgroundAlpha"].toDouble()),50);
        click("background-hex");QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);for(const auto c:QString("#123456"))QTest::keyClick(&window,c.toLatin1());QTest::keyClick(&window,Qt::Key_Return);QTRY_COMPARE(colorModel->hex(),QString("123456"));
        click("backgroundTab-Replace");click("background-replaceStrength");QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);QTest::keyClick(&window,Qt::Key_7);QTest::keyClick(&window,Qt::Key_2);QTest::keyClick(&window,Qt::Key_Return);QTRY_COMPARE(state->property("values").toMap()["replaceStrength"].toDouble(),72.);
        QSignalSpy generation(state.get(),SIGNAL(actionRequested(QString,QString,QVariant)));click("background-generate");QCOMPARE(generation.size(),1);const auto request=generation[0][2].toMap();QCOMPARE(request["replaceStrength"].toDouble(),72.);QCOMPARE(request["field-13"].toString(),QString("Generate"));
        click("background-source-Image");
        QVERIFY(QMetaObject::invokeMethod(state.get(),"setValue",Q_ARG(QVariant,QVariant("backgroundAlpha")),Q_ARG(QVariant,QVariant(50))));
        QVERIFY(QMetaObject::invokeMethod(state.get(),"setValue",Q_ARG(QVariant,QVariant("backgroundFill")),Q_ARG(QVariant,QVariant("Gradient"))));
        click("backgroundTab-Color");
        colorModel=body->findChild<ColorManagerModel*>("backgroundColorModel");QCOMPARE(colorModel->hex(),QString("123456"));
        QCOMPARE(qRound(colorModel->channelValue(10)),50);QCOMPARE(state->property("values").toMap()["backgroundFill"].toString(),QString("Gradient"));
        auto *modeMenu=body->findChild<QObject*>("backgroundPickerMenu");QVERIFY(modeMenu);
        for(int mode=0;mode<9;++mode){click("background-pickerMode");QTRY_VERIFY(modeMenu->property("opened").toBool());QTRY_VERIFY(body->property("modalActive").toBool());QTRY_VERIFY(menuEntry(window.contentItem(),colorModel->modeNames()[mode]));auto *entry=menuEntry(window.contentItem(),colorModel->modeNames()[mode]);QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,entry->mapToScene(entry->boundingRect().center()).toPoint());QTRY_COMPARE(colorModel->mode(),mode);QTRY_VERIFY(!modeMenu->property("visible").toBool());QCOMPARE(colorModel->hex(),QString("123456"));if(mode>2)QVERIFY(visual(panel,"backgroundModeChannel-"+QString::number(colorModel->channels().first().toInt())));}
        click("backgroundTab-Replace");QCOMPARE(state->property("values").toMap()["replaceSource"].toString(),QString("Image"));
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        if(auto *sheet=qobject_cast<QQuickItem*>(state.get()))sheet->setParentItem(nullptr);panel->setParentItem(nullptr);panelState=nullptr;panelCanvas=nullptr;
    }
    void panelControls() {
        QFETCH(int,width);EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Panel"));QVERIFY(shape(canvas,"Rectangle"));QVERIFY(shape(canvas,"Lasso"));
        QQmlEngine engine;QStringList warnings;connect(&engine,&QQmlEngine::warnings,this,[&](const QList<QQmlError> &errors){for(const auto &error:errors)warnings.append(error.toString());});
        const auto path=QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/";
        QQmlComponent stateComponent(&engine,QUrl::fromLocalFile(path+"EditorToolSheet.qml"));
        std::unique_ptr<QObject> state(stateComponent.createWithInitialProperties({{"toolId","background"},{"engine",QVariant::fromValue<QObject*>(&canvas)}}));QVERIFY2(state,qPrintable(stateComponent.errorString()));
        state->setProperty("settingsByTool",QVariantMap{{"background",settings()}});
        panelState=state.get();panelCanvas=&canvas;
        canvas.configureTool("background",state->property("values").toMap());
        QObject::connect(state.get(),SIGNAL(valueEdited(QString,QString,QVariant)),this,SLOT(panelDraftChanged(QString,QString,QVariant)));
        QQmlComponent panelComponent(&engine,QUrl::fromLocalFile(path+"EditorDesktopPanel.qml"));
        std::unique_ptr<QObject> object(panelComponent.createWithInitialProperties({{"toolState",QVariant::fromValue(state.get())}}));QVERIFY2(object,qPrintable(panelComponent.errorString()));
        auto *panel=qobject_cast<QQuickItem*>(object.get());QVERIFY(panel);QQuickWindow window;window.resize(width,720);window.setColor(QColor("#1E1E1E"));panel->setParentItem(window.contentItem());panel->setSize({qreal(width),720});
        QVERIFY(state->setProperty("parent",QVariant::fromValue(window.contentItem())));
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTest::qWait(200);
        auto *body=visual(panel,"editorBackgroundPanel");QVERIFY(body);QCOMPARE(qRound(body->width()),width-24);
        for(const auto &tool:{"Brush","Eraser","Lasso","Polygon","Rectangle","Ellipse","Gradient","Color range"}) {
            auto *button=visual(panel,"maskTool-"+QString(tool));QVERIFY(button);
            QVERIFY(button->property("iconMode").toBool());QVERIFY(button->property("text").toString().isEmpty());
            QCOMPARE(button->property("label").toString(),QString(tool));QCOMPARE(button->height(),30.);
            const auto icon=button->property("resolvedIconSource").toUrl();QVERIFY(!icon.isEmpty());
            const auto file=icon.scheme()=="qrc"?":"+icon.path():icon.toLocalFile();QVERIFY2(!QImage(file).isNull(),qPrintable(file));
        }
        if(width>=402)QVERIFY2(body->implicitHeight()<850,"The icon mask panel must fit the compact Figma layout");
        auto click=[&](QString name){auto *item=visual(panel,name);QVERIFY2(item,qPrintable(name));auto *viewport=visual(panel,"editorDesktopPanelViewport");QVERIFY(viewport);const auto y=item->mapToItem(viewport,{0,0}).y()+viewport->property("contentY").toDouble();viewport->setProperty("contentY",std::clamp(y-200.,0.,viewport->property("contentHeight").toDouble()-viewport->height()));QTest::qWait(30);QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(item->boundingRect().center()).toPoint());};
        for(const auto &tool:{"Brush","Eraser","Lasso","Polygon","Rectangle","Ellipse","Gradient","Color range"}) {
            click("maskTool-"+QString(tool));QCOMPARE(canvas.toolValues("background")["maskTool"].toString(),QString(tool));
        }
        for(const auto &mode:{"Add","Subtract","Intersect","Replace"}) {
            click("maskMode-"+QString(mode));QCOMPARE(canvas.toolValues("background")["maskMode"].toString(),QString(mode));
        }
        click("maskTool-Brush");click("maskMode-Add");
        auto *brushButton=visual(panel,"maskTool-Brush");QVERIFY(brushButton);
        auto *brushTooltip=brushButton->findChild<QObject*>("maskTool-Brush-tooltip");QVERIFY(brushTooltip);
        QTest::mouseMove(&window,brushButton->mapToScene(brushButton->boundingRect().center()).toPoint());
        QTest::qWait(600);QTRY_VERIFY(brushTooltip->property("visible").toBool());
        click("maskTool-Eraser");QCOMPARE(canvas.toolValues("background")["maskTool"].toString(),QString("Eraser"));
        click("maskTool-Brush");
        auto *keyboardTool=visual(panel,"maskTool-Polygon");keyboardTool->forceActiveFocus();
        QTest::keyPress(&window,Qt::Key_Space);QCOMPARE(canvas.toolValues("background")["maskTool"].toString(),QString("Brush"));
        QTest::keyRelease(&window,Qt::Key_Space);QCOMPARE(canvas.toolValues("background")["maskTool"].toString(),QString("Polygon"));
        click("maskTool-Brush");
        auto *pressure=visual(panel,"maskToggle-maskPressure");QVERIFY(pressure);
        const bool pressureBefore=canvas.toolValues("background")["maskPressure"].toBool();
        click("maskToggle-maskPressure");QCOMPARE(canvas.toolValues("background")["maskPressure"].toBool(),!pressureBefore);
        click("maskToggle-maskPressure");
        for(const auto &field:{"maskSize","maskHardness","maskFlow","maskOpacity","feather","density"}) {
            auto *slider=visual(panel,"maskSlider-"+QString(field));auto *entry=visual(panel,"maskNumber-"+QString(field));QVERIFY(slider);QVERIFY(entry);
            QVERIFY(slider->width()>40);QVERIFY(std::abs(slider->mapToItem(body,{0,slider->height()/2}).y()-entry->mapToItem(body,{0,entry->height()/2}).y())<1);
        }
        auto number=[&](const QString &field,const QString &text){click("maskNumber-"+field);QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);for(const auto c:text)QTest::keyClick(&window,c.toLatin1());QTest::keyClick(&window,Qt::Key_Return);};
        number("density","0");QTRY_COMPARE(canvas.maskPanelInfo()["selected"].toMap()["density"].toDouble(),0.);
        number("feather","5.5");QTRY_COMPARE(canvas.maskPanelInfo()["selected"].toMap()["feather"].toDouble(),5.5);
        click("maskAction-reset");
        const auto before=appearance(canvas).maskStack.size();click("maskNew");QTRY_COMPARE(appearance(canvas).maskStack.size(),before+1);
        click("maskDuplicate");QTRY_COMPARE(appearance(canvas).maskStack.size(),before+2);click("maskDelete");QTRY_COMPARE(appearance(canvas).maskStack.size(),before+1);
        click("maskAction-apply");QVERIFY(appearance(canvas).maskStackEnabled);click("maskAction-invert");QVERIFY(canvas.maskPanelInfo()["selected"].toMap()["inverted"].toBool());
        click("maskAction-reset");QCOMPARE(canvas.maskPanelInfo()["selected"].toMap()["density"].toDouble(),100.);
        const auto selectedId=canvas.maskPanelInfo()["selected"].toMap()["id"].toString();
        click("maskMore-"+selectedId);QTRY_VERIFY(menuEntry(window.contentItem(),"Rename"));
        auto *menu=body->findChild<QObject*>("maskOptions");QVERIFY(menu);QSignalSpy triggers(menu,SIGNAL(itemTriggered(int,QVariant)));
        QTRY_VERIFY(menu->property("opened").toBool());
        auto *rename=menuEntry(window.contentItem(),"Rename");QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,rename->mapToScene(rename->boundingRect().center()).toPoint());
        QTRY_COMPARE(triggers.size(),1);
        auto *renameSheet=body->findChild<QObject *>("maskRenameSheet");QVERIFY(renameSheet);QTRY_VERIFY(renameSheet->property("visible").toBool());
        auto *renameContent=renameSheet->property("loadedContent").value<QObject*>();QVERIFY(renameContent);
        auto *renameInput=qobject_cast<QQuickItem *>(renameContent->findChild<QObject*>("maskRenameInput"));QVERIFY(renameInput);
        QTest::qWait(350);
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,renameInput->mapToScene(renameInput->boundingRect().center()).toPoint());QTest::keyClick(&window,Qt::Key_A,Qt::ControlModifier);
        for(const auto c:QString("Details"))QTest::keyClick(&window,c.toLatin1());QTest::keyClick(&window,Qt::Key_Return);
        QTRY_COMPARE(canvas.maskPanelInfo()["selected"].toMap()["name"].toString(),QString("Details"));QTRY_VERIFY(!renameSheet->property("visible").toBool());
        click("maskVisible-"+selectedId);QVERIFY(!canvas.maskPanelInfo()["selected"].toMap()["visible"].toBool());QTest::qWait(250);
        auto *visibility=visual(panel,"maskVisible-"+selectedId);QVERIFY(visibility);QVERIFY(visibility->property("text").toString().isEmpty());QCOMPARE(visibility->property("iconName").toString(),QString("hide"));
        QVERIFY(visibility->property("label").toString().contains("Details"));
        QSignalSpy visibilityClicks(visibility,SIGNAL(clicked()));
        QSignalSpy visibilityDoubles(visibility,SIGNAL(doubleClicked()));
        click("maskVisible-"+selectedId);
        QCOMPARE(visibilityClicks.size()+visibilityDoubles.size(),1);
        QTRY_VERIFY(canvas.maskPanelInfo()["selected"].toMap()["visible"].toBool());
        click("maskCombine");QTRY_VERIFY(menu->property("opened").toBool());QTRY_VERIFY(menuEntry(window.contentItem(),"Intersect"));auto *intersection=menuEntry(window.contentItem(),"Intersect");QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,intersection->mapToScene(intersection->boundingRect().center()).toPoint());
        QTRY_COMPARE(canvas.maskPanelInfo()["selected"].toMap()["combine"].toString(),QString("Intersect"));QTRY_VERIFY(!menu->property("visible").toBool());QTest::qWait(350);click("maskAction-reset");QTRY_COMPARE(canvas.maskPanelInfo()["selected"].toMap()["combine"].toString(),QString("Add"));

        QList<QQuickItem *> items;collect(body,items);for(auto *item:items)if(item->isVisible()&&item->parentItem()&&item->objectName().startsWith("mask")&&item->width()>0)QVERIFY2(item->x()+item->width()<=item->parentItem()->width()+1,qPrintable(item->objectName()));
        const auto compactOutput=QString(DREAMSCAPES_TEST_DIRECTORY)+"/mask-icons-verification";QVERIFY(QDir().mkpath(compactOutput));
        const auto full=body->grabToImage();QVERIFY(full);QSignalSpy fullReady(full.get(),&QQuickItemGrabResult::ready);QVERIFY(fullReady.wait(5000));QVERIFY(full->image().save(compactOutput+QString("/mask-panel-%1.png").arg(width)));
        auto *viewport=visual(panel,"editorDesktopPanelViewport");const auto output=QString(DREAMSCAPES_TEST_DIRECTORY)+"/mask-panel-verification";QVERIFY(QDir().mkpath(output));
        if(width==432)for(int section=0;section<3;++section){viewport->setProperty("contentY",section*(viewport->property("contentHeight").toDouble()-viewport->height())/2);QTest::qWait(200);const auto image=window.grabWindow();QVERIFY(!image.isNull());QVERIFY(image.save(output+QString("/mask-panel-%1.png").arg(section)));}
        QVERIFY2(warnings.isEmpty(),qPrintable(warnings.join('\n')));
        if(auto *sheet=qobject_cast<QQuickItem*>(state.get()))sheet->setParentItem(nullptr);panel->setParentItem(nullptr);
    }
    void panelDraftChanged(const QString &tool,const QString &field,const QVariant &value) {if(panelCanvas&&panelState){panelCanvas->configureTool(tool,panelState->property("values").toMap());panelCanvas->applyToolField(tool,field,value);}}
};
int main(int argc,char **argv) {QQuickStyle::setStyle("Basic");QApplication app(argc,argv);registerStorageViewTypes();MaskPanelTests tests;return QTest::qExec(&tests,argc,argv);}
#include "tst_maskpanel.moc"
