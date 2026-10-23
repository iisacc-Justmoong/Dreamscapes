#include "App/Views/Editor/EditorFrameExporter.h"
#include "App/Views/Editor/EditorCanvas.h"
#include "App/Views/Editor/EditorProject.h"
#include <iiSharedCanvas/Serialization/IiscCodec.h>
#include <iiSharedCanvas/File/DocumentFile.h>
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <QFile>
#include <QImage>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QScopeGuard>
#include <QtTest>
#include <atomic>
#include <semaphore>
#include <limits>
#include <stdexcept>

using namespace iiSharedCanvas;
namespace {
Document fixture() {
    Document d;d.extent={8,4};d.timeline={{24,1},2};
    d.assets.emplace_back(RasterAsset{"red",makeRasterLayer(8,4,0xffff0000U)});
    d.assets.emplace_back(RasterAsset{"blue",makeRasterLayer(8,4,0xff0000ffU)});
    DynamicBitmapLayer layer;layer.properties.id="animation";layer.content.frameIndices={0,1};
    d.layers.emplace_back(layer);d.frames={{0,{{"animation","red"}}},{1,{{"animation","blue"}}}};
    return d;
}
EditorFrameSnapshot snapshot(const Document &document,FrameIndex frame=0) {
    return {std::make_shared<const Document>(document),frame,{}};
}
struct Gate {
    std::counting_semaphore<4> release{0};
    std::atomic<bool> entered=false,finished=false;
};
}
class FrameExporterTests : public QObject {
    Q_OBJECT
private slots:
    void terminalOutcomePrecedesIdleDeletion_data() {
        QTest::addColumn<bool>("failure");QTest::newRow("saved")<<false;QTest::newRow("failed")<<true;
    }
    void terminalOutcomePrecedesIdleDeletion() {
        QFETCH(bool,failure);QStringList order;
        QPointer<EditorFrameExporter> exporter=new EditorFrameExporter([failure](const auto &){return failure?QString("Fixture failure"):QString{};},nullptr);
        connect(exporter,&EditorFrameExporter::saved,this,[&]{order.append("saved");});
        connect(exporter,&EditorFrameExporter::failed,this,[&]{order.append("failed");});
        QVERIFY(exporter->prepare(snapshot(fixture()),300));
        connect(exporter,&EditorFrameExporter::stateChanged,this,[&]{if(exporter&&!exporter->busy()){order.append("idle");delete exporter.data();}});
        QVERIFY(exporter->start(QUrl("file:///destination.png"),"PNG"));
        QTRY_VERIFY(exporter.isNull());QCOMPARE(order,QStringList({failure?"failed":"saved","idle"}));
    }
    void projectReplacementKeepsThePreparedExport() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/replaced-export-XXXXXX");QVERIFY(directory.isValid());
        EditorProject project;QVERIFY(project.currentCanvas()->loadProjectPage(fixture(),"Chosen document"));
        project.currentCanvas()->setFrame(1);QVERIFY(project.currentCanvas()->prepareImageExport());
        QPointer<EditorFrameExporter> exporter=qobject_cast<EditorFrameExporter *>(project.currentCanvas()->frameExporter());QVERIFY(exporter);
        QVERIFY(project.createCanvas({{"width",8},{"height",4},{"unit","px"},{"background","White"}}));
        QVERIFY2(!exporter.isNull(),"Replacing the canvas must retain the project's prepared export owner.");
        QCOMPARE(project.currentCanvas()->frameExporter(),exporter.data());
        QSignalSpy saved(exporter,&EditorFrameExporter::saved);const auto destination=QUrl::fromLocalFile(directory.filePath("chosen.png"));
        QVERIFY(project.currentCanvas()->exportImage(destination,"PNG"));QTRY_COMPARE_WITH_TIMEOUT(saved.count(),1,10000);
        const QImage output(destination.toLocalFile());QCOMPARE(output.size(),QSize(8,4));QCOMPARE(output.pixelColor(0,0),QColor(Qt::blue));
    }
    void invalidRequestsDoNotStartOrPublishFiles() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/invalid-export-XXXXXX");QVERIFY(directory.isValid());
        EditorFrameExporter exporter;QSignalSpy rejected(&exporter,&EditorFrameExporter::rejected),failed(&exporter,&EditorFrameExporter::failed);
        QVERIFY(!exporter.prepare({},300));QVERIFY(!exporter.prepare(snapshot(fixture()),std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!exporter.prepare(snapshot(fixture()),0));QVERIFY(!exporter.busy());QCOMPARE(failed.count(),0);
        QVERIFY(exporter.prepare(snapshot(fixture()),300));
        QVERIFY(!exporter.start(QUrl("https://example.invalid/frame.png"),"PNG"));QVERIFY(exporter.prepared());
        const auto destination=QUrl::fromLocalFile(directory.filePath("invalid.png"));
        EditorFrameExporter::Request request{snapshot(fixture()),destination,"PNG",std::numeric_limits<double>::infinity()};
        QVERIFY(!EditorFrameExporter::exportSnapshot(request).isEmpty());QVERIFY(!QFileInfo::exists(destination.toLocalFile()));
        QCOMPARE(rejected.count(),4);QCOMPARE(failed.count(),0);
    }
    void capturesFrameAndDensityBeforeLaterNavigation() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/frame-export-XXXXXX");QVERIFY(directory.isValid());
        EditorCanvas canvas;QVERIFY(canvas.loadProjectPage(fixture(),"Export owner"));canvas.setFrame(1);
        QVERIFY(canvas.prepareImageExport());
        auto *exporter=qobject_cast<EditorFrameExporter *>(canvas.frameExporter());QVERIFY(exporter);
        QVERIFY(exporter->prepared());
        canvas.setFrame(0);const auto original=encodeIisc(*canvas.document()).bytes;
        QSignalSpy saved(exporter,&EditorFrameExporter::saved),failed(exporter,&EditorFrameExporter::failed);
        QSignalSpy revisions(&canvas,&CanvasItem::revisionChanged);
        const auto destination=QUrl::fromLocalFile(directory.filePath("frame.png"));
        QVERIFY(canvas.exportImage(destination,"PNG"));QVERIFY(exporter->busy());QCOMPARE(saved.count(),0);
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(),1,10000);QCOMPARE(failed.count(),0);QVERIFY(!exporter->busy());
        const QImage image(destination.toLocalFile());QCOMPARE(image.size(),QSize(8,4));QCOMPARE(image.pixelColor(0,0),QColor(Qt::blue));
        QCOMPARE(image.dotsPerMeterX(),qRound(300/0.0254));QCOMPARE(revisions.count(),0);
        QCOMPARE(encodeIisc(*canvas.document()).bytes,original);
    }
    void eventLoopRemainsAvailableAndDuplicateIsNotATerminalFailure() {
        const auto gate=std::make_shared<Gate>();
        EditorFrameExporter exporter([gate](const EditorFrameExporter::Request &request) {
            gate->entered=true;
            if(!gate->release.try_acquire_for(std::chrono::seconds(5)))return QString("Test gate timed out");
            gate->finished=true;return request.snapshot.frame==1?QString{}:QString("Wrong captured frame");
        },nullptr);
        const auto unblock=qScopeGuard([gate]{gate->release.release();});
        QSignalSpy saved(&exporter,&EditorFrameExporter::saved),failed(&exporter,&EditorFrameExporter::failed),rejected(&exporter,&EditorFrameExporter::rejected);
        QVERIFY(exporter.prepare(snapshot(fixture(),1),300));const QUrl first("file:///accepted.png");
        QVERIFY(exporter.start(first,"PNG"));QTRY_VERIFY(gate->entered.load());
        bool input=false;QTimer::singleShot(0,[&]{input=true;});QTRY_VERIFY(input);
        QVERIFY(!exporter.start(QUrl("file:///duplicate.png"),"PNG"));QCOMPARE(rejected.count(),1);QCOMPARE(failed.count(),0);
        QCOMPARE(exporter.destination(),first);QVERIFY(exporter.busy());
        gate->release.release();QTRY_COMPARE(saved.count(),1);QCOMPARE(saved.at(0).at(0).toUrl(),first);
        QCOMPARE(failed.count(),0);QVERIFY(!exporter.busy());
    }
    void ownerDestructionDoesNotWaitOrCancelAcceptedWork() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/detached-export-XXXXXX");QVERIFY(directory.isValid());
        const auto destination=QUrl::fromLocalFile(directory.filePath("accepted.png"));
        const auto gate=std::make_shared<Gate>();
        auto *exporter=new EditorFrameExporter([gate](const EditorFrameExporter::Request &request) {
            gate->entered=true;
            if(!gate->release.try_acquire_for(std::chrono::seconds(5)))return QString("Test gate timed out");
            const auto error=EditorFrameExporter::exportSnapshot(request);gate->finished=true;return error;
        },nullptr);
        const auto unblock=qScopeGuard([gate]{gate->release.release();});
        QVERIFY(exporter->prepare(snapshot(fixture()),300));QVERIFY(exporter->start(destination,"PNG"));
        QTRY_VERIFY(gate->entered.load());QElapsedTimer elapsed;elapsed.start();delete exporter;
        QVERIFY(elapsed.elapsed()<100);QVERIFY(!gate->finished.load());
        gate->release.release();QTRY_VERIFY(gate->finished.load());
        const QImage image(destination.toLocalFile());QCOMPARE(image.pixelColor(0,0),QColor(Qt::red));
    }
    void completionRetainsItsIdentityWhenAnObserverStartsTheNextExport() {
        EditorFrameExporter exporter([](const EditorFrameExporter::Request &request) {
            return request.destination.path().endsWith("first.png")?QString("First export failed"):QString{};
        },nullptr);
        const QUrl first("file:///first.png"),second("file:///second.png");
        QSignalSpy saved(&exporter,&EditorFrameExporter::saved),failed(&exporter,&EditorFrameExporter::failed);
        bool secondAccepted=false;
        connect(&exporter,&EditorFrameExporter::stateChanged,this,[&] {
            if(!exporter.busy()&&exporter.destination()==first&&!secondAccepted) {
                secondAccepted=true;QVERIFY(exporter.prepare(snapshot(fixture()),300));QVERIFY(exporter.start(second,"PNG"));
            }
        });
        QVERIFY(exporter.prepare(snapshot(fixture()),300));QVERIFY(exporter.start(first,"PNG"));
        QTRY_COMPARE(failed.count(),1);QTRY_COMPARE(saved.count(),1);QVERIFY(secondAccepted);
        QCOMPARE(failed.at(0).at(0).toUrl(),first);QCOMPARE(failed.at(0).at(1).toString(),QString("First export failed"));
        QCOMPARE(saved.at(0).at(0).toUrl(),second);QVERIFY(exporter.error().isEmpty());
    }
    void workerExceptionsBecomeTerminalFailures() {
        EditorFrameExporter exporter([](const EditorFrameExporter::Request &)->QString {throw std::runtime_error("Fixture export failure");},nullptr);
        QSignalSpy failed(&exporter,&EditorFrameExporter::failed);const QUrl destination("file:///exception.png");
        QVERIFY(exporter.prepare(snapshot(fixture()),300));QVERIFY(exporter.start(destination,"PNG"));
        QTRY_COMPARE(failed.count(),1);QCOMPARE(failed.at(0).at(0).toUrl(),destination);
        QVERIFY(failed.at(0).at(1).toString().contains("Fixture export failure"));QVERIFY(!exporter.busy());
    }
    void workerErrorsAndOccupiedNativeFilesAreReportedWithoutMutation() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/frame-export-errors-XXXXXX");QVERIFY(directory.isValid());
        EditorFrameExporter exporter;QSignalSpy saved(&exporter,&EditorFrameExporter::saved),failed(&exporter,&EditorFrameExporter::failed);
        const auto target=QUrl::fromLocalFile(directory.filePath("original.png"));
        {QFile file(target.toLocalFile());QVERIFY(file.open(QIODevice::WriteOnly));file.write("preserve original");}
        QVERIFY(exporter.prepare(snapshot(fixture()),300));QVERIFY(exporter.start(target,"unsupported-codec"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,10000);QCOMPARE(saved.count(),0);QVERIFY(!exporter.error().isEmpty());
        {QFile file(target.toLocalFile());QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("preserve original"));}
        const auto native=QUrl::fromLocalFile(directory.filePath("frame.iisc"));
        QVERIFY(exporter.prepare(snapshot(fixture(),1),300));QVERIFY(exporter.start(native,"IISC"));
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(),1,10000);
        DocumentFile reopened;QVERIFY(reopened.open(native.toLocalFile().toStdString()).ok());
        const auto rendered=renderFrame(*reopened.document(),0);QVERIFY(rendered.ok());QCOMPARE(rendered.pixels.pixels.front(),0xff0000ffU);reopened.close();
        QFile file(native.toLocalFile());QVERIFY(file.open(QIODevice::ReadOnly));const auto original=file.readAll();file.close();
        QVERIFY(exporter.prepare(snapshot(fixture()),300));QVERIFY(exporter.start(native,"IISC"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(),2,10000);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),original);
    }
    void cancellationAndReentrantOwnerDestructionAreSafe() {
        int calls=0;EditorFrameExporter cancelled([&](const EditorFrameExporter::Request &){++calls;return QString{};},nullptr);
        QVERIFY(cancelled.prepare(snapshot(fixture()),300));cancelled.cancelPrepared();QVERIFY(!cancelled.prepared());
        QVERIFY(!cancelled.start(QUrl("file:///cancelled.png"),"PNG"));QCOMPARE(calls,0);
        const auto gate=std::make_shared<Gate>();QPointer<EditorFrameExporter> owner=new EditorFrameExporter(
            [gate](const EditorFrameExporter::Request &){gate->finished=true;return QString{};},nullptr);
        QVERIFY(owner->prepare(snapshot(fixture()),300));
        connect(owner,&EditorFrameExporter::stateChanged,this,[&]{if(owner&&owner->busy())delete owner.data();});
        QVERIFY(owner->start(QUrl("file:///accepted.png"),"PNG"));QVERIFY(owner.isNull());QTRY_VERIFY(gate->finished.load());
    }
};
int main(int argc,char **argv) {
    QTemporaryDir settings(DREAMSCAPES_TEST_DIRECTORY "/frame-export-settings-XXXXXX");if(!settings.isValid())return 2;
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH",(settings.path()+"/storage.json").toUtf8());qputenv("SOCIETY_DISABLE_SESSION_RESTORE","1");
    QGuiApplication app(argc,argv);FrameExporterTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "tst_frameexporter.moc"
