#include "App/Views/Editor/EditorTimelineViewport.h"
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest>
#include <cmath>
#include <limits>

namespace {
QVariantMap clip(QString id, int first, int last) {
    return {{"id", id}, {"in", first}, {"out", last}, {"type", "Layer"}, {"timing", "Static"}, {"locked", false}};
}
QVariantMap row(QString id, int top, QVariantList clips) {
    return {{"id", id}, {"top", top}, {"height", 40}, {"type", "Layer"}, {"clips", clips}};
}
}
class TimelineViewportTests : public QObject {
    Q_OBJECT
private slots:
    void anchoredZoomAndFrameMath() {
        EditorTimelineViewport view;
        view.setViewportWidth(900); view.setViewportHeight(180);
        view.setFrameCount(240); view.setZoom(4); view.setContentX(740);
        const auto at = (view.contentX() + 217) / view.pixelsPerFrame();
        const auto offset = view.zoomAt(1.5, 217); view.setContentX(offset);
        QVERIFY(std::abs((view.contentX()+217)/view.pixelsPerFrame()-at) < 1e-9);
        QCOMPARE(view.frameAt(at*view.pixelsPerFrame()), int(std::floor(at)));
        QCOMPARE(view.frameLabel(7), "F007");
        QCOMPARE(view.boundedScroll(-10), 0.0);
        QCOMPARE(view.boundedScroll(1e20), view.contentWidth()-view.viewportWidth());
        const auto before = view.zoom();
        view.zoomAt(std::numeric_limits<double>::quiet_NaN(), 20);
        view.setZoom(std::numeric_limits<double>::infinity());
        QCOMPARE(view.zoom(), before);
        view.setFrameCount(1); view.setFrameRate(30000.0/1001);
        QCOMPARE(view.viewFrameCount(), 30);
        QVERIFY(view.edgeScroll(899, 16) > view.contentX());
        QVERIFY(view.edgeScroll(0, 16) < view.contentX());
        QCOMPARE(view.edgeScroll(450, 16), view.contentX());
        view.setContentX(0);QCOMPARE(view.edgeScroll(-1000,16),0.0);
        view.setContentX(view.contentWidth());
        QCOMPARE(view.edgeScroll(1e10,16),view.contentWidth()-view.viewportWidth());
        view.setZoom(1);QCOMPARE(view.contentX(),0.0);
        QVERIFY(!view.majorTicks().isEmpty());
    }
    void visibilityIsBoundedAndHitTestingUsesTheSameGeometry() {
        EditorTimelineViewport view; view.setViewportWidth(900); view.setViewportHeight(160);
        view.setFrameCount(10000); view.setZoom(64); view.setContentX(25000);
        view.setVerticalOffset(44+4000*40);
        QVariantList rows;
        for(int i=0;i<10000;++i) rows.append(row(QString::number(i),44+i*40,
            {clip(QString::number(i)+"-all",0,10000),clip(QString::number(i)+"-late",9900,10000)}));
        view.setTrackLayout(rows);
        QVERIFY(view.visibleTracks().size() <= 8);
        QVERIFY(view.majorTicks().size() < 25); QVERIFY(view.minorTicks().size() < 160);
        for(const auto &value:view.visibleTracks()) QCOMPARE(value.toMap()["clips"].toList().size(),1);
        const auto y=44+4000*40+15;
        const auto hit=view.hitTest(view.contentX()+100,y);
        QCOMPARE(hit["kind"].toString(),"clip"); QCOMPARE(hit["id"].toString(),"4000-all");
        QCOMPARE(view.hitTest(0,y)["edge"].toString(),"in");
        const auto original=view.trackLayout();
        QSignalSpy changed(&view,&EditorTimelineViewport::visibleTracksChanged);
        view.setContentX(view.contentX()+1);
        QCOMPARE(changed.count(),0); // Stable membership retains delegates while panning.
        QCOMPARE(view.trackLayout(),original);
        QElapsedTimer clock;clock.start();
        for(int i=0;i<240;++i)view.setContentX(25000+i);
        qInfo()<<"240 viewport updates for 10000 tracks (us):"<<clock.nsecsElapsed()/1000;
    }
    void previewFilteringAndEscapeRestoreDoNotChangeProjection() {
        EditorTimelineViewport view;view.setViewportWidth(800);view.setViewportHeight(200);
        view.setFrameCount(1000);view.setZoom(8);view.setContentX(0);
        view.setTrackLayout({row("audio",44,{clip("near",0,10),clip("far",800,900)})});
        QCOMPARE(view.visibleTracks().front().toMap()["clips"].toList().size(),1);
        const auto original=view.trackLayout();
        view.setDragPreview({{"near",clip("near",800,810)},{"far",clip("far",10,20)}});
        const auto clips=view.visibleTracks().front().toMap()["clips"].toList();
        QCOMPARE(clips.size(),1);QCOMPARE(clips.front().toMap()["id"].toString(),"far");
        QCOMPARE(view.hitTest(15*view.pixelsPerFrame(),55)["id"].toString(),"far");
        view.setDragPreview({});
        QCOMPARE(view.visibleTracks().front().toMap()["clips"].toList().front().toMap()["id"].toString(),"near");
        QCOMPARE(view.trackLayout(),original);
    }
    void keyPreviewKeepsVisibleMembershipAndCurveNeighbours() {
        EditorTimelineViewport view;view.setViewportWidth(500);view.setViewportHeight(150);
        view.setFrameCount(10000);view.setZoom(128);view.setContentX(3200);
        auto track=row("keys",44,{});QVariantList keys;
        for(int frame=0;frame<10000;frame+=10)keys.append(QVariantMap{{"frame",frame},{"id",QString::number(frame)},{"kind","Motion"},{"trackId","keys"}});
        track["keys"]=keys;track["motionKeys"]=keys;view.setTrackLayout({track});
        const auto original=view.trackLayout();
        const auto preview=[&](int frame) {return QVariantMap{{"id","0"},{"trackId","keys"},{"kind","Motion"},{"frame",frame},{"valid",true}};};
        view.setKeyDragPreview(preview(525));
        const auto shown=view.visibleTracks().front().toMap();
        const auto matching=[&](const QVariantList &rows){
            int count=0;for(const auto &row:rows)if(row.toMap()["id"]=="0")++count;return count;
        };
        QCOMPARE(matching(shown["keys"].toList()),1);
        QCOMPARE(view.hitTest(525*view.pixelsPerFrame(),48)["id"].toString(),"0");
        QCOMPARE(view.trackLayout(),original);
        QSignalSpy membership(&view,&EditorTimelineViewport::visibleTracksChanged);
        view.setKeyDragPreview(preview(526));
        QCOMPARE(membership.count(),0); // Coordinates retain the same delegates.
        // Move a visible key offscreen; its old position and old curve neighbour disappear.
        auto moved=preview(9000);moved["id"]="510";view.setKeyDragPreview(moved);
        const auto curve=view.visibleTracks().front().toMap()["motionKeys"].toList();
        QCOMPARE(matching(curve),0);
        for(const auto &key:curve)QVERIFY(key.toMap()["id"]!="510");
        view.setKeyDragPreview({});
        QCOMPARE(view.trackLayout(),original);
        auto atBoundary=preview(489);view.setKeyDragPreview(atBoundary);
        const auto boundary=view.curveKeys("keys");
        QCOMPARE(boundary.front().toMap()["frame"].toInt(),489);
        // Other tracks and content keys do not enter a motion curve.
        moved["trackId"]="elsewhere";view.setKeyDragPreview(moved);
        const auto normal=view.visibleTracks().front().toMap();
        atBoundary["kind"]="Content";view.setKeyDragPreview(atBoundary);
        QCOMPARE(view.visibleTracks().front().toMap()["motionKeys"],normal["motionKeys"]);
        QSignalSpy projection(&view,&EditorTimelineViewport::trackLayoutChanged);
        QElapsedTimer clock;clock.start();
        for(int i=0;i<480;++i)view.setKeyDragPreview(preview(525+i%2));
        qInfo()<<"480 projected key previews over 1000 indexed keys (us):"<<clock.nsecsElapsed()/1000;
        QCOMPARE(projection.count(),0);QVERIFY(view.visibleTracks().front().toMap()["keys"].toList().size()<20);
    }
    void keysKeepCurveNeighboursAndMalformedInputsStayBounded() {
        EditorTimelineViewport view;view.setViewportWidth(500);view.setViewportHeight(150);
        view.setFrameCount(1000);view.setZoom(10);view.setContentX(2000);
        auto track=row("keys",44,{}); QVariantList keys;
        for(int frame=0;frame<1000;frame+=10)keys.append(QVariantMap{{"frame",frame},{"id",QString::number(frame)}});
        track["keys"]=keys;track["motionKeys"]=keys;view.setTrackLayout({track});
        const auto shown=view.visibleTracks().front().toMap();
        QVERIFY(shown["keys"].toList().size() < keys.size());
        const auto curve=shown["motionKeys"].toList();
        QVERIFY(curve.front().toMap()["frame"].toInt() < view.firstVisibleFrame());
        QVERIFY(curve.back().toMap()["frame"].toInt() > view.lastVisibleFrame());
        QCOMPARE(view.hitTest(420*view.pixelsPerFrame(),48)["kind"].toString(),"key");
        QCOMPARE(view.hitTest(420*view.pixelsPerFrame(),48)["id"].toString(),"420");
        QCOMPARE(view.hitTest(420*view.pixelsPerFrame(),65)["kind"].toString(),"empty");
        view.setZoom(128);view.setContentX(view.contentWidth());
        view.setZoom(1);QCOMPARE(view.contentX(),0.0);
        QVERIFY(!view.visibleTracks().front().toMap()["keys"].toList().isEmpty());
        view.setZoom(16);view.setContentX(7000);view.setViewportWidth(100);
        QVERIFY(view.contentX()<=view.contentWidth()-view.viewportWidth());
        view.setViewportWidth(std::numeric_limits<double>::quiet_NaN());
        view.setVerticalOffset(std::numeric_limits<double>::infinity());
        QVERIFY(std::isfinite(view.pixelsPerFrame()));
        view.setTrackLayout({QVariantMap{{"top",-1},{"height",-1}}});
        QVERIFY(view.visibleTracks().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TimelineViewportTests)
#include "tst_timelineviewport.moc"
