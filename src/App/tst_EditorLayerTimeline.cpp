#include "Views/Editor/EditorTimelineModel.h"
#include "Views/Editor/EditorTimelineViewport.h"
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
#include <QWheelEvent>
#include <QScopeGuard>
#include <SocietyDrive.h>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <limits>

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
    void rulerHeightChangesOnlyViewGeometry();
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
    void scrubbingDoesNotEditOrSave();
    void pointerScrubCoalescesCanvasUpdates();
    void scrubCancellationAndOwnerChangesDropPendingFrames();
    void pointerRulerScrubFinishesAndEscapeRestoresTheStart();
    void evictedThumbnailCanBeRequestedAgain();
    void mountedThumbnailsUseTargetedCompletion();
    void nativeProjectionRemainsStableDuringSelection();
    void nativeDragPreviewsCommitOnce();
    void denseTimelineDragDoesNotRescanEveryClip();
    void dragSnapTargetsStayLive();
    void nativeKeyDragPreviewsCommitOnce_data();
    void nativeKeyDragPreviewsCommitOnce();
    void pointerDragRetainsLinkedSelection();
    void pointerKeyDragCommitsOnceAndEscapeCancels();
    void denseKeyDragUsesPreparedIndex();
    void asynchronousThumbnailsFollowTheLatestDocument();
    void dragPreviewFollowsPointer();
    void virtualizedNavigationRetainsPointerAndCommitsOnce();
    void keyHitAndPaintSurfacesFollowTheVisibleRange();
    void liveBrushDefersTimelineWorkUntilCommit();
    void rulerGeometry_data();
    void rulerGeometry();
    void rulerReferenceStyle();
    void transparentBackground();
    void responsiveLayout_data();
    void responsiveLayout();
};
void EditorLayerTimelineTests::rulerHeightChangesOnlyViewGeometry() {
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Ruler geometry"));
    EditorTimelineModel model;model.setCanvas(&canvas);
    const auto revision=canvas.revision();const auto layers=canvas.document()->layers.size();
    const auto path=canvas.filePath();const auto undo=model.canUndo();
    const auto clips=model.clips();
    model.setRulerHeight(32);
    QCOMPARE(model.rulerHeight(),32);QCOMPARE(model.trackLayout().front().toMap()["top"].toInt(),32);
    QCOMPARE(canvas.revision(),revision);QCOMPARE(canvas.filePath(),path);
    QCOMPARE(canvas.document()->layers.size(),layers);QCOMPARE(model.clips(),clips);QCOMPARE(model.canUndo(),undo);
    model.setRulerHeight(44);QCOMPARE(model.trackLayout().front().toMap()["top"].toInt(),44);
    QCOMPARE(canvas.revision(),revision);QCOMPARE(model.clips(),clips);
}
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
    QVERIFY(model.setRange("still-layer",2,6));QVERIFY2(canvas.saveDocument(),qPrintable(canvas.error()));
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
    QVERIFY(model.beginDrag("moving-layer",{},0,100));QVERIFY(model.updateDrag(200));
    QCOMPARE(model.dragPreview()["still-layer"].toMap()["in"].toInt(),3);
    QCOMPARE(model.dragPreview()["moving-layer"].toMap()["in"].toInt(),2);
    QCOMPARE(find(model.clips(),"still-layer")["in"].toInt(),1);
    QVERIFY(model.finishDrag());
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
    model.setTool("Roll");QVERIFY(model.beginDrag("middle",{},0,100));QVERIFY(model.updateDrag(100));
    QCOMPARE(model.dragPreview()["middle"].toMap()["out"].toInt(),7);
    QCOMPARE(model.dragPreview()["next"].toMap()["in"].toInt(),7);QVERIFY(model.finishDrag());
    QCOMPARE(find(model.clips(),"middle")["out"].toInt(),7);QCOMPARE(find(model.clips(),"next")["in"].toInt(),7);
    QCOMPARE(canvas.document()->audioTracks.front().clips.back().sourceOffsetSamples,std::uint64_t(14000));
    model.undo();QVERIFY(model.selectClip("middle"));model.setTool("Slide");QVERIFY(model.beginDrag("middle",{},0,100));QVERIFY(model.updateDrag(100));
    QCOMPARE(model.dragPreview()["previous"].toMap()["out"].toInt(),4);
    QCOMPARE(model.dragPreview()["middle"].toMap()["in"].toInt(),4);
    QCOMPARE(model.dragPreview()["next"].toMap()["in"].toInt(),7);QVERIFY(model.finishDrag());
    QCOMPARE(find(model.clips(),"previous")["out"].toInt(),4);QCOMPARE(find(model.clips(),"middle")["in"].toInt(),4);
    QCOMPARE(find(model.clips(),"middle")["frames"].toInt(),3);QCOMPARE(find(model.clips(),"next")["in"].toInt(),7);
    model.undo();QVERIFY(model.selectClip("middle"));model.setTool("Ripple");QVERIFY(model.beginDrag("middle",{},0,100));QVERIFY(model.updateDrag(100));
    QCOMPARE(model.dragPreview()["middle"].toMap()["out"].toInt(),7);
    QCOMPARE(model.dragPreview()["next"].toMap()["in"].toInt(),7);QVERIFY(model.finishDrag());
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
void EditorLayerTimelineTests::liveBrushDefersTimelineWorkUntilCommit() {
    Document d;d.extent={32,32};d.timeline={{24,1},24};
    d.assets.emplace_back(RasterAsset{"paint",makeRasterLayer(32,32)});
    d.layers.emplace_back(StaticBitmapLayer{{"paint-layer","Paint"},StaticSource{"paint"}});
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Live brush"));QVERIFY(canvas.selectLayer("paint-layer"));
    canvas.setBrushColor(Qt::red);canvas.setBrushSize(12);canvas.setBrushOpacity(1);canvas.setBrushFlow(1);canvas.setBrushHardness(1);
    EditorTimelineModel model;model.setCanvas(&canvas);
    const auto original=model.thumbnail("paint-layer",0);QVERIFY(!original.isEmpty());
    const auto revision=model.thumbnailRevision();
    QSignalSpy projections(&model,&EditorTimelineModel::projectionChanged);
    QVERIFY(canvas.beginStrokeAt({8,16}));QVERIFY(canvas.liveStrokeActive());
    QVERIFY(canvas.continueStrokeAt({16,16}));QVERIFY(canvas.continueStrokeAt({20,16}));
    QCOMPARE(model.thumbnailRevision(),revision);
    QCOMPARE(model.requestThumbnail("paint-layer",0),original);QCOMPARE(projections.count(),0);
    QVERIFY(canvas.endStrokeAt({24,16}));QVERIFY(!canvas.liveStrokeActive());
    QTRY_VERIFY(model.thumbnailRevision()>revision);
    QTRY_VERIFY(!model.requestThumbnail("paint-layer",0).isEmpty());
    QVERIFY(model.requestThumbnail("paint-layer",0)!=original);
    QCOMPARE(thumbnail(model.requestThumbnail("paint-layer",0)).pixelColor(32,32),QColor(Qt::red));
}
void EditorLayerTimelineTests::asynchronousThumbnailsFollowTheLatestDocument() {
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Async thumbnails"));
    EditorTimelineModel model;model.setCanvas(&canvas);
    QVERIFY(model.requestThumbnail("still-layer",1).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!model.requestThumbnail("still-layer",1).isEmpty(),5000);
    QCOMPARE(thumbnail(model.requestThumbnail("still-layer",1)).pixelColor(32,32),QColor(Qt::red));
    const auto revision=model.thumbnailRevision();
    QSignalSpy ready(&model,&EditorTimelineModel::thumbnailReady),broad(&model,&EditorTimelineModel::thumbnailsChanged);
    for(int frame=0;frame<6;++frame)model.requestThumbnail("moving-layer",frame);
    QTRY_VERIFY_WITH_TIMEOUT(!model.requestThumbnail("moving-layer",5).isEmpty(),5000);
    QCOMPARE(thumbnail(model.requestThumbnail("moving-layer",5)).pixelColor(32,32),QColor::fromRgba(0xff000000U|6*0x00112233U));
    QCOMPARE(model.thumbnailRevision(),revision);QVERIFY(!ready.isEmpty());QCOMPARE(broad.count(),0);
    model.requestThumbnail("moving-layer",0);
    auto replacement=fixture();std::get<RasterAsset>(replacement.assets[0]).pixels=makeRasterLayer(8,8,0xff0000ffU);
    QVERIFY(canvas.loadProjectPage(replacement,"Replaced during rendering"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.requestThumbnail("still-layer",1).isEmpty(),5000);
    QCOMPARE(thumbnail(model.requestThumbnail("still-layer",1)).pixelColor(32,32),QColor(Qt::blue));
    QVERIFY(model.requestThumbnail("missing",0).isEmpty());
}
void EditorLayerTimelineTests::dragPreviewFollowsPointer() {
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Pointer preview"));
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({900,500});
    QQuickWindow window;window.resize(900,500);window.setColor(QColor("#20232a"));timeline->setParentItem(window.contentItem());window.show();
    QTest::qWait(80);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);model->setSnap(false);
    auto *card=visualItem(timeline.data(),"editorTimelineClip-still-layer");QVERIFY(card);
    auto *lanes=visualItem(timeline.data(),"editorTimelineLanes");QVERIFY(lanes);
    const QPoint start=card->mapToScene(QPointF(card->width()/2,card->height()/2)).toPoint();
    const int pixels=qRound(lanes->property("contentWidth").toDouble()/timeline->property("viewFrameCount").toInt()*2);
    QPointer<QQuickItem> retained(card);
    QSignalSpy edits(&canvas,&CanvasItem::revisionChanged),projections(model,&EditorTimelineModel::projectionChanged);
    QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,start);
    QTRY_VERIFY(model->dragging());
    QTest::mouseMove(&window,start+QPoint(pixels,0),1);
    QTRY_COMPARE(model->dragPreview()["still-layer"].toMap()["in"].toInt(),3);
    QVERIFY(retained);QCOMPARE(visualItem(timeline.data(),"editorTimelineClip-still-layer"),retained.data());
    QCOMPARE(edits.count(),0);QCOMPARE(projections.count(),0);
    QCOMPARE(find(model->clips(),"still-layer")["in"].toInt(),1);
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if(!capture.isEmpty())QVERIFY(window.grabWindow().save(capture+"/timeline-live-drag.png"));
    QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,start+QPoint(pixels,0));
    QTRY_VERIFY(!model->dragging());QCOMPARE(edits.count(),1);
    QCOMPARE(find(model->clips(),"still-layer")["in"].toInt(),3);
    if(!capture.isEmpty())QVERIFY(window.grabWindow().save(capture+"/timeline-committed-drag.png"));
}
void EditorLayerTimelineTests::virtualizedNavigationRetainsPointerAndCommitsOnce() {
    Document d;d.extent={8,8};d.timeline={{24,1},2400};
    d.assets.emplace_back(RasterAsset{"shared",makeRasterLayer(8,8,0xffe8b45aU)});
    for(int i=1499;i>=0;--i) {
        LayerProperties p;p.id="layer-"+std::to_string(i);p.name="Layer "+std::to_string(i);
        p.frameRange=LayerFrameRange{0,FrameIndex(i==0?99:2399)};
        d.layers.emplace_back(StaticBitmapLayer{p,StaticSource{"shared"}});
    }
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Large timeline"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({900,500});
    QQuickWindow window;window.resize(900,500);window.setColor(QColor("#20232a"));timeline->setParentItem(window.contentItem());window.show();
    QTest::qWait(80);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);model->setSnap(false);
    auto *view=qvariant_cast<EditorTimelineViewport *>(timeline->property("viewState"));QVERIFY(view);
    auto *lanes=visualItem(timeline.data(),"editorTimelineLanes");QVERIFY(lanes);
    auto *viewport=visualItem(timeline.data(),"editorTimelineViewport");QVERIFY(viewport);
    auto *pointer=visualItem(timeline.data(),"editorTimelinePointerInput");QVERIFY(pointer);
    QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged),projections(model,&EditorTimelineModel::projectionChanged);
    QCOMPARE(model->trackLayout().size(),1500);
    const auto countItems=[&](const QString &prefix) {
        int count=0;const auto visit=[&](auto &&self,QQuickItem *item)->void {
            if(item->objectName().startsWith(prefix))++count;
            for(auto *child:item->childItems())self(self,child);
        };visit(visit,timeline.data());return count;
    };
    QVERIFY(countItems("editorTimelineRow-")<20);
    QVERIFY(countItems("editorTimelineClip-")<20);
    qInfo()<<"Visible rows/clips for 1500 native layers:"<<countItems("editorTimelineRow-")<<countItems("editorTimelineClip-");
    QVERIFY(timeline->setProperty("timelineScale",8.0));
    QVERIFY(lanes->setProperty("contentX",500.0));
    const QPointF anchor=lanes->mapToScene({200,24});
    const auto frameUnderPointer=(view->contentX()+200)/view->pixelsPerFrame();
    QWheelEvent wheel(anchor,window.mapToGlobal(anchor.toPoint()),{},QPoint(0,120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
    QCoreApplication::sendEvent(&window,&wheel);QCoreApplication::processEvents();
    QCOMPARE(view->zoom(),10.0);
    QVERIFY(qAbs((view->contentX()+200)/view->pixelsPerFrame()-frameUnderPointer)<1e-6);
    QVERIFY(countItems("editorTimelineRulerTick-")<25);
    const auto panPoint=anchor.toPoint();const auto beforePan=lanes->property("contentX").toDouble();
    QTest::mousePress(&window,Qt::MiddleButton,Qt::NoModifier,panPoint);
    QTest::mouseMove(&window,panPoint-QPoint(80,0),1);
    QTest::mouseRelease(&window,Qt::MiddleButton,Qt::NoModifier,panPoint-QPoint(80,0));
    QVERIFY(qAbs(lanes->property("contentX").toDouble()-beforePan-80)<1);
    const auto emptyPoint=lanes->mapToScene({20,82}).toPoint();
    QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,emptyPoint);
    QTest::mouseMove(&window,emptyPoint-QPoint(0,30),1);
    QTest::mouseMove(&window,emptyPoint-QPoint(0,70),1);
    QTRY_VERIFY(viewport->property("contentY").toDouble()>0);
    QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,emptyPoint-QPoint(0,70));
    QVERIFY(QMetaObject::invokeMethod(viewport,"cancelFlick"));
    QVERIFY(!model->dragging());
    QVERIFY(viewport->setProperty("contentY",lanes->y()+44+700*40));
    QTRY_VERIFY(visualItem(timeline.data(),"editorTimelineRow-layer-700"));
    QVERIFY(!visualItem(timeline.data(),"editorTimelineRow-layer-0"));
    QVERIFY(countItems("editorTimelineRow-")<20);
    QVERIFY(countItems("editorTimelineClip-")<20);
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if(!capture.isEmpty())QVERIFY(window.grabWindow().save(capture+"/timeline-1500-layers.png"));
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);
    QVERIFY(viewport->setProperty("contentY",0.0));QVERIFY(lanes->setProperty("contentX",0.0));
    QTRY_VERIFY(visualItem(timeline.data(),"editorTimelineClip-layer-0"));
    // Cull the pressed card while the stable input owner keeps its mouse grab.
    auto *card=visualItem(timeline.data(),"editorTimelineClip-layer-0");
    const auto start=card->mapToScene({card->width()/2,16}).toPoint();
    QPointer<QQuickItem> originalCard(card);
    QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,start);
    QTRY_VERIFY(model->dragging());
    QVERIFY(lanes->setProperty("contentX",lanes->width()*3));
    QTRY_VERIFY(!originalCard);
    QVERIFY(model->dragging());QVERIFY(pointer->property("pressed").toBool());
    QTest::mouseMove(&window,start+QPoint(30,0),1);
    QVERIFY(model->dragPreview()["layer-0"].toMap()["in"].toInt()>100);
    QTest::keyClick(&window,Qt::Key_Escape);
    QVERIFY(!model->dragging());
    QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,start+QPoint(30,0));
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    // The same culling path commits a single native edit when accepted.
    QVERIFY(lanes->setProperty("contentX",0.0));
    QTRY_VERIFY(visualItem(timeline.data(),"editorTimelineClip-layer-0"));
    QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,start);
    QTRY_VERIFY(model->dragging());
    QVERIFY(lanes->setProperty("contentX",lanes->width()*3));
    QTest::mouseMove(&window,start+QPoint(30,0),1);
    const auto accepted=model->dragPreview()["layer-0"].toMap()["in"].toInt();
    QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,start+QPoint(30,0));
    QTRY_VERIFY(!model->dragging());
    QCOMPARE(revisions.count(),1);QCOMPARE(projections.count(),1);
    QCOMPARE(find(model->clips(),"layer-0")["in"].toInt(),accepted);
    model->undo();QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    QVERIFY(lanes->setProperty("contentX",0.0));
    QTRY_VERIFY(visualItem(timeline.data(),"editorTimelineClip-layer-0"));
    revisions.clear();projections.clear();
    QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,start);
    QTRY_VERIFY(model->dragging());
    const auto edge=lanes->mapToScene({lanes->width()-1,68}).toPoint();
    QTest::mouseMove(&window,edge,1);
    const auto beforeEdge=model->dragPreview()["layer-0"].toMap()["in"].toInt();
    QTRY_VERIFY_WITH_TIMEOUT(lanes->property("contentX").toDouble()>30,1000);
    QVERIFY(model->dragPreview()["layer-0"].toMap()["in"].toInt()>beforeEdge);
    QTest::keyClick(&window,Qt::Key_Escape);
    QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,edge);
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
}
void EditorLayerTimelineTests::keyHitAndPaintSurfacesFollowTheVisibleRange() {
    auto d=fixture();d.timeline.frameCount=1200;
    auto &properties=layerProperties(d.layers.front());properties.frameRange=LayerFrameRange{0,1199};
    for(FrameIndex f=0;f<1200;f+=100) {
        MotionKeyframe key;key.frame=f;key.value.opacity=f%200?.8:.2;
        key.interpolation=MotionInterpolation::SmoothStep;properties.motion.push_back(key);
    }
    d.audioAssets.front().samples.resize(48000,8192);
    d.audioTracks.front().clips.front().durationFrames=24;
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Curves and waveform"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({900,500});
    QQuickWindow window;window.resize(900,500);window.setColor(QColor("#20232a"));timeline->setParentItem(window.contentItem());window.show();QTest::qWait(60);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);
    auto *view=qvariant_cast<EditorTimelineViewport *>(timeline->property("viewState"));QVERIFY(view);
    auto *lanes=visualItem(timeline.data(),"editorTimelineLanes");QVERIFY(lanes);
    QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged),projections(model,&EditorTimelineModel::projectionChanged);
    model->setMode("Curves");QVERIFY(timeline->setProperty("timelineScale",128.0));
    auto *audio=visualItem(timeline.data(),"editorTimelineClip-audio-clip");QVERIFY(audio);
    auto *wave=visualItem(timeline.data(),"editorTimelineWaveform-audio-clip");QVERIFY(wave);
    QVERIFY(audio->width()>lanes->width());QVERIFY(wave->width()<=lanes->width());
    QVERIFY(lanes->setProperty("contentX",100*view->pixelsPerFrame()-100));
    auto *curve=visualItem(timeline.data(),"editorTimelineCurve-still-layer");QVERIFY(curve);
    QVERIFY(curve->isVisible());QVERIFY(curve->width()<=lanes->width());
    QCOMPARE(curve->x(),lanes->property("contentX").toDouble());
    const auto visible=find(view->visibleTracks(),"still-layer");
    const auto keys=visible["motionKeys"].toList();QVERIFY(keys.size()<=3);
    QVERIFY(keys.front().toMap()["frame"].toInt()<view->firstVisibleFrame());
    QVERIFY(keys.back().toMap()["frame"].toInt()>view->lastVisibleFrame());
    QTest::qWait(30);
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if(!capture.isEmpty())QVERIFY(window.grabWindow().save(capture+"/timeline-zoomed-curve.png"));
    auto *key=visualItem(timeline.data(),"editorTimelineKey-still-layer:motion:100");QVERIFY(key);
    QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,key->mapToScene({3,3}).toPoint());
    QCOMPARE(model->selectedKey()["id"].toString(),"still-layer:motion:100");
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    // Fit from the far end is safe with curve keys present and resets scroll.
    model->setMode("Curves");QVERIFY(lanes->setProperty("contentX",view->contentWidth()-lanes->width()));
    QVERIFY(QMetaObject::invokeMethod(timeline.data(),"fitTimeline"));
    QCOMPARE(view->zoom(),1.0);QCOMPARE(lanes->property("contentX").toDouble(),0.0);
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
}
void EditorLayerTimelineTests::nativeProjectionRemainsStableDuringSelection() {
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Projection"));
    EditorTimelineModel model;model.setCanvas(&canvas);
    QCOMPARE(model.trackLayout().size(),model.tracks().size());
    QCOMPARE(model.layerCount(),2);QCOMPARE(model.keptCount(),2);QVERIFY(model.hasAudio());
    const auto layout=model.trackLayout();
    QSignalSpy projections(&model,&EditorTimelineModel::projectionChanged),layouts(&model,&EditorTimelineModel::layoutChanged);
    QVERIFY(model.selectClip("still-layer"));model.setSnap(false);model.setLinked(false);model.setTool("Trim");
    QCOMPARE(projections.count(),0);QCOMPARE(layouts.count(),0);QCOMPARE(model.trackLayout(),layout);
    model.setMode("Keyframes");QVERIFY(layouts.count()>0);QCOMPARE(projections.count(),0);
    QCOMPARE(find(model.trackLayout(),"moving-layer")["keys"].toList().size(),6);
    model.setFilter("Out");QCOMPARE(model.trackLayout().size(),1);QCOMPARE(model.trackLayout().front().toMap()["type"].toString(),"Audio");
    model.setFilter("All");model.seek(5);QCOMPARE(model.timecode(),QString("00:00:00:05 · F005"));
}
void EditorLayerTimelineTests::nativeDragPreviewsCommitOnce() {
    QTemporaryDir files(QString(DREAMSCAPES_TEST_DIRECTORY)+"/drag-preview-XXXXXX");QVERIFY(files.isValid());
    const auto path=files.filePath("working.iisc");
    DocumentFile working;QVERIFY(working.create(path.toStdString(),fixture()).ok());working.close();
    EditorCanvas canvas;QVERIFY(canvas.openDocumentSource(QUrl::fromLocalFile(path)));
    EditorTimelineModel model;model.setCanvas(&canvas);model.setSnap(false);QVERIFY(model.selectClip("still-layer"));
    QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged),projections(&model,&EditorTimelineModel::projectionChanged);
    QFile original(path);QVERIFY(original.open(QIODevice::ReadOnly));const auto bytes=original.readAll();original.close();
    QVERIFY(model.beginDrag("still-layer",{},0,100));
    QElapsedTimer elapsed;elapsed.start();
    for(int update=0;update<240;++update)QVERIFY(model.updateDrag(200.0*update/239));
    const auto microseconds=elapsed.nsecsElapsed()/1000;
    qInfo().noquote()<<QString("timeline_drag_preview: 240 updates; %1 us total; document revisions=%2; model resets=%3")
        .arg(microseconds).arg(revisions.count()).arg(projections.count());
    QCOMPARE(model.dragPreview()["still-layer"].toMap()["in"].toInt(),3);
    QCOMPARE(find(model.clips(),"still-layer")["in"].toInt(),1);
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);
    QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(original.readAll(),bytes);original.close();
    QVERIFY(model.finishDrag());QVERIFY(!model.dragging());QCOMPARE(revisions.count(),1);
    QCOMPARE(find(model.clips(),"still-layer")["in"].toInt(),3);
    DocumentFile persisted;QVERIFY(persisted.open(path.toStdString()).ok());
    QCOMPARE(layerProperties(*findLayer(*persisted.document(),"still-layer")).frameRange->firstFrame,3U);persisted.close();
    model.undo();QCOMPARE(find(model.clips(),"still-layer")["in"].toInt(),1);
    QVERIFY(model.beginDrag("still-layer","out",0,100));QVERIFY(model.updateDrag(100));model.cancelDrag();
    QCOMPARE(find(model.clips(),"still-layer")["out"].toInt(),5);
    QVERIFY(model.beginDrag("still-layer",{},0,100));QVERIFY(!model.updateDrag(-200));QVERIFY(!model.finishDrag());
    QCOMPARE(find(model.clips(),"still-layer")["in"].toInt(),1);
    model.setSnap(true);model.seek(0);QVERIFY(model.beginDrag("still-layer",{},0,100,1));
    // The nearest anchor is the audio clip's out point (4), reached by the
    // selected layer's in point. Equal-distance anchors keep the first match.
    QVERIFY(model.updateDrag(200));QCOMPARE(model.dragPreview()["still-layer"].toMap()["in"].toInt(),4);
    QCOMPARE(model.dragPreview()["still-layer"].toMap()["out"].toInt(),8);
    model.cancelDrag();
}
void EditorLayerTimelineTests::denseTimelineDragDoesNotRescanEveryClip() {
    Document d;d.extent={1,1};d.timeline={{24,1},24000};
    d.assets.emplace_back(RasterAsset{"shared",makeRasterLayer(1,1,0xff4080c0U)});
    for(int index=0;index<10000;++index) {
        LayerProperties p;p.id="dense-"+std::to_string(index);p.name=p.id;
        const auto first=FrameIndex(index==0?100:200+index*2);
        p.frameRange=LayerFrameRange{first,first+9};
        d.layers.emplace_back(StaticBitmapLayer{p,StaticSource{"shared"}});
    }
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Dense timeline drag"));
    EditorTimelineModel model;model.setCanvas(&canvas);QVERIFY(model.selectClip("dense-0"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged),projections(&model,&EditorTimelineModel::projectionChanged);
    QVERIFY(model.beginDrag("dense-0",{},500,1,9));
    QElapsedTimer elapsed;elapsed.start();
    for(int update=0;update<480;++update)QVERIFY(model.updateDrag(500+update*0.125));
    const auto microseconds=elapsed.nsecsElapsed()/1000;
    const auto measurement=QString("timeline_dense_drag: 10000 clips, snapping enabled, 480 updates; %1 us; revisions=%2; projections=%3")
        .arg(microseconds).arg(revisions.count()).arg(projections.count());
    qInfo().noquote()<<measurement;
    QCOMPARE(model.dragPreview()["dense-0"].toMap()["in"].toInt(),160);
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);
    model.cancelDrag();QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    // This is the native metadata path only; construction, painting, rendering
    // and durable edits are deliberately outside this interaction budget.
    QVERIFY2(microseconds<250000,qPrintable(measurement));
}
void EditorLayerTimelineTests::dragSnapTargetsStayLive() {
    Document d;d.extent={1,1};d.timeline={{24,1},1000};
    d.assets.emplace_back(RasterAsset{"shared",makeRasterLayer(1,1,0xff4080c0U)});
    LayerProperties target;target.id="target";target.name="Target";target.frameRange=LayerFrameRange{100,109};
    LayerProperties neighbour;neighbour.id="neighbour";neighbour.name="Neighbour";neighbour.frameRange=LayerFrameRange{300,399};
    d.layers.emplace_back(StaticBitmapLayer{target,StaticSource{"shared"}});
    d.layers.emplace_back(StaticBitmapLayer{neighbour,StaticSource{"shared"}});
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Live snap anchors"));
    EditorTimelineModel model;model.setCanvas(&canvas);QVERIFY(model.selectClip("target"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged),projections(&model,&EditorTimelineModel::projectionChanged);
    const auto previewIn=[&]{return model.dragPreview()["target"].toMap()["in"].toInt();};
    QVERIFY(model.beginDrag("target",{},0,1,4));QVERIFY(model.updateDrag(50));QCOMPARE(previewIn(),150);
    model.seek(151);QVERIFY(model.updateDrag(50));QCOMPARE(previewIn(),151);
    model.markRange(149,149);QVERIFY(model.updateDrag(50));QCOMPARE(previewIn(),150);
    // Equal-distance anchors retain the existing priority: the playhead comes
    // before the range marks, and the In edge comes before the Out edge.
    model.markRange(148,148);QVERIFY(model.updateDrag(50));QCOMPARE(previewIn(),151);
    model.seek(155);QVERIFY(model.updateDrag(50));QCOMPARE(previewIn(),149);
    model.cancelDrag();QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    QVERIFY(model.setRange("neighbour",153,164));
    QVERIFY(model.beginDrag("target",{},0,1,4));QVERIFY(model.updateDrag(50));QCOMPARE(previewIn(),149);
    model.markRange(0,0);model.seek(0);QVERIFY(model.updateDrag(50));QCOMPARE(previewIn(),153);
    // Document edits discard the pressed snapshot before rebuilding anchors.
    QVERIFY(model.setRange("neighbour",170,180));QVERIFY(!model.dragging());
    QVERIFY(model.beginDrag("target",{},0,1,4));QVERIFY(model.updateDrag(50));QCOMPARE(previewIn(),150);
    model.cancelDrag();
}
void EditorLayerTimelineTests::nativeKeyDragPreviewsCommitOnce_data() {
    QTest::addColumn<bool>("content");
    QTest::newRow("content")<<true;
    QTest::newRow("motion")<<false;
}
void EditorLayerTimelineTests::nativeKeyDragPreviewsCommitOnce() {
    QFETCH(bool,content);
    auto d=fixture();d.timeline.frameCount=120;
    auto &properties=layerProperties(d.layers.front());
    MotionKeyframe a,b;a.frame=24;b.frame=72;a.value.opacity=.2;b.value.opacity=.8;
    properties.motion={a,b};
    QTemporaryDir files(QString(DREAMSCAPES_TEST_DIRECTORY)+"/key-preview-XXXXXX");QVERIFY(files.isValid());
    const auto path=files.filePath("working.iisc");
    DocumentFile working;QVERIFY(working.create(path.toStdString(),d).ok());working.close();
    EditorCanvas canvas;QVERIFY(canvas.openDocumentSource(QUrl::fromLocalFile(path)));
    EditorTimelineModel model;model.setCanvas(&canvas);model.setSnap(false);
    const auto id=content?QString("moving-layer:content:2"):QString("still-layer:motion:24");
    const auto target=content?10:36,initial=content?2:24;
    const auto original=encodeIisc(*canvas.document()).bytes;
    QFile source(path);QVERIFY(source.open(QIODevice::ReadOnly));const auto bytes=source.readAll();source.close();
    QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged),projections(&model,&EditorTimelineModel::projectionChanged);
    QVERIFY(model.selectKey(id));
    const auto keys=model.keys(),layout=model.trackLayout();
    QVERIFY(model.beginKeyDrag(id,100,10,0));
    QElapsedTimer timer;timer.start();
    for(int i=0;i<480;++i)QVERIFY(model.updateDrag(100+(target-initial)*10+(i%2==0?10:0)));
    qInfo()<<"480 native key preview updates (us):"<<timer.nsecsElapsed()/1000;
    QCOMPARE(model.keyDragPreview()["frame"].toInt(),target);
    QCOMPARE(model.keyDragPreview()["id"].toString(),id);
    QCOMPARE(model.keys(),keys);QCOMPARE(model.trackLayout(),layout);
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),bytes);source.close();
    model.cancelDrag();QVERIFY(!model.dragging());QVERIFY(model.keyDragPreview().isEmpty());
    QVERIFY(model.beginKeyDrag(id,100,10,0));QVERIFY(model.finishDrag());
    QCOMPARE(revisions.count(),0); // A click is not an edit/history checkpoint.
    QVERIFY(model.beginKeyDrag(id,100,10,0));
    QVERIFY(!model.updateDrag(-10000));QVERIFY(!model.finishDrag());
    QCOMPARE(revisions.count(),0);
    QVERIFY(model.beginKeyDrag(id,100,10,0));
    QVERIFY(!model.updateDrag(100+((content?5:72)-initial)*10));
    QVERIFY(!model.finishDrag()); // An occupied key is never overwritten.
    QCOMPARE(revisions.count(),0);
    QVERIFY(model.beginKeyDrag(id,100,10,0));
    QVERIFY(!model.updateDrag(std::numeric_limits<double>::quiet_NaN()));
    QVERIFY(model.updateDrag(100+(target-initial)*10));
    QVERIFY(model.finishDrag());
    QCOMPARE(revisions.count(),1);QCOMPARE(projections.count(),1);
    QCOMPARE(model.selectedKey()["frame"].toInt(),target);
    QCOMPARE(model.selectedKey()["kind"].toString(),content?"Content":"Motion");
    model.undo();QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    QVERIFY(model.selectKey(id));QVERIFY(model.beginKeyDrag(id,100,10,0));
    model.setFilter("Kept");QVERIFY(!model.dragging());QVERIFY(!model.finishDrag());
    if(content)QVERIFY(!model.beginKeyDrag("moving-layer:content:0",0,10,0));
}
void EditorLayerTimelineTests::pointerDragRetainsLinkedSelection() {
    auto d=fixture();d.timeline.frameCount=120;
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Linked selection"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({1200,500});
    QQuickWindow window;window.resize(1200,500);timeline->setParentItem(window.contentItem());window.show();QTest::qWait(60);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);model->setSnap(false);
    auto *view=qvariant_cast<EditorTimelineViewport *>(timeline->property("viewState"));QVERIFY(view);
    const auto point=[&](const QString &id) {
        auto *card=visualItem(timeline.data(),"editorTimelineClip-"+id);
        return card?card->mapToScene({card->width()/2,16}).toPoint():QPoint(-1,-1);
    };
    QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,point("moving-layer"));
    QTest::mouseClick(&window,Qt::LeftButton,Qt::ControlModifier,point("still-layer"));
    QCOMPARE(model->selectedIds().size(),2);
    QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged);
    const auto start=point("moving-layer"),end=start+QPoint(qRound(8*view->pixelsPerFrame()),0);
    QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,start);
    QCOMPARE(model->selectedIds().size(),2);
    QTest::mouseMove(&window,end,1);
    QCOMPARE(model->dragPreview().size(),2);
    const auto moved=model->dragPreview()["moving-layer"].toMap()["in"].toInt();
    QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,end);
    QCOMPARE(revisions.count(),1);
    QCOMPARE(find(model->clips(),"moving-layer")["in"].toInt(),moved);
    QCOMPARE(find(model->clips(),"still-layer")["in"].toInt(),moved+1);
    QCOMPARE(model->selectedIds().size(),2);
    model->undo();QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    // Modifier-clicking a selected item removes it without starting a drag.
    QVERIFY(model->selectClip("moving-layer"));
    QTRY_VERIFY(visualItem(timeline.data(),"editorTimelineClip-moving-layer"));
    QTRY_COMPARE(visualItem(timeline.data(),"editorTimelineClip-moving-layer")->x(),0.0);
    QTest::mousePress(&window,Qt::LeftButton,Qt::ControlModifier,point("moving-layer"));
    QVERIFY(!model->selectedIds().contains("moving-layer"));QVERIFY(!model->dragging());
    QTest::mouseRelease(&window,Qt::LeftButton,Qt::ControlModifier,point("moving-layer"));
}
void EditorLayerTimelineTests::pointerKeyDragCommitsOnceAndEscapeCancels() {
    auto d=fixture();d.timeline.frameCount=120;
    MotionKeyframe a,b;a.frame=24;b.frame=72;a.value.opacity=.2;b.value.opacity=.8;
    layerProperties(d.layers.front()).motion={a,b};
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Key gesture"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({1200,540});
    QQuickWindow window;window.resize(1200,540);timeline->setParentItem(window.contentItem());window.show();QTest::qWait(60);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);model->setSnap(false);model->setMode("Curves");
    auto *view=qvariant_cast<EditorTimelineViewport *>(timeline->property("viewState"));QVERIFY(view);
    auto *key=visualItem(timeline.data(),"editorTimelineKey-still-layer:motion:24");QVERIFY(key);
    const auto start=key->mapToScene({3,3}).toPoint(),end=start+QPoint(qRound(12*view->pixelsPerFrame()),0);
    QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged),projections(model,&EditorTimelineModel::projectionChanged);
    for(bool cancel:{true,false}) {
        QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,start);
        QPointer<QQuickItem> originalKey(visualItem(timeline.data(),"editorTimelineKey-still-layer:motion:24"));
        QTest::mouseMove(&window,end,1);
        QVERIFY(model->dragging());QCOMPARE(model->mode(),"Curves");
        QCOMPARE(model->keyDragPreview()["frame"].toInt(),36);
        auto *preview=visualItem(timeline.data(),"editorTimelineKey-still-layer:motion:24");QVERIFY(preview);
        QVERIFY(originalKey);QCOMPARE(preview,originalKey.data());
        QVERIFY(std::abs(preview->mapToScene({3,3}).x()-end.x())<2);
        QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);
        QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
        if(cancel)QTest::keyClick(&window,Qt::Key_Escape);
        QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(!model->dragging());
        if(cancel){QCOMPARE(revisions.count(),0);QCOMPARE(encodeIisc(*canvas.document()).bytes,original);}
    }
    QCOMPARE(revisions.count(),1);QCOMPARE(projections.count(),1);
    QCOMPARE(model->selectedKey()["frame"].toInt(),36);
    const auto capture=qEnvironmentVariable("DREAMSCAPES_CAPTURE_DIR");
    if(!capture.isEmpty())QVERIFY(window.grabWindow().save(capture+"/timeline-key-gesture.png"));
    model->undo();QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
}
void EditorLayerTimelineTests::denseKeyDragUsesPreparedIndex() {
    Document d;d.extent={1,1};d.timeline={{24,1},30000};
    d.assets.emplace_back(RasterAsset{"pixel",makeRasterLayer(1,1,0xff6688aaU)});
    LayerProperties p;p.id="dense";p.name="Dense keys";
    for(FrameIndex frame=0;frame<20000;frame+=2){MotionKeyframe key;key.frame=frame;p.motion.push_back(key);}
    d.layers.emplace_back(StaticBitmapLayer{std::move(p),StaticSource{"pixel"}});
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Dense keys"));
    EditorTimelineModel model;model.setCanvas(&canvas);model.setSnap(false);
    QVERIFY(model.selectKey("dense:motion:10000"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged),projections(&model,&EditorTimelineModel::projectionChanged);
    QElapsedTimer timer;timer.start();
    QVERIFY(model.beginKeyDrag("dense:motion:10000",0,1));
    const auto preparation=timer.nsecsElapsed()/1000;timer.restart();
    for(int i=0;i<480;++i)QVERIFY(model.updateDrag(i%2?1:3));
    const auto microseconds=timer.nsecsElapsed()/1000;
    qInfo()<<"10000 native motion keys; prepare (us):"<<preparation<<"480 updates (us):"<<microseconds;
    QVERIFY2(microseconds<250000,"Key previews must not rescan all document keys per pointer update");
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    QVERIFY(model.setTrackLocked("dense",true));QVERIFY(!model.dragging());
    QVERIFY(!model.beginKeyDrag("dense:motion:10000",0,1));
    QVERIFY(!model.finishDrag());
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
}
void EditorLayerTimelineTests::scrubbingDoesNotEditOrSave() {
    QTemporaryDir files(QString(DREAMSCAPES_TEST_DIRECTORY)+"/scrubbing-XXXXXX"); QVERIFY(files.isValid());
    Document d; d.extent={512,512}; d.timeline={{24,1},48};
    auto pixels=makeRasterLayer(512,512);
    for(int y=0;y<512;++y)for(int x=0;x<512;++x)
        pixels.pixels[y*512+x]=0xff000000U|(unsigned(x%256)<<16)|(unsigned(y%256)<<8)|unsigned((x^y)%256);
    d.assets.emplace_back(RasterAsset{"image",std::move(pixels)});
    for(int index=0;index<16;++index) {
        LayerProperties p; p.id="layer-"+std::to_string(index); p.name=p.id;
        d.layers.emplace_back(StaticBitmapLayer{p,StaticSource{"image"}});
    }
    const auto path=files.filePath("working.iisc");
    DocumentFile working; QVERIFY(working.create(path.toStdString(),d).ok()); working.close();
    EditorCanvas canvas; QVERIFY2(canvas.openDocumentSource(QUrl::fromLocalFile(path)),qPrintable(canvas.error()));
    EditorTimelineModel model; model.setCanvas(&canvas);
    const auto revision=canvas.revision();
    QFile original(path); QVERIFY(original.open(QIODevice::ReadOnly)); const auto bytes=original.readAll(); original.close();
    const bool couldUndo=canvas.canUndo();
    QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged), projections(&model,&EditorTimelineModel::changed);
    QElapsedTimer elapsed; elapsed.start();
    for(int frame=1;frame<=24;++frame) model.seek(frame);
    const auto microseconds=elapsed.nsecsElapsed()/1000;
    qInfo().noquote()<<QString("timeline_scrub: 24 seeks, 16 layers, 512x512; %1 us total; document revisions=%2; model changes=%3")
        .arg(microseconds).arg(revisions.count()).arg(projections.count());
    QCOMPARE(canvas.revision(),revision); QCOMPARE(revisions.count(),0); QCOMPARE(projections.count(),0);
    QCOMPARE(canvas.canUndo(),couldUndo); QCOMPARE(model.playhead(),24);
    QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(),bytes);
}
void EditorLayerTimelineTests::pointerScrubCoalescesCanvasUpdates() {
    auto d=fixture();d.timeline.frameCount=1000;
    QTemporaryDir files(QString(DREAMSCAPES_TEST_DIRECTORY)+"/pointer-scrub-XXXXXX");QVERIFY(files.isValid());
    const auto path=files.filePath("working.iisc");
    DocumentFile file;QVERIFY(file.create(path.toStdString(),d).ok());file.close();
    EditorCanvas canvas;QVERIFY(canvas.openDocumentSource(QUrl::fromLocalFile(path)));
    EditorTimelineModel model;model.setCanvas(&canvas);
    const auto original=encodeIisc(*canvas.document()).bytes;const auto revision=canvas.revision();
    QFile working(path);QVERIFY(working.open(QIODevice::ReadOnly));const auto bytes=working.readAll();working.close();
    const bool couldUndo=canvas.canUndo();
    QSignalSpy frames(&canvas,&CanvasItem::frameChanged),revisions(&canvas,&CanvasItem::revisionChanged),
        projections(&model,&EditorTimelineModel::projectionChanged),fileInfo(&canvas,&EditorCanvas::fileInformationChanged);
    bool started=false;
    QVERIFY(QMetaObject::invokeMethod(&model,"beginScrub",Q_RETURN_ARG(bool,started),Q_ARG(int,1)));
    QVERIFY(started);QCOMPARE(model.playhead(),1);QVERIFY(model.scrubbing());frames.clear();
    QElapsedTimer elapsed;elapsed.start();
    for(int frame=2;frame<482;++frame)model.updateScrub(frame);
    const auto microseconds=elapsed.nsecsElapsed()/1000;
    QCOMPARE(frames.count(),0);QCOMPARE(model.playhead(),1);
    model.finishScrub(700);QCOMPARE(model.playhead(),700);QCOMPARE(frames.count(),1);QVERIFY(!model.scrubbing());
    QTest::qWait(40);QCOMPARE(model.playhead(),700);QCOMPARE(frames.count(),1);
    qInfo()<<"480 pending scrub inputs (us):"<<microseconds<<"canvas updates before/after release:"<<0<<frames.count();
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);QCOMPARE(fileInfo.count(),0);
    QCOMPARE(canvas.revision(),revision);QCOMPARE(canvas.canUndo(),couldUndo);
    QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    QVERIFY(working.open(QIODevice::ReadOnly));QCOMPARE(working.readAll(),bytes);
    QVERIFY(model.beginScrub(20));model.updateScrub(30);
    QTRY_COMPARE_WITH_TIMEOUT(model.playhead(),30,1000); // A held gesture also advances before release.
    model.cancelScrub();QCOMPARE(model.playhead(),700);QVERIFY(!model.scrubbing());
    QTest::qWait(40);QCOMPARE(model.playhead(),700);
}
void EditorLayerTimelineTests::scrubCancellationAndOwnerChangesDropPendingFrames() {
    auto d=fixture();d.timeline.frameCount=1000;
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Scrub lifecycle"));
    EditorTimelineModel model;model.setCanvas(&canvas);
    QVERIFY(model.beginScrub(10));model.updateScrub(99);model.seek(42);
    QTest::qWait(40);QCOMPARE(model.playhead(),42);QVERIFY(!model.scrubbing());
    QVERIFY(model.beginScrub(10));model.updateScrub(99);canvas.setFrame(80);
    QTest::qWait(40);QCOMPARE(model.playhead(),80);QVERIFY(!model.scrubbing());
    QVERIFY(model.beginScrub(10));model.updateScrub(99);QVERIFY(canvas.loadProjectPage(d,"Replacement"));
    const auto replaced=canvas.frame();QTest::qWait(40);QCOMPARE(canvas.frame(),replaced);QVERIFY(!model.scrubbing());
    QVERIFY(model.beginScrub(10));model.updateScrub(99);
    EditorCanvas other;QVERIFY(other.loadProjectPage(d,"Other canvas"));model.setCanvas(&other);
    QTest::qWait(40);QCOMPARE(other.frame(),FrameIndex(0));QVERIFY(!model.scrubbing());
    auto owned=std::make_unique<EditorTimelineModel>();owned->setCanvas(&canvas);
    QVERIFY(owned->beginScrub(1));owned->updateScrub(77);owned.reset();
    QTest::qWait(40);QCOMPARE(canvas.frame(),FrameIndex(1));
    // Frame delivery can synchronously destroy the native gesture owner.
    owned=std::make_unique<EditorTimelineModel>();owned->setCanvas(&canvas);QVERIFY(owned->beginScrub(2));
    connect(&canvas,&CanvasItem::frameChanged,&canvas,[&]{if(canvas.frame()==20)owned.reset();});
    owned->updateScrub(20);QTRY_VERIFY(!owned);QCOMPARE(canvas.frame(),FrameIndex(20));
}
void EditorLayerTimelineTests::pointerRulerScrubFinishesAndEscapeRestoresTheStart() {
    auto d=fixture();d.timeline.frameCount=240;
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Ruler gesture"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({1200,500});
    QQuickWindow window;window.resize(1200,500);timeline->setParentItem(window.contentItem());window.show();QTest::qWait(60);
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);
    auto *view=qvariant_cast<EditorTimelineViewport *>(timeline->property("viewState"));QVERIFY(view);
    auto *lanes=visualItem(timeline.data(),"editorTimelineLanes");QVERIFY(lanes);
    const auto point=[&](int frame){return lanes->mapToScene({(frame+.25)*view->pixelsPerFrame(),10}).toPoint();};
    model->seek(7);QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged),projections(model,&EditorTimelineModel::projectionChanged);
    QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,point(24));QVERIFY(model->scrubbing());QCOMPARE(model->playhead(),24);
    QTest::mouseMove(&window,point(100),1);QTRY_COMPARE(model->playhead(),100);
    QTest::keyClick(&window,Qt::Key_Escape);QVERIFY(!model->scrubbing());QCOMPARE(model->playhead(),7);
    QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,point(100));QTest::qWait(40);QCOMPARE(model->playhead(),7);
    QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,point(24));QTest::mouseMove(&window,point(80),1);
    QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,point(90));QCOMPARE(model->playhead(),90);QVERIFY(!model->scrubbing());
    QTest::mousePress(&window,Qt::LeftButton,Qt::NoModifier,point(30));QTest::mouseMove(&window,point(120),1);
    timeline->setVisible(false);QVERIFY(!model->scrubbing());QTest::qWait(40);QCOMPARE(model->playhead(),90);
    QTest::mouseRelease(&window,Qt::LeftButton,Qt::NoModifier,point(120));
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
}
void EditorLayerTimelineTests::evictedThumbnailCanBeRequestedAgain() {
    Document d;d.extent={8,8};d.timeline={{24,1},200};
    d.assets.emplace_back(RasterAsset{"held",makeRasterLayer(8,8,0xffd05090U)});
    DynamicBitmapLayer layer;layer.properties.id="dynamic";layer.content.frameIndices={0};d.layers.push_back(layer);
    d.frames.push_back({0,{{"dynamic","held"}}});
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(d,"Thumbnail eviction"));EditorTimelineModel model;model.setCanvas(&canvas);
    QSignalSpy ready(&model,&EditorTimelineModel::thumbnailReady),broad(&model,&EditorTimelineModel::thumbnailsChanged);
    QTRY_VERIFY(!model.requestThumbnail("dynamic",0).isEmpty());
    const auto expected=model.requestThumbnail("dynamic",0);
    for(int frame=1;frame<130;++frame)QVERIFY(!model.thumbnail("dynamic",frame).isEmpty());
    QVERIFY(model.requestThumbnail("dynamic",0).isEmpty());QTRY_COMPARE(model.requestThumbnail("dynamic",0),expected);
    QCOMPARE(ready.count(),2);QCOMPARE(broad.count(),0);
}
void EditorLayerTimelineTests::mountedThumbnailsUseTargetedCompletion() {
    EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Mounted previews"));
    const auto original=encodeIisc(*canvas.document()).bytes;
    QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QString(DREAMSCAPES_QML_SOURCE_DIR)+"/Views/Editor/EditorTimeline.qml"));
    QScopedPointer<QQuickItem> timeline(qobject_cast<QQuickItem *>(component.createWithInitialProperties({{"canvas",QVariant::fromValue(&canvas)}})));
    QVERIFY2(timeline,qPrintable(component.errorString()));timeline->setSize({1200,500});
    QQuickWindow window;window.resize(1200,500);timeline->setParentItem(window.contentItem());window.show();
    auto *model=qvariant_cast<EditorTimelineModel *>(timeline->property("stateModel"));QVERIFY(model);
    auto *still=visualItem(timeline.data(),"editorTimelineThumbnail-still-layer");QVERIFY(still);
    auto *moving=visualItem(timeline.data(),"editorTimelineThumbnail-moving-layer");QVERIFY(moving);
    QTRY_COMPARE(still->property("status").toInt(),1);QTRY_COMPARE(moving->property("status").toInt(),1);
    const auto stillCompletion=still->property("completion").toInt(),movingCompletion=moving->property("completion").toInt();
    const auto previous=moving->property("source").toUrl();
    QSignalSpy ready(model,&EditorTimelineModel::thumbnailReady),broad(model,&EditorTimelineModel::thumbnailsChanged),
        revisions(&canvas,&CanvasItem::revisionChanged),projections(model,&EditorTimelineModel::projectionChanged);
    model->seek(5);
    QTRY_VERIFY(moving->property("source").toUrl()!=previous);
    QTRY_COMPARE(moving->property("status").toInt(),1);
    QCOMPARE(thumbnail(moving->property("source").toUrl()).pixelColor(32,32),QColor::fromRgba(0xff000000U|6*0x00112233U));
    QCOMPARE(still->property("completion").toInt(),stillCompletion);
    QCOMPARE(moving->property("completion").toInt(),movingCompletion+1);
    QCOMPARE(ready.count(),1);QCOMPARE(ready.front().front().toString(),"moving-layer");QCOMPARE(broad.count(),0);
    // A disabled view does not start fresh optional work as another view seeks.
    timeline->setEnabled(false);model->seek(3);QTest::qWait(40);QCOMPARE(ready.count(),1);
    timeline->setEnabled(true);
    QTRY_COMPARE(ready.count(),2);QTRY_COMPARE(moving->property("status").toInt(),1);
    QCOMPARE(thumbnail(moving->property("source").toUrl()).pixelColor(32,32),QColor::fromRgba(0xff000000U|4*0x00112233U));
    QCOMPARE(still->property("completion").toInt(),stillCompletion);QCOMPARE(broad.count(),0);
    QCOMPARE(revisions.count(),0);QCOMPARE(projections.count(),0);QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
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
    const auto origin=-lanes->property("contentX").toDouble();
    QCOMPARE(clip->mapToItem(lanes,QPointF()).x(),origin);
    const auto secondFrame=ticks[1]->objectName().mid(QString("editorTimelineRulerTick-").size()).toInt();
    auto *line=visualItem(timeline.data(),"editorTimelineRulerMark-"+QString::number(secondFrame));QVERIFY(line);
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
    QVERIFY(qAbs(playhead->mapToItem(lanes,QPointF(playhead->width()/2,0)).x()-origin-(frames-1)*pixelsPerFrame)<.01);
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
    QCOMPARE(pixel(marker,{4,4}),QColor("#548af7"));
    QCOMPARE(pixel(marker,{11,24}),QColor("#548af7"));
    QCOMPARE(pixel(marker,{1,23}),window.color());
    QCOMPARE(pixel(head,{1,50}),QColor("#548af7"));
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
    qmlRegisterType<EditorTimelineViewport>("Dreamscapes.Storage", 1, 0, "EditorTimelineViewport");
    EditorLayerTimelineTests tests; return QTest::qExec(&tests,argc,argv);
}
#include "tst_EditorLayerTimeline.moc"
