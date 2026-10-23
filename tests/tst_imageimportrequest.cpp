#include "App/Views/Editor/EditorImageImportRequest.h"
#include "App/Views/Editor/EditorProject.h"
#include "App/StorageViewTypes.h"
#include <SocietyDrive.h>
#include <iiSharedCanvas/File/DocumentFile.h>
#include <QImage>
#include <QDir>
#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <atomic>
#include <mutex>
#include <semaphore>

namespace {
struct Gate {
    std::atomic_bool started{false};
    std::counting_semaphore<100> release{0};
    std::atomic_int calls{0},finished{0},active{0},maximum{0};
    std::mutex mutex;
    std::vector<std::filesystem::path> sources;
};
struct Release {std::shared_ptr<Gate> gate;~Release(){gate->release.release();}};
dreamscapes::ImageImportOperation gated(const std::shared_ptr<Gate> &gate) {
    return [gate](const dreamscapes::ImageImportRequest &request) {
        const auto call=++gate->calls,active=++gate->active;
        gate->maximum.store(std::max(gate->maximum.load(),active));
        {std::lock_guard lock(gate->mutex);gate->sources.push_back(request.sources.front());}
        if(call==1){gate->started=true;(void)gate->release.try_acquire_for(std::chrono::seconds(5));}
        auto result=dreamscapes::prepareImageImport(request);--gate->active;++gate->finished;return result;
    };
}
}
class ImageImportRequestTests : public QObject {
    Q_OBJECT
    QTemporaryDir m_storage{DREAMSCAPES_TEST_DIRECTORY "/import-request-XXXXXX.societycontainer"};
    QString image(const QString &name,QColor color=Qt::red) {
        const auto file=m_storage.filePath(name);QImage pixels(16,12,QImage::Format_ARGB32);pixels.fill(color);
        return pixels.save(file)?file:QString{};
    }
    static QVariantList sources(const QString &path){return {QUrl::fromLocalFile(path)};}
    static QVariantMap spec(){return {{"width",8},{"height",8},{"unit","px"},{"background","White"}};}
private slots:
    void projectBindingPreservesANewerCancellationIntent() {
        const auto file=image("binding-project.png");QVERIFY(!file.isEmpty());
        EditorProject first,outer,latest;const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorImageImportRequest request(gated(gate),nullptr);request.setProject(&first);
        QVERIFY(request.start(sources(file)));QTRY_VERIFY(gate->started.load());
        bool reentered=false,accepted=false;EditorProject *observed=nullptr;
        QSignalSpy ready(&request,&EditorImageImportRequest::ready),rejected(&request,&EditorImageImportRequest::rejected);
        connect(&request,&EditorImageImportRequest::cancelled,&request,[&] {
            if(reentered)return;reentered=true;observed=request.project();
            request.setProject(&latest);accepted=request.start(sources(file));
        });
        request.setProject(&outer);
        QCOMPARE(observed,&outer);QCOMPARE(request.project(),&latest);
        QVERIFY(accepted);QVERIFY(request.active());QCOMPARE(rejected.count(),0);
        request.cancel();gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTest::qWait(20);
        QCOMPARE(ready.count(),0);QVERIFY(!latest.currentCanvas()->documentReady());
    }
    void containerBindingPreservesANewerCancellationIntent() {
        const auto file=image("binding-container.png");QVERIFY(!file.isEmpty());
        EditorProject project;iiSocietyContainer::Container outer,latest;
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorImageImportRequest request(gated(gate),nullptr);request.setProject(&project);
        QVERIFY(request.start(sources(file)));QTRY_VERIFY(gate->started.load());
        iiSocietyContainer::Container *observed=nullptr;
        connect(&request,&EditorImageImportRequest::cancelled,&request,[&] {
            observed=request.container();request.setContainer(&latest);
        });
        request.setContainer(&outer);
        QCOMPARE(observed,&outer);QCOMPARE(request.container(),&latest);QVERIFY(!request.active());
        gate->release.release();QTRY_COMPARE(gate->finished.load(),1);
    }
    void idleBindingDestructionNotifies_data() {
        QTest::addColumn<bool>("projectBinding");
        QTest::newRow("project")<<true;QTest::newRow("container")<<false;
    }
    void idleBindingDestructionNotifies() {
        QFETCH(bool,projectBinding);EditorImageImportRequest request;
        auto project=std::make_unique<EditorProject>();auto container=std::make_unique<iiSocietyContainer::Container>();
        if(projectBinding)request.setProject(project.get());else request.setContainer(container.get());
        QSignalSpy changed(&request,&EditorImageImportRequest::bindingsChanged);
        if(projectBinding)project.reset();else container.reset();
        QVERIFY(projectBinding?request.project()==nullptr:request.container()==nullptr);
        QCOMPARE(changed.count(),1);QVERIFY(!request.active());
    }
    void bindingCancellationMayDeleteTheCommand_data() {
        QTest::addColumn<bool>("projectBinding");
        QTest::newRow("project")<<true;QTest::newRow("container")<<false;
    }
    void bindingCancellationMayDeleteTheCommand() {
        QFETCH(bool,projectBinding);const auto file=image("binding-closed-owner.png");QVERIFY(!file.isEmpty());
        EditorProject original,replacement;iiSocietyContainer::Container container;
        const auto gate=std::make_shared<Gate>();Release release{gate};
        auto request=std::make_unique<EditorImageImportRequest>(gated(gate),nullptr);request->setProject(&original);
        const QPointer<EditorImageImportRequest> weak(request.get());
        QVERIFY(request->start(sources(file)));QTRY_VERIFY(gate->started.load());
        connect(request.get(),&EditorImageImportRequest::cancelled,&original,[&]{request.reset();});
        if(projectBinding)request->setProject(&replacement);else request->setContainer(&container);
        QVERIFY(!request);QVERIFY(weak.isNull());gate->release.release();QTRY_COMPARE(gate->finished.load(),1);
    }
    void cancellationMayDestroyTheProposedBinding_data() {
        QTest::addColumn<bool>("projectBinding");
        QTest::newRow("project")<<true;QTest::newRow("container")<<false;
    }
    void cancellationMayDestroyTheProposedBinding() {
        QFETCH(bool,projectBinding);const auto file=image("binding-closed-target.png");QVERIFY(!file.isEmpty());
        EditorProject original;auto project=std::make_unique<EditorProject>();
        auto container=std::make_unique<iiSocietyContainer::Container>();
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorImageImportRequest request(gated(gate),nullptr);request.setProject(&original);
        QVERIFY(request.start(sources(file)));QTRY_VERIFY(gate->started.load());
        QSignalSpy changed(&request,&EditorImageImportRequest::bindingsChanged),ready(&request,&EditorImageImportRequest::ready);
        connect(&request,&EditorImageImportRequest::cancelled,&request,[&] {
            if(projectBinding)project.reset();else container.reset();
        });
        if(projectBinding)request.setProject(project.get());else request.setContainer(container.get());
        QVERIFY(projectBinding?request.project()==nullptr:request.container()==nullptr);
        QCOMPARE(changed.count(),1);QVERIFY(!request.active());QVERIFY(request.error().isEmpty());
        gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTest::qWait(20);QCOMPARE(ready.count(),0);
    }
    void retryDuringCanvasReplacementAlreadyWatchesTheNewCanvas() {
        const auto file=image("binding-retry.png");QVERIFY(!file.isEmpty());EditorProject project;
        const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorImageImportRequest request(gated(gate),nullptr);request.setProject(&project);
        QVERIFY(request.start(sources(file)));QTRY_VERIFY(gate->started.load());
        bool observed=false,accepted=false,edited=false,retired=false;
        QSignalSpy ready(&request,&EditorImageImportRequest::ready),failed(&request,&EditorImageImportRequest::failed);
        connect(&request,&EditorImageImportRequest::failed,&request,[&] {
            if(observed)return;observed=true;accepted=request.start(sources(file),{},true);
            auto *canvas=project.currentCanvas();edited=canvas->setLayerOpacity(canvas->selectedLayerId(),.4);
            retired=!request.active();
        });
        QVERIFY(project.createCanvas(spec()));QVERIFY(observed);QVERIFY(accepted);QVERIFY(edited);QVERIFY(retired);
        QCOMPARE(failed.count(),2);QCOMPARE(project.currentCanvas()->selectedLayer()["opacity"].toDouble(),.4);
        gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTest::qWait(20);
        QCOMPARE(ready.count(),0);QCOMPARE(gate->calls.load(),1);
    }
    void initTestCase() {
        QVERIFY(m_storage.isValid());QVERIFY(iiSocietyContainer::SocietyDrive::create(m_storage.path()));
        qputenv("SOCIETY_CONTAINER_PATH",m_storage.path().toUtf8());registerStorageViewTypes();
    }
    void preparationDoesNotBlockInputOrMutateTheOldDocument() {
        const auto file=image("gated.png");QVERIFY(!file.isEmpty());EditorProject project;QVERIFY(project.createCanvas(spec()));
        auto *original=project.currentCanvas();const auto bytes=iiSharedCanvas::encodeIisc(*original->document()).bytes;
        const auto gate=std::make_shared<Gate>();Release release{gate};EditorImageImportRequest request(gated(gate),nullptr);request.setProject(&project);
        QSignalSpy ready(&request,&EditorImageImportRequest::ready),revisions(original,&EditorCanvas::revisionChanged);
        const QVariantMap metadata{{"prompt","Keep every parameter"},{"seed",9876},{"model",QVariantMap{{"id","native-model"},{"precision","fp16"}}},{"references",QVariantList{"a","b"}}};
        QElapsedTimer elapsed;elapsed.start();QVERIFY(request.start(sources(file),{metadata}));
        const auto microseconds=elapsed.nsecsElapsed()/1000;QVERIFY2(microseconds<250000,"Original preparation must not run on the input owner");
        QTRY_VERIFY(gate->started.load());bool input=false;QTimer::singleShot(0,&request,[&]{input=true;});QTRY_VERIFY(input);
        QCOMPARE(project.currentCanvas(),original);QCOMPARE(iiSharedCanvas::encodeIisc(*original->document()).bytes,bytes);QCOMPARE(revisions.count(),0);
        gate->release.release();QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);
        QVERIFY(!request.active());QVERIFY(request.error().isEmpty());QVERIFY(project.currentCanvas()!=original);
        QCOMPARE(project.currentCanvas()->editingWidth(),16);QCOMPARE(ready.front()[0].toList(),QVariantList{metadata});
        qInfo()<<"gated original preparation start (us):"<<microseconds;
    }
    void aNewerIntentKeepsOnlyOneLatestPendingBatch() {
        const auto a=image("first.png"),b=image("discarded.png",Qt::blue),c=image("latest.png",Qt::green);QVERIFY(!a.isEmpty()&&!b.isEmpty()&&!c.isEmpty());
        EditorProject project;const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorImageImportRequest request(gated(gate),nullptr);request.setProject(&project);QSignalSpy ready(&request,&EditorImageImportRequest::ready);
        QVERIFY(request.start(sources(a)));QTRY_VERIFY(gate->started.load());
        QVERIFY(request.start(sources(b)));QVERIFY(request.start(sources(c)));gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);QCOMPARE(gate->calls.load(),2);QCOMPARE(gate->maximum.load(),1);
        QCOMPARE(ready.front()[1].toList(),sources(c));
        {std::lock_guard lock(gate->mutex);QCOMPARE(gate->sources,(std::vector<std::filesystem::path>{std::filesystem::path(a.toStdU16String()),std::filesystem::path(c.toStdU16String())}));}
    }
    void cancelAndDocumentEditsPreventLateAdoption() {
        const auto file=image("cancelled.png");QVERIFY(!file.isEmpty());
        for(bool edit:{false,true}) {
            EditorProject project;QVERIFY(project.createCanvas(spec()));auto *original=project.currentCanvas();
            const auto gate=std::make_shared<Gate>();Release release{gate};EditorImageImportRequest request(gated(gate),nullptr);request.setProject(&project);
            QSignalSpy ready(&request,&EditorImageImportRequest::ready),failed(&request,&EditorImageImportRequest::failed),cancelled(&request,&EditorImageImportRequest::cancelled);
            const QDir files(m_storage.filePath("Files"));const auto before=files.entryList(QDir::Files,QDir::Name);
            QVERIFY(request.start(sources(file),{},true));QTRY_VERIFY(gate->started.load());
            original->setFrame(1);original->setZoom(2);QVERIFY(request.active()); // Pure view navigation retains the intent.
            if(edit)QVERIFY(original->setLayerOpacity(original->selectedLayerId(),.6));else request.cancel();
            QVERIFY(!request.active());QCOMPARE(edit?failed.count():cancelled.count(),1);
            gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTest::qWait(50);
            QCOMPARE(ready.count(),0);QCOMPARE(project.currentCanvas(),original);QCOMPARE(project.canvasCount(),1);
            QCOMPARE(files.entryList(QDir::Files,QDir::Name),before);
        }
    }
    void failedPreparationPreservesTheDocumentAndItsErrorState() {
        const auto file=image("rejected-command.png");QVERIFY(!file.isEmpty());EditorProject project;QVERIFY(project.createCanvas(spec()));
        auto *original=project.currentCanvas();const auto bytes=iiSharedCanvas::encodeIisc(*original->document()).bytes;
        const auto projectError=project.error(),canvasError=original->error();
        const QDir files(m_storage.filePath("Files"));const auto before=files.entryList(QDir::Files,QDir::Name);
        EditorImageImportRequest request;request.setProject(&project);
        QSignalSpy failed(&request,&EditorImageImportRequest::failed),ready(&request,&EditorImageImportRequest::ready),revisions(original,&EditorCanvas::revisionChanged);
        QVERIFY(request.start({QUrl::fromLocalFile(file),QUrl::fromLocalFile(m_storage.filePath("missing-original.png"))}));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(),1,10000);QVERIFY(!request.active());QVERIFY(!request.error().isEmpty());
        QCOMPARE(ready.count(),0);QCOMPARE(project.currentCanvas(),original);QCOMPARE(revisions.count(),0);
        QCOMPARE(project.error(),projectError);QCOMPARE(original->error(),canvasError);
        QCOMPARE(iiSharedCanvas::encodeIisc(*original->document()).bytes,bytes);QCOMPARE(files.entryList(QDir::Files,QDir::Name),before);
    }
    void destructionDoesNotJoinAndLateWorkOwnsNoGuiObjects() {
        const auto file=image("destroyed-owner.png");QVERIFY(!file.isEmpty());EditorProject project;QVERIFY(project.createCanvas(spec()));
        auto *original=project.currentCanvas();const auto gate=std::make_shared<Gate>();Release release{gate};
        auto request=std::make_unique<EditorImageImportRequest>(gated(gate),nullptr);request->setProject(&project);
        QVERIFY(request->start(sources(file)));QTRY_VERIFY(gate->started.load());
        QElapsedTimer elapsed;elapsed.start();request.reset();const auto microseconds=elapsed.nsecsElapsed()/1000;
        QVERIFY2(microseconds<250000,"Closing the view must not join original preparation");
        gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTest::qWait(50);QCOMPARE(project.currentCanvas(),original);
        qInfo()<<"original preparation owner destruction (us):"<<microseconds;
    }
    void completionCanDeleteTheOwnerOrStartAnotherIntent() {
        const auto file=image("completion.png");QVERIFY(!file.isEmpty());EditorProject project;
        auto request=std::make_unique<EditorImageImportRequest>();request->setProject(&project);bool delivered=false;
        connect(request.get(),&EditorImageImportRequest::ready,&project,[&]{delivered=true;request.reset();});
        QVERIFY(request->start(sources(file)));QTRY_VERIFY_WITH_TIMEOUT(delivered,10000);QVERIFY(!request);
        EditorImageImportRequest next;next.setProject(&project);QSignalSpy ready(&next,&EditorImageImportRequest::ready);
        connect(&next,&EditorImageImportRequest::ready,&project,[&]{if(ready.count()==1)QVERIFY(next.start(sources(file)));});
        QVERIFY(next.start(sources(file)));QTRY_COMPARE_WITH_TIMEOUT(ready.count(),2,10000);QVERIFY(!next.active());
    }
    void cancellationDuringAdoptionSuppressesDeliveryAndFinishesTheCommand() {
        const auto file=image("cancel-during-adoption.png");QVERIFY(!file.isEmpty());EditorProject project;
        EditorImageImportRequest request;request.setProject(&project);
        QSignalSpy ready(&request,&EditorImageImportRequest::ready),cancelled(&request,&EditorImageImportRequest::cancelled),rejected(&request,&EditorImageImportRequest::rejected);
        bool observed=false,busy=false,accepted=true;
        connect(&project,&EditorProject::currentCanvasChanged,&request,[&] {
            observed=true;busy=request.active();accepted=request.start(sources(file));
            request.cancel();request.cancel();
        });
        QVERIFY(request.start(sources(file)));QTRY_VERIFY_WITH_TIMEOUT(observed,10000);
        QVERIFY(busy);QVERIFY(!accepted);QCOMPARE(rejected.count(),1);QCOMPARE(cancelled.count(),1);
        QCOMPARE(ready.count(),0);QVERIFY(!request.active());QVERIFY(request.error().isEmpty());
        // The already executing atomic native command completes, but cannot reopen a view.
        QCOMPARE(project.currentCanvas()->editingWidth(),16);QCOMPARE(project.canvasCount(),1);
    }
    void retiringAnIntentMayDestroyItsTargetProject() {
        const auto file=image("retired-project.png");QVERIFY(!file.isEmpty());
        auto project=std::make_unique<EditorProject>();const auto gate=std::make_shared<Gate>();Release release{gate};
        EditorImageImportRequest request(gated(gate),nullptr);request.setProject(project.get());
        QSignalSpy ready(&request,&EditorImageImportRequest::ready),rejected(&request,&EditorImageImportRequest::rejected);
        QVERIFY(request.start(sources(file)));QTRY_VERIFY(gate->started.load());
        connect(&request,&EditorImageImportRequest::cancelled,&request,[&]{project.reset();});
        QVERIFY(!request.start(sources(file)));QVERIFY(!project);QVERIFY(!request.active());QCOMPARE(rejected.count(),1);
        gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTest::qWait(50);QCOMPARE(ready.count(),0);
    }
    void nativeArtboardsRepeatTheirOwnOpaqueSourceRecord() {
        using namespace iiSharedCanvas;
        Document document;document.extent={8,8};
        document.artboards={{"one","One",{{0,0},{8,8}},0},{"two","Two",{{16,0},{8,8}},0}};
        const auto file=m_storage.filePath("multiple-records.iisc"),single=image("single-record.png");QVERIFY(!single.isEmpty());
        DocumentFile output;QVERIFY(output.create(file.toStdString(),document).ok());output.close();
        const QVariantMap native{{"model","original-model"},{"parameters",QVariantMap{{"seed",123},{"guidance",7.5}}}};
        const QVariantMap duplicate{{"model","discarded-duplicate"}},other{{"model","other-model"}};
        EditorProject project;EditorImageImportRequest request;request.setProject(&project);QSignalSpy ready(&request,&EditorImageImportRequest::ready);
        QVERIFY(request.start({QUrl::fromLocalFile(file),QUrl::fromLocalFile(file),QUrl::fromLocalFile(single)},{native,duplicate,other}));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);QCOMPARE(project.canvasCount(),3);
        QCOMPARE(ready.front()[0].toList(),(QVariantList{native,native,other}));
        QCOMPARE(ready.front()[1].toList(),(QVariantList{QUrl::fromLocalFile(file),QUrl::fromLocalFile(file),QUrl::fromLocalFile(single)}));
    }
    void aliasReplacementAndDriveChangesRetirePreparedOriginals() {
        const auto file=image("versioned-original.png");QVERIFY(!file.isEmpty());
        for(bool changeDrive:{false,true}) {
            iiSocietyContainer::Container container;
            QVariantMap alias{{"key","files/versioned-original.png"},{"containerId","drive"},{"exists",true},{"available",true},
                {"source",QUrl::fromLocalFile(file)},{"version","captured-version"}};
            container.synchronize("drive",m_storage.path(),{alias},true);
            EditorProject project;const auto gate=std::make_shared<Gate>();Release release{gate};
            EditorImageImportRequest request(gated(gate),nullptr);request.setProject(&project);request.setContainer(&container);
            QSignalSpy ready(&request,&EditorImageImportRequest::ready),failed(&request,&EditorImageImportRequest::failed);
            QVERIFY(request.start(sources(file),{alias}));QTRY_VERIFY(gate->started.load());
            // Updating unrelated presentation metadata keeps the original intent.
            alias["name"]="Updated label";container.synchronize("drive",m_storage.path(),{alias},true);QVERIFY(request.active());
            alias["version"]="replacement-version";
            container.synchronize(changeDrive?"another-drive":"drive",m_storage.path(),{alias},true);
            QVERIFY(!request.active());QCOMPARE(failed.count(),1);
            gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTest::qWait(50);QCOMPARE(ready.count(),0);
            QVERIFY(!project.currentCanvas()->documentReady());
        }
    }
    void productionRegistrationExposesTheActualQmlCommand() {
        const auto file=image("qml-command.png");QVERIFY(!file.isEmpty());EditorProject project;
        QQmlEngine engine;engine.rootContext()->setContextProperty("nativeProject",&project);
        QQmlComponent component(&engine);component.setData(R"(
            import QtQuick
            import Dreamscapes.Storage 1.0
            Item {
                id: root
                property alias request: command
                property bool delivered: false
                EditorImageImportRequest { id: command; project: nativeProject; onReady: root.delivered = true }
            }
        )",QUrl("file:///native-image-command.qml"));
        QScopedPointer<QObject> root(component.create());QVERIFY2(root,qPrintable(component.errorString()));
        auto *request=qobject_cast<EditorImageImportRequest *>(root->property("request").value<QObject *>());QVERIFY(request);
        QVERIFY(request->start(sources(file)));QTRY_VERIFY_WITH_TIMEOUT(root->property("delivered").toBool(),10000);
        QCOMPARE(project.currentCanvas()->editingWidth(),16);QVERIFY(!request->active());
    }
};
QTEST_MAIN(ImageImportRequestTests)
#include "tst_imageimportrequest.moc"
