#include "Views/Editor/EditorTimelineModel.h"
#include "Views/Editor/EditorCanvas.h"
#include <iiSharedCanvas.h>
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QScopeGuard>
#include <SocietyDrive.h>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace iiSharedCanvas;
namespace {
Document fixture() {
    Document d; d.extent={8,8}; d.timeline={{24,1},6};
    d.assets.emplace_back(RasterAsset{"still",makeRasterLayer(8,8,0xffff0000)});
    LayerProperties still; still.id="still-layer"; still.name="Still image"; still.frameRange=LayerFrameRange{1,4};
    d.layers.emplace_back(StaticBitmapLayer{still,StaticSource{"still"}});
    LayerProperties moving; moving.id="moving-layer"; moving.name="Frame animation"; moving.frameRange=LayerFrameRange{0,5};
    DynamicBitmapLayer dynamic; dynamic.properties=moving;
    for(FrameIndex f=0;f<6;++f) {
        const auto id="frame-"+std::to_string(f);
        d.assets.emplace_back(RasterAsset{id,makeRasterLayer(8,8,0xff000000U|((f+1)*0x00112233U))});
        dynamic.content.frameIndices.push_back(f); d.frames.push_back({f,{{"moving-layer",id}}});
    }
    d.layers.emplace_back(std::move(dynamic));
    d.audioAssets.push_back({"audio",48000,1,std::vector<std::int16_t>(12000,8192)});
    d.audioTracks.push_back({"audio-track","Actual audio",false,0,{{"audio-clip","Sound","audio",1,3,0,0,true}}});
    return d;
}
QVariantMap find(const QVariantList &rows,const QString &id) {
    for(const auto &row:rows) if(row.toMap().value("id").toString()==id)return row.toMap();
    return {};
}
QImage thumbnail(const QUrl &source) {
    const auto text=source.toString();
    return QImage::fromData(QByteArray::fromBase64(text.mid(text.indexOf(',')+1).toLatin1()));
}
QQuickItem *visualItem(QQuickItem *root,const QString &name) {
    if(root->objectName()==name)return root;
    for(auto *child:root->childItems())if(auto *found=visualItem(child,name))return found;
    return nullptr;
}
}
class EditorLayerTimelineTests : public QObject {
    Q_OBJECT
private slots:
    void readsNativeLayersAndTiming();
    void resolvesEachDynamicFrame();
    void vectorVideoAndMotionSources();
    void timingEditsPersistAndUndo();
    void parallelLayersDoNotBecomeSerialShots();
    void splitDuplicateAndRemove();
    void audioRowsUseNativeClips();
    void nativeTrackManagement();
    void frameSourceImportAndFilePersistence();
    void linkedMovementAndRangeSelection();
    void audioEditTools();
    void controlsFollowDocumentAndSelection();
    void staticLayerDefaultDuration_data();
    void staticLayerDefaultDuration();
    void newStaticLayersUseCurrentFps();
    void staticImportDurationPersists();
    void controlRowsClipOverflow_data();
    void controlRowsClipOverflow();
    void toolSettingsDoNotRebuildTimeline();
    void interactionPerformance();
    void rulerGeometry_data();
    void rulerGeometry();
    void rulerReferenceStyle();
    void transparentBackground();
    void responsiveLayout_data();
    void responsiveLayout();
};
void EditorLayerTimelineTests::readsNativeLayersAndTiming() {
    EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(fixture(),"Fixture"));
    EditorTimelineModel model; model.setCanvas(&canvas);
    QCOMPARE(model.selectedClip()["id"].toString(),canvas.selectedLayerId());
    QCOMPARE(model.tracks().size(),3);
    QCOMPARE(model.tracks()[0].toMap()["id"].toString(),QString("moving-layer"));
    QCOMPARE(model.tracks()[0].toMap()["type"].toString(),QString("Layer"));
    QCOMPARE(model.tracks()[2].toMap()["type"].toString(),QString("Audio"));
    const auto still=find(model.clips(),"still-layer"),moving=find(model.clips(),"moving-layer");
    QCOMPARE(still["in"].toInt(),1); QCOMPARE(still["out"].toInt(),5); QCOMPARE(still["frames"].toInt(),4);
    QCOMPARE(still["timing"].toString(),QString("Static"));
    QCOMPARE(moving["timing"].toString(),QString("Dynamic")); QCOMPARE(moving["frames"].toInt(),6);
    QCOMPARE(model.keys().size(),6); QCOMPARE(model.totalFrames(),6); QCOMPARE(model.fps(),24.0);
    QVERIFY(model.addLayer("Static vector","New vector")); QCOMPARE(model.tracks().size(),4);
    QVERIFY(std::holds_alternative<StaticVectorLayer>(canvas.document()->layers.back()));
    QVERIFY(canvas.addPaintLayer()); QCOMPARE(model.tracks().size(),5);
    Document empty; empty.extent={8,8}; QVERIFY(canvas.loadProjectPage(empty,"Empty"));
    QCOMPARE(model.tracks().size(),0); QCOMPARE(model.clips().size(),0);
}
void EditorLayerTimelineTests::resolvesEachDynamicFrame() {
    EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(fixture(),"Fixture"));
    EditorTimelineModel model; model.setCanvas(&canvas);
    const auto first=thumbnail(model.thumbnail("moving-layer",0)); QVERIFY(!first.isNull());
    for(int f=0;f<6;++f) {
        model.seek(f); QCOMPARE(int(canvas.frame()),f);
        const auto image=thumbnail(model.thumbnail("moving-layer",f)); QVERIFY(!image.isNull());
        QCOMPARE(image.pixelColor(image.width()/2,image.height()/2).rgba(),QRgb(0xff000000U|((f+1)*0x00112233U)));
    }
    const auto still1=thumbnail(model.thumbnail("still-layer",1)),still4=thumbnail(model.thumbnail("still-layer",4));
    QCOMPARE(still1,still4); QVERIFY(!still1.isNull());
    const auto encoded=encodeIisc(*canvas.document()); QVERIFY(encoded.ok()); const auto decoded=decodeIisc(encoded.bytes); QVERIFY(decoded.ok());
    EditorCanvas reopened; QVERIFY(reopened.loadProjectPage(decoded.document,"Reopened"));
    EditorTimelineModel other; other.setCanvas(&reopened);
    for(int f=5;f>=0;--f) QCOMPARE(other.thumbnail("moving-layer",f),model.thumbnail("moving-layer",f));
}
void EditorLayerTimelineTests::timingEditsPersistAndUndo() {
    EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(fixture(),"Fixture"));
    EditorTimelineModel model; model.setCanvas(&canvas); QVERIFY(model.selectClip("still-layer"));
    QVERIFY(model.setRange("still-layer",2,8)); QCOMPARE(model.totalFrames(),8);
    QCOMPARE(layerProperties(*findLayer(*canvas.document(),"still-layer")).frameRange->firstFrame,FrameIndex(2));
    QCOMPARE(layerProperties(*findLayer(*canvas.document(),"still-layer")).frameRange->lastFrame,FrameIndex(7));
    const auto encoded=encodeIisc(*canvas.document()); QVERIFY(encoded.ok()); const auto decoded=decodeIisc(encoded.bytes); QVERIFY(decoded.ok());
    const auto &range=layerProperties(*findLayer(decoded.document,"still-layer")).frameRange;
    QVERIFY(range); QCOMPARE(range->lastFrame,FrameIndex(7));
    QVERIFY(model.canUndo()); model.undo(); QCOMPARE(find(model.clips(),"still-layer")["out"].toInt(),5); QCOMPARE(model.totalFrames(),6);
    model.redo(); QCOMPARE(find(model.clips(),"still-layer")["out"].toInt(),8);
    const auto before=encodeIisc(*canvas.document()).bytes;
    QVERIFY(!model.setRange("still-layer",4,4)); QVERIFY(!model.setRange("still-layer",-1,2));
    QCOMPARE(encodeIisc(*canvas.document()).bytes,before);
    QVERIFY(model.setClipVisible("still-layer",false)); QVERIFY(!layerProperties(*findLayer(*canvas.document(),"still-layer")).visible);
    model.undo(); QVERIFY(layerProperties(*findLayer(*canvas.document(),"still-layer")).visible);
}
void EditorLayerTimelineTests::vectorVideoAndMotionSources() {
    Document d;d.extent={8,8};d.timeline={{24,1},6};
    DynamicVectorLayer vector;vector.properties.id="vector";vector.properties.name="Animated geometry";
    for(FrameIndex f=0;f<6;++f) {
        VectorPath path;path.commands={MoveTo{{0,0}},LineTo{{8,0}},LineTo{{8,8}},LineTo{{0,8}},ClosePath{}};
        path.fill=SolidPaint{0xff000000U|((f+1)*0x00112233U)};
        const auto asset="vector-"+std::to_string(f);d.assets.emplace_back(VectorAsset{asset,{8,8},{path}});
        vector.content.frameIndices.push_back(f);d.frames.push_back({f,{{"vector",asset}}});
    }
    d.layers.emplace_back(vector);
    d.assets.emplace_back(VideoAsset{"video-source",{24,1},{makeRasterLayer(8,8,0xffff0000),makeRasterLayer(8,8,0xff00ff00),makeRasterLayer(8,8,0xff0000ff)}});
    LayerProperties video;video.id="video";video.name="Native video";video.frameRange=LayerFrameRange{1,4};
    d.layers.emplace_back(VideoLayer{video,StaticSource{"video-source"},{0,{},VideoEndBehavior::Hold}});
    MotionKeyframe first,last;first.value.opacity=.25;first.interpolation=MotionInterpolation::SmoothStep;last.frame=5;
    layerProperties(d.layers.front()).motion={first,last};
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Vector and video"));
    EditorTimelineModel model;model.setCanvas(&canvas);
    QCOMPARE(find(model.clips(),"vector")["representation"].toString(),QString("Vector"));
    QCOMPARE(find(model.clips(),"video")["timing"].toString(),QString("Dynamic"));
    QCOMPARE(thumbnail(model.thumbnail("video",1)).pixelColor(32,32),QColor(Qt::red));
    QCOMPARE(thumbnail(model.thumbnail("video",2)).pixelColor(32,32),QColor(Qt::green));
    QCOMPARE(thumbnail(model.thumbnail("video",4)).pixelColor(32,32),QColor(Qt::blue));
    const auto a=thumbnail(model.thumbnail("vector",0)),b=thumbnail(model.thumbnail("vector",5));QVERIFY(a!=b);
    QVERIFY(model.selectKey("vector:motion:0"));QVERIFY(model.setKeyOpacity("vector:motion:0",.5));
    QCOMPARE(layerProperties(canvas.document()->layers.front()).motion.front().value.opacity,.5);
    QVERIFY(model.moveKey("vector:motion:0",1));QCOMPARE(layerProperties(canvas.document()->layers.front()).motion.front().frame,FrameIndex(1));
    model.undo();QCOMPARE(layerProperties(canvas.document()->layers.front()).motion.front().frame,FrameIndex(0));
    QVERIFY(model.removeKey("vector:motion:5"));QCOMPARE(layerProperties(canvas.document()->layers.front()).motion.size(),std::size_t(1));
    model.undo();QVERIFY(model.setSourceFrame("video",1));
    QCOMPARE(thumbnail(model.thumbnail("video",1)).pixelColor(32,32),QColor(Qt::green));
    QVERIFY(validate(*canvas.document()).ok());
}
void EditorLayerTimelineTests::parallelLayersDoNotBecomeSerialShots() {
    EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(fixture(),"Fixture"));
    EditorTimelineModel model; model.setCanvas(&canvas);
    QCOMPARE(model.totalFrames(),6); QCOMPARE(find(model.clips(),"still-layer")["in"].toInt(),1);
    QVERIFY2(model.moveClip("moving-layer",2),qPrintable(model.error())); QCOMPARE(find(model.clips(),"moving-layer")["in"].toInt(),2);
    QCOMPARE(find(model.clips(),"still-layer")["in"].toInt(),1); QCOMPARE(model.totalFrames(),8);
    const auto *layer=findLayer(*canvas.document(),"moving-layer");
    QCOMPARE(assetId(*resolveAssetAt(*canvas.document(),*layer,2)),std::string("frame-0"));
    QCOMPARE(assetId(*resolveAssetAt(*canvas.document(),*layer,7)),std::string("frame-5"));
    model.undo(); QCOMPARE(model.totalFrames(),6); QCOMPARE(find(model.clips(),"moving-layer")["in"].toInt(),0);
}
void EditorLayerTimelineTests::splitDuplicateAndRemove() {
    EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(fixture(),"Fixture"));
    EditorTimelineModel model; model.setCanvas(&canvas); QVERIFY(model.selectClip("moving-layer")); model.seek(3);
    QVERIFY(model.split()); QCOMPARE(canvas.document()->layers.size(),std::size_t(3)); QCOMPARE(model.totalFrames(),6);
    QCOMPARE(find(model.clips(),"moving-layer")["out"].toInt(),3);
    const auto right=model.selectedClip(); QCOMPARE(right["in"].toInt(),3); QCOMPARE(right["out"].toInt(),6);
    const auto *rightLayer=findLayer(*canvas.document(),right["id"].toString().toStdString()); QVERIFY(rightLayer);
    QVERIFY(resolveAssetAt(*canvas.document(),*rightLayer,4));
    model.undo(); QCOMPARE(canvas.document()->layers.size(),std::size_t(2)); QCOMPARE(find(model.clips(),"moving-layer")["out"].toInt(),6);
    QVERIFY(model.selectClip("still-layer")); QVERIFY(model.duplicateSelected()); QCOMPARE(canvas.document()->layers.size(),std::size_t(3));
    QVERIFY(model.removeSelected()); QCOMPARE(canvas.document()->layers.size(),std::size_t(2));
    model.undo(); QCOMPARE(canvas.document()->layers.size(),std::size_t(3));
}
void EditorLayerTimelineTests::audioRowsUseNativeClips() {
    EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(fixture(),"Fixture"));
    EditorTimelineModel model; model.setCanvas(&canvas);
    const auto audio=find(model.clips(),"audio-clip"); QCOMPARE(audio["trackId"].toString(),QString("audio-track"));
    QCOMPARE(audio["in"].toInt(),1); QCOMPARE(audio["out"].toInt(),4); QVERIFY(!audio["waveform"].toList().isEmpty());
    QVERIFY(model.setTrackMuted("audio-track",true)); QVERIFY(canvas.document()->audioTracks[0].muted);
    model.undo(); QVERIFY(!canvas.document()->audioTracks[0].muted);
}
void EditorLayerTimelineTests::nativeTrackManagement() {
    EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(fixture(),"Fixture"));
    EditorTimelineModel model;model.setCanvas(&canvas);
    QVERIFY(model.addAudioTrack());const auto audioId=model.selectedTrackId();
    QCOMPARE(model.selectedIds().size(),0);QCOMPARE(canvas.document()->audioTracks.size(),std::size_t(2));
    QVERIFY(model.renameTrack(audioId,"Voice"));QCOMPARE(find(model.tracks(),audioId)["name"].toString(),QString("Voice"));
    QVERIFY(model.moveTrack(audioId,-1));QCOMPARE(canvas.document()->audioTracks.front().id,audioId.toStdString());
    QVERIFY(model.duplicateSelected());QCOMPARE(canvas.document()->audioTracks.size(),std::size_t(3));
    const auto duplicate=model.selectedTrackId();QVERIFY(duplicate!=audioId);
    QVERIFY(model.removeTrack(duplicate));QCOMPARE(canvas.document()->audioTracks.size(),std::size_t(2));
    model.undo();QCOMPARE(canvas.document()->audioTracks.size(),std::size_t(3));
    QVERIFY(model.setTrackLocked("still-layer",true));
    const auto before=encodeIisc(*canvas.document()).bytes;
    QVERIFY(!model.setRange("still-layer",0,3));QVERIFY(!model.removeTrack("still-layer"));
    QCOMPARE(encodeIisc(*canvas.document()).bytes,before);
    QVERIFY(model.setTrackLocked("still-layer",false));
    QVERIFY(model.setTrackSolo("still-layer"));QVERIFY(!layerProperties(*findLayer(*canvas.document(),"moving-layer")).visible);
    QVERIFY(canvas.document()->audioTracks.front().muted);
    model.undo();QVERIFY(!find(model.tracks(),"still-layer")["solo"].toBool());
    QVERIFY(layerProperties(*findLayer(*canvas.document(),"moving-layer")).visible);
    model.redo();QVERIFY(find(model.tracks(),"still-layer")["solo"].toBool());
    QVERIFY(model.setTrackSolo("still-layer"));QVERIFY(layerProperties(*findLayer(*canvas.document(),"moving-layer")).visible);
    QVERIFY(!canvas.document()->audioTracks.front().muted);
    QVERIFY(model.moveTrack("still-layer",-1));
    QCOMPARE(layerProperties(canvas.document()->layers.back()).id,std::string("still-layer"));
    QVERIFY(!model.setSourceFrame("missing",2));
}
void EditorLayerTimelineTests::frameSourceImportAndFilePersistence() {
    QTemporaryDir files(QString(DREAMSCAPES_TEST_DIRECTORY)+"/native-layer-file-XXXXXX");QVERIFY(files.isValid());
    const auto path=files.filePath("working.iisc");
    DocumentFile working;QVERIFY(working.create(path.toStdString(),fixture()).ok());working.close();
    EditorCanvas canvas;QVERIFY(canvas.openDocumentSource(QUrl::fromLocalFile(path)));
    EditorTimelineModel model;model.setCanvas(&canvas);
    QImage green(8,8,QImage::Format_ARGB32);green.fill(Qt::green);
    const auto imagePath=files.filePath("replacement.png");QVERIFY(green.save(imagePath));
    const auto original=model.thumbnail("moving-layer",1);
    QVERIFY2(model.importFrameSource("moving-layer",2,QUrl::fromLocalFile(imagePath)),qPrintable(model.error()));
    QCOMPARE(thumbnail(model.thumbnail("moving-layer",2)).pixelColor(32,32),QColor(Qt::green));
    QCOMPARE(model.thumbnail("moving-layer",1),original);
    QVERIFY(model.setRange("still-layer",2,6));QVERIFY(canvas.saveDocument());
    EditorCanvas reopened;QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path)));
    EditorTimelineModel restored;restored.setCanvas(&reopened);
    QCOMPARE(find(restored.clips(),"still-layer")["in"].toInt(),2);
    QCOMPARE(find(restored.clips(),"still-layer")["out"].toInt(),6);
    QCOMPARE(thumbnail(restored.thumbnail("moving-layer",2)).pixelColor(32,32),QColor(Qt::green));
    QCOMPARE(restored.thumbnail("moving-layer",1),original);
    QCOMPARE(canvas.filePath(),path);
}
void EditorLayerTimelineTests::linkedMovementAndRangeSelection() {
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Fixture"));
    EditorTimelineModel model;model.setCanvas(&canvas);model.setSnap(false);
    QVERIFY(model.selectClip("still-layer"));QVERIFY(model.selectClip("moving-layer",true));
    QVERIFY(model.dragClip("moving-layer",2));
    QCOMPARE(find(model.clips(),"still-layer")["in"].toInt(),3);
    QCOMPARE(find(model.clips(),"moving-layer")["in"].toInt(),2);QCOMPARE(model.selectedIds().size(),2);
    model.undo();model.setLinked(false);QVERIFY(model.selectClip("moving-layer"));
    QVERIFY(model.dragClip("moving-layer",1));QCOMPARE(find(model.clips(),"still-layer")["in"].toInt(),1);
    model.undo();model.setSnap(true);model.seek(0);
    auto single=fixture();single.layers.erase(single.layers.begin()+1);single.frames.clear();single.audioTracks.clear();
    QVERIFY(canvas.loadProjectPage(single,"Snap"));QVERIFY(model.selectClip("still-layer"));
    QVERIFY(model.dragClip("still-layer",2,{},1));QCOMPARE(find(model.clips(),"still-layer")["out"].toInt(),6);
    QVERIFY(canvas.loadProjectPage(fixture(),"Range"));
    model.markRange(3,2);model.selectMarkedRange();
    QVERIFY(model.selectedIds().contains("moving-layer"));QVERIFY(model.selectedIds().contains("audio-clip"));
    QVERIFY(!model.toolAvailable("Roll"));QVERIFY(!model.toolAvailable("Slide"));
}
void EditorLayerTimelineTests::audioEditTools() {
    auto d=fixture();d.timeline.frameCount=12;
    d.audioAssets.front().samples.resize(24000,8192);
    d.audioTracks.front().clips={{"previous","Previous","audio",0,3,0,0,true},{"middle","Middle","audio",3,3,6000,0,true},{"next","Next","audio",6,3,12000,0,true}};
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Audio"));
    EditorTimelineModel model;model.setCanvas(&canvas);model.setSnap(false);QVERIFY(model.selectClip("middle"));
    QVERIFY(model.toolAvailable("Roll"));QVERIFY(model.toolAvailable("Slide"));
    model.setTool("Roll");QVERIFY(model.dragClip("middle",1));
    QCOMPARE(find(model.clips(),"middle")["out"].toInt(),7);QCOMPARE(find(model.clips(),"next")["in"].toInt(),7);
    QCOMPARE(canvas.document()->audioTracks.front().clips.back().sourceOffsetSamples,std::uint64_t(14000));
    model.undo();QVERIFY(model.selectClip("middle"));model.setTool("Slide");QVERIFY(model.dragClip("middle",1));
    QCOMPARE(find(model.clips(),"previous")["out"].toInt(),4);QCOMPARE(find(model.clips(),"middle")["in"].toInt(),4);
    QCOMPARE(find(model.clips(),"middle")["frames"].toInt(),3);QCOMPARE(find(model.clips(),"next")["in"].toInt(),7);
    model.undo();QVERIFY(model.selectClip("middle"));model.setTool("Ripple");QVERIFY(model.dragClip("middle",1));
    QCOMPARE(find(model.clips(),"next")["in"].toInt(),7);QCOMPARE(find(model.clips(),"still-layer")["in"].toInt(),1);
    model.undo();QVERIFY(model.selectClip("middle"));model.seek(4);QCOMPARE(model.selectedClip()["id"].toString(),QString("middle"));QVERIFY2(model.split(),qPrintable(model.error()));
    QCOMPARE(canvas.document()->audioTracks.front().clips.size(),std::size_t(4));
    QCOMPARE(model.selectedClip()["in"].toInt(),4);QCOMPARE(model.selectedClip()["frames"].toInt(),2);
    QVERIFY(validate(*canvas.document()).ok());
    model.undo();model.setTool("Select");model.setLinked(false);QVERIFY(model.selectClip("previous"));
    QVERIFY(model.dragClip("previous",9));
    QCOMPARE(canvas.document()->audioTracks.front().clips.back().id,std::string("previous"));
    QCOMPARE(canvas.document()->audioTracks.front().clips.back().startFrame,FrameIndex(9));
    QVERIFY(validate(*canvas.document()).ok());
}
void EditorLayerTimelineTests::staticLayerDefaultDuration_data() {
    QTest::addColumn<int>("numerator");QTest::addColumn<int>("denominator");QTest::addColumn<int>("frames");
    QTest::newRow("24fps")<<24<<1<<24;QTest::newRow("30fps")<<30<<1<<30;QTest::newRow("60fps")<<60<<1<<60;
    QTest::newRow("23.976fps")<<24000<<1001<<24;QTest::newRow("29.97fps")<<30000<<1001<<30;
}
void EditorLayerTimelineTests::staticLayerDefaultDuration() {
    QFETCH(int,numerator);QFETCH(int,denominator);QFETCH(int,frames);
    Document d;d.extent={8,8};d.timeline.frameRate={std::uint32_t(numerator),std::uint32_t(denominator)};
    d.assets.emplace_back(RasterAsset{"still",makeRasterLayer(8,8,0xffc04080)});
    d.layers.emplace_back(StaticBitmapLayer{{"still-layer","Static image"},StaticSource{"still"}});
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Default still"));EditorTimelineModel model;model.setCanvas(&canvas);
    QCOMPARE(model.totalFrames(),frames);QCOMPARE(model.clips().first().toMap()["frames"].toInt(),frames);
    QCOMPARE(canvas.document()->assets.size(),std::size_t(1));QVERIFY(canvas.document()->frames.empty());
    for(int frame=0;frame<frames;++frame){model.seek(frame);QCOMPARE(int(canvas.frame()),frame);const auto result=renderFrame(*canvas.document(),FrameIndex(frame));QVERIFY(result.ok());QCOMPARE(result.pixels.pixels.front(),0xffc04080U);}
    const auto encoded=encodeIisc(*canvas.document());QVERIFY(encoded.ok());const auto decoded=decodeIisc(encoded.bytes);QVERIFY(decoded.ok());
    QCOMPARE(decoded.document.timeline.frameCount,FrameIndex(frames));QCOMPARE(decoded.document.assets.size(),std::size_t(1));QVERIFY(decoded.document.frames.empty());
    const auto &range=layerProperties(decoded.document.layers.front()).frameRange;QVERIFY(range);QCOMPARE(range->lastFrame,FrameIndex(frames-1));
    // An explicitly authored short still remains a supported edit.
    d.layers.front()=StaticBitmapLayer{{"still-layer","Trimmed image"},StaticSource{"still"}};
    layerProperties(d.layers.front()).frameRange=LayerFrameRange{0,0};
    EditorCanvas trimmed;QVERIFY(trimmed.loadProjectPage(d,"Explicit one frame"));QCOMPARE(trimmed.document()->timeline.frameCount,1U);
    QVERIFY(model.setRange("still-layer",0,1));QCOMPARE(model.selectedClip()["frames"].toInt(),1);model.undo();QCOMPARE(model.selectedClip()["frames"].toInt(),frames);
}
void EditorLayerTimelineTests::newStaticLayersUseCurrentFps() {
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Mixed timeline"));EditorTimelineModel model;model.setCanvas(&canvas);
    QVERIFY(model.setFrameRate(60));QVERIFY(model.addLayer("Static bitmap","One second"));const auto id=model.selectedClip()["id"].toString();
    QCOMPARE(model.totalFrames(),60);QCOMPARE(model.selectedClip()["frames"].toInt(),60);
    QCOMPARE(find(model.clips(),"moving-layer")["frames"].toInt(),6);QCOMPARE(find(model.clips(),"still-layer")["frames"].toInt(),4);
    QCOMPARE(find(model.clips(),"audio-clip")["frames"].toInt(),3);QCOMPARE(canvas.document()->frames.size(),std::size_t(6));
    model.undo();QCOMPARE(model.totalFrames(),6);QVERIFY(find(model.clips(),id).isEmpty());model.redo();QCOMPARE(find(model.clips(),id)["frames"].toInt(),60);
    EditorCanvas paint;QVERIFY(paint.createCanvas({{"width",8},{"height",8},{"unit","px"},{"background","Transparent"}}));
    EditorTimelineModel paintModel;paintModel.setCanvas(&paint);QVERIFY(paintModel.setFrameRate(30));QVERIFY(paint.addPaintLayer());
    QCOMPARE(paintModel.totalFrames(),30);QCOMPARE(paintModel.clips().first().toMap()["frames"].toInt(),30);
    QVERIFY(paintModel.addLayer("Static vector"));QCOMPARE(paintModel.selectedClip()["frames"].toInt(),30);
    EditorCanvas background;QVERIFY(background.createCanvas({{"width",8},{"height",8},{"unit","px"},{"background","White"}}));
    QCOMPARE(background.document()->timeline.frameCount,24U);
    // Extending a timeline for a new still does not extend an existing implicit animation.
    auto implicit=fixture();layerProperties(implicit.layers[1]).frameRange.reset();
    EditorCanvas animation;QVERIFY(animation.loadProjectPage(implicit,"Implicit animation"));EditorTimelineModel animationModel;animationModel.setCanvas(&animation);
    QVERIFY(animationModel.addLayer("Static vector"));QCOMPARE(animationModel.totalFrames(),24);QCOMPARE(find(animationModel.clips(),"moving-layer")["frames"].toInt(),6);
}
void EditorLayerTimelineTests::staticImportDurationPersists() {
    QTemporaryDir temp(QString(DREAMSCAPES_TEST_DIRECTORY)+"/static-duration-XXXXXX");QVERIFY(temp.isValid());
    const auto storage=temp.path()+"/storage.societycontainer";QVERIFY(QDir().mkpath(storage));QString storageError;
    QVERIFY2(iiSocietyContainer::SocietyDrive::create(storage,&storageError),qPrintable(storageError));
    const auto old=qgetenv("SOCIETY_CONTAINER_PATH");qputenv("SOCIETY_CONTAINER_PATH",storage.toUtf8());
    const auto restore=qScopeGuard([&]{if(old.isEmpty())qunsetenv("SOCIETY_CONTAINER_PATH");else qputenv("SOCIETY_CONTAINER_PATH",old);});
    const auto source=temp.path()+"/still.png";QImage image(8,8,QImage::Format_ARGB32);image.fill(QColor("#c04080"));QVERIFY(image.save(source));
    EditorCanvas imported;QVERIFY2(imported.openImages({QUrl::fromLocalFile(source)}),qPrintable(imported.error()));QCOMPARE(imported.document()->timeline.frameCount,24U);
    const auto saved=temp.path()+"/saved.iisc";QVERIFY(imported.saveDocumentAs(QUrl::fromLocalFile(saved)));
    EditorCanvas reopened;QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(saved)));QCOMPARE(reopened.document()->timeline.frameCount,24U);
    Document legacy=*imported.document();legacy.timeline.frameCount=1;for(auto &layer:legacy.layers)layerProperties(layer).frameRange.reset();
    const auto encoded=encodeIisc(legacy);QVERIFY(encoded.ok());const auto path=temp.path()+"/legacy.iisc";
    {QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(reinterpret_cast<const char *>(encoded.bytes.data()),qint64(encoded.bytes.size())),qint64(encoded.bytes.size()));}
    EditorCanvas copy;QVERIFY(copy.openDocumentSource(QUrl::fromLocalFile(path),true));QCOMPARE(copy.document()->timeline.frameCount,24U);
    {QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray(reinterpret_cast<const char *>(encoded.bytes.data()),qsizetype(encoded.bytes.size())));}
    EditorCanvas migrated;QVERIFY2(migrated.openDocumentSource(QUrl::fromLocalFile(path)),qPrintable(migrated.error()));QCOMPARE(migrated.document()->timeline.frameCount,24U);QVERIFY(migrated.documentSynchronized());
    EditorCanvas persisted;QVERIFY(persisted.openDocumentSource(QUrl::fromLocalFile(path)));QCOMPARE(persisted.document()->timeline.frameCount,24U);
    const auto rendered=renderFrame(*persisted.document(),23);QVERIFY(rendered.ok());QCOMPARE(rendered.pixels.pixels.front(),0xffc04080U);
}
void EditorLayerTimelineTests::toolSettingsDoNotRebuildTimeline() {
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Tool settings"));EditorTimelineModel model;model.setCanvas(&canvas);
    const auto tracks=model.tracks(),clips=model.clips(),keys=model.keys();QSignalSpy changed(&model,&EditorTimelineModel::changed);
    canvas.configureTool("brush",{{"field-4",16}});canvas.configureTool("brush",{{"field-4",32}});
    QCOMPARE(changed.count(),0);QCOMPARE(model.tracks(),tracks);QCOMPARE(model.clips(),clips);QCOMPARE(model.keys(),keys);
    QVERIFY(model.selectClip("still-layer"));QVERIFY(model.setTrackLocked("still-layer",true));
    QVERIFY(find(model.clips(),"still-layer")["locked"].toBool());QVERIFY(model.setTrackLocked("still-layer",false));
    const auto original=model.thumbnail("still-layer",1);QVERIFY(!original.isEmpty());
    model.seek(1);QVERIFY(canvas.selectLayer("still-layer"));QVERIFY(canvas.replaceSelectedPixels(makeRasterLayer(8,8,0xff00ff00U)));
    QVERIFY(model.thumbnail("still-layer",1)!=original);
}
void EditorLayerTimelineTests::interactionPerformance() {
    Document d;d.extent={2048,2048};d.timeline.frameCount=24;auto pixels=makeRasterLayer(256,256);
    for(int y=0;y<256;++y)for(int x=0;x<256;++x)pixels.pixels[y*256+x]=0xff000000U|(unsigned(x)<<16)|(unsigned(y)<<8)|unsigned(x^y);
    d.assets.emplace_back(RasterAsset{"image",std::move(pixels)});
    for(int index=0;index<32;++index){LayerProperties p;p.id="layer-"+std::to_string(index);p.name=p.id;p.transform.m11=p.transform.m22=8;d.layers.emplace_back(StaticBitmapLayer{p,StaticSource{"image"}});}
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Interaction benchmark"));EditorTimelineModel model;model.setCanvas(&canvas);
    for(int index=0;index<32;++index)QVERIFY(!model.thumbnail(QString("layer-%1").arg(index),0).isEmpty());
    QSignalSpy changed(&model,&EditorTimelineModel::changed);QElapsedTimer timer;timer.start();
    for(int iteration=0;iteration<20;++iteration){canvas.configureTool("brush",{{"field-4",8+iteration}});for(int index=0;index<32;++index)QVERIFY(!model.thumbnail(QString("layer-%1").arg(index),0).isEmpty());}
    qInfo().noquote()<<QString("PERF timeline_tool_settings iterations=20 layers=32 elapsed_ms=%1 model_notifications=%2").arg(timer.nsecsElapsed()/1000000.0,0,'f',3).arg(changed.count());
}
void EditorLayerTimelineTests::controlRowsClipOverflow_data() {
    QTest::addColumn<int>("width");
    for(const auto width:{320,430,768,900,1200,1568})QTest::newRow(qPrintable(QString::number(width)))<<width;
}
void EditorLayerTimelineTests::controlRowsClipOverflow() {
    QFETCH(int,width);
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Clipped controls"));
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({qreal(width),500});
    QQuickWindow window;window.resize(1768,500);timeline->setParentItem(window.contentItem());window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QTest::qWait(40);
    for(const auto *name:{"editorTimelineHeader","editorTimelineTools","editorTimelinePrecision","editorTimelineFooter"}) {
        auto *row=visualItem(timeline.data(),name);QVERIFY(row);QVERIFY2(row->clip(),name);qreal tallest=0;
        for(auto *child:row->childItems())if(child->isVisible()&&child->width()>0&&child->height()>0){QCOMPARE(child->y(),0.0);tallest=std::max(tallest,child->height());}
        QCOMPARE(row->height(),tallest);
    }
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);
    auto *mode=visualItem(timeline.data(),"editorTimelineMode-Keyframes");QVERIFY(mode);
    const auto center=[&]{return mode->mapToScene({mode->width()/2,mode->height()/2}).toPoint();};
    if(center().x()>timeline->width()) {
        QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,center());QCOMPARE(model->mode(),QString("Layers"));
    }
    timeline->setWidth(1568);QTest::qWait(40);
    QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,center());QCOMPARE(model->mode(),QString("Keyframes"));
}
void EditorLayerTimelineTests::rulerGeometry_data() {
    QTest::addColumn<int>("frames"); QTest::addColumn<int>("width"); QTest::addColumn<qreal>("scale");
    for(const auto frames:{1,6,120}) for(const auto width:{320,900,1568})
        QTest::newRow(qPrintable(QString("%1f-%2px").arg(frames).arg(width)))<<frames<<width<<qreal(1);
    QTest::newRow("zoomed-and-panned")<<120<<900<<qreal(2);
}
void EditorLayerTimelineTests::rulerGeometry() {
    QFETCH(int,frames);QFETCH(int,width);QFETCH(qreal,scale);
    Document document; document.extent={8,8};document.timeline={{24,1},FrameIndex(frames)};
    document.assets.emplace_back(RasterAsset{"ruler-image",makeRasterLayer(8,8,0xffff0000)});
    LayerProperties properties;properties.id="ruler-layer";properties.name="Real image";
    properties.frameRange=LayerFrameRange{0,FrameIndex(frames-1)};
    document.layers.emplace_back(StaticBitmapLayer{properties,StaticSource{"ruler-image"}});
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(document,"Ruler regression"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({qreal(width),500});
    QQuickWindow window;window.resize(width,500);window.setColor(QColor("#1e1e1e"));timeline->setParentItem(window.contentItem());window.show();QTest::qWait(40);
    QVERIFY(timeline->setProperty("timelineScale",scale));
    auto *lanes=visualItem(timeline.data(),"editorTimelineLanes");QVERIFY(lanes);
    QTest::qWait(20);
    if(scale>1)QVERIFY(lanes->setProperty("contentX",lanes->width()/3));
    QList<QQuickItem *> ticks;
    const auto visit=[&](auto &&self,QQuickItem *item)->void {
        if(item->objectName().startsWith("editorTimelineRulerTick-"))ticks.append(item);
        for(auto *child:item->childItems())self(self,child);
    };visit(visit,timeline.data());
    QVERIFY2(ticks.size()>=3,"Short documents still need a readable ruler beyond the playhead badge");
    std::sort(ticks.begin(),ticks.end(),[](auto *a,auto *b){return a->x()<b->x();});
    auto *badge=visualItem(timeline.data(),"editorTimelinePlayheadBadge");QVERIFY(badge);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);
    QCOMPARE(model->totalFrames(),frames);QCOMPARE(model->clips().size(),1);
    QCOMPARE(model->clips().first().toMap()["frames"].toInt(),frames);
    const auto step=timeline->property("tickStep").toInt();QVERIFY(step>=1);
    auto *clip=visualItem(timeline.data(),"editorTimelineClip-ruler-layer");QVERIFY(clip);
    const auto first=ticks[0]->mapToItem(lanes,QPointF());
    const auto second=ticks[1]->mapToItem(lanes,QPointF());
    QVERIFY(second.x()>first.x());
    const auto pixelsPerFrame=(second.x()-first.x())/step;
    QVERIFY(qAbs((clip->width()+4)-frames*pixelsPerFrame)<.01);
    QCOMPARE(clip->mapToItem(lanes,QPointF()).x(),first.x());
    auto *line=visualItem(timeline.data(),"editorTimelineRulerMark-"+QString::number(step));QVERIFY(line);
    QVERIFY(line->height()>=4);QCOMPARE(line->mapToItem(lanes,QPointF()).x(),second.x());
    if(scale==1) {
        auto *label=visualItem(timeline.data(),"editorTimelineRulerLabel-"+QString::number(step));QVERIFY(label);
        const auto labelBounds=label->mapRectToItem(lanes,label->boundingRect());
        const auto badgeBounds=badge->mapRectToItem(lanes,badge->boundingRect());
        QVERIFY(!labelBounds.intersects(badgeBounds));
        const auto image=window.grabWindow();QVERIFY(!image.isNull());
        const auto ratio=image.width()/qreal(width);
        const auto bounds=label->mapRectToItem(window.contentItem(),label->boundingRect());
        const QRect pixels(qFloor(bounds.x()*ratio),qFloor(bounds.y()*ratio),qCeil(bounds.width()*ratio),qCeil(bounds.height()*ratio));
        int ink=0;
        for(int y=pixels.top();y<=pixels.bottom()&&y<image.height();++y)
            for(int x=pixels.left();x<=pixels.right()&&x<image.width();++x)
                if(x>=0&&y>=0&&image.pixelColor(x,y)!=window.color())++ink;
        QVERIFY2(ink>=5,"A nonzero frame label must actually render above the parent background");
    }
    model->seek(frames-1);
    auto *playhead=visualItem(timeline.data(),"editorTimelinePlayhead");QVERIFY(playhead);
    QVERIFY(qAbs(playhead->mapToItem(lanes,QPointF()).x()-first.x()-(frames-1)*pixelsPerFrame)<.01);
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if(!capture.isEmpty()&&frames==1&&width==1568)QVERIFY(window.grabWindow().save(capture+"/single-frame-ruler.png"));
}
void EditorLayerTimelineTests::rulerReferenceStyle() {
    auto document=fixture();document.timeline.frameCount=120;
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(document,"Reference ruler"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({1568,520});
    QQuickWindow window;window.resize(1568,520);window.setColor(QColor("#1e1e1e"));timeline->setParentItem(window.contentItem());window.show();QTest::qWait(60);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);model->seek(60);
    auto *lanes=visualItem(timeline.data(),"editorTimelineLanes");QVERIFY(lanes);
    auto *baseline=visualItem(timeline.data(),"editorTimelineRulerBaseline");QVERIFY(baseline);
    QCOMPARE(baseline->height(),4.0);QCOMPARE(baseline->width(),lanes->property("contentWidth").toReal());
    QCOMPARE(baseline->property("color").value<QColor>(),QColor("#f0f000"));
    const auto step=timeline->property("tickStep").toInt();
    auto *major=visualItem(timeline.data(),"editorTimelineRulerMark-"+QString::number(step));QVERIFY(major);
    QCOMPARE(major->width(),2.0);QCOMPARE(major->height(),14.0);
    QCOMPARE(major->property("color").value<QColor>(),QColor("#8d8d8d"));
    QQuickItem *minor=nullptr;
    const auto visit=[&](auto &&self,QQuickItem *item)->void {
        if(item->isVisible()&&item->objectName().startsWith("editorTimelineRulerMinor-"))minor=item;
        for(auto *child:item->childItems())self(self,child);
    };visit(visit,timeline.data());QVERIFY(minor);
    QCOMPARE(minor->width(),2.0);QCOMPARE(minor->height(),10.0);
    QCOMPARE(minor->mapToItem(lanes,QPointF(0,minor->height())).y(),major->mapToItem(lanes,QPointF(0,major->height())).y());
    auto *head=visualItem(timeline.data(),"editorTimelinePlayhead");QVERIFY(head);
    auto *marker=visualItem(timeline.data(),"editorTimelinePlayheadBadge");QVERIFY(marker);
    QCOMPARE(marker->width(),22.0);QCOMPARE(marker->height(),26.0);
    const auto tip=marker->mapToItem(lanes,QPointF(marker->width()/2,marker->height()));
    QCOMPARE(tip.x(),head->mapToItem(lanes,QPointF(head->width()/2,0)).x());
    QCOMPARE(tip.y(),baseline->mapToItem(lanes,QPointF(0,baseline->height()/2)).y());
    QTest::qWait(60);const auto image=window.grabWindow();QVERIFY(!image.isNull());const auto ratio=image.width()/1568.0;
    const auto pixel=[&](QQuickItem *item,QPointF point){const auto scene=item->mapToItem(window.contentItem(),point);return image.pixelColor(qRound(scene.x()*ratio),qRound(scene.y()*ratio));};
    QCOMPARE(pixel(baseline,{30,2}),QColor("#f0f000"));
    QCOMPARE(pixel(marker,{4,4}),QColor("#4096f3"));
    QCOMPARE(pixel(marker,{11,24}),QColor("#4096f3"));
    QCOMPARE(pixel(marker,{1,23}),window.color());
    QCOMPARE(pixel(head,{1,50}),QColor("#4096f3"));
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");if(!capture.isEmpty())QVERIFY(image.save(capture+"/reference-ruler.png"));
}
void EditorLayerTimelineTests::transparentBackground() {
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Transparent timeline"));
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({900,500});
    QQuickWindow window;window.resize(900,500);const QColor backdrop("#415b72");window.setColor(backdrop);
    timeline->setParentItem(window.contentItem());window.show();QTest::qWait(80);
    const auto image=window.grabWindow();QVERIFY(!image.isNull());
    QList<QPointF> samples={{898,2}};
    for(const auto &name:{"editorTimelineTools","editorTimelinePrecision","editorTimelineFooter"}) {
        auto *item=visualItem(timeline.data(),name);QVERIFY(item);samples.append({898,item->mapToItem(timeline.data(),QPointF()).y()-1});
    }
    auto *lanes=visualItem(timeline.data(),"editorTimelineLanes");QVERIFY(lanes);
    samples.append({1,lanes->mapToItem(timeline.data(),QPointF()).y()+25});
    for(const auto &point:samples) {
        const auto ratio=image.width()/900.0;const auto color=image.pixelColor(qRound(point.x()*ratio),qRound(point.y()*ratio));
        QVERIFY2(color==backdrop,qPrintable(QString("Panel background covers the parent at %1,%2: %3").arg(point.x()).arg(point.y()).arg(color.name())));
    }
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if(!capture.isEmpty())QVERIFY(image.save(capture+"/transparent-background.png"));
}
void EditorLayerTimelineTests::responsiveLayout_data() {
    QTest::addColumn<QSize>("size");
    for(const auto size:{QSize(1568,500),QSize(900,400),QSize(430,400),QSize(320,400)})
        QTest::newRow(qPrintable(QString::number(size.width())))<<size;
}
void EditorLayerTimelineTests::controlsFollowDocumentAndSelection() {
    EditorCanvas canvas;QQmlEngine engine;
    QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({900,400});
    auto *select=visualItem(timeline.data(),"editorTimelineTool-Select");QVERIFY(select);QVERIFY(!select->isEnabled());
    QVERIFY(canvas.loadProjectPage(fixture(),"Opened after view creation"));QTRY_VERIFY(select->isEnabled());
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);
    QVERIFY(model->selectClip("still-layer"));auto *trim=visualItem(timeline.data(),"editorTimelineTool-Trim");QVERIFY(trim);QTRY_VERIFY(trim->isEnabled());
    QVERIFY(model->setTrackLocked("still-layer",true));QTRY_VERIFY(!trim->isEnabled());
    QVERIFY(model->setTrackLocked("still-layer",false));QTRY_VERIFY(trim->isEnabled());
}
void EditorLayerTimelineTests::responsiveLayout() {
    QFETCH(QSize,size);
    EditorCanvas canvas; QVERIFY(canvas.loadProjectPage(fixture(),"Fixture"));
    QQmlEngine engine; QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString())); timeline->setSize(size);
    QQuickWindow window; window.resize(size); timeline->setParentItem(window.contentItem()); window.show();
    QTRY_VERIFY(window.isVisible()); QTest::qWait(50);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel")); QVERIFY(model); QCOMPARE(model->tracks().size(),3);
    for(const auto &name:{"editorTimelineHeader","editorTimelineTools","editorTimelinePrecision","editorTimelineFooter"}) {
        auto *item=timeline->findChild<QQuickItem *>(name); QVERIFY2(item,name);
        QVERIFY(item->x()>=0 && item->x()+item->width()<=timeline->width()+1);
        QVERIFY(item->clip());
    }
    auto *lanes=timeline->findChild<QQuickItem *>("editorTimelineLanes"); QVERIFY(lanes);
    QCOMPARE(lanes->property("contentWidth").toReal(),lanes->width());
    QVERIFY(visualItem(timeline.data(),"editorTimelineTool-Select"));
    QVERIFY(timeline->findChild<QObject *>("editorTimelineAddLayer"));
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if(!capture.isEmpty())QVERIFY(window.grabWindow().save(capture+"/timeline-"+QString::number(size.width())+".png"));
}
int main(int argc,char **argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QQuickStyle::setStyle("Basic");
    QTemporaryDir settings(QString(DREAMSCAPES_TEST_DIRECTORY)+"/layer-timeline-settings-XXXXXX");
    if(!settings.isValid())return 2;
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH",(settings.path()+"/storage.json").toUtf8());
    qputenv("DREAMSCAPES_TEMP_DIRECTORY",DREAMSCAPES_TEST_DIRECTORY);
    qputenv("SOCIETY_DISABLE_SESSION_RESTORE","1");
    qunsetenv("SOCIETY_CONTAINER_PATH");
    QApplication app(argc,argv);
    qmlRegisterType<EditorCanvas>("Dreamscapes.Storage",1,0,"EditorCanvas");
    qmlRegisterType<EditorTimelineModel>("Dreamscapes.Storage",1,0,"EditorTimelineModel");
    EditorLayerTimelineTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "tst_EditorLayerTimeline.moc"
