#include "App/Views/Editor/EditorDocumentOpenRequest.h"
#include "App/Views/Editor/EditorProject.h"
#include "App/Views/Editor/EditorProjectCodec.h"
#include <SocietyDrive.h>
#include <SharedStorage.h>
#include <TemporaryLease.h>
#include <iiFileProvider.h>
#include <iiFileProviderPresentation.h>
#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <mutex>
#include <semaphore>
#include <thread>

namespace {
struct Gate {
    std::atomic_bool started=false;
    std::atomic_int calls=0,finished=0,active=0,maximum=0;
    std::counting_semaphore<100> release{0};
    std::mutex mutex;
    std::vector<std::filesystem::path> sources;
};
struct Release {std::shared_ptr<Gate> gate;~Release(){gate->release.release();}};
dreamscapes::DocumentOpenOperation gated(const std::shared_ptr<Gate> &gate,bool after=false) {
    return [gate,after](const dreamscapes::DocumentOpenInput &input) {
        const auto call=++gate->calls,active=++gate->active;gate->maximum.store(std::max(gate->maximum.load(),active));
        {std::lock_guard lock(gate->mutex);gate->sources.push_back(input.source);}
        dreamscapes::DocumentOpenResult result;
        if(after)result=dreamscapes::prepareDocumentOpen(input);
        if(call==1){gate->started=true;(void)gate->release.try_acquire_for(std::chrono::seconds(8));}
        if(!after)result=dreamscapes::prepareDocumentOpen(input);
        --gate->active;++gate->finished;return result;
    };
}
QVariantMap specification(int width=8){return {{"width",width},{"height",8},{"unit","px"},{"background","White"}};}
}
class DocumentOpenTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/document-open-XXXXXX.societycontainer"};
    QString native(const QString &name,int width=16,bool database=false) {
        EditorCanvas canvas;if(!canvas.createCanvas(specification(width)))return {};
        if(!canvas.setLayerOpacity(canvas.selectedLayerId(),.6))return {};
        const auto file=m_storage.filePath("Files/"+name+".iisc");
        if(database)return canvas.saveDocumentAs(QUrl::fromLocalFile(file))?file:QString{};
        const auto encoded=iiSharedCanvas::encodeIisc(*canvas.document());if(!encoded.ok())return {};
        iiFileProvider::File::create(file,QByteArray(reinterpret_cast<const char *>(encoded.bytes.data()),encoded.bytes.size()));return file;
    }
    static dreamscapes::DocumentOpenInput input(const QString &path,bool copy=false) {
        return {std::filesystem::path(path.toStdU16String()),{},std::make_shared<std::atomic_bool>(false),copy};
    }
private slots:
    void initTestCase() {
        QVERIFY(m_storage.isValid());QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH",m_storage.path().toUtf8());
    }
    void bitmapPublicationDoesNotHoldTheOriginalReadLock() {
        const auto path=m_storage.filePath("Files/publication-lock.png");
        QImage image(16,12,QImage::Format_RGB32);image.fill(Qt::red);QVERIFY(image.save(path));
        const auto bytes=iiFileProvider::File::read(path);
        const auto storage=iiSocietyContainer::SharedStorage::open(m_storage.path());QVERIFY(storage);
        auto repository=storage->fileRepository();QVERIFY(repository);
        const auto package=iiSocietyContainer::FileRepository::packagePath(
            std::filesystem::path(m_storage.path().toStdU16String()),storage->drive().identifier().toStdString());
        // A catalog intake owns this lease before requesting the original's
        // presentation. Import must release the source before waiting to publish.
        auto lease=std::make_unique<iiFileProvider::TemporaryLease>(package/"operations",iiFileProvider::TemporaryLease::Mode::TryExclusive);
        QVERIFY(lease->locked());
        EditorProject project;EditorDocumentOpenRequest request;request.setProject(&project);
        QSignalSpy ready(&request,&EditorDocumentOpenRequest::ready);
        QVERIFY(request.start(QUrl::fromLocalFile(path)));
        const QDir files(m_storage.filePath("Files"));
        QElapsedTimer elapsed;elapsed.start();
        while(files.entryList({"publication-lock.png.*.iisc"},QDir::Files).isEmpty()&&elapsed.elapsed()<5000)QTest::qWait(10);
        const bool published=!files.entryList({"publication-lock.png.*.iisc"},QDir::Files).isEmpty();
        std::atomic_bool readerAcquired=false;
        std::jthread reader([&] {
            iiFileProvider::native::SourceAccess access(std::filesystem::path(path.toStdU16String()));
            std::lock_guard lock(access);readerAcquired=true;
        });
        elapsed.restart();while(!readerAcquired&&elapsed.elapsed()<1000)QTest::qWait(10);
        const bool readableDuringPublication=readerAcquired.load();
        lease.reset(); // Always unblock the worker before asserting or joining.
        reader.join();
        QVERIFY(published);
        QVERIFY2(readableDuringPublication,"The source lock was held while waiting for the catalog publication lease");
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,30000);
        QCOMPARE(iiFileProvider::File::read(path),bytes);
        QCOMPARE(project.currentCanvas()->canvasWidth(),16);
        QCOMPARE(project.currentCanvas()->canvasHeight(),12);
    }
    void preparedValuesRetainStorageBindingAndConflictChecksWithoutRescanning() {
        const auto path=native("prepared %20 #",16,true);QVERIFY(!path.isEmpty());const auto bytes=iiFileProvider::File::read(path);
        auto prepared=dreamscapes::prepareDocumentOpen(input(path));QVERIFY2(prepared.ok(),prepared.error.c_str());
        QVERIFY(prepared.bindFile);QVERIFY(prepared.storage.isValid());
        const auto encoded=iiSharedCanvas::encodeIisc(prepared.document).bytes;
        const auto later=m_storage.filePath("Files/added-after-prepare.txt");iiFileProvider::File::write(later,"later");
        EditorProject project;QVERIFY(project.adoptDocumentOpen(std::move(prepared)));QCOMPARE(project.filePath(),path);
        QCOMPARE(iiSharedCanvas::encodeIisc(*project.currentCanvas()->document()).bytes,encoded);
        auto *files=project.currentCanvas()->findChild<iiSocietyHelper::FileSystem *>("editorDocumentStorage");QVERIFY(files);
        QVERIFY(files->container()->alias("files/added-after-prepare.txt").isEmpty());
        QCOMPARE(iiFileProvider::File::read(path),bytes);
        EditorCanvas stale;QVERIFY(stale.openDocumentSource(QUrl::fromLocalFile(path)));
        auto *canvas=project.currentCanvas();QVERIFY(canvas->setLayerOpacity(canvas->selectedLayerId(),.4));
        QVERIFY(!stale.setLayerOpacity(stale.selectedLayerId(),.2));
        EditorCanvas reopened;QVERIFY(reopened.openDocumentSource(QUrl::fromLocalFile(path)));
        QCOMPARE(reopened.selectedLayer()["opacity"].toDouble(),.4);
    }
    void copyHasNoWritableBindingAndDoesNotChangeItsOriginal() {
        const auto path=native("copy");QVERIFY(!path.isEmpty());const auto bytes=iiFileProvider::File::read(path);
        auto prepared=dreamscapes::prepareDocumentOpen(input(path,true));QVERIFY(prepared.ok());
        QVERIFY(!prepared.bindFile);QVERIFY(!prepared.storage.isValid());EditorProject project;
        QVERIFY(project.adoptDocumentOpen(std::move(prepared)));QVERIFY(project.filePath().isEmpty());
        QVERIFY(project.currentCanvas()->setLayerOpacity(project.currentCanvas()->selectedLayerId(),.2));
        QCOMPARE(iiFileProvider::File::read(path),bytes);
    }
    void acceptedReadKeepsTheInputLoopAndOldDocumentAvailable() {
        const auto path=native("responsive");EditorProject project;QVERIFY(project.createCanvas(specification()));
        auto *before=project.currentCanvas();const auto bytes=iiSharedCanvas::encodeIisc(*before->document()).bytes;
        const auto gate=std::make_shared<Gate>();Release release{gate};EditorDocumentOpenRequest request(gated(gate),nullptr);request.setProject(&project);
        QSignalSpy ready(&request,&EditorDocumentOpenRequest::ready);QElapsedTimer elapsed;elapsed.start();
        QVERIFY(request.start(QUrl::fromLocalFile(path)));QVERIFY(elapsed.nsecsElapsed()/1000<250000);
        QTRY_VERIFY(gate->started.load());bool inputDelivered=false;QTimer::singleShot(0,&request,[&]{inputDelivered=true;});QTRY_VERIFY(inputDelivered);
        before->setFrame(1);before->setZoom(2);QVERIFY(request.active());
        QCOMPARE(project.currentCanvas(),before);QCOMPARE(iiSharedCanvas::encodeIisc(*before->document()).bytes,bytes);
        gate->release.release();QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);QVERIFY(!request.active());
        QCOMPARE(project.filePath(),path);QCOMPARE(project.currentCanvas()->canvasWidth(),16);
    }
    void newerIntentsUseOneRunningAndOneLatestPendingRead() {
        const auto a=native("queue-a",16),b=native("queue-b",20),c=native("queue-c",24);EditorProject project;
        const auto gate=std::make_shared<Gate>();Release release{gate};EditorDocumentOpenRequest request(gated(gate),nullptr);request.setProject(&project);
        QSignalSpy ready(&request,&EditorDocumentOpenRequest::ready);QVERIFY(request.start(QUrl::fromLocalFile(a)));QTRY_VERIFY(gate->started.load());
        QVERIFY(request.start(QUrl::fromLocalFile(b)));QVERIFY(request.start(QUrl::fromLocalFile(c)));gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);QCOMPARE(project.filePath(),c);QCOMPARE(gate->calls.load(),2);QCOMPARE(gate->maximum.load(),1);
        std::lock_guard lock(gate->mutex);QCOMPARE(gate->sources,(std::vector<std::filesystem::path>{std::filesystem::path(a.toStdU16String()),std::filesystem::path(c.toStdU16String())}));
    }
    void cancelEditsAndReplacementRejectLateResults_data() {
        QTest::addColumn<QString>("action");for(const auto &action:{"cancel","edit","replace"})QTest::newRow(action)<<QString(action);
    }
    void cancelEditsAndReplacementRejectLateResults() {
        QFETCH(QString,action);const auto path=native("stale-"+action);EditorProject project;QVERIFY(project.createCanvas(specification()));
        const auto gate=std::make_shared<Gate>();Release release{gate};EditorDocumentOpenRequest request(gated(gate),nullptr);request.setProject(&project);
        QSignalSpy ready(&request,&EditorDocumentOpenRequest::ready),failed(&request,&EditorDocumentOpenRequest::failed),cancelled(&request,&EditorDocumentOpenRequest::cancelled);
        QVERIFY(request.start(QUrl::fromLocalFile(path)));QTRY_VERIFY(gate->started.load());
        if(action=="cancel")request.cancel();else if(action=="edit")QVERIFY(project.currentCanvas()->setLayerName(project.currentCanvas()->selectedLayerId(),"Keep this edit"));
        else QVERIFY(project.createCanvas(specification(4)));
        QVERIFY(!request.active());QCOMPARE(action=="cancel"?cancelled.count():failed.count(),1);auto *retained=project.currentCanvas();
        gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTest::qWait(20);QCOMPARE(ready.count(),0);QCOMPARE(project.currentCanvas(),retained);
    }
    void sourceReplacementWithRestoredMtimeIsRejected() {
        const auto path=native("replaced");EditorProject project;auto *before=project.currentCanvas();
        const auto gate=std::make_shared<Gate>();Release release{gate};EditorDocumentOpenRequest request(gated(gate,true),nullptr);request.setProject(&project);
        QSignalSpy failed(&request,&EditorDocumentOpenRequest::failed),ready(&request,&EditorDocumentOpenRequest::ready);
        QVERIFY(request.start(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(gate->started.load(),10000);
        const auto nativePath=std::filesystem::path(path.toStdU16String());const auto modified=std::filesystem::last_write_time(nativePath);
        const auto bytes=iiFileProvider::File::read(path);QVERIFY(QFile::rename(path,path+".previous"));iiFileProvider::File::create(path,bytes);
        std::filesystem::last_write_time(nativePath,modified);gate->release.release();QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,10000);
        QCOMPARE(ready.count(),0);QCOMPARE(project.currentCanvas(),before);QVERIFY(request.error().contains("changed"));
    }
    void driveChangesAndReentrantCancellationKeepTheNewestIntent() {
        const auto a=native("reentrant-a"),b=native("reentrant-b"),c=native("reentrant-c");EditorProject project;
        const auto gate=std::make_shared<Gate>();Release release{gate};EditorDocumentOpenRequest request(gated(gate),nullptr);request.setProject(&project);
        QSignalSpy ready(&request,&EditorDocumentOpenRequest::ready);QVERIFY(request.start(QUrl::fromLocalFile(a)));QTRY_VERIFY(gate->started.load());
        connect(&request,&EditorDocumentOpenRequest::cancelled,&request,[&]{QVERIFY(request.start(QUrl::fromLocalFile(c)));});
        QVERIFY(request.start(QUrl::fromLocalFile(b)));gate->release.release();QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);
        QCOMPARE(project.filePath(),c);QCOMPARE(gate->calls.load(),2);
        iiSocietyContainer::Container container;container.synchronize("drive-a",m_storage.path(),{},true);
        const auto second=std::make_shared<Gate>();Release secondRelease{second};EditorDocumentOpenRequest changed(gated(second),nullptr);changed.setProject(&project);changed.setContainer(&container);
        QSignalSpy failed(&changed,&EditorDocumentOpenRequest::failed);QVERIFY(changed.start(QUrl::fromLocalFile(a)));QTRY_VERIFY(second->started.load());
        container.synchronize("drive-b",m_storage.path()+".other",{},true);QCOMPARE(failed.count(),1);QVERIFY(!changed.active());
        second->release.release();QTRY_COMPARE(second->finished.load(),1);QTest::qWait(20);QCOMPARE(project.filePath(),c);
    }
    void ownerDestructionDoesNotJoinTheRead() {
        const auto path=native("destroyed");EditorProject project;auto *before=project.currentCanvas();
        const auto gate=std::make_shared<Gate>();Release release{gate};auto request=std::make_unique<EditorDocumentOpenRequest>(gated(gate),nullptr);request->setProject(&project);
        QVERIFY(request->start(QUrl::fromLocalFile(path)));QTRY_VERIFY(gate->started.load());QElapsedTimer elapsed;elapsed.start();request.reset();
        const auto microseconds=elapsed.nsecsElapsed()/1000;qInfo()<<"document-open owner destruction microseconds"<<microseconds;QVERIFY(microseconds<250000);
        gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTest::qWait(20);QCOMPARE(project.currentCanvas(),before);
    }
    void projectDestructionRetiresItsOwnReadWithoutJoining() {
        const auto path=native("destroyed-project");const auto gate=std::make_shared<Gate>();Release release{gate};
        auto project=std::make_unique<EditorProject>(EditorPagePreviews::render,gated(gate),nullptr);
        QVERIFY(project->createCanvas(specification()));QVERIFY(project->openDocumentSource(QUrl::fromLocalFile(path)));QTRY_VERIFY(gate->started.load());
        QElapsedTimer elapsed;elapsed.start();project.reset();const auto microseconds=elapsed.nsecsElapsed()/1000;
        qInfo()<<"document-open project destruction microseconds"<<microseconds;QVERIFY(microseconds<250000);
        gate->release.release();QTRY_COMPARE(gate->finished.load(),1);
    }
    void rebindingPublishesTheNewContextBeforeCancellationCallbacks_data() {
        QTest::addColumn<bool>("projectTarget");QTest::newRow("project")<<true;QTest::newRow("container")<<false;
    }
    void rebindingPublishesTheNewContextBeforeCancellationCallbacks() {
        QFETCH(bool,projectTarget);
        const auto prefix=projectTarget?QString("rebind-project"):QString("rebind-container");
        const auto a=native(prefix+"-a"),b=native(prefix+"-b");EditorProject first,second;
        const auto gate=std::make_shared<Gate>();Release release{gate};EditorDocumentOpenRequest request(gated(gate),nullptr);request.setProject(&first);
        iiSocietyContainer::Container container;container.synchronize("rebound",m_storage.path(),{},true);
        bool observed=false;
        connect(&request,&EditorDocumentOpenRequest::cancelled,&request,[&] {
            observed=projectTarget||request.container()==&container;QVERIFY(request.start(QUrl::fromLocalFile(b)));
        });
        QSignalSpy ready(&request,&EditorDocumentOpenRequest::ready);QVERIFY(request.start(QUrl::fromLocalFile(a)));QTRY_VERIFY(gate->started.load());
        if(projectTarget)request.setProject(&second);else request.setContainer(&container);
        QVERIFY(observed);gate->release.release();QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);
        QCOMPARE(projectTarget?second.filePath():first.filePath(),b);QCOMPARE(gate->maximum.load(),1);
        QVERIFY(projectTarget?first.filePath().isEmpty():second.filePath().isEmpty());
    }
    void legacyActivePageMapsToItsFirstActualArtboard() {
        using namespace iiSharedCanvas;
        EditorCanvas first,second;QVERIFY(first.createCanvas(specification()));QVERIFY(second.createCanvas(specification(12)));
        auto document=*first.document();document.artboards={{"one","One",{{0,0},{8,8}},0},{"two","Two",{{16,0},{8,8}},0}};
        layerProperties(document.layers.front()).artboardId="one";
        auto copy=document.layers.front();layerProperties(copy).id="second.layer";layerProperties(copy).artboardId="two";document.layers.push_back(std::move(copy));
        const std::vector<dreamscapes::ProjectPageView> pages{{"First",&document},{"Second",second.document()}};
        const auto encoded=dreamscapes::encodeProject(pages,1);QVERIFY(encoded.error.empty());const auto path=m_storage.filePath("legacy.iiscp");
        const QByteArray bytes(reinterpret_cast<const char *>(encoded.bytes.data()),encoded.bytes.size());iiFileProvider::File::write(path,bytes);
        auto prepared=dreamscapes::prepareDocumentOpen(input(path));QVERIFY2(prepared.ok(),prepared.error.c_str());QCOMPARE(prepared.activeArtboard,2);
        EditorProject project;QVERIFY(project.adoptDocumentOpen(std::move(prepared)));QCOMPARE(project.canvasCount(),3);QCOMPARE(project.currentIndex(),2);
        QVERIFY(project.filePath().isEmpty());QCOMPARE(iiFileProvider::File::read(path),bytes);
    }
    void publicProjectFailuresPreserveTheOldDocumentAndCanvasCannotBypassTheCommand() {
        EditorProject project;QVERIFY(project.createCanvas(specification()));auto *before=project.currentCanvas();
        QSignalSpy failed(&project,&EditorProject::documentOpenFailed),ready(&project,&EditorProject::documentOpened);
        QVERIFY(project.openDocumentSource(QUrl::fromLocalFile(m_storage.filePath("missing.iisc"))));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,10000);QVERIFY(!project.opening());QCOMPARE(ready.count(),0);QCOMPARE(project.currentCanvas(),before);
        QVERIFY(!project.error().isEmpty());QVERIFY(before->error().isEmpty());
        QCOMPARE(before->metaObject()->indexOfMethod("openDocumentSource(QUrl,bool)"),-1);
        QCOMPARE(before->metaObject()->indexOfMethod("openImages(QVariantList)"),-1);
        QCOMPARE(project.metaObject()->indexOfMethod("openImages(QVariantList)"),-1);
        QCOMPARE(project.metaObject()->indexOfMethod("attachImages(QVariantList)"),-1);
    }
};
QTEST_MAIN(DocumentOpenTests)
#include "tst_documentopen.moc"
