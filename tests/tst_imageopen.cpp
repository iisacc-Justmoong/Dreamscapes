#include "App/Views/Home/ImageOpenRequest.h"
#include <iiSocietyHelper.h>
#include <SharedStorage.h>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "App/ViewModels/Native/ImageSelection.h"
#include "App/ViewModels/ViewData.h"
#include <QtTest>

namespace {
QVariantMap image(const QString &path) {return {{"imageSource",QUrl::fromLocalFile(path)},{"mediaType","Image"}};}
struct Drive {
    QTemporaryDir directory{QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/image-intent-XXXXXX.societycontainer")};
    iiSocietyHelper::FileSystem files;
    iiSocietyHelper::FileDownload download{&files,nullptr};
    QString first,second;
    bool open() {
        if(!iiSocietyContainer::SocietyDrive::create(directory.path()))return false;
        first=directory.filePath("Files/one % #.png");second=directory.filePath("Files/two.png");
        QImage pixels(53,31,QImage::Format_RGB32);pixels.fill(Qt::cyan);
        return pixels.save(first)&&pixels.save(second)&&files.open(directory.path())&&files.synchronize();
    }
    QVariantMap alias(const QString &name) const {return files.container()->alias("files/"+name);}
    QVariantMap stale(const QString &name) {
        auto value=alias(name);value["available"]=false;value["path"]="";value["source"]=QUrl{};
        value["previewSource"]=QUrl::fromLocalFile(directory.filePath("tiny-preview.jpg"));return value;
    }
    void publish(const QVariantList &rows) {files.container()->synchronize(files.containerId(),files.rootPath(),rows,true);}
    void bind(ImageOpenRequest &request) {request.setContainer(files.container());request.setDownload(&download);}
};
}
class ImageOpenTests : public QObject {
    Q_OBJECT
private slots:
    void viewCancellationOnlyStopsItsOwnedDownload() {
        using dreamscapes::vm::ownsDownload;
        const auto state = dreamscapes::nativeValue(QVariantMap{{"key", "files/one.iisc"}, {"containerId", "drive-a"}});
        QVERIFY(!ownsDownload(true, state, "", ""));
        QVERIFY(!ownsDownload(true, state, "files/two.iisc", "drive-a"));
        QVERIFY(!ownsDownload(true, state, "files/one.iisc", "drive-b"));
        QVERIFY(ownsDownload(true, state, "files/one.iisc", "drive-a"));
        QVERIFY(!ownsDownload(false, state, "files/one.iisc", "drive-a"));
    }
    void originalPriorityAndDeferredAdoption() {
        ImageOpenRequest request;QSignalSpy ready(&request,&ImageOpenRequest::ready),changed(&request,&ImageOpenRequest::changed);
        auto native=image("/unmounted/thumbnail.png");native["documentSource"]=QUrl::fromLocalFile("/unmounted/original.iisc");
        native["imageSource"]=QUrl("image://iisc-preview/rendered");
        const QVariantMap path{{"path","/unmounted/original % #.png"},{"mediaType","Image"}};
        QVERIFY(request.start({native,path,native}));QVERIFY(request.active());QCOMPARE(request.total(),2);QCOMPARE(ready.size(),0);
        QTRY_COMPARE(ready.size(),1);QVERIFY(!request.active());QCOMPARE(request.completed(),2);
        const auto result=ready[0][0].toList();QCOMPARE(result.size(),2);
        QCOMPARE(result[0].toMap(),native);QCOMPARE(result[1].toMap(),path);
        const auto sources=ready[0][1].toList();
        QCOMPARE(sources[0].toUrl().toLocalFile(),"/unmounted/original.iisc");
        QCOMPARE(sources[1].toUrl().toLocalFile(),"/unmounted/original % #.png");
        QCOMPARE(changed.size(),2); // Acceptance and one completed intent; no probes.
    }
    void invalidCommandDoesNotRetireAcceptedWork() {
        ImageOpenRequest request;QSignalSpy ready(&request,&ImageOpenRequest::ready),rejected(&request,&ImageOpenRequest::rejected),failed(&request,&ImageOpenRequest::failed);
        const auto valid=image("/not-mounted/original.png");QVERIFY(request.start({valid}));
        QVariantList invalid{QVariantMap{},QVariantMap{{"previewSource",QUrl::fromLocalFile("/cache/tiny.png")}},
            QVariantMap{{"imageSource",QUrl("image://preview/key")}},QVariantMap{{"imageSource",QUrl("https://invalid/image.png")}},
            QVariantMap{{"path","relative.png"}},QVariantMap{{"available",false},{"imageSource",QUrl::fromLocalFile("/cache/tiny.png")}},
            QVariantMap{{"path","/not-mounted/original.png"},{"exists",false}},
            QVariantMap{{"path","/not-mounted/original.png"},{"mediaType","Video"}},
            QVariantMap{{"path","/not-mounted/original.png"},{"containerId","other"},{"key","files/file.png"}}};
        for(const auto &entry:invalid){QVERIFY(!request.start({entry}));QVERIFY(request.active());}
        QVERIFY(!request.start({}));QVariantList excessive;for(int i=0;i<1001;++i)excessive.append(valid);QVERIFY(!request.start(excessive));
        QCOMPARE(rejected.size(),invalid.size()+2);QTRY_COMPARE(ready.size(),1);QCOMPARE(failed.size(),0);
    }
    void staleAliasesResolveThroughTheNativeDownload() {
        Drive drive;QVERIFY(drive.open());auto one=drive.stale("one % #.png"),two=drive.stale("two.png");drive.publish({one,two});
        ImageOpenRequest request;drive.bind(request);QSignalSpy ready(&request,&ImageOpenRequest::ready),downloads(&drive.download,&iiSocietyHelper::FileDownload::ready);
        QVERIFY(request.start({one,two}));QCOMPARE(request.completed(),0);QVERIFY(!drive.download.active());
        QTRY_COMPARE(ready.size(),1);QCOMPARE(downloads.size(),1);QCOMPARE(request.completed(),2);QVERIFY(!request.active());
        const auto result=ready[0][0].toList();QCOMPARE(result.size(),2);
        QCOMPARE(result[0].toMap()["source"].toUrl(),QUrl::fromLocalFile(drive.first));
        QCOMPARE(result[1].toMap()["source"].toUrl(),QUrl::fromLocalFile(drive.second));
        QVERIFY(result[0].toMap()["available"].toBool());QVERIFY(result[1].toMap()["available"].toBool());
    }
    void nativeGeneratedResultsKeepTheirGenerationSchema() {
        Drive drive;QVERIFY(drive.open());const auto alias=drive.alias("one % #.png");
        ImageOpenRequest request;drive.bind(request);QSignalSpy ready(&request,&ImageOpenRequest::ready);
        const QVariantMap result{{"id","job"},{"prompt","keep"},{"model","model"},{"seed",42},{"version","generation-schema"},
            {"alias",alias},{"documentSource",alias.value("source")},{"imageSource",QUrl("image://preview/key")},{"mediaType","Image"}};
        QVERIFY(request.start({result}));QTRY_COMPARE(ready.size(),1);QCOMPARE(ready[0][0].toList().first().toMap(),result);
        QCOMPARE(ready[0][1].toList().first().toUrl(),QUrl::fromLocalFile(drive.first));
    }
    void cancellationBeforeAndDuringDownload_data() {QTest::addColumn<bool>("during");QTest::newRow("queued")<<false;QTest::newRow("downloading")<<true;}
    void cancellationBeforeAndDuringDownload() {
        QFETCH(bool,during);Drive drive;QVERIFY(drive.open());const auto one=drive.stale("one % #.png");drive.publish({one});
        ImageOpenRequest request;drive.bind(request);QSignalSpy ready(&request,&ImageOpenRequest::ready),cancelled(&request,&ImageOpenRequest::cancelled),failed(&request,&ImageOpenRequest::failed);
        connect(&drive.download,&iiSocietyHelper::FileDownload::changed,&request,[&]{if(during&&drive.download.active())request.cancel();});
        QVERIFY(request.start({one}));if(!during)request.cancel();
        QTRY_COMPARE(cancelled.size(),1);QTest::qWait(30);QCOMPARE(ready.size(),0);QCOMPARE(failed.size(),0);QVERIFY(!drive.download.active());QVERIFY(!request.active());
    }
    void transferFailureAndSourceChangesHaveOneOutcome() {
        Drive drive;QVERIFY(drive.open());auto missing=drive.stale("two.png");QVERIFY(QFile::remove(drive.second));drive.publish({missing});
        ImageOpenRequest request;drive.bind(request);QSignalSpy ready(&request,&ImageOpenRequest::ready),failed(&request,&ImageOpenRequest::failed);
        QVERIFY(request.start({missing}));QTRY_COMPARE(failed.size(),1);QCOMPARE(ready.size(),0);QVERIFY(!request.error().isEmpty());QVERIFY(!request.active());
        QVERIFY(drive.files.synchronize());
        auto current=drive.alias("one % #.png");QVERIFY(!current.isEmpty());
        QVERIFY(request.start({current}));drive.files.container()->synchronize("replacement",drive.directory.path(),{},true);
        QTRY_COMPARE(failed.size(),2);QCOMPARE(ready.size(),0);QVERIFY(!request.active());
    }
    void replacingTheSharedDownloadDoesNotCancelItsNewOwner() {
        Drive drive;QVERIFY(drive.open());const auto one=drive.stale("one % #.png");drive.publish({one,drive.alias("two.png")});
        ImageOpenRequest request;drive.bind(request);QSignalSpy ready(&request,&ImageOpenRequest::ready),cancelled(&request,&ImageOpenRequest::cancelled),downloaded(&drive.download,&iiSocietyHelper::FileDownload::ready);
        bool replaced=false;
        connect(&drive.download,&iiSocietyHelper::FileDownload::changed,&drive.download,[&] {
            if(!replaced&&drive.download.active()){replaced=true;QVERIFY(drive.download.start("files/two.png",drive.files.containerId()));}
        });
        QVERIFY(request.start({one}));QTRY_COMPARE(downloaded.size(),1);QCOMPARE(cancelled.size(),1);QCOMPARE(ready.size(),0);
        QCOMPARE(downloaded[0][0].toMap()["key"].toString(),"files/two.png");
    }
    void retryRetainsTheAcceptedBatch() {
        Drive drive;QVERIFY(drive.open());const auto one=drive.alias("one % #.png"),two=drive.stale("two.png");
        QVERIFY(QFile::remove(drive.second));drive.publish({one,two});
        ImageOpenRequest request;drive.bind(request);QSignalSpy ready(&request,&ImageOpenRequest::ready),failed(&request,&ImageOpenRequest::failed);
        QVERIFY(request.start({one,two}));QTRY_COMPARE(failed.size(),1);QCOMPARE(ready.size(),0);QVERIFY(request.canRetry());QCOMPARE(request.completed(),1);
        QImage pixels(89,63,QImage::Format_RGB32);pixels.fill(Qt::green);QVERIFY(pixels.save(drive.second));
        QVERIFY(request.retry());QTRY_COMPARE(ready.size(),1);QCOMPARE(request.total(),2);QCOMPARE(request.completed(),2);QVERIFY(!request.canRetry());
        const auto sources=ready[0][1].toList();QCOMPARE(sources.size(),2);
        QCOMPARE(sources[0].toUrl(),QUrl::fromLocalFile(drive.first));QCOMPARE(sources[1].toUrl(),QUrl::fromLocalFile(drive.second));
        QVERIFY(request.error().isEmpty());QVERIFY(!request.retry());QCOMPARE(failed.size(),1);
    }
    void failureObserversCanDeleteTheView() {
        Drive drive;QVERIFY(drive.open());const auto missing=drive.stale("two.png");QVERIFY(QFile::remove(drive.second));drive.publish({missing});
        auto *request=new ImageOpenRequest;drive.bind(*request);QPointer<ImageOpenRequest> guard(request);int failures=0,idle=0;
        connect(request,&ImageOpenRequest::changed,this,[&]{if(guard&&!guard->active())++idle;});
        connect(request,&ImageOpenRequest::failed,this,[&]{++failures;delete request;});
        QVERIFY(request->start({missing}));QTRY_VERIFY(!guard);QCOMPARE(failures,1);QCOMPARE(idle,0);QVERIFY(!drive.download.active());
    }
    void terminalObserversCanDeleteOrStartTheNextRequest() {
        auto *deleted=new ImageOpenRequest;QPointer<ImageOpenRequest> guard(deleted);int deliveries=0,idle=0;
        connect(deleted,&ImageOpenRequest::changed,this,[&]{if(guard&&!guard->active())++idle;});
        connect(deleted,&ImageOpenRequest::ready,this,[&]{++deliveries;delete deleted;});
        QVERIFY(deleted->start({image("/first.png")}));QTRY_VERIFY(!guard);QCOMPARE(deliveries,1);QCOMPARE(idle,0);
        ImageOpenRequest next;QSignalSpy ready(&next,&ImageOpenRequest::ready);int idleNext=0;
        connect(&next,&ImageOpenRequest::changed,this,[&]{if(!next.active())++idleNext;});
        connect(&next,&ImageOpenRequest::ready,this,[&]{if(ready.size()==1)QVERIFY(next.start({image("/second.png")}));});
        QVERIFY(next.start({image("/first.png")}));QTRY_COMPARE(ready.size(),2);QCOMPARE(idleNext,1);
        QCOMPARE(ready[1][1].toList()[0].toUrl().toLocalFile(),"/second.png");
    }
    void cancellationObserverCanAcceptANewerIntent() {
        ImageOpenRequest request;QSignalSpy ready(&request,&ImageOpenRequest::ready);
        connect(&request,&ImageOpenRequest::cancelled,this,[&]{QVERIFY(request.start({image("/newest.png")}));});
        QVERIFY(request.start({image("/old.png")}));QVERIFY(request.start({image("/superseded.png")}));
        QTRY_COMPARE(ready.size(),1);QCOMPARE(ready[0][1].toList()[0].toUrl().toLocalFile(),"/newest.png");
    }
    void deletingTheViewRetiresItsQueuedIntent() {
        auto *request=new ImageOpenRequest;int ready=0;connect(request,&ImageOpenRequest::ready,this,[&]{++ready;});
        QVERIFY(request->start({image("/original.png")}));delete request;QTest::qWait(20);QCOMPARE(ready,0);
    }
    void cancellationObserverCannotRetargetASelectionToAnotherDrive() {
        Drive original,replacement;QVERIFY(original.open());QVERIFY(replacement.open());
        ImageOpenRequest request;original.bind(request);QSignalSpy ready(&request,&ImageOpenRequest::ready),failed(&request,&ImageOpenRequest::failed);
        QVERIFY(request.start({image("/previous.png")}));
        connect(&request,&ImageOpenRequest::cancelled,this,[&]{request.setContainer(replacement.files.container());});
        QVERIFY(request.start({original.alias("one % #.png")}));
        QTRY_COMPARE(failed.size(),1);QCOMPARE(ready.size(),0);QVERIFY(!original.download.active());QVERIFY(!replacement.download.active());
    }
};
QTEST_GUILESS_MAIN(ImageOpenTests)
#include "tst_imageopen.moc"
