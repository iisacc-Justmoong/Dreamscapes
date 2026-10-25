#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorTimelineModel.h"
#include <iiSharedCanvas/File/DocumentFile.h>
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <QFile>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace iiSharedCanvas;
namespace {
Document frames(int count=3) {
    Document document;document.extent={512,128};document.timeline={{24,1},quint32(count)};
    auto split=makeRasterLayer(512,128,0xffe03030U);
    for(int y=0;y<128;++y)for(int x=256;x<512;++x)split.pixels[y*512+x]=0xff3050e0U;
    document.assets.emplace_back(RasterAsset{"first",std::move(split)});
    document.assets.emplace_back(RasterAsset{"next",makeRasterLayer(512,128,0xff20c050U)});
    document.assets.emplace_back(RasterAsset{"empty",makeRasterLayer(512,128,0x00000000U)});
    DynamicBitmapLayer layer;layer.properties.id="animation";layer.content.frameIndices={0,1,2};
    document.layers.emplace_back(std::move(layer));
    document.frames={{0,{{"animation","first"}}},{1,{{"animation","next"}}},{2,{{"animation","empty"}}}};
    return document;
}
QColor pixel(const QImage& image,int x,int y) {
    if(image.isNull())return {};
    const auto scale=image.width()/256.;return image.pixelColor(qRound(x*scale),qRound(y*scale));
}
bool prepared(EditorCanvas& canvas) {return !canvas.rendering()&&canvas.residentTileCount()>0;}
}
class CanvasPresentationTests:public QObject {
    Q_OBJECT
private slots:
    void workingFileKeepsTheCompleteFrameUntilItsReplacementIsReady_data() {
        QTest::addColumn<bool>("animated");
        QTest::newRow("static-iisc")<<false;QTest::newRow("dynamic-iisc")<<true;
    }
    void workingFileKeepsTheCompleteFrameUntilItsReplacementIsReady() {
        QFETCH(bool,animated);auto document=frames();
        if(!animated) {
            document.layers.clear();document.frames.clear();
            document.layers.emplace_back(StaticBitmapLayer{{"animation","Still"},StaticSource{"first"}});
        }
        QTemporaryDir files(QString(DREAMSCAPES_TEST_DIRECTORY)+"/frame-swap-XXXXXX");QVERIFY(files.isValid());
        const auto path=files.filePath("animation.iisc");DocumentFile file;QVERIFY(file.create(path.toStdString(),document).ok());file.close();
        QFile original(path);QVERIFY(original.open(QIODevice::ReadOnly));const auto bytes=original.readAll();original.close();
        QQuickWindow window;window.resize(256,128);window.setColor(Qt::cyan);
        EditorCanvas canvas(window.contentItem());canvas.setSize(window.size());
        QVERIFY2(canvas.openDocumentSource(QUrl::fromLocalFile(path)),qPrintable(canvas.error()));
        canvas.setZoom(1);canvas.setPanX(0);canvas.setPanY(0);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(prepared(canvas));QTRY_COMPARE(pixel(window.grabWindow(),64,64),QColor(0xe0,0x30,0x30));
        const auto revision=canvas.revision();const bool undo=canvas.canUndo();QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged);
        for(int iteration=0;iteration<6;++iteration) {
            const bool next=iteration%2==0;canvas.setFrame(next?1:0);
            QCOMPARE(canvas.residentTileCount(),0);QVERIFY(canvas.rendering());
            // Synchronous scene-graph presentation before queued rendering can start.
            QCOMPARE(pixel(window.grabWindow(),64,64),animated&&!next?QColor(0x20,0xc0,0x50):QColor(0xe0,0x30,0x30));
            QTRY_VERIFY(prepared(canvas));QTRY_COMPARE(pixel(window.grabWindow(),64,64),animated&&next?QColor(0x20,0xc0,0x50):QColor(0xe0,0x30,0x30));
        }
        QCOMPARE(revisions.count(),0);QCOMPARE(canvas.revision(),revision);QCOMPARE(canvas.canUndo(),undo);
        QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(original.readAll(),bytes);
    }
    void aDecodedTransparentFrameActuallyReplacesThePriorImage() {
        QQuickWindow window;window.resize(256,128);window.setColor(Qt::cyan);
        EditorCanvas canvas(window.contentItem());canvas.setSize(window.size());QVERIFY(canvas.loadProjectPage(frames(),"Transparency"));
        canvas.setZoom(1);canvas.setPanX(0);canvas.setPanY(0);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(prepared(canvas));QTRY_COMPARE(pixel(window.grabWindow(),64,64),QColor(0xe0,0x30,0x30));
        canvas.setFrame(2);QCOMPARE(pixel(window.grabWindow(),64,64),QColor(0xe0,0x30,0x30));
        QTRY_VERIFY(prepared(canvas));QTRY_COMPARE(pixel(window.grabWindow(),64,64),QColor(Qt::cyan));
    }
    void navigationTransformsTheRetainedFrameWhileRendering() {
        QQuickWindow window;window.resize(256,128);window.setColor(Qt::cyan);
        EditorCanvas canvas(window.contentItem());canvas.setSize(window.size());QVERIFY(canvas.loadProjectPage(frames(),"Pan pending"));
        canvas.setZoom(1);canvas.setPanX(0);canvas.setPanY(0);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(prepared(canvas));QTRY_COMPARE(pixel(window.grabWindow(),240,64),QColor(0xe0,0x30,0x30));
        canvas.setFrame(1);canvas.setPanX(-64);QCOMPARE(canvas.panX(),-64.);
        const auto pending=window.grabWindow();QCOMPARE(pixel(pending,64,64),QColor(0xe0,0x30,0x30));
        QCOMPARE(pixel(pending,240,64),QColor(0x30,0x50,0xe0));
        QTRY_VERIFY(prepared(canvas));QTRY_COMPARE(pixel(window.grabWindow(),240,64),QColor(0x20,0xc0,0x50));
    }
    void replacingOrClosingTheDocumentDiscardsThePriorFrame() {
        QQuickWindow window;window.resize(256,128);window.setColor(Qt::cyan);
        EditorCanvas canvas(window.contentItem());canvas.setSize(window.size());QVERIFY(canvas.loadProjectPage(frames(),"Original"));
        canvas.setZoom(1);canvas.setPanX(0);canvas.setPanY(0);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(prepared(canvas));QTRY_COMPARE(pixel(window.grabWindow(),64,64),QColor(0xe0,0x30,0x30));
        canvas.setFrame(1);Document empty;empty.extent={512,128};QVERIFY(canvas.loadProjectPage(empty,"Replacement"));
        QCOMPARE(pixel(window.grabWindow(),64,64),QColor(Qt::cyan));
        QTRY_VERIFY(prepared(canvas));canvas.unbind();QCOMPARE(pixel(window.grabWindow(),64,64),QColor(Qt::cyan));
    }
    void playbackDoesNotCancelAnOutstandingFrameOnEveryTick() {
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(frames(60),"Playback"));
        EditorTimelineModel timeline;timeline.setCanvas(&canvas);QTRY_VERIFY(prepared(canvas));
        const auto original=encodeIisc(*canvas.document()).bytes;const auto revision=canvas.revision();
        timeline.seek(1);QVERIFY(canvas.rendering());timeline.setPlaying(true);QVERIFY(timeline.playing());
        // Leave the owner thread's scheduled render pending while the wall clock advances.
        QTest::qSleep(100);timeline.updatePlaybackPosition();QCOMPARE(timeline.playhead(),1);
        QTRY_VERIFY(timeline.playhead()>1);timeline.setPlaying(false);
        QCOMPARE(canvas.revision(),revision);QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    }
};
QTEST_MAIN(CanvasPresentationTests)
#include "tst_canvaspresentation.moc"
