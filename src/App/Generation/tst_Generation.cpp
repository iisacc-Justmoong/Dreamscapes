#include "../../../tests/native_link.h"
#include "GenerationController.h"
#include "ModelPreferences.h"
#include "ImageCanvas.h"
#include "VideoGeneration.h"
#include "AdvancedImageParameters.h"
#include "AdvancedImageOutput.h"
#include "SocietyGenerationStorage.h"
#include <Generation/NativePose.hpp>
#include <StorageMap.h>
#include <iiSocietyContainer/DashboardFiles.h>
#include <iiFileProviderPresentation.h>
#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLockFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest>
#include <semaphore>

using namespace iiSocietyContainer;
namespace {
bool write(const QString &path, const QByteArray &value)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(value) == value.size();
}
QJsonObject read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
QImage readCanvasImage(const QString &path)
{
    QString error;
    const auto preview = dreamscapes::imageCanvasPreview(path, QFileInfo(path).dir().filePath("../Models/.test-previews"), &error);
    return QImage(preview);
}
GenerationRuntime fakeRuntime()
{
    return {QStringLiteral(DREAMSCAPES_FAKE_GENERATOR), "cpu", 1, 64, {}};
}
QJsonObject recordedJob(const GenerationController &controller, const QString &id)
{
    for (const auto &value : controller.jobs()) {
        const auto job = value.toMap();
        if (job.value("id") == id) return QJsonObject::fromVariantMap(job);
    }
    return {};
}
QString state(const GenerationController &controller, const QString &id)
{
    for (const auto &value : controller.jobs()) {
        const auto job = value.toMap();
        if (job.value("id") == id) return job.value("state").toString();
    }
    return {};
}
bool prepare(const QTemporaryDir &root)
{
    return root.isValid() && SocietyDrive::create(root.path()).has_value()
        && write(root.filePath("Models/first model.safetensor"), "test weight A")
        && write(root.filePath("Models/second.SAFETENSORS"), "test weight B");
}
QStringList contentEntries(const QDir &directory, QDir::Filters filters)
{
    auto entries = directory.entryList(filters);
    entries.removeIf([](const QString &name) {
        return iiFileProvider::native::Presentation::isControlFile(
            std::filesystem::path(name.toStdU16String()));
    });
    return entries;
}
GenerationRuntime publicationRuntime() {
    auto runtime=fakeRuntime();runtime.nativeInference=true;
    runtime.nativeGenerate=[](const auto &request,const auto &,const auto &,const auto &,const auto &) {
        iiLocalDiffusion::NativeGenerationResult result;result.width=request.width;result.height=request.height;
        result.rgb.resize(std::size_t(request.width)*request.height*3,100);return result;
    };
    return runtime;
}
struct PublicationGate {
    std::counting_semaphore<4> release{0};
    std::atomic_bool entered{false}, done{false}, cancelled{false};
    QString directory;
};
}

class GenerationTests : public QObject
{
    Q_OBJECT
private slots:
    void fileFailureAnalysisDoesNotMutateGenerationOrStartWork() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/operation-failure-report-XXXXXX"); QVERIFY(root.isValid());
        auto runtime = fakeRuntime(); runtime.failureAgentExecutable = root.filePath("missing-agent");
        runtime.failureReportsDirectory = root.filePath("reports");
        GenerationController controller(runtime); QSignalSpy explained(&controller, &GenerationController::failureExplained);
        const auto jobs = controller.jobs(); const auto inference = controller.inferenceStatus();
        const auto error = controller.errorString();
        const auto id = controller.explainOperationFailure("Save media", "Permission denied", {{"destination", "/read-only/output.png"}});
        QVERIFY(!id.isEmpty()); QVERIFY(!controller.busy()); QCOMPARE(controller.jobs(), jobs);
        QTRY_COMPARE_WITH_TIMEOUT(explained.size(), 1, 15000);
        const auto report = controller.failureAnalysis(id); QCOMPARE(report["category"].toString(), QString("storage-access"));
        QCOMPARE(report["state"].toString(), QString("evidence-only")); QVERIFY(report["persistenceError"].toString().isEmpty());
        QCOMPARE(controller.jobs(), jobs); QCOMPARE(controller.inferenceStatus(), inference);
        QCOMPARE(controller.errorString(), error); QVERIFY(!controller.busy());
        QVERIFY(QFileInfo::exists(root.filePath("reports/." + id + ".json")));
    }
    void failedGenerationPreservesErrorAndReturnsAgentReport() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/generation-failure-report-XXXXXX.societycontainer"); QVERIFY(prepare(root));
        auto runtime = publicationRuntime();
        runtime.failureAgentExecutable = QStringLiteral(DREAMSCAPES_TEST_DIRECTORY) + "/DreamscapesFailureAgent";
        runtime.failureReportsDirectory = root.filePath("Files/FailureReports");
        runtime.nativeGenerate = [](const auto&, const auto&, const auto&, const auto& progress, const auto&) {
            progress({iiLocalDiffusion::NativeGenerationStage::Loading, 2, 8});
            iiLocalDiffusion::NativeGenerationResult result; result.error = "Fixture model loading: unsupported BF16 operation"; return result;
        };
        GenerationController controller(runtime); controller.setForeground(true); QVERIFY(controller.connectStorage(root.path()));
        QSignalSpy explained(&controller, &GenerationController::failureExplained);
        const auto id = controller.enqueue("failed request", "1:1", 1, 17); QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("failed"), 15000);
        QVERIFY(!controller.busy());
        const auto originalError = controller.errorString(); QVERIFY(originalError.contains("BF16"));
        const auto baseline = controller.failureAnalysis(id); QVERIFY(!baseline.isEmpty());
        QCOMPARE(baseline["rawError"].toString(), originalError);
        QCOMPARE(baseline["evidence"].toMap()["inference"].toMap()["state"].toString(), QString("loading"));
        QTRY_COMPARE_WITH_TIMEOUT(explained.size(), 1, 60000);
        const auto report = controller.failureAnalysis(id);
        QVERIFY2(report["agentError"].toString().isEmpty(), qPrintable(report["agentError"].toString()));
        QCOMPARE(report["category"].toString(), QString("runtime-compatibility"));
        QVERIFY(report["details"].toString().contains("2 / 8"));
        QCOMPARE(controller.errorString(), originalError); QVERIFY(!controller.busy());
        QCOMPARE(recordedJob(controller, id)["state"], "failed");
        QCOMPARE(recordedJob(controller, id)["error"].toString(), originalError);
        QVERIFY(!recordedJob(controller, id).contains("lastProgress"));
    }
    void nativeOwnerCloseRetainsWorkerValues_data() {
        QTest::addColumn<bool>("advanced");QTest::newRow("native")<<false;QTest::newRow("advanced")<<true;
    }
    void nativeOwnerCloseRetainsWorkerValues() {
        QFETCH(bool,advanced);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/native-owner-close-XXXXXX.societycontainer");QVERIFY(prepare(root));
        struct Gate : PublicationGate { iiLocalDiffusion::NativeProgressCallback progress;iiLocalDiffusion::NativePreviewCallback preview; };
        const auto gate=std::make_shared<Gate>();const auto release=qScopeGuard([gate]{gate->release.release();});
        auto sentinel=std::make_shared<int>(1);std::weak_ptr<int> provider=sentinel;auto runtime=publicationRuntime();
        dreamscapes::NativeGenerationOperation run=[gate,sentinel](const auto &request,const auto &,const auto &cancelled,const auto &progress,const auto &preview) {
            gate->progress=progress;gate->preview=preview;gate->entered=true;
            gate->release.try_acquire_for(std::chrono::seconds(5));gate->cancelled=cancelled.load();
            progress({iiLocalDiffusion::NativeGenerationStage::Decoding,1,1});
            iiLocalDiffusion::NativeGenerationPreview frame;frame.sequence=frame.step=frame.total=1;frame.width=frame.height=16;frame.rgb.resize(16*16*3,77);preview(frame);
            iiLocalDiffusion::NativeGenerationResult result;result.width=request.width;result.height=request.height;result.rgb.resize(std::size_t(request.width)*request.height*3,100);gate->done=true;return result;
        };
        if(advanced)runtime.nativeGenerateAdvanced=[run](const auto &r,const auto &o,const auto &,const auto &,const auto &c,const auto &p,const auto &v){return run(r,o,c,p,v);};
        else runtime.nativeGenerate=run;
        auto controller=std::make_unique<GenerationController>(std::move(runtime));sentinel.reset();run={};
        QVERIFY(controller->connectStorage(root.path()));QString id;
        if(advanced){AdvancedImageParameters draft(root.filePath("presets.json"));QVERIFY(draft.updateParameters({{"prompt","held native owner"},{"model","first model.safetensor"},{"width",64},{"height",64},{"steps",1}}));id=controller->enqueueAdvanced(draft.parameters());}
        else id=controller->enqueue("held native owner");
        QVERIFY(!id.isEmpty());controller->setForeground(true);QTRY_VERIFY(gate->entered.load());
        const QDir directory(root.filePath("Models/.society-runtime/iiLocalDiffusion"));const auto temporary=directory.entryList({"dreamscapes-generation-*"},QDir::Dirs|QDir::NoDotAndDotDot);QCOMPARE(temporary.size(),1);const auto working=directory.filePath(temporary.first());
        QElapsedTimer clock;clock.start();controller.reset();const auto elapsed=clock.nsecsElapsed();
        QVERIFY(!gate->done);QVERIFY(!provider.expired());QVERIFY(QFileInfo::exists(working));gate->release.release();QTRY_VERIFY(gate->done.load());QVERIFY(gate->cancelled);
        QTRY_VERIFY(!QFileInfo::exists(working));QTRY_VERIFY(provider.expired());
        gate->progress({iiLocalDiffusion::NativeGenerationStage::Encoding,1,1});iiLocalDiffusion::NativeGenerationPreview late;gate->preview(late);
        QVERIFY(!QFileInfo::exists(root.filePath("Generation History/"+id+"-0001.png")));qInfo()<<"PERF native_owner_close_ns="<<elapsed;
    }
    void cacheMigrationOwnerCloseDoesNotJoin() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/migration-owner-close-XXXXXX.societycontainer");QVERIFY(prepare(root));
        QTemporaryDir previous(DREAMSCAPES_TEST_DIRECTORY "/migration-owner-source-XXXXXX");QVERIFY(previous.isValid());QVERIFY(write(previous.filePath("cache.gguf"),"preserve cancelled cache"));
        const auto gate=std::make_shared<PublicationGate>();const auto release=qScopeGuard([gate]{gate->release.release();});auto runtime=fakeRuntime();runtime.legacyQ8CacheDirectory=previous.path();
        runtime.migrateLegacyCache=[gate](const QString &from,const QString &to,const std::atomic_bool &cancelled,QString *error){gate->entered=true;gate->release.try_acquire_for(std::chrono::seconds(5));gate->cancelled=cancelled.load();const auto result=dreamscapes::migrateLegacyQ8Cache(from,to,cancelled,error);gate->done=true;return result;};
        auto controller=std::make_unique<GenerationController>(std::move(runtime));QVERIFY(controller->connectStorage(root.path()));QTRY_VERIFY(gate->entered.load());controller.reset();
        QVERIFY(!gate->done);gate->release.release();QTRY_VERIFY(gate->done.load());QVERIFY(gate->cancelled);QVERIFY(QFileInfo::exists(previous.filePath("cache.gguf")));
    }
    void nativeCacheReleaseRunsAfterOwnerCloseOnWorker() {
        const auto gate=std::make_shared<PublicationGate>();const auto release=qScopeGuard([gate]{gate->release.release();});auto runtime=publicationRuntime();auto *owner=QThread::currentThread();const auto offOwner=std::make_shared<std::atomic_bool>(false);
        runtime.releaseNativeCache=[gate,owner,offOwner]{*offOwner=QThread::currentThread()!=owner;gate->entered=true;gate->release.try_acquire_for(std::chrono::seconds(5));gate->done=true;};
        auto controller=std::make_unique<GenerationController>(std::move(runtime));controller.reset();QTRY_VERIFY(gate->entered.load());QVERIFY(offOwner->load());QVERIFY(!gate->done);gate->release.release();QTRY_VERIFY(gate->done.load());
    }
    void inferenceObserverCanCloseTheOwner_data() {
        QTest::addColumn<QString>("phase");for(const auto *phase:{"native-loading","native-progress","worker-control","worker-result","foreground"})QTest::newRow(phase)<<QString::fromLatin1(phase);
    }
    void inferenceObserverCanCloseTheOwner() {
        QFETCH(QString,phase);QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/inference-observer-XXXXXX.societycontainer");QVERIFY(prepare(root));
        auto runtime=phase.startsWith("native")?publicationRuntime():fakeRuntime();
        if(phase=="native-progress")runtime.nativeGenerate=[](const auto &request,const auto &,const auto &,const auto &progress,const auto &){progress({iiLocalDiffusion::NativeGenerationStage::Encoding,1,1});iiLocalDiffusion::NativeGenerationResult r;r.width=request.width;r.height=request.height;r.rgb.resize(std::size_t(r.width)*r.height*3,100);return r;};
        auto controller=std::make_unique<GenerationController>(std::move(runtime));QVERIFY(controller->connectStorage(root.path()));QPointer<GenerationController> observed(controller.get());
        connect(controller.get(),&GenerationController::inferenceStatusChanged,this,[&]{if(!controller)return;const auto status=controller->inferenceStatus();if((phase=="native-loading"&&status.value("state")=="loading")||(phase=="native-progress"&&status.value("state")=="encoding")||(phase.startsWith("worker")&&status.value("ready").toBool()))controller.reset();});
        if(phase=="foreground")connect(controller.get(),&GenerationController::foregroundChanged,this,[&]{controller.reset();});
        if(phase!="worker-control"&&phase!="foreground")QVERIFY(!controller->enqueue("observer lifetime").isEmpty());controller->setForeground(true);QTRY_VERIFY(observed.isNull());
    }
    void workerResultObserverCancellationPrecedesPublication() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/worker-result-cancel-XXXXXX.societycontainer");QVERIFY(prepare(root));
        const auto publications=std::make_shared<std::atomic_int>(0);auto runtime=fakeRuntime();
        runtime.publishResult=[publications](dreamscapes::GenerationPublicationInput input){++*publications;return dreamscapes::publishGenerationResult(std::move(input));};
        GenerationController controller(runtime);QVERIFY(controller.connectStorage(root.path()));QString id;bool cancelled=false;
        connect(&controller,&GenerationController::inferenceStatusChanged,this,[&]{if(!cancelled&&controller.busy()&&controller.inferenceStatus().value("ready").toBool()){cancelled=controller.cancel(id);}});
        id=controller.enqueue("cancel ready worker result");QVERIFY(!id.isEmpty());controller.setForeground(true);QTRY_COMPARE(state(controller,id),QString("cancelled"));QVERIFY(cancelled);
        QCOMPARE(publications->load(),0);QVERIFY(!QFileInfo::exists(root.filePath("Generation History/"+id+"-0001.png")));
    }
    void videoWaitsForNativeCacheRelease_data() {
        QTest::addColumn<QString>("action");for(const auto *action:{"run","cancel","failure","close"})QTest::newRow(action)<<QString::fromLatin1(action);
    }
    void videoWaitsForNativeCacheRelease() {
        QFETCH(QString,action);QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/video-cache-release-XXXXXX.societycontainer");QVERIFY(prepare(root));
        QVERIFY(QDir().mkpath(root.filePath("Models/ltx")));QVERIFY(write(root.filePath("Models/ltx/model_index.json"),R"({"_class_name":"LTXPipeline"})"));
        struct Gate : PublicationGate { std::atomic_int calls{0};std::atomic_bool offOwner{false}; };
        const auto gate=std::make_shared<Gate>();const auto release=qScopeGuard([gate]{gate->release.release();});auto runtime=publicationRuntime();auto *owner=QThread::currentThread();
        runtime.releaseNativeCache=[gate,owner,action]{if(++gate->calls==1){gate->offOwner=QThread::currentThread()!=owner;gate->entered=true;gate->release.try_acquire_for(std::chrono::seconds(5));gate->done=true;if(action=="failure")throw std::runtime_error("Cache release fixture failed");}};
        auto controller=std::make_unique<GenerationController>(std::move(runtime));QVERIFY(controller->connectStorage(root.path()));const auto id=controller->enqueueVideo("video","1:1",1,{},1,24,5);QVERIFY(!id.isEmpty());controller->setForeground(true);
        QTRY_VERIFY(gate->entered.load());QVERIFY(gate->offOwner);QVERIFY(controller->busy());
        const QDir directory(root.filePath("Models/.society-runtime/iiLocalDiffusion"));QVERIFY(directory.entryList({"dreamscapes-inference-*"},QDir::Dirs|QDir::NoDotAndDotDot).isEmpty());
        int ticks=0;QTimer timer;timer.setInterval(1);connect(&timer,&QTimer::timeout,this,[&]{++ticks;});timer.start();QTRY_VERIFY(ticks>=3);
        if(action=="close"){controller.reset();QCOMPARE(gate->calls.load(),1);QVERIFY(!gate->done);gate->release.release();QTRY_VERIFY(gate->done.load());return;}
        if(action=="cancel"){QVERIFY(controller->cancel(id));QVERIFY(controller->busy());QVERIFY(!gate->done);}
        gate->release.release();QTRY_COMPARE_WITH_TIMEOUT(state(*controller,id),action=="run"?QString("completed"):action=="cancel"?QString("cancelled"):QString("failed"),10000);
        QCOMPARE(QFileInfo::exists(root.filePath("Generation History/"+id+"-0001.mp4")),action=="run");if(action=="failure")QVERIFY(controller->errorString().contains("Cache release fixture failed"));
    }
    void nativePreviewIsPreparedBeforeGuiDelivery() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/native-preview-worker-XXXXXX.societycontainer");QVERIFY(prepare(root));
        struct Gate : PublicationGate { std::counting_semaphore<2> start{0};std::atomic_bool inspected{false},filesReady{false};iiLocalDiffusion::NativePreviewCallback preview; };
        const auto gate=std::make_shared<Gate>();const auto release=qScopeGuard([gate]{gate->start.release();gate->release.release();});auto runtime=publicationRuntime();
        runtime.nativeGenerate=[gate](const auto &request,const auto &,const auto &,const auto &,const auto &preview){
            gate->preview=preview;gate->entered=true;gate->start.try_acquire_for(std::chrono::seconds(5));
            for(int sequence:{1,0,1,2}){iiLocalDiffusion::NativeGenerationPreview frame;frame.width=frame.height=16;frame.sequence=frame.step=sequence;frame.total=2;frame.rgb.resize(16*16*3,std::uint8_t(sequence*80));preview(frame);}
            gate->filesReady=QDir(gate->directory).entryList({"*.png"},QDir::Files).size()==2;gate->inspected=true;gate->release.try_acquire_for(std::chrono::seconds(5));
            iiLocalDiffusion::NativeGenerationResult r;r.width=request.width;r.height=request.height;r.rgb.resize(std::size_t(r.width)*r.height*3,100);return r;
        };
        GenerationController controller(runtime);QVERIFY(controller.connectStorage(root.path()));const auto id=controller.enqueue("native preview worker");QVERIFY(!id.isEmpty());controller.setForeground(true);QTRY_VERIFY(gate->entered.load());
        const QDir runtimeDirectory(root.filePath("Models/.society-runtime/iiLocalDiffusion"));const auto working=runtimeDirectory.entryList({"dreamscapes-generation-*"},QDir::Dirs|QDir::NoDotAndDotDot);QCOMPARE(working.size(),1);
        const QDir directory(runtimeDirectory.filePath(working.first()));const auto previews=directory.entryList({"preview-*"},QDir::Dirs|QDir::NoDotAndDotDot);QCOMPARE(previews.size(),1);gate->directory=directory.filePath(previews.first());gate->start.release();
        QElapsedTimer clock;clock.start();while(!gate->inspected&&clock.elapsed()<3000)QThread::msleep(1);QVERIFY(gate->inspected);QVERIFY(gate->filesReady);
        QTRY_COMPARE(controller.previewStep(),2);QCOMPARE(QImage(controller.previewImage().toLocalFile()).pixelColor(0,0).red(),160);
        gate->release.release();QTRY_COMPARE(state(controller,id),QString("completed"));QTRY_VERIFY(!controller.busy());QVERIFY(!QFileInfo::exists(gate->directory));
        iiLocalDiffusion::NativeGenerationPreview late;late.width=late.height=16;late.sequence=late.step=late.total=3;late.rgb.resize(16*16*3,200);gate->preview(late);QVERIFY(!QFileInfo::exists(gate->directory));
    }
    void resultPublicationKeepsOwnerResponsiveAndJobsSerial_data() {
        QTest::addColumn<bool>("native");
        QTest::newRow("native") << true; QTest::newRow("worker") << false;
    }
    void resultPublicationKeepsOwnerResponsiveAndJobsSerial() {
        QFETCH(bool, native);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/publication-queue-XXXXXX.societycontainer"); QVERIFY(prepare(root));
        struct Gate { std::counting_semaphore<4> release{0}; std::atomic_int calls{0}; std::atomic_bool entered{false}, offOwner{false}; };
        const auto gate = std::make_shared<Gate>(); const auto release = qScopeGuard([gate] { gate->release.release(); });
        auto runtime = fakeRuntime(); runtime.nativeInference = native;
        runtime.nativeGenerate = [](const auto &request, const auto &, const auto &, const auto &, const auto &) {
            iiLocalDiffusion::NativeGenerationResult result; result.width=request.width;result.height=request.height;
            result.rgb.resize(std::size_t(request.width)*request.height*3,100);return result;
        };
        auto *owner = QThread::currentThread();
        runtime.publishResult = [gate,owner](dreamscapes::GenerationPublicationInput input) {
            const auto call = ++gate->calls; gate->offOwner = QThread::currentThread()!=owner;
            if (call == 1) {
                gate->entered = true;
                if (!gate->release.try_acquire_for(std::chrono::seconds(5))) {
                    dreamscapes::GenerationPublicationResult result; result.error="Publication gate timed out"; return result;
                }
            }
            return dreamscapes::publishGenerationResult(std::move(input));
        };
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        const auto first=controller.enqueue("publication queue","1:1",2);QVERIFY(!first.isEmpty());controller.setForeground(true);
        QTRY_VERIFY(gate->entered.load());QVERIFY(gate->offOwner);QVERIFY(controller.busy());QCOMPARE(gate->calls.load(),1);
        int queued=0;for(const auto &entry:controller.jobs())queued+=entry.toMap().value("state")=="queued";QCOMPARE(queued,1);
        int inputs=0;QTimer timer;timer.setInterval(1);connect(&timer,&QTimer::timeout,this,[&]{++inputs;});timer.start();
        QTRY_VERIFY(inputs>=3);
        const auto revision=controller.container()->revision();
        for(int i=0;i<100;++i){QVERIFY(controller.completedResults().isEmpty());QVERIFY(controller.latestResult().isEmpty());QCOMPARE(controller.jobs().size(),2);}
        QCOMPARE(controller.container()->revision(),revision);
        gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(controller.completedResults().size(),2,10000);QTRY_VERIFY(!controller.busy());QCOMPARE(gate->calls.load(),2);
        for(const auto &entry:controller.jobs()) {
            const auto job=entry.toMap();QCOMPARE(job.value("state").toString(),QString("completed"));
            const QImage image(root.filePath(job.value("image").toString()));QCOMPARE(image.size(),QSize(64,64));
        }
    }
    void advancedPublicationAlwaysEncodesPng() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/publication-codec-XXXXXX");QVERIFY(root.isValid());
        QImage image(13,7,QImage::Format_RGB32);image.fill(QColor(50,100,150));
        const auto path=root.filePath("generated.jpg");QVERIFY(image.save(path,"JPEG"));QBuffer buffer;QVERIFY(buffer.open(QIODevice::WriteOnly));
        QString error;QVERIFY2(dreamscapes::writeAdvancedImage(path,&buffer,{{"preserveMetadata",false},{"colorProfile","sRGB"}},7,&error),qPrintable(error));
        QVERIFY(buffer.data().startsWith(QByteArray::fromHex("89504e470d0a1a0a")));
        QCOMPARE(QImage::fromData(buffer.data()).size(),image.size());
    }
    void publicationFollowsFileIntentAcrossViewAndCancellation_data() {
        QTest::addColumn<QString>("action");
        for(const auto *action:{"view","cancel","late-cancel","model-change","exception"})QTest::newRow(action)<<QString(action);
    }
    void publicationFollowsFileIntentAcrossViewAndCancellation() {
        QFETCH(QString,action);QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/publication-intent-XXXXXX.societycontainer");QVERIFY(prepare(root));
        const auto sentinel=root.filePath("Generation History/untouched.txt");QVERIFY(write(sentinel,"Existing user bytes"));
        const auto gate=std::make_shared<PublicationGate>();const auto release=qScopeGuard([gate]{gate->release.release();});
        auto runtime=publicationRuntime();
        runtime.publishResult=[gate,action](dreamscapes::GenerationPublicationInput input) {
            dreamscapes::GenerationPublicationResult result;
            if(action=="late-cancel")result=dreamscapes::publishGenerationResult(std::move(input));
            gate->entered=true;
            if(!gate->release.try_acquire_for(std::chrono::seconds(5))){result.error="Publication gate timed out";return result;}
            if(action=="exception")throw std::runtime_error("Publication fixture failed");
            if(action!="late-cancel")result=dreamscapes::publishGenerationResult(std::move(input));
            gate->done=true;return result;
        };
        GenerationController controller(runtime);QVERIFY(controller.connectStorage(root.path()));
        const auto id=controller.enqueue("file intent");QVERIFY(!id.isEmpty());controller.setForeground(true);QTRY_VERIFY(gate->entered.load());
        QVERIFY(controller.busy());QVERIFY(controller.latestResult().isEmpty());
        if(action=="view"||action=="late-cancel") {
            const auto revision=controller.container()->revision();controller.setForeground(false);
            QCOMPARE(controller.container()->revision(),revision);
        }
        if(action=="cancel")QVERIFY(controller.cancel(id));
        if(action=="late-cancel") {
            QVERIFY(QFileInfo::exists(root.filePath("Generation History/"+id+"-0001.png")));
            QVERIFY(!controller.cancel(id));
        }
        if(action=="model-change")QVERIFY(write(root.filePath("Models/first model.safetensor"),"Modified model contents"));
        gate->release.release();QTRY_VERIFY(!controller.busy());
        const bool saved=action=="view"||action=="late-cancel";
        QCOMPARE(state(controller,id),saved?QString("completed"):action=="cancel"?QString("cancelled"):QString("failed"));
        QCOMPARE(QFileInfo::exists(root.filePath("Generation History/"+id+"-0001.png")),saved);
        if(saved)QCOMPARE(QImage(controller.latestImage().toLocalFile()).size(),QSize(64,64));
        else QVERIFY(controller.latestResult().isEmpty());
        QFile existing(sentinel);QVERIFY(existing.open(QIODevice::ReadOnly));QCOMPARE(existing.readAll(),QByteArray("Existing user bytes"));
        if(action=="exception")QVERIFY(controller.errorString().contains("Publication fixture failed"));
    }
    void publicationOwnerCanCloseWithoutJoiningPreparation() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/publication-close-XXXXXX.societycontainer");QVERIFY(prepare(root));
        const auto gate=std::make_shared<PublicationGate>();const auto release=qScopeGuard([gate]{gate->release.release();});auto runtime=publicationRuntime();
        runtime.publishResult=[gate](dreamscapes::GenerationPublicationInput input) {
            gate->directory=input.directory;gate->entered=true;
            if(!gate->release.try_acquire_for(std::chrono::seconds(5))){dreamscapes::GenerationPublicationResult r;r.error="Publication gate timed out";return r;}
            auto result=dreamscapes::publishGenerationResult(std::move(input));gate->cancelled=result.cancelled;gate->done=true;return result;
        };
        auto controller=std::make_unique<GenerationController>(runtime);QVERIFY(controller->connectStorage(root.path()));
        const auto id=controller->enqueue("close preparation");QVERIFY(!id.isEmpty());controller->setForeground(true);QTRY_VERIFY(gate->entered.load());
        QElapsedTimer elapsed;elapsed.start();controller.reset();const auto closeNanoseconds=elapsed.nsecsElapsed();
        QVERIFY(!gate->done);QVERIFY(QFileInfo::exists(gate->directory));gate->release.release();QTRY_VERIFY(gate->done.load());
        QVERIFY(gate->cancelled);QTRY_VERIFY(!QFileInfo::exists(gate->directory));
        QVERIFY(!QFileInfo::exists(root.filePath("Generation History/"+id+"-0001.png")));
        qInfo()<<"PERF publication_owner_close_ns="<<closeNanoseconds;
    }
    void publicationStorageObserverCanDeleteTheOwner() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/publication-adopt-XXXXXX.societycontainer");QVERIFY(prepare(root));
        auto controller=std::make_unique<GenerationController>(publicationRuntime());QVERIFY(controller->connectStorage(root.path()));
        const auto id=controller->enqueue("close adoption");QVERIFY(!id.isEmpty());QPointer<GenerationController> observed(controller.get());
        auto *files=qobject_cast<iiSocietyHelper::FileSystem *>(controller->container()->parent());QVERIFY(files);
        connect(files,&iiSocietyHelper::FileSystem::storageChanged,this,[&] {
            if(controller&&controller->container()->alias("generation-history/"+id+"-0001.png").value("available").toBool())controller.reset();
        });
        controller->setForeground(true);QTRY_VERIFY(observed.isNull());
        QCOMPARE(QImage(root.filePath("Generation History/"+id+"-0001.png")).size(),QSize(64,64));
    }
    void terminalScreenActivityCanDeleteOwner() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/publication-screen-close-XXXXXX.societycontainer");QVERIFY(prepare(root));
        std::unique_ptr<GenerationController> controller;
        auto runtime=publicationRuntime();bool wasActive=false;
        runtime.nativeGenerate=[](const auto &,const auto &,const auto &,const auto &,const auto &) {
            iiLocalDiffusion::NativeGenerationResult result;result.error="Generation fixture failed";return result;
        };
        runtime.screenActivity=[&](bool active) {
            if(active)wasActive=true;
            else if(wasActive&&controller)controller.reset();
        };
        controller=std::make_unique<GenerationController>(runtime);QVERIFY(controller->connectStorage(root.path()));
        QVERIFY(!controller->enqueue("close terminal activity").isEmpty());QPointer<GenerationController> observed(controller.get());
        controller->setForeground(true);QTRY_VERIFY(observed.isNull());QVERIFY(wasActive);
    }
    void terminalNotificationWaitsForTemporaryFiles() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/publication-retire-XXXXXX.societycontainer");QVERIFY(prepare(root));
        auto *pool=QThreadPool::globalInstance();const auto threads=pool->maxThreadCount();pool->setMaxThreadCount(1);
        const auto gate=std::make_shared<PublicationGate>();QFuture<void> blocker;
        const auto release=qScopeGuard([&]{gate->release.release();pool->setMaxThreadCount(threads);if(blocker.isValid())blocker.waitForFinished();});
        auto runtime=fakeRuntime();runtime.steps=3;GenerationController controller(runtime);QVERIFY(controller.connectStorage(root.path()));
        QList<QUrl> previews;bool cleared=false;
        connect(&controller,&GenerationController::previewChanged,this,[&] {
            if(controller.previewImage().isEmpty()){if(!previews.isEmpty())cleared=true;return;}
            previews.append(controller.previewImage());
            if(controller.previewStep()==3)blocker=QtConcurrent::run([gate]{gate->entered=true;gate->release.try_acquire_for(std::chrono::seconds(5));});
        });
        const auto id=controller.enqueue("live-fail");QVERIFY(!id.isEmpty());QTRY_VERIFY(gate->entered.load());QTRY_VERIFY(cleared);
        QCOMPARE(state(controller,id),QString("running"));QVERIFY(controller.busy());QVERIFY(QFileInfo::exists(previews.first().toLocalFile()));
        int ticks=0;QTimer timer;timer.setInterval(1);connect(&timer,&QTimer::timeout,this,[&]{++ticks;});timer.start();QTRY_VERIFY(ticks>=3);
        gate->release.release();QTRY_COMPARE(state(controller,id),QString("failed"));QTRY_VERIFY(!controller.busy());
        for(const auto &preview:previews)QVERIFY(!QFileInfo::exists(preview.toLocalFile()));
    }
    void helperContainerAvailabilityNotifiesTheApplication() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/container-state-XXXXXX.societycontainer");
        QVERIFY(SocietyDrive::create(root.path()));
        GenerationController controller(fakeRuntime()); QVERIFY(controller.connectStorage(root.path()));
        QSignalSpy changed(&controller, &GenerationController::storageChanged);
        const auto manifest = root.filePath(".society-drive.json"), hidden = root.filePath("manifest-unavailable");
        QVERIFY(QFile::rename(manifest, hidden));
        controller.container()->refresh();
        QTRY_VERIFY(!controller.connected()); QVERIFY(!controller.container()->available()); QVERIFY(!changed.isEmpty());
        QVERIFY(QFile::rename(hidden, manifest));
        changed.clear(); controller.container()->refresh();
        QTRY_VERIFY(controller.connected()); QVERIFY(controller.container()->available()); QVERIFY(!changed.isEmpty());
    }
    void generatedPngBecomesCanvasOnlyWhenOpenedForEditing() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/png-history-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime()); QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(controller.container()); QVERIFY(controller.container()->available());
        QCOMPARE(controller.container()->rootPath(), root.path());
        const auto containerRevision = controller.container()->revision();
        DashboardFiles history; history.setContainerPath(root.path()); QTRY_VERIFY(!history.loading());
        QVERIFY(history.generationHistory().isEmpty());
        const auto id = controller.enqueue("keep the generated PNG"); QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("completed"), 10000);
        const auto path = root.filePath("Generation History/" + id + "-0001.png");
        QFile original(path); QVERIFY(original.open(QIODevice::ReadOnly));
        const auto bytes = original.readAll(); original.close();
        QVERIFY(bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a")));
        QCOMPARE(QImage(path).size(), QSize(64, 64));
        QCOMPARE(controller.latestResult().value("imageSource").toUrl(), QUrl::fromLocalFile(path));
        QTRY_COMPARE(history.generationHistory().size(), 1);
        QCOMPARE(history.generationHistory().first().toMap().value("previewSource").toUrl().toLocalFile(), path);
        QVERIFY(QDir(root.filePath("Generation History")).entryList({"*.iisc"}, QDir::Files).isEmpty());
        const auto native = controller.imageDocument(QUrl::fromLocalFile(path));
        const auto pngAlias = controller.container()->alias("generation-history/" + id + "-0001.png");
        QVERIFY(pngAlias["exists"].toBool()); QVERIFY(pngAlias["available"].toBool());
        QCOMPARE(pngAlias["previewSource"].toUrl().toLocalFile(), path);
        const auto documentAlias = controller.container()->alias("files/" + QFileInfo(native.toLocalFile()).fileName());
        QVERIFY(documentAlias["exists"].toBool()); QVERIFY(controller.container()->revision() > containerRevision);
        QVERIFY(native.toLocalFile().endsWith(".iisc"));
        QVERIFY(native.toLocalFile().startsWith(root.filePath("Files/")));
        QVERIFY(QDir(root.filePath("Generation History")).entryList({"*.iisc"}, QDir::Files).isEmpty());
        const auto rendered = readCanvasImage(native.toLocalFile()); QCOMPARE(rendered.size(), QImage(path).size());
        QCOMPARE(rendered.pixelColor(0, 0), QImage(path).pixelColor(0, 0));
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), bytes);
        QCOMPARE(controller.latestResult().value("imageSource").toUrl(), QUrl::fromLocalFile(path));
        history.refresh(); QTRY_VERIFY(!history.loading()); QCOMPARE(history.generationHistory().size(), 1);
    }
    void storedCanvasPresentationReadsOnlyTheAliasSnapshot() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/stored-canvas-view-XXXXXX.societycontainer"); QVERIFY(prepare(root));
        QImage pixels(19, 11, QImage::Format_RGB32); pixels.fill(Qt::red);
        const auto png = root.filePath("Files/reference.png"); QVERIFY(pixels.save(png));
        GenerationController controller(fakeRuntime()); QVERIFY(controller.connectStorage(root.path()));
        const auto source = controller.imageDocument(QUrl::fromLocalFile(png)); QVERIFY(source.isLocalFile());
        QVariantMap alias;
        for (const auto &row : controller.container()->aliases())
            if (row.toMap().value("path").toString() == source.toLocalFile()) alias = row.toMap();
        QVERIFY(!alias.isEmpty());
        auto *files = qobject_cast<iiSocietyHelper::FileSystem *>(controller.container()->parent()); QVERIFY(files);
        QSignalSpy sync(files, &iiSocietyHelper::FileSystem::synchronizationFinished);
        const auto error = files->errorString();
        // A view reads the last published snapshot; opening/editing validates
        // the actual file independently. Removing it must not turn a getter
        // into an implicit hash/preview read or storage rescan.
        const auto parked = source.toLocalFile() + ".parked";
        QVERIFY(QFile::rename(source.toLocalFile(), parked));
        QUrl preview;
        for (int i = 0; i < 100; ++i) {
            const auto result = controller.storedImage(alias);
            QCOMPARE(result.value("documentSource").toUrl(), source);
            preview = result.value("imageSource").toUrl();
            QCOMPARE(preview.scheme(), "image"); QCOMPARE(preview.host(), "iisc-preview");
        }
        QCOMPARE(sync.count(), 0); QCOMPARE(files->errorString(), error);
        QVERIFY(!QDir(root.filePath("Models/.society-runtime/canvas-previews")).exists());
        QVERIFY(QFile::rename(parked, source.toLocalFile()));
        const auto before = controller.storedImage(alias).value("imageSource").toUrl();
        QVERIFY(!before.isEmpty());
        auto forged = alias; forged["path"] = root.filePath("Files/untracked.iisc");
        QVERIFY(controller.storedImage(forged).isEmpty());
        QVERIFY(QFile::remove(source.toLocalFile())); QVERIFY(files->refresh());
        QVERIFY(controller.storedImage(alias).isEmpty());
    }
    void unavailableNativeHistoryRetainsItsOpenIdentity() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/remote-native-view-XXXXXX.societycontainer");QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());QVERIFY(controller.connectStorage(root.path()));
        auto *container=controller.container();
        QVariantMap remote{{"key","generation-history/remote.iisc"},{"section","generation-history"},
            {"relativePath","remote.iisc"},{"name","remote.iisc"},{"containerId",container->containerId()},
            {"exists",true},{"available",false},{"path",QString{}},{"source",QUrl{}},{"isImage",false}};
        container->synchronize(container->containerId(),container->rootPath(),{remote},true);
        auto *files=qobject_cast<iiSocietyHelper::FileSystem *>(container->parent());QVERIFY(files);
        QSignalSpy sync(files,&iiSocietyHelper::FileSystem::synchronizationFinished);
        const auto shown=controller.storedImage(remote);
        QCOMPARE(shown.value("key"),remote.value("key"));
        QCOMPARE(shown.value("containerId"),remote.value("containerId"));
        QCOMPARE(shown.value("name").toString(),"remote.iisc");
        QVERIFY(!shown.value("available").toBool());
        QVERIFY(shown.value("documentSource").toUrl().isEmpty());
        QVERIFY(shown.value("imageSource").toUrl().isEmpty());
        QCOMPARE(shown,remote);
        QCOMPARE(sync.count(),0);
    }
    void resultSnapshotReadsStayIndependentOfProducerWork() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/result-snapshot-XXXXXX.societycontainer");QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());QVERIFY(controller.connectStorage(root.path()));
        const auto id=controller.enqueue("snapshot","1:1",4);QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(controller.completedResults().size(),4,10000);
        const auto expected=controller.completedResults();const auto latest=controller.latestResult();
        QSignalSpy changed(&controller,&GenerationController::resultsChanged),jobs(&controller,&GenerationController::jobsChanged);
        auto *files=qobject_cast<iiSocietyHelper::FileSystem *>(controller.container()->parent());QVERIFY(files);
        QSignalSpy scans(files,&iiSocietyHelper::FileSystem::synchronizationFinished);
        QElapsedTimer clock;clock.start();
        for(int i=0;i<2000;++i) {
            QCOMPARE(controller.completedResults().size(),4);
            QCOMPARE(controller.latestResult().value("id").toString(),latest.value("id").toString());
        }
        qInfo()<<"PERF result_snapshot reads=2000 outputs=4 elapsed_us="<<clock.nsecsElapsed()/1000;
        QCOMPARE(changed.size(),0);QCOMPARE(jobs.size(),0);QCOMPARE(scans.size(),0);
        QCOMPARE(controller.completedResults(),expected);
        auto caller=controller.latestResult();caller["prompt"]="Caller-only edit";
        QCOMPARE(controller.latestResult(),latest);
        const auto original=controller.container()->aliases();QVariantList unavailable;
        for(const auto &entry:original) {auto value=entry.toMap();if(value.value("section")=="generation-history")value["available"]=false;unavailable.append(value);}
        const auto owner=controller.container()->containerId(),path=controller.container()->rootPath();
        controller.container()->synchronize(owner,path,unavailable,true);
        QVERIFY(controller.completedResults().isEmpty());QVERIFY(controller.latestResult().isEmpty());
        QCOMPARE(jobs.size(),0);
        controller.container()->synchronize(owner,path,original,true);
        QCOMPARE(controller.completedResults(),expected);QCOMPARE(controller.latestResult(),latest);
        const auto count=changed.size();controller.container()->setQuery("Home-only query");
        QCOMPARE(changed.size(),count);QCOMPARE(controller.completedResults(),expected);
    }
    void rejectedStorageReconnectClearsPublishedResults() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/result-reconnect-XXXXXX.societycontainer"); QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime()); QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(!controller.enqueue("reconnect", "1:1", 1).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(controller.completedResults().size(), 1, 10000);
        QVERIFY(!controller.latestResult().isEmpty());
        // A preserved, occupied legacy path makes this reconnect reject cleanup
        // after the owner has opened the same drive and cleared its session jobs.
        QVERIFY(QDir(root.path()).mkpath(".dreamscapes"));
        QVERIFY(write(root.filePath(".dreamscapes/generation"), "preserve occupied legacy path"));
        bool errorSawClearedResults = false;
        connect(&controller, &GenerationController::errorChanged, this, [&] {
            if (controller.errorString().isEmpty()) return;
            errorSawClearedResults = controller.jobs().isEmpty()
                && controller.completedResults().isEmpty() && controller.latestResult().isEmpty();
        });
        QSignalSpy jobs(&controller, &GenerationController::jobsChanged);
        QVERIFY(!controller.connectStorage(root.path()));
        QVERIFY(!controller.errorString().isEmpty());
        QVERIFY(controller.jobs().isEmpty());
        QVERIFY(controller.completedResults().isEmpty());
        QVERIFY(controller.latestResult().isEmpty());
        QVERIFY(errorSawClearedResults);
        QCOMPARE(jobs.count(), 1);
        QFile occupied(root.filePath(".dreamscapes/generation")); QVERIFY(occupied.open(QIODevice::ReadOnly));
        QCOMPARE(occupied.readAll(), QByteArray("preserve occupied legacy path"));
    }
    void nativeFillCanvasCanBeUsedAsAnAdvancedReference() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/native-reference-XXXXXX.societycontainer"); QVERIFY(prepare(root));
        QImage pixels(19, 11, QImage::Format_RGB32); pixels.fill(QColor(10, 20, 30));
        const auto png = root.filePath("Files/generated.png"); QVERIFY(pixels.save(png));
        QString error;
        const auto native = dreamscapes::persistImageCanvas(png, &error); QVERIFY2(!native.isEmpty(), qPrintable(error));
        auto runtime = fakeRuntime(); runtime.nativeInference = true;
        bool received = false;
        runtime.nativeGenerateAdvanced = [&](const auto &request, const auto &, const auto &, const auto &controls,
            const auto &, const auto &, const auto &) {
            received = controls.references.size() == 1 && controls.references[0].width == 19
                && controls.references[0].height == 11 && controls.references[0].rgb[0] == 10;
            iiLocalDiffusion::NativeGenerationResult result; result.width = request.width; result.height = request.height;
            result.rgb.resize(request.width * request.height * 3, 100); return result;
        };
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        QCOMPARE(controller.imageDocument(QUrl::fromLocalFile(png)), QUrl::fromLocalFile(native));
        QVERIFY(QFile::remove(png));
        QCOMPARE(controller.imageDocument(QUrl::fromLocalFile(native)), QUrl::fromLocalFile(native));
        QVERIFY(!QImage(controller.imagePreview(QUrl::fromLocalFile(native)).toLocalFile()).isNull());
        AdvancedImageParameters draft(root.filePath("presets.json"));
        QVERIFY(draft.updateParameters({{"prompt", "use native reference"}, {"model", "first model.safetensor"},
            {"width", 64}, {"height", 64}, {"steps", 1}, {"referenceImages", QVariantList{QUrl::fromLocalFile(native).toString()}}}));
        const auto id = controller.enqueueAdvanced(draft.parameters()); QVERIFY2(!id.isEmpty(), qPrintable(controller.errorString()));
        controller.setForeground(true); QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("completed"), 10000);
        QVERIFY(received); QVERIFY(controller.latestResult().value("imageSource").toUrl().toLocalFile().endsWith(".png"));
    }
    void videoRecipeValidatesRuntimeContract()
    {
        const QJsonObject defaults{{"prompt","a scene"}};
        QString error;
        auto valid=defaults;
        QVERIFY2(dreamscapes::normalizeVideoRecipe(&valid,&error),qPrintable(error));
        QCOMPARE(valid.value("frames").toInt(),120);
        QCOMPARE(valid.value("width").toInt(),1024);
        QCOMPARE(valid.value("shots").toArray().size(),1);
        for (int fps : {60,120}) {
            auto recipe=defaults; recipe["fps"]=fps; recipe["duration"]=30;
            QVERIFY2(dreamscapes::normalizeVideoRecipe(&recipe,&error),qPrintable(error));
            QCOMPARE(recipe.value("fps").toInt(),fps);
            QCOMPARE(recipe.value("frames").toInt(),30*fps);
            QCOMPARE(recipe.value("interpolationFactor").toInt(),2);
        }
        const QList<QPair<QString,QJsonValue>> invalid{
            {"width",65},{"height",0},{"duration",31},{"fps",25},{"fps",90},{"outputCount",1.5},{"steps",0},
            {"cfgScale",-1},{"seed",4294967296.},{"decodeTimestep",1.1},{"decodeNoiseScale",-.1},
            {"imageConditionNoise",2},{"interpolationFactor",1},{"crf",52},{"cpuTextEncoding",1},
            {"vaeTiling","true"},{"device","unknown"},{"precision","half"},{"offload","swap"},
            {"encodingPreset","best"},{"prompt",""},{"negativePrompt",1},{"shots",QJsonObject{}}};
        for(const auto &entry:invalid) {
            auto recipe=defaults; recipe[entry.first]=entry.second; error.clear();
            QVERIFY2(!dreamscapes::normalizeVideoRecipe(&recipe,&error),qPrintable(entry.first));
            QVERIFY(!error.isEmpty());
        }
        auto cpu=defaults; cpu["device"]="cpu"; cpu["offload"]="model";
        QVERIFY(!dreamscapes::normalizeVideoRecipe(&cpu,&error));
        auto cameraRecipe=defaults; cameraRecipe["camera"]=QJsonArray{"dolly-in","pan-left","tilt-up"};
        QVERIFY(dreamscapes::normalizeVideoRecipe(&cameraRecipe,&error));
        QCOMPARE(cameraRecipe["shots"].toArray().first().toObject()["camera"].toArray(),cameraRecipe["camera"].toArray());
        for(const auto &camera:QList<QJsonValue>{QJsonArray{},QJsonArray{"none","dolly-in"},QJsonArray{"pan-left","pan-right"},QJsonArray{"dolly-in","dolly-in"},QJsonArray{"invalid"},QJsonArray{"static","pan-left"},QString("pan-left")}) {
            auto invalidCamera=defaults; invalidCamera["camera"]=camera;
            QVERIFY(!dreamscapes::normalizeVideoRecipe(&invalidCamera,&error));
        }
        auto composition=defaults;
        composition["shots"]=QJsonArray{QJsonObject{{"frames",24},{"prompt","first"}},
            QJsonObject{{"frames",36},{"conditions",QJsonArray{QJsonObject{{"image","file:///frame.png"},{"frame",13},{"strength",.65}}}}}};
        QVERIFY(dreamscapes::normalizeVideoRecipe(&composition,&error));
        QCOMPARE(composition.value("frames").toInt(),60);
        auto badShots=defaults; badShots["shots"]=QJsonArray{};
        QVERIFY(!dreamscapes::normalizeVideoRecipe(&badShots,&error));
        for(int frame:{-1,24}) {
            auto bad=defaults; bad["shots"]=QJsonArray{QJsonObject{{"frames",24},{"conditions",QJsonArray{QJsonObject{{"image","file:///frame.png"},{"frame",frame}}}}}};
            QVERIFY(!dreamscapes::normalizeVideoRecipe(&bad,&error));
        }
        const QJsonObject condition{{"image","file:///frame.png"},{"frame",4}};
        auto duplicate=defaults; duplicate["shots"]=QJsonArray{QJsonObject{{"frames",24},{"conditions",QJsonArray{condition,condition}}}};
        QVERIFY(!dreamscapes::normalizeVideoRecipe(&duplicate,&error));
        auto excessive=defaults; excessive["shots"]=QJsonArray{QJsonObject{{"frames",4097}},QJsonObject{{"frames",2}}};
        QVERIFY(!dreamscapes::normalizeVideoRecipe(&excessive,&error));
    }
    void videoPresetsKeepOnlyGenerationControls()
    {
        QTemporaryDir directory;
        GenerationRuntime runtime;
        runtime.modelPreferencesFile=directory.filePath("설정/generation.conf");
        QVariantMap values{{"prompt",""},{"width",576},{"height",1024},{"duration",5},{"fps",24},
            {"randomSeed",true},{"seed",-1},{"camera1","dolly-in"},{"camera2","pan-left"},{"camera3","none"},
            {"model","Checkpoint/LTX 2.3.safetensors"},{"shots",QVariantList{QVariantMap{{"prompt","do not save"}}}},
            {"keys",QVariantList{"file:///private-reference.png"}}};
        GenerationController first(runtime);
        QVERIFY2(first.saveVideoPreset("Portrait",values),qPrintable(first.videoPresetError()));
        QCOMPARE(first.videoPresetNames(),QStringList{"Portrait"});
        GenerationController restored(runtime);
        const auto preset=restored.videoPreset("Portrait");
        QCOMPARE(preset.value("width").toInt(),576);
        QCOMPARE(preset.value("camera2").toString(),QString("pan-left"));
        QVERIFY(!preset.contains("shots")); QVERIFY(!preset.contains("keys"));
        QVERIFY(preset.value("prompt").toString().isEmpty());
        values["camera2"]="dolly-out";
        QVERIFY(!restored.saveVideoPreset("Portrait",values));
        QCOMPARE(restored.videoPreset("Portrait"),preset);
        const auto path=runtime.modelPreferencesFile+".video-presets.json";
        auto catalog=read(path); auto presets=catalog.value("presets").toObject();
        auto corrupt=presets.value("Portrait").toObject(); corrupt["width"]=1;
        presets["Portrait"]=corrupt; catalog["presets"]=presets;
        QVERIFY(write(path,QJsonDocument(catalog).toJson()));
        QVERIFY(restored.videoPreset("Portrait").isEmpty());
        QVERIFY(!restored.videoPresetError().isEmpty());
        QVERIFY(restored.jobs().isEmpty());
    }
    void ltx2CheckpointClassificationAndUserOverrides()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/ltx2-modalities-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        const QString id = QString::fromUtf8("Checkpoint/이름을 바꾼 checkpoint.safetensors");
        const auto path = root.filePath("Models/" + id);
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        const QJsonObject config{{"transformer", QJsonObject{{"_class_name", "AVTransformer3DModel"}}}};
        const QJsonObject header{{"__metadata__", QJsonObject{{"model_version", "2.3.0"},
            {"config", QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact))}}}};
        const auto json = QJsonDocument(header).toJson(QJsonDocument::Compact);
        QByteArray weight(8, '\0');
        for (int i = 0; i < 8; ++i) weight[i] = char((quint64(json.size()) >> (8 * i)) & 255);
        weight += json;
        QVERIFY(write(path, weight));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        QCOMPARE(controller.videoModels().size(), 1);
        QCOMPARE(controller.videoModels().first().toMap().value("id").toString(), id);
        QCOMPARE(controller.models().size(), 2);
        QCOMPARE(controller.modelModalities(id), (QStringList{"audio", "video"}));
        QCOMPARE(controller.selectedVideoModel(), id);
        controller.setSelectedModel(id);
        QVERIFY(controller.selectedModel() != id);

        QVERIFY2(controller.setModelModalities(id, {"image", "video"}), qPrintable(controller.errorString()));
        QCOMPARE(controller.models().size(), 3);
        QCOMPARE(controller.videoModels().size(), 1);
        controller.setSelectedModel(id);
        QCOMPARE(controller.selectedModel(), id);
        controller.refreshModels();
        QCOMPARE(controller.selectedModel(), id);
        const auto before = controller.jobs();
        QVERIFY(controller.setModelModalities(id, {"video"}));
        QCOMPARE(controller.jobs(), before);
        QCOMPARE(controller.models().size(), 2);
        QVERIFY(!controller.setModelModalities(id, {"invalid"}));
        QCOMPARE(controller.modelModalities(id), (QStringList{"video"}));
        QVERIFY(controller.setModelModalities(id, {}));
        QCOMPARE(controller.modelModalities(id), (QStringList{"audio", "video"}));
        QFile source(path); QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), weight);

        GenerationController restored(fakeRuntime());
        QVERIFY(restored.connectStorage(root.path()));
        QCOMPARE(restored.selectedVideoModel(), id);
        QCOMPARE(restored.modelModalities(id), (QStringList{"audio", "video"}));
        QVERIFY(!restored.setModelModalities("../outside", {"video"}));
    }
    void videoRecipeUsesExplicitModelWithoutChangingCurrentSelection()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/video-explicit-model-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        for (const auto *name : {"ltx-a", "ltx-b"}) {
            const auto directory = "Models/" + QString::fromLatin1(name);
            QVERIFY(QDir().mkpath(root.filePath(directory)));
            QVERIFY(write(root.filePath(directory + "/model_index.json"), R"({"_class_name":"LTX2Pipeline"})"));
        }
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        QCOMPARE(controller.selectedVideoModel(), QString("ltx-a"));
        const auto job = controller.enqueueVideoRecipe({{"model", "ltx-b"}, {"prompt", "A camera moves through a studio"}});
        QVERIFY2(!job.isEmpty(), qPrintable(controller.errorString()));
        QCOMPARE(recordedJob(controller, job).value("model").toObject().value("path").toString(), QString("ltx-b"));
        QCOMPARE(controller.selectedVideoModel(), QString("ltx-a"));
    }
    void defaultModelsPersistAndPreserveQueuedRequests()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/model-preferences-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        for (const auto *name : {"ltx-a", "ltx-b"}) {
            const auto directory = "Models/" + QString::fromLatin1(name);
            QVERIFY(QDir().mkpath(root.filePath(directory)));
            QVERIFY(write(root.filePath(directory + "/model_index.json"), R"({"_class_name":"LTXPipeline"})"));
        }
        auto runtime = fakeRuntime();
        runtime.modelPreferencesFile = root.filePath("settings/default-models.conf");
        {
            GenerationController controller(runtime);
            QVERIFY(controller.connectStorage(root.path()));
            const auto queued = controller.enqueue("queued before preference change");
            QVERIFY(!queued.isEmpty());
            const auto original = recordedJob(controller, queued).value("model").toObject();
            QCOMPARE(original.value("path").toString(), controller.selectedModel());
            QVERIFY(controller.setDefaultImageModel("second.SAFETENSORS"));
            QVERIFY(controller.setDefaultVideoModel("ltx-b"));
            QCOMPARE(recordedJob(controller, queued).value("model").toObject(), original);
            QCOMPARE(controller.selectedModel(), QString("second.SAFETENSORS"));
            QCOMPARE(controller.selectedVideoModel(), QString("ltx-b"));
            const auto next = controller.enqueue("queued after preference change");
            QVERIFY2(!next.isEmpty(), qPrintable(controller.errorString()));
            QCOMPARE(recordedJob(controller, next).value("model").toObject().value("path").toString(), QString("second.SAFETENSORS"));
            controller.setSelectedModel("first model.safetensor");
            controller.setSelectedVideoModel("ltx-a");
            controller.refreshModels();
            QCOMPARE(controller.selectedModel(), QString("first model.safetensor"));
            QCOMPARE(controller.defaultImageModel(), QString("second.SAFETENSORS"));
            QVERIFY(!controller.setDefaultImageModel("ltx-b"));
            QVERIFY(!controller.setDefaultVideoModel("first model.safetensor"));
            QCOMPARE(controller.defaultVideoModel(), QString("ltx-b"));
        }
        GenerationController restored(runtime);
        QVERIFY(restored.connectStorage(root.path()));
        QCOMPARE(restored.selectedModel(), QString("second.SAFETENSORS"));
        QCOMPARE(restored.selectedVideoModel(), QString("ltx-b"));
        QVERIFY(QFile::remove(root.filePath("Models/second.SAFETENSORS")));
        QVERIFY(QDir(root.filePath("Models/ltx-b")).removeRecursively());
        restored.refreshModels();
        QCOMPARE(restored.selectedModel(), QString("first model.safetensor"));
        QCOMPARE(restored.selectedVideoModel(), QString("ltx-a"));
        QCOMPARE(restored.defaultImageModel(), QString("second.SAFETENSORS"));
        QCOMPARE(restored.defaultVideoModel(), QString("ltx-b"));
        QVERIFY(write(root.filePath("Models/second.SAFETENSORS"), "restored model"));
        QVERIFY(QDir().mkpath(root.filePath("Models/ltx-b")));
        QVERIFY(write(root.filePath("Models/ltx-b/model_index.json"), R"({"_class_name":"LTXPipeline"})"));
        restored.refreshModels();
        QCOMPARE(restored.selectedModel(), QString("second.SAFETENSORS"));
        QCOMPARE(restored.selectedVideoModel(), QString("ltx-b"));
        QVERIFY(restored.setDefaultImageModel({}));
        QVERIFY(restored.setDefaultVideoModel({}));
        GenerationController automatic(runtime);
        QVERIFY(automatic.connectStorage(root.path()));
        QVERIFY(automatic.defaultImageModel().isEmpty());
        QVERIFY(automatic.defaultVideoModel().isEmpty());
        QCOMPARE(automatic.selectedModel(), QString("first model.safetensor"));
        QCOMPARE(automatic.selectedVideoModel(), QString("ltx-a"));
    }
    void defaultPromptsPersistAndReadLegacyModels()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/prompt-preferences-XXXXXX");
        auto runtime = fakeRuntime();
        runtime.modelPreferencesFile = root.filePath("defaults.conf");
        GenerationController fresh(runtime);
        for (const auto &category : {QString("Image"), QString("Video"), QString("Audio"), QString("Agents")}) {
            const auto prompts = fresh.defaultGenerationPrompts().value(category).toMap();
            QVERIFY(prompts.value("prompt").toString().isEmpty());
            QVERIFY(prompts.value("negativePrompt").toString().isEmpty());
        }
        QVERIFY(write(runtime.modelPreferencesFile,
            "Dreamscapes generation models 1\n\"legacy image\"\n\"legacy video\"\n"));
        QVariantMap expected;
        {
            GenerationController controller(runtime);
            QCOMPARE(controller.defaultImageModel(), QString("legacy image"));
            QCOMPARE(controller.defaultVideoModel(), QString("legacy video"));
            QVERIFY(controller.defaultGenerationPrompts().value("Video").toMap().value("negativePrompt").toString().isEmpty());
            for (const auto &category : {QString("Image"), QString("Video"), QString("Audio"), QString("Agents")}) {
                const auto prompt = category + QString::fromUtf8(" 기본 \"프롬프트\" \\ 경로\n다음 줄 ").repeated(1000);
                const auto negative = category + QString::fromUtf8(" 제외할 요소, noise, artifacts");
                QVERIFY(controller.setDefaultGenerationPrompt(category, prompt, false));
                QVERIFY(controller.setDefaultGenerationPrompt(category, negative, true));
                expected[category] = QVariantMap{{"prompt", prompt}, {"negativePrompt", negative}};
            }
            QCOMPARE(controller.defaultGenerationPrompts(), expected);
            QVERIFY(!controller.setDefaultGenerationPrompt("Unknown", "wrong category", false));
            QVERIFY(!controller.setDefaultGenerationPrompt("Image", QString(32001, 'x'), true));
            QCOMPARE(controller.defaultGenerationPrompts(), expected);
        }
        GenerationController restored(runtime);
        QCOMPARE(restored.defaultGenerationPrompts(), expected);
        QCOMPARE(restored.defaultImageModel(), QString("legacy image"));
        QCOMPARE(restored.defaultVideoModel(), QString("legacy video"));
        QVERIFY(restored.setDefaultGenerationPrompt("Image", {}, true));
        GenerationController cleared(runtime);
        QVERIFY(cleared.defaultGenerationPrompts().value("Image").toMap().value("negativePrompt").toString().isEmpty());
        QCOMPARE(cleared.defaultGenerationPrompts().value("Video"), expected.value("Video"));
        QVERIFY(write(runtime.modelPreferencesFile, "Dreamscapes generation preferences 2\n\"image\"\n\"video\"\n"));
        GenerationController malformed(runtime);
        QVERIFY(!malformed.modelPreferencesError().isEmpty());
        QVERIFY(malformed.defaultImageModel().isEmpty());
        QVERIFY(malformed.defaultGenerationPrompts().value("Image").toMap().value("prompt").toString().isEmpty());
    }
    void defaultPromptSaveFailureAndQueuedSnapshot()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/prompt-snapshot-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.modelPreferencesFile = root.filePath("settings/defaults.conf");
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(controller.setDefaultGenerationPrompt("Image", "first default", false));
        QVERIFY(controller.setDefaultGenerationPrompt("Image", "first negative", true));
        const auto first = controller.enqueue("explicit first prompt");
        QVERIFY(!first.isEmpty());
        QVERIFY(controller.setDefaultGenerationPrompt("Image", "next negative", true));
        const auto second = controller.enqueue("explicit next prompt");
        QVERIFY(!second.isEmpty());
        QCOMPARE(recordedJob(controller, first).value("prompt").toString(), QString("explicit first prompt"));
        QCOMPARE(recordedJob(controller, first).value("negativePrompt").toString(), QString("first negative"));
        QCOMPARE(recordedJob(controller, second).value("negativePrompt").toString(), QString("next negative"));
        const auto before = controller.defaultGenerationPrompts();
        QFile saved(runtime.modelPreferencesFile); QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto bytes = saved.readAll(); saved.close();
        QVERIFY(QDir().mkpath(runtime.modelPreferencesFile + ".tmp"));
        QVERIFY(!controller.setDefaultGenerationPrompt("Image", "cannot save", false));
        QVERIFY(!controller.modelPreferencesError().isEmpty());
        QCOMPARE(controller.defaultGenerationPrompts(), before);
        QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), bytes);
        QCOMPARE(recordedJob(controller, first).value("negativePrompt").toString(), QString("first negative"));
    }
    void defaultModelSaveFailurePreservesSelection()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/model-preferences-error-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.modelPreferencesFile = root.filePath("settings/default-models.conf");
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(controller.setDefaultImageModel("first model.safetensor"));
        const auto saved = [&] { QFile file(runtime.modelPreferencesFile); file.open(QIODevice::ReadOnly); return file.readAll(); }();
        QVERIFY(QDir().mkpath(runtime.modelPreferencesFile + ".tmp"));
        QVERIFY(!controller.setDefaultImageModel("second.SAFETENSORS"));
        QVERIFY(!controller.modelPreferencesError().isEmpty());
        QCOMPARE(controller.defaultImageModel(), QString("first model.safetensor"));
        QCOMPARE(controller.selectedModel(), QString("first model.safetensor"));
        QFile file(runtime.modelPreferencesFile); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), saved);
    }
    void unchangedPreferencesDoNotWriteOrRepublish()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/unchanged-preferences-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.modelPreferencesFile = root.filePath("settings/default-models.conf");
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(controller.setDefaultImageModel("first model.safetensor"));
        QVERIFY(controller.setDefaultGenerationPrompt("Image", "A quiet forest", false));
        QFile saved(runtime.modelPreferencesFile); QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto bytes = saved.readAll(); saved.close();
        // An unchanged setter must not enter the writer or treat its temporary
        // path as a new save request. Real changed writes retain error handling.
        QVERIFY(QDir().mkpath(runtime.modelPreferencesFile + ".tmp"));
        QSignalSpy preferences(&controller, &GenerationController::modelPreferencesChanged);
        QSignalSpy jobs(&controller, &GenerationController::jobsChanged);
        for (int i = 0; i < 20; ++i) {
            QVERIFY(controller.setDefaultImageModel("first model.safetensor"));
            QVERIFY(controller.setDefaultGenerationPrompt("Image", "A quiet forest", false));
        }
        QCOMPARE(preferences.size(), 0); QCOMPARE(jobs.size(), 0);
        QVERIFY(controller.modelPreferencesError().isEmpty());
        QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), bytes);
        controller.setSelectedModel("second.SAFETENSORS");
        QCOMPARE(controller.selectedModel(), QString("second.SAFETENSORS"));
        QVERIFY(controller.setDefaultImageModel("first model.safetensor"));
        QCOMPARE(controller.selectedModel(), QString("first model.safetensor"));
        QCOMPARE(preferences.size(), 0); QCOMPARE(jobs.size(), 0);
    }
    void malformedModelPreferencesUseSafeDefaults()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/model-preferences-malformed-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime(); runtime.modelPreferencesFile = root.filePath("defaults.conf");
        QVERIFY(write(runtime.modelPreferencesFile, "invalid preferences"));
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(!controller.modelPreferencesError().isEmpty());
        QVERIFY(controller.defaultImageModel().isEmpty());
        QCOMPARE(controller.selectedModel(), QString("first model.safetensor"));
        // Equal safe defaults still repair a failed load; only a healthy,
        // unchanged configuration can skip its write.
        QVERIFY(controller.setDefaultImageModel({}));
        QVERIFY(controller.modelPreferencesError().isEmpty());
        dreamscapes::ModelPreferences repaired;
        std::string repairError;
        QVERIFY(dreamscapes::readModelPreferences(runtime.modelPreferencesFile.toStdString(), repaired, repairError));
        QVERIFY(repaired.imageModel.empty());
        QVERIFY(controller.setDefaultImageModel("second.SAFETENSORS"));
        QVERIFY(controller.modelPreferencesError().isEmpty());
    }
    void videoInstalledRuntimeCreatesDecodedMp4()
    {
        const auto model=qEnvironmentVariable("DREAMSCAPES_VIDEO_SMOKE_MODEL");
        const auto launcher=qEnvironmentVariable("DREAMSCAPES_VIDEO_SMOKE_LAUNCHER");
        if(model.isEmpty() || launcher.isEmpty()) QSKIP("Set a local LTX fixture/model and installed SDK launcher for real video inference.");
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/real-video-XXXXXX.societycontainer");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        const auto target=root.filePath("Models/Checkpoint/LTX-runtime-smoke");
        QVERIFY(QDir().mkpath(target));
        QDirIterator iterator(model,QDir::Files,QDirIterator::Subdirectories);
        while(iterator.hasNext()) {
            iterator.next();
            const auto relative=QDir(model).relativeFilePath(iterator.filePath());
            const auto destination=QDir(target).filePath(relative);
            QVERIFY(QDir().mkpath(QFileInfo(destination).absolutePath()));
            QVERIFY(QFile::copy(iterator.filePath(),destination));
        }
        auto runtime=fakeRuntime(); runtime.executable=launcher; runtime.device="mps"; runtime.nativeInference=true;
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        QCOMPARE(controller.videoModels().size(),1);
        const auto id=controller.enqueueVideo("A red cube rotates slowly.","1:1",1,{},1,24,42);
        QVERIFY2(!id.isEmpty(),qPrintable(controller.errorString()));
        connect(&controller,&GenerationController::inferenceStatusChanged,&controller,[&controller] {
            qInfo() << "Video runtime:" << controller.inferenceStatus();
        });
        controller.setForeground(true);
        QTRY_VERIFY_WITH_TIMEOUT(state(controller,id)=="completed" || state(controller,id)=="failed",360000);
        QVERIFY2(state(controller,id)=="completed",qPrintable(controller.errorString()));
        const auto result=controller.latestResult();
        const auto generation=result.value("generation").toMap();
        QCOMPARE(generation.value("video").toMap().value("frame_count").toInt(),24);
        QVERIFY(generation.value("video").toMap().value("verified_decode").toBool());
        QCOMPARE(generation.value("stages").toList().size(),2);
        QVERIFY(result.value("mediaSource").toUrl().isLocalFile());
        root.setAutoRemove(false);
        QVERIFY(write(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/video-verification/runtime-result.json"),
            QJsonDocument(QJsonObject::fromVariantMap(result)).toJson(QJsonDocument::Indented)));
    }
    void videoQueuePreservesInputsAndPublishesAllResults_data()
    {
        QTest::addColumn<int>("fps");
        QTest::newRow("24-fps") << 24;
        QTest::newRow("60-fps") << 60;
        QTest::newRow("120-fps") << 120;
    }
    void videoQueuePreservesInputsAndPublishesAllResults()
    {
        QFETCH(int,fps);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/video-queue-XXXXXX.societycontainer"); QVERIFY(prepare(root));
        QVERIFY(QDir().mkpath(root.filePath("Models/Checkpoint/ltx")));
        QVERIFY(write(root.filePath("Models/Checkpoint/ltx/model_index.json"),
            R"({"_class_name":"LTXConditionPipeline"})"));
        QImage image(80,40,QImage::Format_RGB32); image.fill(Qt::red);
        const auto png = root.filePath("Files/keyframe.png"); QVERIFY(image.save(png));
        QString nativeError;
        const auto source = QUrl::fromLocalFile(dreamscapes::persistImageCanvas(png, &nativeError));
        QVERIFY2(source.isLocalFile(), qPrintable(nativeError)); QVERIFY(QFile::remove(png));
        auto runtime = fakeRuntime(); runtime.nativeInference = true;
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        QCOMPARE(controller.videoModels().size(),1);
        QCOMPARE(controller.selectedVideoModel(),QString("Checkpoint/ltx"));
        QVERIFY(controller.models().size() == 2);
        QSignalSpy submitted(&controller,&GenerationController::submissionQueued);
        const auto id = controller.enqueueVideo("video","16:9",2,source,1,fps,42);
        QVERIFY2(!id.isEmpty(),qPrintable(controller.errorString()));
        QCOMPARE(submitted.size(),1); QCOMPARE(submitted[0][0].toStringList().size(),2);
        const auto queued = recordedJob(controller,id);
        QCOMPARE(queued.value("mediaType").toString(),QString("Video"));
        QCOMPARE(queued.value("width").toInt(),1024); QCOMPARE(queued.value("height").toInt(),576);
        const auto owned = queued.value("videoParameters").toObject().value("firstFrame").toString();
        QVERIFY(owned != source.toLocalFile()); QVERIFY(QFileInfo::exists(owned));
        QVERIFY(QFile::remove(source.toLocalFile()));
        controller.setForeground(true);
        // Two output publications include owned keyframe and IISC persistence.
        // This checks completion/ownership, not shared-storage throughput.
        QTRY_VERIFY2_WITH_TIMEOUT(controller.completedResults().size() == 2,
            QJsonDocument::fromVariant(controller.jobs()).toJson(QJsonDocument::Compact).constData(), 60000);
        QCOMPARE(state(controller,id),QString("completed"));
        for(const auto &entry:controller.completedResults()) {
            const auto result=entry.toMap();
            QCOMPARE(result.value("mediaType").toString(),QString("Video"));
            QVERIFY(QFileInfo(result.value("mediaSource").toUrl().toLocalFile()).isFile());
            QVERIFY(!QImage(result.value("imageSource").toUrl().toLocalFile()).isNull());
            QCOMPARE(result.value("generation").toMap().value("configuration").toMap().value("first_frame").toString(),owned);
            QCOMPARE(result.value("generation").toMap().value("configuration").toMap().value("fps").toInt(),fps);
            QCOMPARE(result.value("generation").toMap().value("video").toMap().value("frame_count").toInt(),fps);
        }
        // The common file catalog owns portable companions; standalone
        // generation reports must still stay inside their native documents.
        const QDir history(root.filePath("Generation History"));
        for (const auto &name : history.entryList({"*.json"}, QDir::Files)) {
            const auto metadata = read(history.filePath(name));
            QVERIFY2(name.endsWith(".iisacc.json") && metadata.value("format") == "iiFileProvider.Metadata"
                && metadata.value("schema").toInt() == 1, qPrintable("Unexpected generation JSON: " + name));
        }
        QCOMPARE(QDir(root.filePath("Generation History")).entryList({"*.mp4"},QDir::Files).size(),2);
        QCOMPARE(controller.latestResult().value("mediaType").toString(),QString("Video"));
        QVERIFY(write(root.filePath("Models/Checkpoint/ltx/model_index.json"),R"({"_class_name":"StableDiffusionPipeline"})"));
        controller.refreshModels(); QVERIFY(controller.videoModels().isEmpty());
        QVERIFY(controller.enqueueVideo("video").isEmpty());
    }
    void videoRejectsBadOutputAndCancelsWorker_data()
    {
        QTest::addColumn<QString>("prompt"); QTest::addColumn<QString>("expected");
        QTest::newRow("bad-hash") << QString("video-corrupt") << QString("failed");
        QTest::newRow("bad-size") << QString("video-size") << QString("failed");
        QTest::newRow("bad-report") << QString("video-report") << QString("failed");
        QTest::newRow("cancel") << QString("video-hold") << QString("cancelled");
    }
    void videoRejectsBadOutputAndCancelsWorker()
    {
        QFETCH(QString,prompt); QFETCH(QString,expected);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/video-errors-XXXXXX.societycontainer"); QVERIFY(prepare(root));
        QVERIFY(QDir().mkpath(root.filePath("Models/ltx")));
        QVERIFY(write(root.filePath("Models/ltx/model_index.json"),R"({"_class_name":"LTXPipeline"})"));
        auto runtime=fakeRuntime(); runtime.nativeInference=true;
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(controller.enqueueVideo("video","invalid").isEmpty());
        QVERIFY(controller.enqueueVideo("video","1:1",0).isEmpty());
        QVERIFY(controller.enqueueVideo("video","1:1",1,{},0).isEmpty());
        QVERIFY(controller.enqueueVideo("video","1:1",1,{},5,0).isEmpty());
        const auto id=controller.enqueueVideo(prompt); QVERIFY2(!id.isEmpty(),qPrintable(controller.errorString()));
        controller.setForeground(true);
        if(expected=="cancelled") { QTRY_COMPARE_WITH_TIMEOUT(state(controller,id),QString("running"),5000); QVERIFY(controller.cancel(id)); }
        QTRY_COMPARE_WITH_TIMEOUT(state(controller,id),expected,15000);
        QVERIFY(controller.completedResults().isEmpty());
        QVERIFY(QDir(root.filePath("Generation History")).entryList(QDir::Files).isEmpty());
    }
    void advancedWatermarkIsAppliedAtPublication_data()
    {
        QTest::addColumn<bool>("native");
        QTest::newRow("native") << true;
        QTest::newRow("worker") << false;
    }
    void advancedWatermarkIsAppliedAtPublication()
    {
        QFETCH(bool, native);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/watermark-queue-XXXXXX.societycontainer"); QVERIFY(prepare(root));
        auto runtime = fakeRuntime(); runtime.nativeInference = native;
        runtime.nativeGenerateAdvanced = [](const auto &request, const auto &, const auto &, const auto &,
            const auto &, const auto &, const auto &) {
            iiLocalDiffusion::NativeGenerationResult result;
            result.width = request.width; result.height = request.height;
            result.rgb.resize(request.width * request.height * 3, 100);
            return result;
        };
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        AdvancedImageParameters draft(root.filePath("presets.json"));
        QVERIFY(draft.updateParameters({{"prompt", "watermark"}, {"model", "first model.safetensor"},
            {"width", 128}, {"height", 128}, {"outputCount", 2}, {"steps", 1}, {"seed", 42},
            {"watermark", true}, {"preserveMetadata", false}}));
        const auto markedId = controller.enqueueAdvanced(draft.parameters());
        QVERIFY2(!markedId.isEmpty(), qPrintable(controller.errorString()));
        QVERIFY(draft.updateParameters({{"watermark", false}, {"outputCount", 1}}));
        const auto plainId = controller.enqueueAdvanced(draft.parameters()); QVERIFY(!plainId.isEmpty());
        controller.setForeground(true);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, plainId), QString("completed"), 15000);
        const QImage plain(controller.latestImage().toLocalFile()); QVERIFY(!plain.isNull());
        int markedCount = 0;
        for (const auto &entry : controller.jobs()) {
            const auto job = QJsonObject::fromVariantMap(entry.toMap());
            const bool marked = job.value("advancedParameters").toObject().value("watermark").toBool();
            const QImage output = readCanvasImage(root.filePath(job.value("image").toString())); QVERIFY(!output.isNull());
            QCOMPARE(output.size(), QSize(128, 128)); QVERIFY(output.textKeys().isEmpty());
            if (!marked) continue;
            ++markedCount;
            QCOMPARE(job.value("state").toString(), QString("completed"));
            int changed = 0;
            for (int y = 0; y < 128; ++y) for (int x = 0; x < 128; ++x) {
                if (output.pixelColor(x, y) != plain.pixelColor(x, y)) {
                    QVERIFY(QRect(118, 118, 8, 8).contains(x, y)); ++changed;
                }
            }
            QVERIFY(changed > 0);
        }
        QCOMPARE(markedCount, 2); // Later draft edits cannot disable queued watermarks.
    }
    void homeCanvasOwnsReferencesAndForwardsNativePixels()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/home-inputs-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        QImage reference(80,40,QImage::Format_RGB888); reference.fill(QColor(11,12,13));
        const auto original = root.filePath("Files/reference.png"); QVERIFY(reference.save(original));
        auto runtime = fakeRuntime(); runtime.nativeInference = true;
        iiLocalDiffusion::NativeAdvancedControls received;
        runtime.nativeGenerateAdvanced = [&](const auto &r, const auto &, const auto &, const auto &s,
            const auto &, const auto &, const auto &) {
            received = s;
            iiLocalDiffusion::NativeGenerationResult result;
            result.width = r.width; result.height = r.height; result.rgb.resize(r.width*r.height*3,100);
            return result;
        };
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        AdvancedImageParameters draft(root.filePath("presets.json"));
        QVERIFY(draft.updateParameters({{"prompt","home canvas"},{"model","first model.safetensor"},
            {"width",1824},{"height",1024},{"outputCount",1},{"steps",1},
            {"referenceImages",QVariantList{QUrl::fromLocalFile(original).toString()}}}));
        const auto id = controller.enqueueHomeCanvas(draft.parameters(),"16:9");
        QVERIFY2(!id.isEmpty(),qPrintable(controller.errorString()));
        const auto job = recordedJob(controller,id);
        QCOMPARE(job.value("aspectRatio").toString(),QString("16:9"));
        const auto referencePath = job.value("advancedParameters").toObject().value("referenceImages").toArray()[0].toString();
        const auto owned = QUrl(referencePath).isLocalFile() ? QUrl(referencePath).toLocalFile() : referencePath;
        QVERIFY(owned != original); QVERIFY(QFileInfo::exists(owned));
        QDirIterator inputCanvases(root.filePath("Files/Dreamscapes/GenerationInputs"), {"*.iisc"}, QDir::Files, QDirIterator::Subdirectories);
        QVERIFY(inputCanvases.hasNext());
        const auto preserved = dreamscapes::readImageCanvas(inputCanvases.next());
        QVERIFY(preserved.ok()); QCOMPARE(preserved.document.extent.width, 80);
        QVERIFY(QFile::remove(original));
        controller.setForeground(true);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller,id),QString("completed"),15000);
        QCOMPARE(received.references.size(),size_t(1));
        QCOMPARE(received.references[0].width,80); QCOMPARE(received.references[0].height,40);
        QCOMPARE(received.references[0].rgb[0],quint8(11));
        QCOMPARE(received.references[0].rgb[1],quint8(12));
        QCOMPARE(received.references[0].rgb[2],quint8(13));
    }

    void homeKreaDefaultsAndAdvancedOverrides()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/home-krea-XXXXXX.societycontainer"); QVERIFY(prepare(root));
        // Recognizable metadata under a neutral filename; no pretrained tensor compute.
        const QByteArray header = R"({"model.diffusion_model.txtfusion.projector.weight":{"dtype":"F32","shape":[1],"data_offsets":[0,4]}})";
        const quint64 size = header.size();
        QByteArray weights(reinterpret_cast<const char *>(&size), sizeof(size));
        weights += header; weights += QByteArray(4, 0);
        QVERIFY(write(root.filePath("Models/first model.safetensor"), weights));
        QImage image(80, 40, QImage::Format_RGB888); image.fill(QColor(11, 12, 13));
        const auto input = root.filePath("Files/reference.png"); QVERIFY(image.save(input));
        auto runtime = fakeRuntime(); runtime.nativeInference = true;
        int steps = 0; float cfg = 0;
        runtime.nativeGenerateAdvanced = [&](const auto &request, const auto &, const auto &components,
            const auto &sampling, const auto &, const auto &, const auto &) {
            steps = request.steps; cfg = components.guidanceScale;
            if (sampling.references.size() != 1) return iiLocalDiffusion::NativeGenerationResult{};
            iiLocalDiffusion::NativeGenerationResult result;
            result.width = request.width; result.height = request.height;
            result.rgb.resize(request.width * request.height * 3, 100); return result;
        };
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        auto parameters = dreamscapes::imageParametersToMap(iiLocalDiffusion::ImageParameters::defaults());
        parameters["prompt"] = "Krea reference"; parameters["model"] = "first model.safetensor";
        parameters["width"] = 1024; parameters["height"] = 1368; parameters["outputCount"] = 1;
        parameters["referenceImages"] = QVariantList{QUrl::fromLocalFile(input).toString()};
        const auto home = controller.enqueueHomeCanvas(parameters, "3:4"); QVERIFY(!home.isEmpty());
        controller.setForeground(true);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, home), QString("completed"), 15000);
        QCOMPARE(steps, 52); QCOMPARE(cfg, 7.0f);
        const auto recipe = recordedJob(controller, home).value("advancedParameters").toObject();
        QCOMPARE(recipe.value("steps").toInt(), 52); QCOMPARE(recipe.value("cfgScale").toDouble(), 7.0);
        parameters["steps"] = 9; parameters["cfgScale"] = 2.0;
        const auto advanced = controller.enqueueAdvanced(parameters); QVERIFY(!advanced.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, advanced), QString("completed"), 15000);
        QCOMPARE(steps, 9); QCOMPARE(cfg, 2.0f);
    }

    void advancedSubmissionSnapshotsAndForwardsParameters()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/advanced-worker-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        QVERIFY(write(root.filePath("Models/style.safetensors"), "test adapter"));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        AdvancedImageParameters draft(root.filePath("presets.json"));
        QVERIFY(draft.updateParameters({{"prompt", "slow advanced"}, {"negativePrompt", "noise, blur"},
            {"model", "first model.safetensor"}, {"width", 64}, {"height", 128},
            {"outputCount", 2}, {"steps", 23}, {"seed", 100}, {"cfgScale", 4.5}, {"sampler", "heun"}}));
        const auto lora = draft.addLora(root.filePath("Models/style.safetensors"), "Style"); QVERIFY(!lora.isEmpty());
        QVERIFY(draft.updateLora(lora, {{"weight", 0.35}}));
        const auto id = controller.enqueueAdvanced(draft.parameters());
        QVERIFY2(!id.isEmpty(), qPrintable(controller.errorString()));
        QString second;
        for (const auto &value : controller.jobs()) {
            const auto candidate = value.toMap().value("id").toString();
            if (candidate != id) second = candidate;
        }
        QVERIFY(!second.isEmpty());
        QVERIFY(draft.updateParameters({{"prompt", "later edit"}, {"steps", 99}, {"cfgScale", 1.0}}));
        controller.setSelectedModel("second.SAFETENSORS");
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, second), QString("completed"), 15000);
        const auto job = recordedJob(controller, id);
        const auto generation = job.value("generation").toObject();
        QCOMPARE(job.value("prompt").toString(), QString("slow advanced"));
        QCOMPARE(job.value("steps").toInt(), 23);
        QCOMPARE(job.value("width").toInt(), 64); QCOMPARE(job.value("height").toInt(), 128);
        QCOMPARE(job.value("model").toObject().value("path").toString(), QString("first model.safetensor"));
        QCOMPARE(generation.value("negative_prompt").toString(), QString("noise, blur"));
        QCOMPARE(generation.value("guidance_scale").toDouble(), 4.5);
        QCOMPARE(generation.value("native_sampler").toString(), QString("heun"));
        QCOMPARE(generation.value("lora_scale").toDouble(), 0.35);
        QCOMPARE(generation.value("engine").toString(), QString("native"));
        QCOMPARE(recordedJob(controller, second).value("seed").toInteger(), qint64(101));
        const auto snapshot = job.value("advancedParameters").toObject();
        QCOMPARE(snapshot.value("cfgScale").toDouble(), 4.5);
        QVERIFY(!snapshot.value("loras").toArray().isEmpty());
        QVERIFY(draft.updateParameters({{"transparentBackground", true}}));
        QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
        QVERIFY(controller.errorString().contains("transparentBackground"));
        QCOMPARE(controller.jobs().size(), 2);
        QVERIFY(draft.updateParameters({{"transparentBackground", false}}));
        QVERIFY(draft.addReferenceImage("reference.png"));
        QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
        QVERIFY(controller.errorString().contains("referenceImages"));
        QCOMPARE(controller.jobs().size(), 2);
    }

    void advancedPoseResourcesReachNativeQueue()
    {
        if (!iiLocalDiffusion::nativePoseAvailable()) QSKIP("Pose backend disabled");
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/advanced-pose-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        QVERIFY(write(root.filePath("Models/control.safetensors"), "control fixture"));
        QVERIFY(write(root.filePath("Models/detector.onnx"), "detector fixture"));
        QVERIFY(write(root.filePath("Models/pose.onnx"), "pose fixture"));
        QImage reference(3, 2, QImage::Format_RGB888); reference.fill(QColor(11, 12, 13));
        QVERIFY(reference.save(root.filePath("Files/reference.png")));
        auto runtime = fakeRuntime(); runtime.nativeInference = true;
        iiLocalDiffusion::NativeAdvancedControls received;
        runtime.nativeGenerateAdvanced = [&](const auto &r, const auto &, const auto &, const auto &s,
            const auto &, const auto &, const auto &) {
            received = s;
            iiLocalDiffusion::NativeGenerationResult result;
            result.width = r.width; result.height = r.height; result.rgb.resize(r.width * r.height * 3, 100);
            return result;
        };
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        AdvancedImageParameters draft(root.filePath("presets.json"));
        QVERIFY(draft.updateParameters({{"prompt", "pose queue"}, {"model", "first model.safetensor"},
            {"width", 64}, {"height", 64}, {"outputCount", 1}}));
        const auto controlId = draft.addControlNet("Pose");
        QVERIFY(draft.updateControlNet(controlId, {{"imageSource", "reference.png"},
            {"model", "control.safetensors"}, {"poseDetector", "detector.onnx"}, {"poseModel", "pose.onnx"}}));
        QVERIFY(draft.applyControlNet(controlId));
        const auto id = controller.enqueueAdvanced(draft.parameters());
        QVERIFY2(!id.isEmpty(), qPrintable(controller.errorString()));
        QVERIFY(draft.updateControlNet(controlId, {{"poseModel", "missing.onnx"}}));
        controller.setForeground(true);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("completed"), 15000);
        QCOMPARE(received.controls.size(), size_t(1));
        QCOMPARE(received.controls.front().process, std::string("Pose"));
        QCOMPARE(QString::fromStdString(received.controls.front().poseDetector.string()),
            QFileInfo(root.filePath("Models/detector.onnx")).canonicalFilePath());
        QCOMPARE(QString::fromStdString(received.controls.front().poseModel.string()),
            QFileInfo(root.filePath("Models/pose.onnx")).canonicalFilePath());
        QVERIFY(draft.applyControlNet(controlId));
        QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
        QCOMPARE(controller.jobs().size(), 1);
    }

    void advancedNativeReceivesSamplingComponentsAndAdapters()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/advanced-native-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        QVERIFY(QDir().mkpath(root.filePath("Models/VAE")));
        QVERIFY(write(root.filePath("Models/VAE/custom.safetensors"), "test vae"));
        QVERIFY(write(root.filePath("Models/style.safetensors"), "test adapter"));
        QImage reference(3, 2, QImage::Format_RGB888); reference.fill(QColor(11, 12, 13));
        QVERIFY(write(root.filePath("Models/detail.safetensors"), "test embedding"));
        QVERIFY(write(root.filePath("Models/control.safetensors"), "test control"));
        QVERIFY(write(root.filePath("Models/second-control.safetensors"), "test second control"));
        QVERIFY(write(root.filePath("Models/ip.safetensors"), "test IP adapter"));
        QVERIFY(write(root.filePath("Models/vision.safetensors"), "test CLIP vision"));
        QVERIFY(write(root.filePath("Models/upscaler.safetensors"), "test upscaler"));
        QVERIFY(write(root.filePath("Models/detector.safetensors"), "test detector"));
        QVERIFY(write(root.filePath("Models/refiner.safetensors"), "test refiner"));
        QVERIFY(reference.save(root.filePath("Files/reference.png")));
        reference.fill(QColor(21, 22, 23)); QVERIFY(reference.save(root.filePath("Files/second.png")));
        auto runtime = fakeRuntime(); runtime.nativeInference = true;
        iiLocalDiffusion::NativeGenerationRequest received;
        iiLocalDiffusion::NativeGenerationOptions options;
        iiLocalDiffusion::NativeModelComponents components;
        iiLocalDiffusion::NativeAdvancedControls sampling;
        runtime.nativeGenerateAdvanced = [&](const auto &r, const auto &o, const auto &c, const auto &s,
            const auto &, const auto &, const auto &) {
            received = r; options = o; components = c; sampling = s;
            iiLocalDiffusion::NativeGenerationResult result;
            result.width = r.width; result.height = r.height; result.rgb.resize(r.width * r.height * 3, 100);
            return result;
        };
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        AdvancedImageParameters draft(root.filePath("presets.json"));
        QVERIFY(draft.updateParameters({{"prompt", "native advanced"}, {"negativePrompt", "noise"},
            {"model", "first model.safetensor"}, {"vae", "VAE/custom.safetensors"},
            {"width", 64}, {"height", 128}, {"outputCount", 1}, {"steps", 19}, {"seed", 42},
            {"cfgScale", 6.5}, {"sampler", "dpmpp_2m"}, {"scheduler", "karras"}, {"clipSkip", 2},
            {"eta", 0.65}, {"seamlessTiling", true}, {"hiresFix", true}, {"denoiseStrength", 0.4},
            {"upscaler", "4x-ultra"}, {"upscalerModel", "upscaler.safetensors"},
            {"detailer", true}, {"detailerModel", "detector.safetensors"},
            {"refiner", true}, {"refinerSwitch", 0.65}, {"refinerModel", "refiner.safetensors"},
            {"textualEmbeddings", "detail.safetensors"}, {"promptWeighting", false}, {"freeU", true}}));
        QVERIFY(!draft.addLora(root.filePath("Models/style.safetensors")).isEmpty());
        QVERIFY(draft.addReferenceImage(QUrl::fromLocalFile(root.filePath("Files/reference.png")).toString()));
        QVERIFY(draft.addReferenceImage("second.png"));
        QVERIFY(draft.updateParameters({{"imageStrength", 0.45}}));
        const auto controlId = draft.addControlNet("Canny");
        QVERIFY(!controlId.isEmpty());
        QVERIFY(draft.updateControlNet(controlId, {{"imageSource", "second.png"},
            {"model", QUrl::fromLocalFile(root.filePath("Models/control.safetensors")).toString()}, {"weight", 0.7},
            {"regionalMask", true}, {"maskSource", "second.png"}, {"ipAdapter", true},
            {"ipAdapterModel", "ip.safetensors"}, {"ipAdapterVision", "vision.safetensors"}}));
        QVERIFY(draft.applyControlNet(controlId));
        QVERIFY(!draft.addControlNet("Pose").isEmpty()); // Unapplied drafts never execute.
        const auto secondControlId = draft.addControlNet("Tile");
        QVERIFY(!secondControlId.isEmpty());
        QVERIFY(draft.updateControlNet(secondControlId, {{"imageSource", "reference.png"},
            {"model", "second-control.safetensors"}, {"weight", 1.25},
            {"regionalMask", true}, {"maskSource", "reference.png"}}));
        QVERIFY(draft.applyControlNet(secondControlId));
        const auto ipOnly = draft.addControlNet("IP-Adapter");
        QVERIFY(draft.updateControlNet(ipOnly, {{"imageSource", "reference.png"}, {"ipAdapter", true},
            {"ipAdapterModel", QUrl::fromLocalFile(root.filePath("Models/ip.safetensors")).toString()},
            {"ipAdapterVision", root.filePath("Models/vision.safetensors")}, {"weight", 0.4}}));
        QVERIFY(draft.applyControlNet(ipOnly));
        const auto id = controller.enqueueAdvanced(draft.parameters());
        QVERIFY2(!id.isEmpty(), qPrintable(controller.errorString()));
        controller.setForeground(true);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("completed"), 15000);
        QCOMPARE(received.steps, 19); QCOMPARE(received.seed, 42); QCOMPARE(received.height, 128);
        QCOMPARE(options.negativePrompt, std::string("noise")); QCOMPARE(options.loras.size(), size_t(1));
        QCOMPARE(components.guidanceScale, 6.5f); QVERIFY(!components.vae.empty());
        QCOMPARE(sampling.sampler, std::string("dpmpp_2m"));
        QCOMPARE(sampling.scheduler, std::string("karras"));
        QCOMPARE(sampling.clipSkip, 2); QCOMPARE(sampling.eta, 0.65f);
        QVERIFY(sampling.seamlessTiling);
        QVERIFY(sampling.hires); QCOMPARE(sampling.denoiseStrength, 0.4f);
        QCOMPARE(sampling.upscaler, std::string("4x-ultra"));
        QCOMPARE(QString::fromStdString(sampling.upscalerModel.string()),
            QFileInfo(root.filePath("Models/upscaler.safetensors")).canonicalFilePath());
        QVERIFY(sampling.detailer);
        QVERIFY(sampling.refiner); QCOMPARE(sampling.refinerSwitch, 0.65f);
        QCOMPARE(QString::fromStdString(sampling.refinerModel.string()),
            QFileInfo(root.filePath("Models/refiner.safetensors")).canonicalFilePath());
        QCOMPARE(QString::fromStdString(sampling.detailerModel.string()),
            QFileInfo(root.filePath("Models/detector.safetensors")).canonicalFilePath());
        QVERIFY(!sampling.promptWeighting); QCOMPARE(sampling.embeddings.size(), size_t(1));
        QVERIFY(sampling.freeU);
        QCOMPARE(sampling.embeddings[0].token, std::string("user_detail"));
        QCOMPARE(QString::fromStdString(sampling.embeddings[0].path.string()),
            QFileInfo(root.filePath("Models/detail.safetensors")).canonicalFilePath());
        QCOMPARE(sampling.imageStrength, 0.45f); QCOMPARE(sampling.references.size(), size_t(2));
        QCOMPARE(sampling.references[0].width, 3); QCOMPARE(sampling.references[0].height, 2);
        QCOMPARE(sampling.references[0].rgb.front(), uint8_t(11));
        QCOMPARE(sampling.references[1].rgb.front(), uint8_t(21));
        QCOMPARE(sampling.controls.size(), size_t(2));
        QCOMPARE(sampling.ipAdapters.size(), size_t(2));
        QCOMPARE(sampling.ipAdapters.front().weight, .7f);
        QCOMPARE(sampling.ipAdapters.front().image.rgb.front(), uint8_t(21));
        QCOMPARE(sampling.ipAdapters.front().mask.rgb.front(), uint8_t(22));
        QCOMPARE(sampling.ipAdapters.back().weight, .4f);
        QCOMPARE(sampling.ipAdapters.back().image.rgb.front(), uint8_t(11));
        QVERIFY(sampling.ipAdapters.back().mask.rgb.empty());
        QCOMPARE(QString::fromStdString(sampling.ipAdapters.front().model.string()),
            QFileInfo(root.filePath("Models/ip.safetensors")).canonicalFilePath());
        QCOMPARE(QString::fromStdString(sampling.ipAdapters.front().vision.string()),
            QFileInfo(root.filePath("Models/vision.safetensors")).canonicalFilePath());
        QCOMPARE(sampling.controls.front().process, std::string("Canny"));
        QCOMPARE(sampling.controls.front().weight, 0.7f);
        QCOMPARE(sampling.controls.front().image.width, 3);
        QCOMPARE(sampling.controls.front().image.height, 2);
        QCOMPARE(sampling.controls.front().image.rgb.front(), uint8_t(21));
        QCOMPARE(sampling.controls.front().mask.width, 3);
        QCOMPARE(sampling.controls.front().mask.height, 2);
        QCOMPARE(sampling.controls.front().mask.rgb.front(), uint8_t(22));
        QCOMPARE(QString::fromStdString(sampling.controls.front().model.string()),
            QFileInfo(root.filePath("Models/control.safetensors")).canonicalFilePath());
        QCOMPARE(sampling.controls.back().process, std::string("Tile"));
        QCOMPARE(sampling.controls.back().weight, 1.25f);
        QCOMPARE(sampling.controls.back().image.rgb.front(), uint8_t(11));
        QCOMPARE(sampling.controls.back().mask.rgb.front(), uint8_t(12));
        QCOMPARE(QString::fromStdString(sampling.controls.back().model.string()),
            QFileInfo(root.filePath("Models/second-control.safetensors")).canonicalFilePath());
        const auto snapshot = recordedJob(controller, id).value("advancedParameters").toObject();
        const auto ipSnapshot = snapshot.value("controlNets").toArray()[3].toObject();
        QCOMPARE(ipSnapshot.value("ipAdapterModel").toString(), QFileInfo(root.filePath("Models/ip.safetensors")).canonicalFilePath());
        QCOMPARE(ipSnapshot.value("ipAdapterVision").toString(), QFileInfo(root.filePath("Models/vision.safetensors")).canonicalFilePath());
        for (const auto &field : {"ipAdapterModel", "ipAdapterVision"}) {
            const auto original = draft.parameters().value("controlNets").toList()[3].toMap().value(field);
            for (const auto &bad : {"https://example.invalid/ip.safetensors", "missing-ip.safetensors"}) {
                QVERIFY(draft.updateControlNet(ipOnly, {{field, bad}})); QVERIFY(draft.applyControlNet(ipOnly));
                QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty()); QCOMPARE(controller.jobs().size(), 1);
            }
            QVERIFY(draft.updateControlNet(ipOnly, {{field, original}})); QVERIFY(draft.applyControlNet(ipOnly));
        }
        const auto secondControlSnapshot = snapshot.value("controlNets").toArray()[2].toObject();
        QCOMPARE(secondControlSnapshot.value("model").toString(),
            QFileInfo(root.filePath("Models/second-control.safetensors")).canonicalFilePath());
        QCOMPARE(secondControlSnapshot.value("imageSource").toString(),
            QFileInfo(root.filePath("Files/reference.png")).canonicalFilePath());
        QCOMPARE(secondControlSnapshot.value("maskSource").toString(),
            QFileInfo(root.filePath("Files/reference.png")).canonicalFilePath());
        for (const auto &field : {"model", "imageSource", "maskSource"}) {
            const auto original = draft.parameters().value("controlNets").toList()[2].toMap().value(field);
            QVERIFY(draft.updateControlNet(secondControlId, {{field, "missing-control-resource"}}));
            QVERIFY(draft.applyControlNet(secondControlId));
            QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
            QCOMPARE(controller.jobs().size(), 1);
            QVERIFY(draft.updateControlNet(secondControlId, {{field, original}}));
            QVERIFY(draft.applyControlNet(secondControlId));
        }
        QCOMPARE(snapshot.value("upscalerModel").toString(), QFileInfo(root.filePath("Models/upscaler.safetensors")).canonicalFilePath());
        QCOMPARE(snapshot.value("detailerModel").toString(), QFileInfo(root.filePath("Models/detector.safetensors")).canonicalFilePath());
        QCOMPARE(snapshot.value("refinerModel").toString(), QFileInfo(root.filePath("Models/refiner.safetensors")).canonicalFilePath());
        QCOMPARE(snapshot.value("refinerSwitch").toDouble(), 0.65);
        for (const auto &source : {"https://example.invalid/refiner.safetensors", "missing-refiner.safetensors", ""}) {
            QVERIFY(draft.updateParameters({{"refinerModel", source}}));
            QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
            QCOMPARE(controller.jobs().size(), 1);
        }
        QVERIFY(draft.updateParameters({{"refinerModel", "refiner.safetensors"}}));
        for (const auto &source : {"https://example.invalid/detector.safetensors", "missing-detector.safetensors", ""}) {
            QVERIFY(draft.updateParameters({{"detailerModel", source}}));
            QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
            QCOMPARE(controller.jobs().size(), 1);
        }
        QVERIFY(draft.updateParameters({{"detailerModel", "detector.safetensors"}}));
        QVERIFY(draft.updateParameters({{"upscalerModel", "https://example.invalid/upscaler.safetensors"}}));
        QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
        QCOMPARE(controller.jobs().size(), 1);
        QVERIFY(draft.updateParameters({{"upscalerModel", "missing.safetensors"}}));
        QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
        QCOMPARE(controller.jobs().size(), 1);
        QVERIFY(draft.updateParameters({{"upscalerModel", "upscaler.safetensors"}}));
        QCOMPARE(snapshot.value("referenceImages").toArray()[0].toString(),
            QFileInfo(root.filePath("Files/reference.png")).canonicalFilePath());
        QCOMPARE(snapshot.value("textualEmbeddings").toString(), QFileInfo(root.filePath("Models/detail.safetensors")).canonicalFilePath());
        const auto controlSnapshot = snapshot.value("controlNets").toArray()[0].toObject();
        QCOMPARE(controlSnapshot.value("maskSource").toString(), QFileInfo(root.filePath("Files/second.png")).canonicalFilePath());
        for (const auto &maskSource : {"https://example.invalid/mask.png", "missing-mask.png"}) {
            QVERIFY(draft.updateControlNet(controlId, {{"maskSource", maskSource}}));
            QVERIFY(draft.applyControlNet(controlId));
            QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
            QCOMPARE(controller.jobs().size(), 1);
        }
        QVERIFY(draft.updateControlNet(controlId, {{"maskSource", "second.png"}}));
        QVERIFY(draft.applyControlNet(controlId));
        QCOMPARE(controlSnapshot.value("imageSource").toString(), QFileInfo(root.filePath("Files/second.png")).canonicalFilePath());
        QCOMPARE(controlSnapshot.value("model").toString(), QFileInfo(root.filePath("Models/control.safetensors")).canonicalFilePath());
        QVERIFY(draft.updateControlNet(controlId, {{"model", "https://example.invalid/control.safetensors"}}));
        QVERIFY(draft.applyControlNet(controlId));
        QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
        QCOMPARE(controller.jobs().size(), 1);
        QVERIFY(draft.updateControlNet(controlId, {{"model", "control.safetensors"}, {"imageSource", "missing.png"}}));
        QVERIFY(draft.applyControlNet(controlId));
        QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
        QCOMPARE(controller.jobs().size(), 1);
        QVERIFY(draft.updateControlNet(controlId, {{"imageSource", "second.png"}}));
        QVERIFY(draft.applyControlNet(controlId));
        QVERIFY(draft.updateParameters({{"textualEmbeddings", "https://example.invalid/embedding.safetensors"}}));
        QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
        QCOMPARE(controller.jobs().size(), 1);
        QVERIFY(draft.updateParameters({{"textualEmbeddings", "detail.safetensors"}}));
        QVERIFY(draft.addReferenceImage("https://example.invalid/image.png"));
        QVERIFY(controller.enqueueAdvanced(draft.parameters()).isEmpty());
        QCOMPARE(controller.jobs().size(), 1);
    }

    void backgroundModelInventoryDoesNotReopenTheFileMirror()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/inventory-poll-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        auto *files = qobject_cast<iiSocietyHelper::FileSystem *>(controller.container()->parent());
        QVERIFY(files);
        controller.setSelectedModel("first model.safetensor");
        controller.setForeground(true);
        QTRY_VERIFY_WITH_TIMEOUT(!files->containerState().isEmpty(), 8000);
        QTest::qWait(100);
        QSignalSpy reopened(files, &iiSocietyHelper::FileSystem::storageChanged);
        QTest::qWait(2500);
        QCOMPARE(reopened.size(), 0);
        QVERIFY(write(root.filePath("Models/poll-added.safetensors"), "new weight"));
        QTRY_COMPARE_WITH_TIMEOUT(controller.models().size(), 3, 8000);
        QCOMPARE(controller.selectedModel(), QString("first model.safetensor"));
        QCOMPARE(reopened.size(), 0);
        QVERIFY(controller.enqueue("  ").isEmpty());
        const auto generationError = controller.errorString();
        QVERIFY(!generationError.isEmpty());
        QTest::qWait(2500);
        QCOMPARE(controller.errorString(), generationError);
    }

    void modelInventoryFollowsSocietyOwnerAtStartupAndRefresh()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/model-inventory-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        const auto drive = SocietyDrive::open(root.path()); QVERIFY(drive);
        StorageMap map(*drive);
        QVERIFY(map.publish({QJsonObject{{"path", "models/Checkpoint/deleted.safetensors"},
            {"kind", "file"}, {"size", "7"}, {"version", QString(64, 'a')}}}));
        QVERIFY(write(root.filePath(".society-sync/primary.json"), QJsonDocument(QJsonObject{
            {"schema", 1}, {"container", drive->identifier()}, {"scope", QString(64, 'a')}, {"host", "test-host"}}).toJson()));
        QVERIFY(QDir().mkpath(root.filePath("Models/VAE")));
        QVERIFY(QDir().mkpath(root.filePath("Models/Checkpoint")));
        QVERIFY(write(root.filePath("Models/VAE/selected.safetensors"), "vae"));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        QCOMPARE(controller.models().size(), 2);
        controller.setSelectedModel("first model.safetensor");
        controller.setSelectedVae("VAE/selected.safetensors");
        QVERIFY(QFile::rename(root.filePath("Models/first model.safetensor"), root.filePath("Deleted/first model.safetensor")));
        QVERIFY(QFile::remove(root.filePath("Models/VAE/selected.safetensors")));
        QVERIFY(write(root.filePath("Models/Checkpoint/new.safetensors"), "new model"));
        controller.refreshModels();
        QCOMPARE(controller.models().size(), 2); QVERIFY(controller.vaes().isEmpty());
        QVERIFY(controller.selectedVae().isEmpty());
        QCOMPARE(controller.selectedModel(), QString("Checkpoint/new.safetensors"));
        QSignalSpy changes(&controller, &GenerationController::modelsChanged);
        controller.refreshModels(); QCOMPARE(changes.size(), 0);
        QVERIFY(QFile::remove(root.filePath("Models/second.SAFETENSORS")));
        QVERIFY(QFile::remove(root.filePath("Models/Checkpoint/new.safetensors")));
        controller.refreshModels(); QVERIFY(controller.models().isEmpty()); QVERIFY(controller.selectedModel().isEmpty());
    }

    void explicitVaeIsPinnedPerJobAndForwardedToWorker()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/explicit-vae-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        QVERIFY(QDir().mkpath(root.filePath("Models/VAE")));
        QVERIFY(write(root.filePath("Models/VAE/sdxl.safetensors"), "test vae"));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        QCOMPARE(controller.vaes().size(), 1);
        QCOMPARE(controller.models().size(), 2);
        controller.setSelectedModel("first model.safetensor");
        controller.setSelectedVae("VAE/sdxl.safetensors");
        const auto first = controller.enqueue("slow external vae", "1:1", 1, 123456);
        controller.setSelectedVae({});
        const auto second = controller.enqueue("embedded vae");
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, first), QString("completed"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, second), QString("completed"), 10000);
        const auto firstJob = recordedJob(controller, first);
        QCOMPARE(firstJob.value("seed").toInteger(), qint64(123456));
        QCOMPARE(firstJob.value("generation").toObject().value("seed").toInteger(), firstJob.value("seed").toInteger());
        QCOMPARE(firstJob.value("vae").toObject().value("path").toString(), QString("VAE/sdxl.safetensors"));
        QCOMPARE(firstJob.value("generation").toObject().value("vae").toString(), root.filePath("Models/VAE/sdxl.safetensors"));
        QVERIFY(recordedJob(controller, second).value("generation").toObject().value("vae").isNull());
        controller.setSelectedVae("../outside.safetensors");
        QVERIFY(controller.selectedVae().isEmpty());
        QVERIFY(!controller.errorString().isEmpty());
    }

    void pythonLauncherAvailabilityChecksInterpreter()
    {
#ifdef Q_OS_WIN
        GenerationController available(fakeRuntime());
        QVERIFY(available.runtimeAvailable());
        auto missing = fakeRuntime();
        missing.pythonExecutable = QStringLiteral("C:/missing-dreamscapes-python/python.exe");
        GenerationController unavailable(missing);
        QVERIFY(!unavailable.runtimeAvailable());
#endif
    }
    void driveLocationSelectionPersistsAndRejectsInvalidFolders()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/drive-location-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        const auto settings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto containerOverride = qgetenv("SOCIETY_CONTAINER_PATH");
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("settings.json").toUtf8());
        qunsetenv("SOCIETY_CONTAINER_PATH");
        const auto restore = qScopeGuard([&] {
            settings.isNull() ? qunsetenv("SOCIETY_STORAGE_SETTINGS_PATH") : qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settings);
            containerOverride.isNull() ? qunsetenv("SOCIETY_CONTAINER_PATH") : qputenv("SOCIETY_CONTAINER_PATH", containerOverride);
        });
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.selectStorageLocation(QUrl::fromLocalFile(root.path()).toString()));
        QCOMPARE(controller.containerPath(), root.path());
        auto saved = SharedStorage::open();
        QVERIFY(saved);
        QCOMPARE(saved->drive().rootPath(), root.path());
        for (const auto &path : {QString(), QString("relative"), root.filePath("missing"), root.filePath("Files")}) {
            QVERIFY(!controller.selectStorageLocation(path));
            QCOMPARE(controller.containerPath(), root.path());
            QCOMPARE(SharedStorage::open()->drive().rootPath(), root.path());
        }
        GenerationController reopened(fakeRuntime());
        QVERIFY(reopened.connectStorage());
        QCOMPARE(reopened.containerPath(), root.path());
    }

    void unifiedModelReachesTheNativeRuntimeAsOnePackage()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/unified-generation-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        const auto package = root.filePath("Models/cascade.iildmodel");
        QVERIFY(QDir().mkpath(package + "/members"));
        QVERIFY(write(package + "/model_index.json",
            "{\"schema\":\"iild-unified-model-v1\",\"_class_name\":\"IILDUnifiedCascade\"}"));
        QVERIFY(write(package + "/members/a.safetensors", "test member"));
        auto runtime = fakeRuntime();
        runtime.nativeInference = true;
        QString received;
        runtime.nativeGenerate = [&received](const auto &request, const auto &, const auto &, const auto &, const auto &) {
            received = QString::fromStdString(request.modelPath.string());
            return iiLocalDiffusion::NativeGenerationResult{
                std::vector<std::uint8_t>(request.width * request.height * 3, 127), request.width, request.height};
        };
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        controller.setSelectedModel("cascade.iildmodel");
        controller.setForeground(true);
        const auto id = controller.enqueue("unified fixture");
        QVERIFY2(!id.isEmpty(), qPrintable(controller.errorString()));
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("completed"), 10000);
        QCOMPARE(received, package);
    }
    void modelSelectionKeepsThePreparingRuntimeAlive()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/obsolete-preparation-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        QVERIFY(write(root.filePath("Models/first model.safetensor"), "prepare-retain"));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_VERIFY(controller.inferenceStatus().contains("requestId"));
        const auto pidPath = root.filePath("Models/.preparation-pid");
        QTRY_VERIFY(QFileInfo::exists(pidPath));
        QFile pidFile(pidPath); QVERIFY(pidFile.open(QIODevice::ReadOnly));
        const auto preparingPid = pidFile.readAll().trimmed().toLongLong();
        QVERIFY(preparingPid > 0);
        controller.setSelectedModel(controller.models().last().toMap().value("id").toString());
        const auto id = controller.enqueue("the newly selected model must run");
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("completed"), 5000);
        QVERIFY(recordedJob(controller, id).value("generation").toObject()
                    .value("model_path").toString().endsWith("second.SAFETENSORS"));
        QCOMPARE(recordedJob(controller, id).value("worker").toObject().value("pid").toInteger(), preparingPid);
    }
    void videoPreparationProgressRenewsDeadline_data()
    {
        QTest::addColumn<QString>("prompt");
        QTest::addColumn<QString>("phase");
        QTest::addColumn<bool>("progressing");
        QTest::newRow("asset-copy") << QString("video-preparing-progress") << QString("preparing-model") << true;
        QTest::newRow("cpu-graph-batches") << QString("video-computing-progress") << QString("computing") << true;
        QTest::newRow("repeated-notification-is-not-progress") << QString("video-preparing-stall") << QString("preparing-model") << false;
    }
    void videoPreparationProgressRenewsDeadline()
    {
        QFETCH(QString, prompt); QFETCH(QString, phase); QFETCH(bool, progressing);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/video-watchdog-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        QVERIFY(QDir().mkpath(root.filePath("Models/Checkpoint/ltx")));
        QVERIFY(write(root.filePath("Models/Checkpoint/ltx/model_index.json"), R"({"_class_name":"LTXConditionPipeline"})"));
        auto runtime = fakeRuntime(); runtime.nativeTimeoutMilliseconds = 30000;
        const auto barrier = root.filePath("video-progress-barrier");
        const auto oldBarrier = qgetenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER");
        qputenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER", barrier.toUtf8());
        const auto restoreBarrier = qScopeGuard([&] {
            if (oldBarrier.isNull()) qunsetenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER");
            else qputenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER", oldBarrier);
        });
        GenerationController controller(runtime); QVERIFY(controller.connectStorage(root.path()));
        const auto id = controller.enqueueVideo(prompt, "1:1", 1, {}, 1, 12, 42);
        QVERIFY2(!id.isEmpty(), qPrintable(controller.errorString()));
        QTRY_COMPARE_WITH_TIMEOUT(controller.inferenceStatus().value("state").toString(), phase, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(barrier + ".started"), 10000);
        auto *watchdog = controller.findChild<QTimer *>("generationWorkerProgressWatchdog");
        QVERIFY(watchdog);
        QVERIFY(write(barrier + ".release", "ready"));
        QElapsedTimer wait; wait.start();
        while (!QFileInfo::exists(barrier + ".buffered") && wait.elapsed() < 5000) QThread::msleep(10);
        QVERIFY(QFileInfo::exists(barrier + ".buffered"));
        watchdog->stop();
        QVERIFY(QMetaObject::invokeMethod(watchdog, "timeout", Qt::DirectConnection));
        if (progressing) {
            QTRY_COMPARE_WITH_TIMEOUT(controller.inferenceStatus().value("step").toInt(), 2, 10000);
            QVERIFY(controller.busy());
            const auto asset = root.filePath("Models/.society-runtime/iiLocalDiffusion/cache/video/verified-encoder-fixture");
            QVERIFY(QFileInfo::exists(asset));
            QVERIFY(controller.cancel(id));
            QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("cancelled"), 10000);
            QVERIFY(QFileInfo::exists(asset));
            if (phase == "preparing-model") {
                const auto modified = QFileInfo(asset).lastModified();
                for (const auto *suffix : {".started", ".release", ".buffered"}) QFile::remove(barrier + suffix);
                const auto next = controller.enqueueVideo(prompt, "1:1", 1, {}, 1, 12, 42);
                QVERIFY(!next.isEmpty());
                QTRY_COMPARE_WITH_TIMEOUT(controller.inferenceStatus().value("state").toString(), phase, 10000);
                QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(barrier + ".started"), 10000);
                QCOMPARE(QFileInfo(asset).lastModified(), modified);
                QVERIFY(controller.cancel(next));
                QTRY_COMPARE_WITH_TIMEOUT(state(controller, next), QString("cancelled"), 10000);
            }
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("failed"), 10000);
            const auto error = recordedJob(controller, id).value("error").toString();
            QVERIFY2(error.contains("preparing-model (1/12)"), qPrintable(error));
        }
    }
    void workerStallDeadlineIgnoresHeartbeats_data()
    {
        QTest::addColumn<bool>("progressing");
        QTest::addColumn<QString>("prompt");
        QTest::newRow("heartbeat-only-stalls") << false << QString("telemetry-stall");
        QTest::newRow("real-steps-renew-deadline") << true << QString("telemetry-progress");
        QTest::newRow("real-cpu-batches-renew-deadline") << true << QString("telemetry-computing");
    }
    void workerDeadlineConsumesPendingOutputBeforeFailure_data()
    {
        QTest::addColumn<bool>("progressing");
        QTest::newRow("buffered-progress") << true;
        QTest::newRow("buffered-heartbeats") << false;
    }
    void workerDeadlineConsumesPendingOutputBeforeFailure()
    {
        QFETCH(bool, progressing);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/buffered-watchdog-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime(); runtime.steps = 30;
        runtime.nativeTimeoutMilliseconds = 30000; // Expiry is injected after the barrier, not timed.
        const auto barrier = root.filePath("watchdog-barrier");
        const auto oldBarrier = qgetenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER");
        qputenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER", barrier.toUtf8());
        const auto restoreBarrier = qScopeGuard([&] {
            if (oldBarrier.isNull()) qunsetenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER");
            else qputenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER", oldBarrier);
        });
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        const auto id = controller.enqueue(progressing ? "buffered-progress" : "buffered-heartbeats");
        QVERIFY(!id.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(barrier + ".started"), 10000);
        if (progressing) QTRY_COMPARE(controller.previewStep(), 1);
        auto *watchdog = controller.findChild<QTimer *>("generationWorkerProgressWatchdog");
        QVERIFY(watchdog);
        QVERIFY(write(barrier + ".release", "ready"));
        QElapsedTimer wait; wait.start();
        // Deliberately do not dispatch GUI events: the worker continues writing
        // progress while its QProcess socket notification remains pending.
        while (!QFileInfo::exists(barrier + ".buffered") && wait.elapsed() < 5000) QThread::msleep(10);
        QVERIFY(QFileInfo::exists(barrier + ".buffered"));
        watchdog->stop();
        QVERIFY(QMetaObject::invokeMethod(watchdog, "timeout", Qt::DirectConnection));
        if (progressing) {
            QTRY_COMPARE_WITH_TIMEOUT(controller.previewStep(), 2, 10000);
            QVERIFY(controller.busy());
            QTRY_VERIFY_WITH_TIMEOUT(watchdog->isActive(), 10000);
            QVERIFY(controller.cancel(id));
        }
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), progressing ? QString("cancelled") : QString("failed"), 10000);
        if (!progressing) QVERIFY(recordedJob(controller, id).value("error").toString().contains("stopped making progress"));
    }
    void workerStallDeadlineIgnoresHeartbeats()
    {
        QFETCH(bool, progressing);
        QFETCH(QString, prompt);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/worker-watchdog-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.steps = 30;
        runtime.nativeTimeoutMilliseconds = 30000; // Expiry is injected after the barrier.
        const auto barrier = root.filePath("watchdog-barrier");
        const auto oldBarrier = qgetenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER");
        qputenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER", barrier.toUtf8());
        const auto restoreBarrier = qScopeGuard([&] {
            if (oldBarrier.isNull()) qunsetenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER");
            else qputenv("DREAMSCAPES_TEST_WATCHDOG_BARRIER", oldBarrier);
        });
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        const auto id = controller.enqueue(prompt);
        QVERIFY(!id.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(barrier + ".started"), 10000);
        auto *watchdog = controller.findChild<QTimer *>("generationWorkerProgressWatchdog");
        QVERIFY(watchdog);
        QVERIFY(write(barrier + ".release", "ready"));
        QElapsedTimer wait; wait.start();
        while (!QFileInfo::exists(barrier + ".buffered") && wait.elapsed() < 5000) QThread::msleep(10);
        QVERIFY(QFileInfo::exists(barrier + ".buffered"));
        watchdog->stop();
        QVERIFY(QMetaObject::invokeMethod(watchdog, "timeout", Qt::DirectConnection));
        if (progressing) {
            QVERIFY(controller.busy());
            QTRY_VERIFY_WITH_TIMEOUT(watchdog->isActive(), 10000);
            QVERIFY(controller.cancel(id));
        }
        QTRY_VERIFY2_WITH_TIMEOUT(state(controller, id) == (progressing ? QString("cancelled") : QString("failed")),
            qPrintable(recordedJob(controller, id).value("error").toString()), 10000);
        const auto job = recordedJob(controller, id);
        const auto trace = job.value("telemetry").toObject();
        QCOMPARE(trace.value("backend").toString(), QString("Metal"));
        QVERIFY(QFileInfo::exists(trace.value("trace_path").toString()));
        if (!progressing) {
            QVERIFY(job.value("error").toString().contains("denoise"));
            QVERIFY(job.value("error").toString().contains("stopped making progress"));
        }
    }

    void nativeWorkerReportsStagesWithoutInventingPreviewImages()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/native-worker-progress-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.steps = 10;
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        QList<int> steps;
        QStringList phases;
        connect(&controller, &GenerationController::previewChanged, this, [&] {
            QVERIFY(controller.previewImage().isEmpty());
            if (controller.previewStep()) {
                QCOMPARE(controller.previewTotalSteps(), 10);
                steps.append(controller.previewStep());
            }
        });
        connect(&controller, &GenerationController::inferenceStatusChanged, this, [&] {
            const auto status = controller.inferenceStatus();
            if (status.value("backend").toString() == "native") phases.append(status.value("state").toString());
        });
        const auto id = controller.enqueue("native-progress", "9:16");
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("completed"), 10000);
        QCOMPARE(steps, QList<int>({1, 10}));
        QCOMPARE(phases, QStringList({"loading", "encoding", "denoising", "denoising", "decoding"}));
        QCOMPARE(QImage(controller.latestImage().toLocalFile()).size(), QSize(64, 112));
    }

    void defaultResolutionReachesBothGenerationBackends_data()
    {
        QTest::addColumn<QString>("ratio");
        QTest::addColumn<QSize>("expected");
        QTest::addColumn<bool>("native");
        const QList<QPair<QString, QSize>> sizes{{"1:1", {1024, 1024}}, {"4:3", {1368, 1024}},
            {"3:4", {1024, 1368}}, {"16:9", {1824, 1024}}, {"9:16", {1024, 1824}}};
        for (bool native : {false, true})
            for (const auto &[ratio, size] : sizes)
                QTest::newRow(qPrintable((native ? "native-" : "worker-") + ratio)) << ratio << size << native;
    }

    void defaultResolutionReachesBothGenerationBackends()
    {
        QFETCH(QString, ratio);
        QFETCH(QSize, expected);
        QFETCH(bool, native);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/default-resolution-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationRuntime runtime;
        runtime.executable = QStringLiteral(DREAMSCAPES_FAKE_GENERATOR);

        runtime.nativeInference = native;
        iiLocalDiffusion::NativeGenerationRequest nativeRequest;
        runtime.nativeGenerate = [&nativeRequest](const auto &request, const auto &, const auto &, const auto &, const auto &) {
            nativeRequest = request;
            return iiLocalDiffusion::NativeGenerationResult{
                std::vector<std::uint8_t>(request.width * request.height * 3, 127), request.width, request.height};
        };
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        const auto id = controller.enqueue("default resolution", ratio);
        QVERIFY(!id.isEmpty());
        const auto submitted = recordedJob(controller, id);
        QCOMPARE(QSize(submitted.value("width").toInt(), submitted.value("height").toInt()), expected);
        QCOMPARE(qMin(submitted.value("width").toInt(), submitted.value("height").toInt()), 1024);
        QCOMPARE(submitted.value("steps").toInt(), 10);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("completed"), 10000);
        QCOMPARE(QImage(controller.latestImage().toLocalFile()).size(), expected);
        if (native) {
            QCOMPARE(QSize(nativeRequest.width, nativeRequest.height), expected);
            QCOMPARE(nativeRequest.steps, 10);
        } else {
            const auto received = recordedJob(controller, id).value("generation").toObject();
            QCOMPARE(QSize(received.value("width").toInt(), received.value("height").toInt()), expected);
            QCOMPARE(received.value("steps").toInt(), 10);
        }
    }

    void completedResultsExposeEveryImageAndExcludeUnpublishedFiles()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/gallery-results-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(controller.property("completedResults").isValid());
        QVERIFY(controller.property("completedResults").toList().isEmpty());
        const auto first = controller.enqueue("multiple", "4:3");
        const auto second = controller.enqueue("another image");
        const auto cancelled = controller.enqueue("cancel before execution");
        QVERIFY(controller.cancel(cancelled));
        const auto failed = controller.enqueue("fail");
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, failed), QString("failed"), 10000);
        QCOMPARE(state(controller, first), "completed");
        QCOMPARE(state(controller, second), "completed");
        const auto results = controller.property("completedResults").toList();
        QCOMPARE(results.size(), 3);
        QSignalSpy resultChanges(&controller, &GenerationController::resultsChanged);
        controller.container()->setQuery("does not change the published results");
        QCOMPARE(resultChanges.count(), 0);
        QVERIFY(QMetaObject::invokeMethod(&controller, "jobsChanged"));
        QCOMPARE(resultChanges.count(), 0); // Unchanged progress does not invalidate a result gallery.
        QSet<QUrl> sources;
        for (int index = 0; index < results.size(); ++index) {
            const auto result = results[index].toMap();
            QCOMPARE(result.value("id").toString(), index < 2 ? first : second);
            QCOMPARE(result.value("prompt").toString(), index < 2 ? "multiple" : "another image");
            const auto source = result.value("imageSource").toUrl();
            QVERIFY(!QImage(source.toLocalFile()).isNull());
            QCOMPARE(source.toLocalFile(), root.filePath(result.value("image").toString()));
            QVERIFY(source.toLocalFile().endsWith(".png")); QVERIFY(!result.contains("documentSource"));
            sources.insert(source);
        }
        QCOMPARE(sources.size(), 3);
        QCOMPARE(results.last().toMap(), controller.latestResult());
        // A deleted or redirected published file cannot become a gallery/project input.
        const auto firstPath = results.first().toMap().value("imageSource").toUrl().toLocalFile();
        QVERIFY(QFile::remove(firstPath));
        QCOMPARE(controller.property("completedResults").toList().size(), 3); // Last published storage snapshot.
        auto *files = qobject_cast<iiSocietyHelper::FileSystem *>(controller.container()->parent()); QVERIFY(files);
        QVERIFY(files->refresh());
        QCOMPARE(controller.property("completedResults").toList().size(), 2);
        QCOMPARE(resultChanges.count(), 1);
        QVERIFY(!QFileInfo::exists(firstPath));
        QVERIFY(createNativeTestLink(results.last().toMap().value("imageSource").toUrl().toLocalFile(), firstPath));
        QVERIFY(files->refresh());
        QCOMPARE(controller.property("completedResults").toList().size(), 2);
        QTemporaryDir other(DREAMSCAPES_TEST_DIRECTORY "/gallery-other-storage-XXXXXX.societycontainer");
        QVERIFY(prepare(other));
        QVERIFY(controller.connectStorage(other.path()));
        QVERIFY(controller.property("completedResults").toList().isEmpty());
    }

    void batchSubmissionKeepsOneModelSnapshotAndRejectsInvalidCounts()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/batch-queue-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.executable.clear();
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        const auto model = controller.selectedModel();
        QSignalSpy changes(&controller, &GenerationController::jobsChanged);
        QSignalSpy submissions(&controller, &GenerationController::submissionQueued);
        connect(&controller, &GenerationController::submissionQueued, this, [&](const QStringList &jobIds) {
            // Subscribers receive the complete, queued batch before the worker starts.
            for (const auto &id : jobIds) QCOMPARE(state(controller, id), QString("queued"));
        });
        const auto first = controller.enqueue("  a forest  ", "16:9", 1000);
        QVERIFY(!first.isEmpty());
        QCOMPARE(controller.jobs().size(), 1000);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(submissions.size(), 1);
        const auto submittedIds = submissions.first().first().toStringList();
        QCOMPARE(submittedIds.size(), 1000);
        QCOMPARE(submittedIds.first(), first);
        controller.setSelectedModel(controller.models().last().toMap().value("id").toString());
        QSet<QString> ids;
        QSet<QString> creationTimes;
        for (const auto &entry : controller.jobs()) {
            const auto job = entry.toMap();
            ids.insert(job.value("id").toString());
            creationTimes.insert(job.value("createdAt").toString());
            QCOMPARE(job.value("state").toString(), QString("queued"));
            QCOMPARE(job.value("prompt").toString(), QString("a forest"));
            QCOMPARE(job.value("aspectRatio").toString(), QString("16:9"));
            QCOMPARE(job.value("model").toMap().value("path").toString(), model);
        }
        QCOMPARE(ids.size(), 1000);
        QCOMPARE(creationTimes.size(), 1000);
        QVERIFY(ids.contains(first));
        QCOMPARE(QSet<QString>(submittedIds.begin(), submittedIds.end()), ids);
        for (int invalid : {-1, 0, 1001})
            QVERIFY(controller.enqueue("invalid count", "1:1", invalid).isEmpty());
        QVERIFY(controller.enqueue(" ", "1:1", 3).isEmpty());
        QVERIFY(controller.enqueue("invalid ratio", "bad", 3).isEmpty());
        QCOMPARE(controller.jobs().size(), 1000);
        QCOMPARE(submissions.size(), 1);
        const auto next = controller.enqueue("next submission", "1:1", 3);
        QVERIFY(!next.isEmpty());
        QCOMPARE(submissions.size(), 2);
        const auto nextIds = submissions.last().first().toStringList();
        QCOMPARE(nextIds.size(), 3);
        QCOMPARE(nextIds.first(), next);
        for (const auto &id : nextIds) QVERIFY(!ids.contains(id));
        QVERIFY(controller.cancel(first));
        QCOMPARE(state(controller, first), QString("cancelled"));
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
    }

    void batchGenerationPublishesTheSelectedNumberOfImagesSerially()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/batch-generation-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        connect(&controller, &GenerationController::jobsChanged, this, [&] {
            int running = 0;
            for (const auto &entry : controller.jobs())
                running += entry.toMap().value("state") == "running";
            QVERIFY(running <= 1);
        });
        QVERIFY(!controller.enqueue("three images", "4:3", 3).isEmpty());
        QCOMPARE(controller.jobs().size(), 3);
        const auto allCompleted = [&] {
            for (const auto &entry : controller.jobs())
                if (entry.toMap().value("state") != "completed") return false;
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(allCompleted(), 10000);
        QCOMPARE(QDir(root.filePath("Generation History")).entryList(QDir::Files).size(), 3);
        QSet<qint64> workerIds;
        for (const auto &entry : controller.jobs()) {
            const auto job = QJsonObject::fromVariantMap(entry.toMap());
            workerIds.insert(job.value("worker").toObject().value("pid").toInteger());
        }
        QCOMPARE(workerIds.size(), 1);
        QVERIFY(!workerIds.contains(0));
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
    }

    void foregroundPreparationFailureCanRecoverWithAnotherModel()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/foreground-failure-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        QVERIFY(write(root.filePath("Models/first model.safetensor"), "prepare-fail"));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_COMPARE(controller.inferenceStatus().value("state").toString(), QString("error"));
        QVERIFY(!controller.inferenceStatus().value("ready").toBool());
        QVERIFY(controller.jobs().isEmpty());
        controller.setSelectedModel(controller.models().last().toMap().value("id").toString());
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
    }

    void foregroundControlRequiresTheSdkCapability()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/foreground-legacy-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        const auto executable = root.filePath("legacy.py");
        QVERIFY(write(executable,
            "#!/usr/bin/env python3\nimport sys\n"
            "print('IILD_READY {\"schema\":\"iild-worker-v1\"}', flush=True)\n"
            "for line in sys.stdin:\n print('Unexpected request to a legacy worker', flush=True)\n"));
        QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        auto runtime = fakeRuntime();
        runtime.executable = executable;
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_COMPARE(controller.inferenceStatus().value("state").toString(), QString("error"));
        QVERIFY(controller.inferenceStatus().value("error").toString().contains("iiLocalDiffusion"));
        QVERIFY(controller.jobs().isEmpty());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
    }

    void foregroundAndGenerationUseMetadataOnlyModelValidation()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/metadata-model-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        QVERIFY(write(root.filePath("Models/first model.safetensor"), "prepare-metadata"));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        const auto id = controller.enqueue("metadata-only generation");
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("completed"), 10000);
    }

    void completedModelCheckDoesNotRemainCheckingDuringPreparation()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/model-check-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        QVERIFY(write(root.filePath("Models/first model.safetensor"), "prepare-check"));
        GenerationController controller(fakeRuntime());
        QStringList phases;
        connect(&controller, &GenerationController::inferenceStatusChanged, this, [&] {
            const auto status = controller.inferenceStatus();
            if (status.contains("completedBytes")) phases.append(status.value("state").toString());
        });
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        QCOMPARE(phases, QStringList({"checking-model", "preparing"}));
    }

    void foregroundPreparesWithoutAQueueAndReusesTheWorker()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/foreground-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        QVERIFY(controller.jobs().isEmpty());
        QVERIFY(!controller.busy());
        QVERIFY(controller.latestImage().isEmpty());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
        const auto before = controller.inferenceStatus();
        const auto id = controller.enqueue("use the prepared model");
        QTRY_COMPARE(state(controller, id), QString("completed"));
        const auto worker = recordedJob(controller, id).value("worker").toObject();
        QCOMPARE(worker.value("pid").toVariant(), before.value("pid"));
        QCOMPARE(worker.value("cache").toObject().value("pipeline_loads").toInt(-1), 0);
        QCOMPARE(worker.value("cache").toObject().value("device_placements").toInt(-1), 0);
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        controller.setForeground(false);
        QTRY_COMPARE(controller.inferenceStatus().value("foreground").toBool(), false);
        controller.setForeground(true);
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        QCOMPARE(controller.inferenceStatus().value("pid"), before.value("pid"));
        const auto resumed = controller.enqueue("reuse anonymous sources after idle transition");
        QTRY_COMPARE(state(controller, resumed), QString("completed"));
        const auto resumedWorker = recordedJob(controller, resumed).value("worker").toObject();
        QCOMPARE(resumedWorker.value("pid").toVariant(), before.value("pid"));
        QCOMPARE(resumedWorker.value("cache").toObject().value("pipeline_loads").toInt(-1), 0);
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        QVERIFY(QFile::remove(root.filePath("Models/first model.safetensor")));
        QVERIFY(QFile::remove(root.filePath("Models/second.SAFETENSORS")));
        controller.refreshModels();
        QTRY_COMPARE(controller.inferenceStatus().value("state").toString(), QString("waiting-model"));
        QVERIFY(!controller.inferenceStatus().value("ready").toBool());
        QCOMPARE(controller.jobs().size(), 2);
    }

    void foregroundModelChangesAndQueuedRequestsRemainSeparate()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/foreground-model-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_VERIFY(controller.inferenceStatus().contains("requestId"));
        const auto first = controller.enqueue("queued during preparation");
        controller.setSelectedModel(controller.models().last().toMap().value("id").toString());
        const auto second = controller.enqueue("selected second model");
        QTRY_COMPARE(state(controller, first), QString("completed"));
        QTRY_COMPARE(state(controller, second), QString("completed"));
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        QVERIFY(controller.inferenceStatus().value("model").toString().endsWith("second.SAFETENSORS"));
        QCOMPARE(controller.jobs().size(), 2);
        QCOMPARE(QDir(root.filePath("Generation History")).entryList(QDir::Files).size(), 2);
    }

    void queuesBelongOnlyToTheCurrentAppSession()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/memory-queue-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.executable.clear();
        {
            GenerationController first(runtime);
            QVERIFY(first.connectStorage(root.path()));
            QVERIFY(!first.enqueue("discard when this app closes").isEmpty());
            QCOMPARE(first.jobs().size(), 1);
            GenerationController otherApp(runtime);
            QVERIFY(otherApp.connectStorage(root.path()));
            QVERIFY(otherApp.jobs().isEmpty());
            QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
        }
        GenerationController reopened(runtime);
        QVERIFY(reopened.connectStorage(root.path()));
        QVERIFY(reopened.jobs().isEmpty());
        QVERIFY(reopened.latestImage().isEmpty());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
    }

    void livePreviewsAreOrderedAndNeverBecomeFinalArtifacts_data()
    {
        QTest::addColumn<QString>("prompt");
        QTest::addColumn<QString>("expectedState");
        QTest::newRow("completed") << "live" << "completed";
        QTest::newRow("malformed events") << "live-invalid" << "completed";
        QTest::newRow("failed after previews") << "live-fail" << "failed";
        QTest::newRow("previews without final image") << "live-empty" << "failed";
    }

    void livePreviewsAreOrderedAndNeverBecomeFinalArtifacts()
    {
        QFETCH(QString, prompt);
        QFETCH(QString, expectedState);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/live-generation-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.steps = 3;
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        QList<int> steps;
        QList<QUrl> images;
        QString expectedPreviewJobId;
        connect(&controller, &GenerationController::previewChanged, this, [&] {
            if (controller.previewImage().isEmpty()) return;
            QCOMPARE(controller.previewJobId(), expectedPreviewJobId);
            QVERIFY(controller.busy());
            QVERIFY(controller.latestImage().isEmpty());
            QCOMPARE(controller.previewTotalSteps(), 3);
            steps.append(controller.previewStep());
            images.append(controller.previewImage());
            const QImage image(controller.previewImage().toLocalFile());
            QCOMPARE(image.pixelColor(0, 0).red(), controller.previewStep() * 60);
        });
        const auto id = controller.enqueue(prompt);
        expectedPreviewJobId = id;
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), expectedState, 10000);
        QCOMPARE(steps, QList<int>({1, 2, 3}));
        QCOMPARE(QSet<QUrl>(images.cbegin(), images.cend()).size(), 3);
        QVERIFY(controller.previewImage().isEmpty());
        QVERIFY(controller.previewJobId().isEmpty());
        QCOMPARE(controller.previewStep(), 0);
        QCOMPARE(controller.previewTotalSteps(), 0);
        for (const auto &image : images) QVERIFY(!QFileInfo::exists(image.toLocalFile()));
        QCOMPARE(controller.latestImage().isEmpty(), expectedState != "completed");
        QCOMPARE(recordedJob(controller, id)
                     .value("previewSteps").toInt(), 3);
    }

    void cancellationClearsLivePreviewBeforeNextJob()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/cancel-preview-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto first = controller.enqueue("live-hold");
        QTRY_COMPARE(controller.previewStep(), 1);
        QCOMPARE(controller.previewJobId(), first);
        const auto preview = controller.previewImage();
        QVERIFY(controller.cancel(first));
        QTRY_COMPARE(state(controller, first), QString("cancelled"));
        QVERIFY(controller.previewImage().isEmpty());
        QVERIFY(controller.previewJobId().isEmpty());
        QVERIFY(!QFileInfo::exists(preview.toLocalFile()));
        const auto next = controller.enqueue("next");
        QTRY_COMPARE(state(controller, next), QString("completed"));
        QCOMPARE(controller.previewStep(), 0);
    }

    void sharedModelsAreCapturedAtSubmissionAndOutputsStayPrivate()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/shared-generation-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        const auto settings = qgetenv("SOCIETY_STORAGE_SETTINGS_PATH");
        const auto containerOverride = qgetenv("SOCIETY_CONTAINER_PATH");
        const auto restore = qScopeGuard([&] {
            settings.isNull() ? qunsetenv("SOCIETY_STORAGE_SETTINGS_PATH") : qputenv("SOCIETY_STORAGE_SETTINGS_PATH", settings);
            containerOverride.isNull() ? qunsetenv("SOCIETY_CONTAINER_PATH") : qputenv("SOCIETY_CONTAINER_PATH", containerOverride);
        });
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("settings.json").toUtf8());
        qunsetenv("SOCIETY_CONTAINER_PATH");
        QVERIFY(SharedStorage::setDefaultContainer(root.path()));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage());
        QCOMPARE(controller.models().size(), 2);
        QCOMPARE(controller.containerPath(), root.path());
        controller.setSelectedModel("first model.safetensor");
        const auto literal = QStringLiteral("slow moon\n$(this is literal prompt text)");
        const auto first = controller.enqueue(literal, "4:3");
        controller.setSelectedModel("second.SAFETENSORS");
        const auto second = controller.enqueue("checkpoint");
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, first), QString("completed"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, second), QString("completed"), 10000);
        const auto firstOutput = recordedJob(controller, first).value("generation").toObject();
        QCOMPARE(firstOutput.value("model_path").toString(), root.filePath("Models/first model.safetensor"));
        QCOMPARE(firstOutput.value("prompt").toString(), literal);
        QCOMPARE(firstOutput.value("width").toInt(), 88);
        QCOMPARE(firstOutput.value("height").toInt(), 64);
        for (const auto &key : {"work_dir", "cache_dir", "output_dir", "preview_dir"}) {
            const auto path = QDir::fromNativeSeparators(firstOutput.value(key).toString());
            QVERIFY(!path.isEmpty());
            QVERIFY(path.startsWith(root.filePath("Models/.society-runtime/iiLocalDiffusion/")));
            QVERIFY(!QFileInfo::exists(path));
        }
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
        QCOMPARE(firstOutput.value("backend").toString(), QString("local"));
        QCOMPARE(QDir::fromNativeSeparators(firstOutput.value("generation_resources").toString()), root.filePath("Models/.generation-resources/iiLocalDiffusion"));
        QCOMPARE(QDir::fromNativeSeparators(firstOutput.value("resource_environment").toString()),
            QDir::fromNativeSeparators(firstOutput.value("generation_resources").toString()));
        QVERIFY(!firstOutput.value("default_modifiers").toBool());
        QVERIFY(firstOutput.value("hf_home").toString().startsWith(root.filePath("Models/.society-runtime/iiLocalDiffusion/")));
        QCOMPARE(firstOutput.value("hf_offline").toString(), QString("1"));
        QVERIFY(!firstOutput.contains("startup_timeout"));
        const auto secondOutput = recordedJob(controller, second).value("generation").toObject();
        QCOMPARE(firstOutput.value("worker_pid"), secondOutput.value("worker_pid"));
        QCOMPARE(firstOutput.value("request_count").toInt(), 1);
        QCOMPARE(secondOutput.value("request_count").toInt(), 2);
        QVERIFY(firstOutput.value("python_cache_prefix").isNull());
        QCOMPARE(firstOutput.value("dont_write_bytecode").toString(), QString("1"));
        QCOMPARE(secondOutput.value("model_path").toString(), root.filePath("Models/second.SAFETENSORS"));
        const auto request = recordedJob(controller, first);
        QCOMPARE(request.value("model").toObject().value("containerId").toString(), SharedStorage::open()->drive().identifier());
        QVERIFY(!QDirIterator(root.filePath("Files"), QDir::Files | QDir::Hidden | QDir::System,
                              QDirIterator::Subdirectories).hasNext());
        QVERIFY(QDir(root.filePath("Asset Library")).isEmpty());
        const auto history = contentEntries(QDir(root.filePath("Generation History")), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        QCOMPARE(history.size(), 2);
        QVERIFY(history.contains(first + "-0001.png"));
        QVERIFY(history.contains(second + "-0001.png"));
        QVERIFY(QFileInfo::exists(root.filePath("Models/first model.safetensor")));
        QCOMPARE(controller.latestResult().value("imageSource").toUrl().toLocalFile(), root.filePath("Generation History/" + second + "-0001.png"));
        QVERIFY(!QImage(controller.latestImage().toLocalFile()).isNull());
        const auto latest = controller.latestResult();
        QCOMPARE(latest.value("imageSource").toUrl(), controller.latestImage());
        QCOMPARE(latest.value("id").toString(), second);
        QCOMPARE(latest.value("prompt").toString(), "checkpoint");
        QCOMPARE(latest.value("aspectRatio").toString(), "1:1");
        QCOMPARE(latest.value("model").toMap().value("path").toString(), "second.SAFETENSORS");
        QVERIFY(request.value("finishedAt").toString() <= recordedJob(controller, second).value("startedAt").toString());
    }

    void changedModelsAreRejectedInTheCurrentMemoryQueue()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/memory-model-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto good = controller.enqueue("keep original model");
        const auto cancelled = controller.enqueue("cancel before execution");
        QVERIFY(controller.cancel(cancelled));
        controller.setSelectedModel("second.SAFETENSORS");
        const auto changed = controller.enqueue("must use original model");
        QVERIFY(write(root.filePath("Models/second.SAFETENSORS"), "replacement model"));
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, good), QString("completed"), 10000);
        QTRY_COMPARE(state(controller, changed), QString("failed"));
        QCOMPARE(state(controller, cancelled), QString("cancelled"));
        QVERIFY(QDir(root.filePath("Asset Library")).isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath("Generation History/" + changed + "-0001.png")));
    }

    void failuresCannotBecomeSuccessfulImages_data()
    {
        QTest::addColumn<QString>("prompt");
        QTest::newRow("nonzero exit") << "fail";
        QTest::newRow("zero exit without image") << "empty";
        QTest::newRow("image dimensions do not match request") << "wrong-size";
        QTest::newRow("invalid second image leaves no partial history") << "mixed-invalid";
    }
    void failuresCannotBecomeSuccessfulImages()
    {
        QFETCH(QString, prompt);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/failed-generation-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto id = controller.enqueue(prompt);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("failed"), 10000);
        QVERIFY(controller.latestImage().isEmpty());
        QVERIFY(controller.latestResult().isEmpty());
        QVERIFY(!controller.errorString().isEmpty());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
        QVERIFY(QDir(root.filePath("Asset Library")).isEmpty());
        QVERIFY(!controller.enqueue(" ").size());
        QVERIFY(controller.enqueue("valid", "unsupported").isEmpty());
    }

    void cancellationReleasesTheWorkerForTheNextJob()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/cancel-generation-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto first = controller.enqueue("hold");
        QTRY_VERIFY(controller.busy());
        QTest::qWait(100);
        GenerationController secondWindow(fakeRuntime());
        QVERIFY(secondWindow.connectStorage(root.path()));
        QTest::qWait(100);
        QVERIFY(!secondWindow.busy());
        QVERIFY(secondWindow.jobs().isEmpty());
        const auto independent = secondWindow.enqueue("independent app session");
        QTRY_COMPARE(state(secondWindow, independent), QString("completed"));
        QCOMPARE(state(controller, first), QString("running"));
        QVERIFY(controller.cancel(first));
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, first), QString("cancelled"), 5000);
        const auto next = controller.enqueue("next");
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, next), QString("completed"), 10000);
    }

    void workerFailureAndCrashDoNotBlockTheNextRequest_data()
    {
        QTest::addColumn<QString>("failure");
        QTest::newRow("request fails") << "fail";
        QTest::newRow("worker crashes") << "crash";
        QTest::newRow("fragmented unicode error") << "long-error";
    }

    void workerFailureAndCrashDoNotBlockTheNextRequest()
    {
        QFETCH(QString, failure);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/worker-recovery-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto first = controller.enqueue("first");
        const auto failed = controller.enqueue(failure);
        const auto next = controller.enqueue("next");
        QTRY_COMPARE(state(controller, first), QString("completed"));
        QTRY_COMPARE(state(controller, failed), QString("failed"));
        QTRY_COMPARE(state(controller, next), QString("completed"));
        const auto before = recordedJob(controller, first).value("generation").toObject();
        const auto after = recordedJob(controller, next).value("generation").toObject();
        QCOMPARE(before.value("worker_pid") == after.value("worker_pid"), failure != "crash");
        QCOMPARE(after.value("request_count").toInt(), failure != "crash" ? 3 : 1);
        if (failure == "long-error")
            QCOMPARE(recordedJob(controller, failed).value("error").toString(), QString("오류").repeated(2000));
        QCOMPARE(QDir(root.filePath("Generation History")).entryList(QDir::Files).size(), 2);
    }

    void redirectedOutputCannotEscapeSociety()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/redirect-generation-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.executable.clear();
        QString id;
        {
            GenerationController queued(runtime);
            QVERIFY(queued.connectStorage(root.path()));
            id = queued.enqueue("outside must remain empty");
        }
        QVERIFY(QDir().rmdir(root.filePath("Generation History")));
        QVERIFY(createNativeTestLink(root.filePath("Files"), root.filePath("Generation History")));
        GenerationController controller(fakeRuntime());
        QVERIFY(!controller.connectStorage(root.path()));
        QVERIFY(!QDirIterator(root.filePath("Files"), QDir::Files | QDir::Hidden | QDir::System,
                              QDirIterator::Subdirectories).hasNext());
    }

    void allImagesShareOneFlatHistoryAndAssetsAreUntouched()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/flat-history-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        const auto asset = root.filePath("Asset Library/existing.png");
        QVERIFY(write(asset, "an existing asset must remain unchanged"));
        // Provider-owned metadata is not a user asset. Keep a real companion
        // present so this contract does not depend on background worker timing.
        QImage preview(2, 2, QImage::Format_ARGB32); preview.fill(Qt::red);
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(preview.save(&buffer, "PNG"));
        const auto presentation = iiFileProvider::native::Presentation::attach(
            std::filesystem::path(asset.toStdU16String()), {"image/png", "image", png.toStdString(), 2, 2});
        QVERIFY(!presentation.document.empty());
        QVERIFY(std::filesystem::is_regular_file(iiFileProvider::native::Presentation::companionPath(
            std::filesystem::path(asset.toStdU16String()))));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
        const auto first = controller.enqueue("multiple");
        const auto second = controller.enqueue("multiple");
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, second), QString("completed"), 10000);
        QCOMPARE(state(controller, first), QString("completed"));
        const QDir history(root.filePath("Generation History"));
        QCOMPARE(history.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden).size(), 0);
        QCOMPARE(contentEntries(history, QDir::Files | QDir::Hidden).size(), 4);
        for (const auto &id : {first, second}) {
            for (int index = 1; index <= 2; ++index) {
                const QImage image(history.filePath(id + QString("-%1.png").arg(index, 4, 10, QLatin1Char('0'))));
                QCOMPARE(image.size(), QSize(64, 64));
            }
            const auto request = recordedJob(controller, id);
            QCOMPARE(request.value("images").toArray().size(), 2);
        }
        QCOMPARE(contentEntries(QDir(root.filePath("Asset Library")),
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), QStringList{"existing.png"});
        QFile preserved(asset);
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), QByteArray("an existing asset must remain unchanged"));
        GenerationController reopened(fakeRuntime());
        QVERIFY(reopened.connectStorage(root.path()));
        QVERIFY(reopened.latestImage().isEmpty());
        QVERIFY(reopened.jobs().isEmpty());
        QCOMPARE(contentEntries(history, QDir::Files | QDir::Hidden).size(), 4);
    }

    void historyNameCollisionPreservesExistingImage()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/history-collision-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto id = controller.enqueue("collision");
        const auto existing = root.filePath("Generation History/" + id + "-0001.png");
        QVERIFY(write(existing, "preserve existing bytes"));
        QTRY_COMPARE(state(controller, id), QString("failed"));
        QFile preserved(existing);
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), QByteArray("preserve existing bytes"));
        QVERIFY(controller.latestImage().isEmpty());
        QVERIFY(QDir(root.filePath("Asset Library")).isEmpty());
    }

    void legacyQueuesAreDiscardedWhileCompletedImagesArePreserved()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/legacy-memory-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.executable.clear();
        GenerationController offline(runtime);
        QVERIFY(offline.connectStorage(root.path()));
        const auto completed = offline.enqueue("old completed image");
        const auto queued = offline.enqueue("do not resume this old queue");
        auto job = recordedJob(offline, completed);
        const auto oldOutput = "Asset Library/Dreamscapes/" + completed;
        QVERIFY(QDir().mkpath(root.filePath(oldOutput)));
        QImage generated(64, 64, QImage::Format_RGB32);
        generated.fill(Qt::blue);
        QVERIFY(generated.save(root.filePath(oldOutput + "/image-0001.png")));
        QVERIFY(write(root.filePath(oldOutput + "/generation.json"), "{\"backend\":\"diffusers\"}"));
        job["state"] = "completed";
        job["output"] = oldOutput;
        job["image"] = oldOutput + "/image-0001.png";
        const auto legacy = root.filePath("Generation History/Dreamscapes");
        QVERIFY(QDir().mkpath(legacy + '/' + completed));
        QVERIFY(QDir().mkpath(legacy + '/' + queued));
        QVERIFY(write(legacy + '/' + completed + "/request.json", QJsonDocument(job).toJson()));
        QVERIFY(write(legacy + '/' + queued + "/request.json", QJsonDocument(recordedJob(offline, queued)).toJson()));
        QVERIFY(write(root.filePath("Asset Library/preserved.asset"), "keep this asset"));
        GenerationController migrated(fakeRuntime());
        QVERIFY2(migrated.connectStorage(root.path()), qPrintable(migrated.errorString()));
        QVERIFY(migrated.jobs().isEmpty());
        QVERIFY(migrated.latestImage().isEmpty());
        QTest::qWait(100);
        QVERIFY(!migrated.busy());
        const QDir history(root.filePath("Generation History"));
        QCOMPARE(history.entryList({"*.png"}, QDir::Files).size(), 1);
        const QImage preservedPng(root.filePath("Generation History/" + completed + "-0001.png"));
        QCOMPARE(preservedPng, generated);
        QCOMPARE(QDir(root.filePath("Asset Library")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), QStringList{"preserved.asset"});
        QVERIFY(!QFileInfo::exists(legacy));
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
    }

    void obsoleteWorkingFilesAreRemovedWithoutFollowingModelLinks()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/obsolete-working-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        const auto legacy = root.filePath(".dreamscapes/generation");
        QVERIFY(QDir().mkpath(legacy + "/Runtime Cache"));
        QVERIFY(write(legacy + "/request.json", "{\"state\":\"queued\"}"));
        QVERIFY(write(legacy + "/Runtime Cache/temporary.bin", "disposable"));
        QVERIFY(createNativeTestLink(root.filePath("Models"), legacy + "/Runtime Cache/model-link"));
        QImage completed(16, 16, QImage::Format_RGB32);
        completed.fill(Qt::red);
        QVERIFY(completed.save(root.filePath("Generation History/existing.png")));
        GenerationController controller(fakeRuntime());
        QVERIFY2(controller.connectStorage(root.path()), qPrintable(controller.errorString()));
        QVERIFY(controller.jobs().isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
        QVERIFY(QFileInfo::exists(root.filePath("Models/first model.safetensor")));
        QVERIFY(QFileInfo::exists(root.filePath("Models/second.SAFETENSORS")));
        QCOMPARE(QImage(root.filePath("Generation History/existing.png")), completed);
    }

    void redirectedLegacyStorageIsPreserved()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/redirected-legacy-XXXXXX.societycontainer");
        QTemporaryDir outside(DREAMSCAPES_TEST_DIRECTORY "/preserved-legacy-XXXXXX");
        QVERIFY(prepare(root));
        QVERIFY(outside.isValid());
        QVERIFY(QDir().mkpath(outside.filePath("generation")));
        QVERIFY(write(outside.filePath("generation/preserved.txt"), "preserve foreign data"));
        QVERIFY(createNativeTestLink(outside.path(), root.filePath(".dreamscapes")));
        GenerationController controller(fakeRuntime());
        QVERIFY(!controller.connectStorage(root.path()));
        QVERIFY(QFileInfo(root.filePath(".dreamscapes")).isSymLink());
        QVERIFY(QFileInfo::exists(outside.filePath("generation/preserved.txt")));
    }

    void obsoleteFilesInUseArePreserved()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/obsolete-busy-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        const auto legacy = root.filePath(".dreamscapes/generation");
        QVERIFY(QDir().mkpath(legacy));
        QVERIFY(write(legacy + "/request.json", "still in use"));
        QLockFile previous(legacy + "/.worker.lock");
        QVERIFY(previous.tryLock());
        GenerationController controller(fakeRuntime());
        QVERIFY(!controller.connectStorage(root.path()));
        QVERIFY(QFileInfo::exists(legacy + "/request.json"));
        previous.unlock();
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
    }

    void temporaryFilesAreRemovedAtEveryJobExit_data()
    {
        QTest::addColumn<QString>("prompt");
        QTest::addColumn<QString>("expected");
        QTest::newRow("completed") << "live" << "completed";
        QTest::newRow("failed") << "live-fail" << "failed";
        QTest::newRow("cancelled") << "live-hold" << "cancelled";
        QTest::newRow("app closes") << "live-hold" << "closed";
    }

    void temporaryFilesAreRemovedAtEveryJobExit()
    {
        QFETCH(QString, prompt);
        QFETCH(QString, expected);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/session-society-XXXXXX.societycontainer");
        QTemporaryDir temporary(DREAMSCAPES_TEST_DIRECTORY "/session-temporary-XXXXXX");
        QVERIFY(prepare(root));
        QVERIFY(temporary.isValid());
        const auto original = QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        auto runtime = fakeRuntime();

        runtime.steps = 3;
        {
            GenerationController controller(runtime);
            QVERIFY(controller.connectStorage(root.path()));
            const auto id = controller.enqueue(prompt);
            QTRY_VERIFY(!controller.previewImage().isEmpty());
            auto jobDirectory = QFileInfo(controller.previewImage().toLocalFile()).dir();
            QVERIFY(jobDirectory.cdUp());
            QVERIFY(controller.previewImage().toLocalFile().startsWith(root.filePath("Models/.society-runtime/iiLocalDiffusion/")));
            QCOMPARE(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), original);
            QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
            if (expected == "cancelled") QVERIFY(controller.cancel(id));
            if (expected != "closed") {
                QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), expected, 10000);
                QVERIFY(!QFileInfo::exists(jobDirectory.path()));
                for (const auto &name : QDir(root.filePath("Models/.society-runtime/iiLocalDiffusion")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
                    QVERIFY(name.startsWith("dreamscapes-inference-"));
            }
        }
        QVERIFY(QDir(temporary.path()).isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
        QVERIFY(QDir(root.filePath("Asset Library")).isEmpty());
        QCOMPARE(contentEntries(QDir(root.filePath("Generation History")), QDir::Files | QDir::Hidden).size(), expected == "completed" ? 1 : 0);
        GenerationController reopened(runtime);
        QVERIFY(reopened.connectStorage(root.path()));
        QVERIFY(reopened.jobs().isEmpty());
    }

    void storageDirectoriesCannotBeRedirectedOutsideSociety_data()
    {
        QTest::addColumn<QString>("directory");
        QTest::newRow("runtime") << "Models/.society-runtime";
        QTest::newRow("model-resources") << "Models/.generation-resources";
    }
    void storageDirectoriesCannotBeRedirectedOutsideSociety()
    {
        QFETCH(QString, directory);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/invalid-temporary-XXXXXX.societycontainer");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();

        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        QTemporaryDir outside(DREAMSCAPES_TEST_DIRECTORY "/outside-runtime-XXXXXX");
        QVERIFY(createNativeTestLink(outside.path(), root.filePath(directory)));
        const auto id = controller.enqueue("must not follow redirected runtime storage");
        QTRY_COMPARE(state(controller, id), QString("failed"));
        QVERIFY(QDir(outside.path()).isEmpty());
        QVERIFY(!QDirIterator(root.filePath("Files"), QDir::Files | QDir::Hidden | QDir::System,
                              QDirIterator::Subdirectories).hasNext());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
    }

    void realForegroundPreparation()
    {
        const auto root = qEnvironmentVariable("DREAMSCAPES_REAL_SMOKE_CONTAINER");
        if (root.isEmpty()) QSKIP("Opt-in foreground preparation requires a local Society model fixture.");
        GenerationRuntime runtime{qEnvironmentVariable("IILD_GENERATOR_EXECUTABLE"), "auto", 1, 64, {}};
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root));
        const auto before = QDir(root + "/Generation History").entryList(QDir::Files);
        controller.setForeground(true);
        QTRY_VERIFY_WITH_TIMEOUT(controller.inferenceStatus().value("ready").toBool()
                                || controller.inferenceStatus().value("state") == "error", 180000);
        QVERIFY2(controller.inferenceStatus().value("ready").toBool(),
                 qPrintable(controller.inferenceStatus().value("error").toString()));
        QVERIFY(controller.inferenceStatus().value("gpu_resident").toBool());
        QCOMPARE(QDir(root + "/Generation History").entryList(QDir::Files), before);
        QVERIFY(controller.jobs().isEmpty());
        const auto pid = controller.inferenceStatus().value("pid");
        const auto id = controller.enqueue("Use the model already waiting on the GPU");
        QTRY_VERIFY_WITH_TIMEOUT(state(controller, id) == "completed" || state(controller, id) == "failed", 180000);
        QCOMPARE(state(controller, id), QString("completed"));
        const auto worker = recordedJob(controller, id).value("worker").toObject();
        QCOMPARE(worker.value("pid").toVariant(), pid);
        for (const auto &key : {"model_hashes", "configuration_reads", "pipeline_loads", "device_placements"})
            QCOMPARE(worker.value("cache").toObject().value(key).toInt(-1), 0);
        qInfo().noquote() << "Foreground GPU residency:" << QJsonDocument(worker).toJson(QJsonDocument::Compact);
    }

    void realSocietyInference()
    {
        const auto root = qEnvironmentVariable("DREAMSCAPES_REAL_SMOKE_CONTAINER");
        if (root.isEmpty()) QSKIP("Opt-in real inference requires a prepared Society model fixture.");
        GenerationRuntime runtime{qEnvironmentVariable("IILD_GENERATOR_EXECUTABLE"), "cpu", 1, 64, {}};
        QVERIFY(!runtime.executable.isEmpty());
        GenerationController controller(runtime);
        QVERIFY2(controller.connectStorage(root), qPrintable(controller.errorString()));
        QVERIFY(!controller.models().isEmpty());
        const auto id = controller.enqueue("Society shared storage neural inference verification");
        QVERIFY2(!id.isEmpty(), qPrintable(controller.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(state(controller, id) == "completed" || state(controller, id) == "failed", 180000);
        QCOMPARE(state(controller, id), QString("completed"));
        QVERIFY(!controller.latestImage().isEmpty());
        const auto second = controller.enqueue("A second fresh prompt using the same Society model");
        QTRY_VERIFY_WITH_TIMEOUT(state(controller, second) == "completed" || state(controller, second) == "failed", 180000);
        QCOMPARE(state(controller, second), QString("completed"));
        const auto firstWorker = recordedJob(controller, id).value("worker").toObject();
        const auto secondWorker = recordedJob(controller, second).value("worker").toObject();
        QCOMPARE(firstWorker.value("pid"), secondWorker.value("pid"));
        QCOMPARE(firstWorker.value("cache").toObject().value("pipeline_loads").toInt(-1), 1);
        QCOMPARE(firstWorker.value("cache").toObject().value("device_placements").toInt(-1), 1);
        QCOMPARE(secondWorker.value("cache").toObject().value("model_hashes").toInt(-1), 0);
        QCOMPARE(secondWorker.value("cache").toObject().value("pipeline_hits").toInt(-1), 1);
        QCOMPARE(secondWorker.value("cache").toObject().value("pipeline_loads").toInt(-1), 0);
        QCOMPARE(secondWorker.value("cache").toObject().value("configuration_reads").toInt(-1), 0);
        QCOMPARE(secondWorker.value("cache").toObject().value("device_placements").toInt(-1), 0);
        QCOMPARE(secondWorker.value("cache").toObject().value("device_placement_hits").toInt(-1), 1);
        qInfo().noquote() << "Society inference output:" << controller.latestImage().toLocalFile();
    }
};
QTEST_GUILESS_MAIN(GenerationTests)
#include "tst_Generation.moc"
