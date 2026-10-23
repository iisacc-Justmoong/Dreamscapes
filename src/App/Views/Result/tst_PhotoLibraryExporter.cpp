#include "PhotoLibraryExporter.h"
#include <QFile>
#include <QImage>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <semaphore>
#include <QTimer>
#include <iiSharedCanvas/File/DocumentFile.h>
#include <iiSharedCanvas/Serialization/IiscCodec.h>

class PhotoLibraryExporterTests : public QObject
{
    Q_OBJECT
private slots:
    void preparationDoesNotBlockAndAcceptedDispatchSurvivesViewDeletion_data() {
        QTest::addColumn<bool>("close");QTest::newRow("visible")<<false;QTest::newRow("closed")<<true;
    }
    void preparationDoesNotBlockAndAcceptedDispatchSurvivesViewDeletion() {
        QFETCH(bool,close);auto gate=std::make_shared<std::counting_semaphore<2>>(0);
        auto entered=std::make_shared<std::atomic_bool>(false);auto thread=std::make_shared<std::thread::id>();
        const auto owner=std::this_thread::get_id();const auto unblock=qScopeGuard([gate]{gate->release();});
        int nativeCalls=0;PhotoLibraryExporter::Completion reply;
        QPointer<PhotoLibraryExporter> exporter=new PhotoLibraryExporter(
            [gate,entered,thread](const QUrl &source) {
                *thread=std::this_thread::get_id();entered->store(true);
                if(!gate->try_acquire_for(std::chrono::seconds(5)))return PhotoLibraryExporter::Preparation{{},"Gate timed out",{}};
                return PhotoLibraryExporter::Preparation{{source.toLocalFile(),"image/png"},{},{}};
            },[&](const PhotoLibraryExporter::Image &image,auto completion){
                QVERIFY(std::this_thread::get_id()==owner);QCOMPARE(image.mimeType,QString("image/png"));
                ++nativeCalls;reply=std::move(completion);
            },nullptr);
        int inputs=0;QTimer input;input.setInterval(1);connect(&input,&QTimer::timeout,this,[&]{++inputs;});input.start();
        QVERIFY(exporter->save(QUrl("file:///captured-source.png")));QTRY_VERIFY(entered->load());
        QVERIFY(*thread!=owner);QTRY_VERIFY(inputs>=3);QCOMPARE(nativeCalls,0);
        if(close)delete exporter.data();
        gate->release();QTRY_COMPARE(nativeCalls,1);reply("accepted-after-preparation",{});
        if(!close){QTRY_VERIFY(!exporter->busy());delete exporter.data();}
        QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
    }
    void nativeCanvasPreparationRetainsItsTemporaryUntilFirstReply_data() {
        QTest::addColumn<bool>("working");QTest::addColumn<bool>("close");QTest::addColumn<bool>("failure");
        QTest::newRow("snapshot-saved")<<false<<false<<false;
        QTest::newRow("working-failed")<<true<<false<<true;
        QTest::newRow("working-closed")<<true<<true<<false;
    }
    void nativeCanvasPreparationRetainsItsTemporaryUntilFirstReply() {
        QFETCH(bool,working);QFETCH(bool,close);QFETCH(bool,failure);using namespace iiSharedCanvas;
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/native-photo-XXXXXX");QVERIFY(directory.isValid());
        const auto source=directory.filePath("original.iisc");Document d;d.extent={5001,2};
        d.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(5001,2,0x804080c0U)});
        d.layers.emplace_back(StaticBitmapLayer{{"layer","Native"},StaticSource{"pixels"}});
        StableDiffusionMetadata metadata;metadata.positivePrompt="Preserve native";
        metadata.generationParametersText="Preserve native\nSteps: 20, Seed: 7";d.stableDiffusionMetadata=metadata;
        if(working){DocumentFile author;QVERIFY(author.create(source.toStdString(),d).ok());}
        else{const auto encoded=encodeIisc(d);QVERIFY(encoded.ok());QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));file.write(reinterpret_cast<const char *>(encoded.bytes.data()),qint64(encoded.bytes.size()));}
        QFile original(source);QVERIFY(original.open(QIODevice::ReadOnly));const auto bytes=original.readAll();original.close();
        QString prepared;int nativeCalls=0;PhotoLibraryExporter::Completion reply;
        QPointer<PhotoLibraryExporter> exporter=new PhotoLibraryExporter([&](const PhotoLibraryExporter::Image &image,auto completion){
            prepared=image.path;QCOMPARE(image.mimeType,QString("image/png"));++nativeCalls;reply=std::move(completion);
        },nullptr);
        QVERIFY(exporter->save(QUrl::fromLocalFile(source)));QTRY_COMPARE(nativeCalls,1);
        QVERIFY(prepared!=source);QVERIFY(QFileInfo::exists(prepared));const QImage image(prepared);
        QCOMPARE(image.size(),QSize(5001,2));QCOMPARE(image.pixel(5000,1),0x804080c0U);
        QCOMPARE(image.text("parameters"),QString::fromStdString(metadata.generationParametersText));
        if(close)delete exporter.data();
        reply(failure?QString{}:QString("native-asset"),failure?QString("Permission denied"):QString{});
        QTRY_VERIFY(!QFileInfo::exists(prepared));
        if(!close){QTRY_VERIFY(!exporter->busy());delete exporter.data();}
        reply("obsolete",{});QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
        QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(original.readAll(),bytes);
    }
    void synchronousReentrantReplyKeepsThePreparedFileUntilDispatchReturns() {
        using namespace iiSharedCanvas;
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/reentrant-photo-XXXXXX");QVERIFY(directory.isValid());
        const auto source=directory.filePath("source.iisc");Document d;d.extent={2,2};
        d.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(2,2,0xff0088ffU)});
        d.layers.emplace_back(StaticBitmapLayer{{"layer","Native"},StaticSource{"pixels"}});
        DocumentFile author;QVERIFY(author.create(source.toStdString(),d).ok());author.close();
        QString prepared;bool returned=false;
        QPointer<PhotoLibraryExporter> exporter=new PhotoLibraryExporter([&](const PhotoLibraryExporter::Image &image,auto reply){
            prepared=image.path;reply("asset",{});QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
            QVERIFY(QFileInfo::exists(prepared));returned=true;
        },nullptr);
        connect(exporter,&PhotoLibraryExporter::saved,this,[&]{delete exporter.data();});
        QVERIFY(exporter->save(QUrl::fromLocalFile(source)));QTRY_VERIFY(returned);QVERIFY(exporter.isNull());
        QTRY_VERIFY(!QFileInfo::exists(prepared));
    }
    void preparationExceptionsBecomeAcceptedFailures() {
        int nativeCalls=0;
        PhotoLibraryExporter exporter([](const QUrl &)->PhotoLibraryExporter::Preparation{throw std::runtime_error("Preparation fixture failed");},
            [&](const PhotoLibraryExporter::Image &,auto){++nativeCalls;},nullptr);
        QSignalSpy failed(&exporter,&PhotoLibraryExporter::failed),rejected(&exporter,&PhotoLibraryExporter::rejected);
        QVERIFY(exporter.save(QUrl("file:///source.png")));QTRY_COMPARE(failed.count(),1);
        QVERIFY(failed.at(0).at(0).toString().contains("Preparation fixture failed"));QVERIFY(!exporter.busy());
        QCOMPARE(nativeCalls,0);QCOMPARE(rejected.count(),0);
    }
    void nativePayloadUsesContentMimeWithoutChangingTheOriginal() {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-mime-XXXXXX");QVERIFY(directory.isValid());
        const auto path=directory.filePath("original.blob");QImage image(24,12,QImage::Format_RGB32);image.fill(Qt::cyan);
        QVERIFY(image.save(path,"PNG"));QFile original(path);QVERIFY(original.open(QIODevice::ReadOnly));const auto bytes=original.readAll();original.close();
        int calls=0;PhotoLibraryExporter exporter([&](const PhotoLibraryExporter::Image &native,auto reply){
            QCOMPARE(native.path,path);QCOMPARE(native.mimeType,QString("image/png"));++calls;reply("asset",{});
        },nullptr);
        QSignalSpy saved(&exporter,&PhotoLibraryExporter::saved);
        QVERIFY(exporter.save(QUrl::fromLocalFile(path)));QTRY_COMPARE(saved.count(),1);QCOMPARE(calls,1);
        QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(original.readAll(),bytes);
    }
    void duplicateNativeRepliesCannotFinishTheNextRequest()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-replies-XXXXXX");QVERIFY(directory.isValid());
        QImage image(8,8,QImage::Format_RGB32);image.fill(Qt::blue);
        const auto path=directory.filePath("image.png");QVERIFY(image.save(path));
        QList<PhotoLibraryExporter::Completion> replies;
        PhotoLibraryExporter exporter([&](const PhotoLibraryExporter::Image &, auto callback){replies.append(std::move(callback));},nullptr);
        QSignalSpy saved(&exporter,&PhotoLibraryExporter::saved),failed(&exporter,&PhotoLibraryExporter::failed);
        QVERIFY(exporter.save(QUrl::fromLocalFile(path)));QTRY_COMPARE(replies.size(),1);replies[0]("first",{});QTRY_COMPARE(saved.count(),1);
        QVERIFY(exporter.save(QUrl::fromLocalFile(path)));QVERIFY(exporter.busy());
        replies[0]({},"Obsolete native error");QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
        QVERIFY(exporter.busy());QCOMPARE(failed.count(),0);QCOMPARE(saved.count(),1);
        QTRY_COMPARE(replies.size(),2);replies[1]("second",{});QTRY_COMPARE(saved.count(),2);QVERIFY(!exporter.busy());
        replies[1]("duplicate",{});QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCOMPARE(saved.count(),2);
    }

    void nativeExceptionsBecomeTerminalFailures()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-exception-XXXXXX");QVERIFY(directory.isValid());
        QImage image(8,8,QImage::Format_RGB32);image.fill(Qt::blue);
        const auto path=directory.filePath("image.png");QVERIFY(image.save(path));
        PhotoLibraryExporter exporter([](const PhotoLibraryExporter::Image &,auto){throw std::runtime_error("Native fixture failed");},nullptr);
        QSignalSpy saved(&exporter,&PhotoLibraryExporter::saved),failed(&exporter,&PhotoLibraryExporter::failed);
        bool threw=false,accepted=false;
        try {accepted=exporter.save(QUrl::fromLocalFile(path));}catch(...){threw=true;}
        QVERIFY(!threw);QVERIFY(accepted);QTRY_COMPARE(failed.count(),1);QVERIFY(!exporter.busy());
        QVERIFY(failed.at(0).at(0).toString().contains("Native fixture failed"));QCOMPARE(saved.count(),0);
    }

    void acceptedDispatchSurvivesDeletionDuringItsBusyNotification()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-dispatch-XXXXXX");QVERIFY(directory.isValid());
        QImage image(8,8,QImage::Format_RGB32);image.fill(Qt::blue);
        const auto path=directory.filePath("image.png");QVERIFY(image.save(path));
        int calls=0;PhotoLibraryExporter::Completion reply;
        QPointer<PhotoLibraryExporter> exporter=new PhotoLibraryExporter([&](const PhotoLibraryExporter::Image &source, auto callback) {
            QCOMPARE(source.path,path);++calls;reply=std::move(callback);
        },nullptr);
        connect(exporter,&PhotoLibraryExporter::busyChanged,this,[&]{if(exporter&&exporter->busy())delete exporter.data();});
        QVERIFY(exporter->save(QUrl::fromLocalFile(path)));QVERIFY(exporter.isNull());QTRY_COMPARE(calls,1);
        reply("after-close",{});QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);
    }

    void completionPrecedesIdleNotificationAndSurvivesReentrantOwners()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-idle-XXXXXX");QVERIFY(directory.isValid());
        QImage image(8,8,QImage::Format_RGB32);image.fill(Qt::blue);
        const auto source=QUrl::fromLocalFile(directory.filePath("image.png"));QVERIFY(image.save(source.toLocalFile()));
        PhotoLibraryExporter::Completion reply;
        QPointer<PhotoLibraryExporter> exporter=new PhotoLibraryExporter([&](const PhotoLibraryExporter::Image &, auto callback){reply=std::move(callback);},nullptr);
        QSignalSpy saved(exporter,&PhotoLibraryExporter::saved);
        QStringList order;
        connect(exporter,&PhotoLibraryExporter::saved,this,[&]{order.append("saved");});
        connect(exporter,&PhotoLibraryExporter::busyChanged,this,[&]{
            if(exporter&&!exporter->busy()){order.append("idle");delete exporter.data();}
        });
        QVERIFY(exporter->save(source));QTRY_VERIFY(bool(reply));reply("first",{});QTRY_VERIFY(exporter.isNull());
        QCOMPARE(saved.count(),1);QCOMPARE(order,QStringList({"saved","idle"}));
        QList<PhotoLibraryExporter::Completion> replies;
        PhotoLibraryExporter reused([&](const PhotoLibraryExporter::Image &, auto callback){replies.append(std::move(callback));},nullptr);
        QSignalSpy outcomes(&reused,&PhotoLibraryExporter::saved);QSignalSpy busy(&reused,&PhotoLibraryExporter::busyChanged);
        connect(&reused,&PhotoLibraryExporter::saved,this,[&](const QString &id){if(id=="first")QVERIFY(reused.save(source));});
        QVERIFY(reused.save(source));QTRY_COMPARE(replies.size(),1);replies[0]("first",{});QTRY_COMPARE(outcomes.count(),1);QVERIFY(reused.busy());
        QTRY_COMPARE(replies.size(),2);QCOMPARE(busy.count(),2);
        replies[1]("second",{});QTRY_COMPARE(outcomes.count(),2);QVERIFY(!reused.busy());QCOMPARE(busy.count(),3);
    }

    void concurrentRepliesAndAnExceptionStillDeliverOnlyOneOutcome()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-concurrent-XXXXXX");QVERIFY(directory.isValid());
        QImage image(8,8,QImage::Format_RGB32);image.fill(Qt::blue);
        const auto source=QUrl::fromLocalFile(directory.filePath("image.png"));QVERIFY(image.save(source.toLocalFile()));
        PhotoLibraryExporter exporter([](const PhotoLibraryExporter::Image &, auto reply) {
            std::thread first([reply]{reply("one",{});}),second([reply]{reply("one",{});});
            first.join();second.join();throw std::runtime_error("After the accepted reply");
        },nullptr);
        QSignalSpy saved(&exporter,&PhotoLibraryExporter::saved),failed(&exporter,&PhotoLibraryExporter::failed);
        QVERIFY(exporter.save(source));QTRY_COMPARE(saved.count(),1);QVERIFY(!exporter.busy());
        QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCOMPARE(saved.count(),1);QCOMPARE(failed.count(),0);
    }

    void terminalObserverCanDestroyTheOwner_data()
    {
        QTest::addColumn<bool>("failure");QTest::newRow("saved")<<false;QTest::newRow("failed")<<true;
    }
    void terminalObserverCanDestroyTheOwner()
    {
        QFETCH(bool,failure);
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-terminal-XXXXXX");QVERIFY(directory.isValid());
        QImage image(8,8,QImage::Format_RGB32);image.fill(Qt::blue);
        const auto source=QUrl::fromLocalFile(directory.filePath("image.png"));QVERIFY(image.save(source.toLocalFile()));
        PhotoLibraryExporter::Completion reply;
        QPointer<PhotoLibraryExporter> exporter=new PhotoLibraryExporter([&](const PhotoLibraryExporter::Image &, auto callback){reply=std::move(callback);},nullptr);
        int outcomes=0;
        const auto remove=[&]{++outcomes;delete exporter.data();};
        connect(exporter,&PhotoLibraryExporter::saved,this,remove);connect(exporter,&PhotoLibraryExporter::failed,this,remove);
        QVERIFY(exporter->save(source));QTRY_VERIFY(bool(reply));reply(failure?QString{}:QString("asset"),failure?QString("Denied"):QString{});
        QTRY_VERIFY(exporter.isNull());QCOMPARE(outcomes,1);
        reply("late",{});QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCOMPARE(outcomes,1);
    }

    void savesOriginalAsynchronouslyAndPreventsDuplicates()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-library-XXXXXX");
        QImage image(1200, 800, QImage::Format_RGB32);
        image.fill(Qt::cyan);
        image.setText("seed", "987654");
        const auto path = directory.filePath(QStringLiteral("원본 #1.png"));
        QVERIFY(image.save(path));
        PhotoLibraryExporter::Completion finish;
        QString imported;
        int requests = 0;
        PhotoLibraryExporter exporter([&](const PhotoLibraryExporter::Image &source, auto completion) {
            imported = source.path;
            finish = completion;
            ++requests;
        }, nullptr);
        QSignalSpy busy(&exporter, &PhotoLibraryExporter::busyChanged);
        QSignalSpy saved(&exporter, &PhotoLibraryExporter::saved);
        QSignalSpy failed(&exporter, &PhotoLibraryExporter::failed);
        QSignalSpy rejected(&exporter, &PhotoLibraryExporter::rejected);
        QVERIFY(exporter.supported());
        QVERIFY(exporter.save(QUrl::fromLocalFile(path)));
        QVERIFY(exporter.busy());
        QTRY_COMPARE(requests, 1);
        QCOMPARE(imported, path);
        QCOMPARE(QImage(imported).text("seed"), QString("987654"));
        QVERIFY(!exporter.save(QUrl::fromLocalFile(path)));
        QCOMPARE(requests, 1);
        QCOMPARE(rejected.count(),1);QVERIFY(!exporter.lastRejection().isEmpty());
        QCOMPARE(saved.size(), 0);
        // PhotoKit and Android invoke the result on a background thread.
        std::thread worker([&] { finish("native-library-asset", {}); });
        worker.join();
        QTRY_COMPARE(saved.size(), 1);
        QVERIFY(!exporter.busy());
        QCOMPARE(saved.first().first().toString(), QString("native-library-asset"));
        QCOMPARE(failed.size(), 0);
        QCOMPARE(busy.size(), 2);
        QCOMPARE(QImage(path).size(), image.size());

        QVERIFY(exporter.save(QUrl::fromLocalFile(path)));
        QTRY_COMPARE(requests, 2);
        finish({}, "Permission was denied.");
        QTRY_COMPARE(failed.size(), 1);
        QVERIFY(!exporter.busy());
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Permission was denied."));
        QVERIFY(exporter.save(QUrl::fromLocalFile(path))); // A failed import can be retried.
        QTRY_COMPARE(requests, 3);
        finish({}, {});
        QTRY_COMPARE(failed.size(), 2);
        QCOMPARE(saved.size(), 1); // A missing asset identifier is never a success.
    }

    void rejectsUnsupportedAndUnreadableSources()
    {
        PhotoLibraryExporter unsupported({}, nullptr);
        QSignalSpy unsupportedFailure(&unsupported, &PhotoLibraryExporter::rejected);
        QVERIFY(!unsupported.supported());
        QVERIFY(!unsupported.save(QUrl("https://example.com/image.png")));
        QCOMPARE(unsupportedFailure.size(), 1);
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-invalid-XXXXXX");
        const auto path = directory.filePath("invalid.png");
        QFile invalid(path);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not an image");
        invalid.close();
        int calls = 0;
        PhotoLibraryExporter exporter([&](const PhotoLibraryExporter::Image &, auto) { ++calls; }, nullptr);
        QSignalSpy rejected(&exporter, &PhotoLibraryExporter::rejected),failed(&exporter,&PhotoLibraryExporter::failed);
        for (const auto &url : {QUrl(), QUrl("https://example.com/image.png")}) {
            QVERIFY(!exporter.save(url));
            QVERIFY(!exporter.busy());
        }
        for(const auto &source:{directory.path(),path,directory.filePath("missing.png")}) {
            QVERIFY(exporter.save(QUrl::fromLocalFile(source)));
            QTRY_VERIFY(!exporter.busy());
        }
        QCOMPARE(calls, 0);
        QCOMPARE(rejected.size(), 2);QCOMPARE(failed.size(),3);
    }

    void callbackSurvivesClosedView()
    {
        QTemporaryDir directory(DREAMSCAPES_TEST_DIRECTORY "/photo-lifetime-XXXXXX");
        QImage image(16, 16, QImage::Format_RGB32);
        image.fill(Qt::blue);
        const auto path = directory.filePath("image.png");
        QVERIFY(image.save(path));
        PhotoLibraryExporter::Completion finish;
        auto *exporter = new PhotoLibraryExporter([&](const PhotoLibraryExporter::Image &, auto callback) { finish = callback; }, nullptr);
        QVERIFY(exporter->save(QUrl::fromLocalFile(path)));
        delete exporter;
        QTRY_VERIFY(bool(finish));
        finish("completed-after-close", {});
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    }
};

QTEST_GUILESS_MAIN(PhotoLibraryExporterTests)
#include "tst_PhotoLibraryExporter.moc"
