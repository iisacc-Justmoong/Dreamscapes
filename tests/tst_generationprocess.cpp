#include "App/Generation/GenerationProcess.h"
#include "App/Generation/GenerationController.h"
#include <SocietyDrive.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <thread>
#if defined(Q_OS_UNIX)
#include <signal.h>
#include <cerrno>
#if defined(Q_OS_MACOS)
#include <libproc.h>
#include <sys/proc.h>
#endif
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
void launch(GenerationProcess &process, const QStringList &arguments, std::shared_ptr<void> lifetime = {}) {
#ifdef Q_OS_WIN
    process.start(QStringLiteral(DREAMSCAPES_TEST_PYTHON), {QStringLiteral(DREAMSCAPES_PROCESS_FIXTURE)} + arguments, std::move(lifetime));
#else
    process.start(QStringLiteral(DREAMSCAPES_PROCESS_FIXTURE), arguments, std::move(lifetime));
#endif
}
bool gone(quint64 pid) {
#if defined(Q_OS_UNIX)
    return pid > 1 && ::kill(pid_t(pid), 0) < 0 && errno == ESRCH;
#elif defined(Q_OS_WIN)
    const auto handle=OpenProcess(SYNCHRONIZE,FALSE,DWORD(pid));
    if(!handle)return GetLastError()==ERROR_INVALID_PARAMETER;
    const bool ended=WaitForSingleObject(handle,0)==WAIT_OBJECT_0;CloseHandle(handle);return ended;
#else
    Q_UNUSED(pid);return false;
#endif
}
bool retired(quint64 pid) {
    if(gone(pid))return true;
#if defined(Q_OS_MACOS)
    proc_bsdinfo info{};return proc_pidinfo(int(pid),PROC_PIDTBSDINFO,0,&info,sizeof(info))==sizeof(info)&&info.pbi_status==SZOMB;
#elif defined(Q_OS_LINUX)
    QFile record(QString("/proc/%1/stat").arg(pid));if(!record.open(QIODevice::ReadOnly))return false;const auto text=record.readAll();const auto end=text.lastIndexOf(')');return end>=0&&text.size()>end+2&&text[end+2]=='Z';
#else
    return false;
#endif
}
}
class GenerationProcessTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { if (!ii::process::capabilities().launch) QSKIP("Native processes are unavailable on this target."); }
    void literalArgumentsEnvironmentAndDirectory() {
        QTemporaryDir root(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/process spaces-XXXXXX"));QVERIFY(root.isValid());
        GenerationProcess process;process.setWorkingDirectory(root.path());
        auto environment=process.processEnvironment();environment["DREAMSCAPES_PROCESS_VALUE"]="value 한글";environment["DREAMSCAPES_PROCESS_REMOVED"]=std::nullopt;process.setProcessEnvironment(environment);
        QByteArray bytes;connect(&process,&GenerationProcess::outputAvailable,this,[&](const auto &data){bytes+=data;});QSignalSpy done(&process,&GenerationProcess::finished);
        const QStringList args{"", "quoted \" \\ end", "; $() & | < > 끝"};launch(process,QStringList{"arguments"}+args);QTRY_COMPARE(done.size(),1);
        const auto record=QJsonDocument::fromJson(bytes).object();QCOMPARE(record.value("arguments").toArray(),QJsonArray::fromStringList(args));QCOMPARE(record.value("directory").toString(),root.path());QCOMPARE(record.value("value").toString(),QString::fromUtf8("value 한글"));QVERIFY(record.value("removed").isNull());
    }
    void repeatedRequestsPreserveBinaryAndPid() {
        GenerationProcess process;QByteArray bytes;connect(&process,&GenerationProcess::outputAvailable,this,[&](const auto &data){bytes+=data;});launch(process,{"lines"});QTRY_COMPARE(process.state(),GenerationProcess::Running);const auto pid=process.processId();QVERIFY(pid>1);
        const QByteArray first("first\0body\n",11),second("second\n");QCOMPARE(process.write(first),qint64(first.size()));QTRY_COMPARE(bytes,first);process.releaseInput();
        QCOMPARE(process.write(second),qint64(second.size()));QTRY_COMPARE(bytes,first+second);QCOMPARE(process.processId(),pid);process.cancel();QTRY_COMPARE(process.state(),GenerationProcess::NotRunning);QVERIFY(gone(pid));
    }
    void outputBeforeTerminalAndBoundedGuiTurns() {
        GenerationProcess process;qsizetype received=0;qsizetype largest=0;int ticks=0;bool ended=false;QTimer timer;timer.setInterval(1);connect(&timer,&QTimer::timeout,this,[&]{++ticks;});timer.start();
        connect(&process,&GenerationProcess::outputAvailable,this,[&](const QByteArray &data){QVERIFY(!ended);received+=data.size();largest=std::max(largest,data.size());});
        connect(&process,&GenerationProcess::finished,this,[&](qint64 code,auto){QCOMPARE(code,qint64(0));QCOMPARE(received,qsizetype(4*1024*1024));ended=true;});
        launch(process,{"flood"});QTRY_VERIFY_WITH_TIMEOUT(ended,10000);QVERIFY(largest<=64*1024);QVERIFY(ticks>0);
    }
    void nonzeroExitRetainsTheTail() {
        GenerationProcess process;QByteArray bytes;connect(&process,&GenerationProcess::outputAvailable,this,[&](const auto &data){bytes+=data;});QSignalSpy done(&process,&GenerationProcess::finished);launch(process,{"exit","37"});QTRY_COMPARE(done.size(),1);QCOMPARE(done.first().first().toLongLong(),qint64(37));QCOMPARE(bytes,QByteArray("last bytes 끝\n"));
    }
    void terminalProbeFollowsAllBufferedOutput() {
        GenerationProcess process;qsizetype bytes=0;bool ended=false,probed=false;
        connect(&process,&GenerationProcess::outputAvailable,this,[&](const QByteArray &data){bytes+=data.size();});
        connect(&process,&GenerationProcess::finished,this,[&]{ended=true;});
        connect(&process,&GenerationProcess::outputProbed,this,[&](quint64 token){QCOMPARE(token,quint64(73));QCOMPARE(bytes,qsizetype(128*1024));probed=true;});
        const auto released=std::make_shared<std::atomic_bool>(false);struct Lease{std::shared_ptr<std::atomic_bool> released;~Lease(){*released=true;}};auto lease=std::make_shared<Lease>();lease->released=released;
        launch(process,{"small-flood"},lease);lease.reset();QElapsedTimer clock;clock.start();
        // The native owner completes while GUI delivery remains deliberately held.
        while(!released->load()&&clock.elapsed()<5000)QThread::msleep(1);
        QVERIFY(released->load());QCOMPARE(bytes,qsizetype(0));process.probeOutput(73);QTRY_VERIFY(probed);QTRY_VERIFY(ended);
    }
    void immediateCancellationAndMissingExecutable() {
        GenerationProcess process;QSignalSpy done(&process,&GenerationProcess::finished);launch(process,{"hold"});process.cancel();QTRY_COMPARE(done.size(),1);QCOMPARE(process.state(),GenerationProcess::NotRunning);
        GenerationProcess missing;QSignalSpy failed(&missing,&GenerationProcess::errorOccurred);missing.start(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/missing-owned-executable"),{});QTRY_COMPARE(failed.size(),1);QCOMPARE(missing.state(),GenerationProcess::NotRunning);QVERIFY(!missing.errorString().isEmpty());
    }
    void removalRetiresOnlyItsOwnedTreeAndFiles() {
        QTemporaryDir root(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/process-owned-tree-XXXXXX"));QVERIFY(root.isValid());const auto record=root.filePath("pids.json");
        const auto retired=std::make_shared<std::atomic_bool>(false);struct Lease { std::shared_ptr<std::atomic_bool> retired;~Lease(){*retired=true;} };auto lease=std::make_shared<Lease>();lease->retired=retired;
        auto process=std::make_unique<GenerationProcess>();QByteArray bytes;connect(process.get(),&GenerationProcess::outputAvailable,this,[&](const auto &data){bytes+=data;});launch(*process,{"tree",record},lease);lease.reset();QTRY_VERIFY(bytes.contains("ready"));QFile file(record);QVERIFY(file.open(QIODevice::ReadOnly));const auto pids=QJsonDocument::fromJson(file.readAll()).object();const auto parent=quint64(pids.value("parent").toDouble()),child=quint64(pids.value("child").toDouble());QVERIFY(parent>1&&child>1);QVERIFY(!retired->load());
        QElapsedTimer clock;clock.start();process.reset();const auto elapsed=clock.nsecsElapsed();QVERIFY(elapsed<250000000);QTRY_VERIFY(gone(parent));QTRY_VERIFY(retired->load());
        // Descendant reaping belongs to the host; either absent or zombie is dead.
        QTRY_VERIFY(::retired(child));
        qInfo()<<"PERF process_owner_close_ns="<<elapsed;
    }
    void outputObserverCanRemoveThePresenter() {
        auto process=std::make_unique<GenerationProcess>();QPointer<GenerationProcess> observed(process.get());connect(process.get(),&GenerationProcess::outputAvailable,this,[&](const auto &){process.reset();});launch(*process,{"flood"});QTRY_VERIFY(observed.isNull());
    }
    void controllerRemovalRetiresItsWorkerWithoutJoining() {
        QTemporaryDir root(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/process-controller-close-XXXXXX.societycontainer"));
        QVERIFY(root.isValid());QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));
        QFile model(root.filePath("Models/model.safetensors"));QVERIFY(model.open(QIODevice::WriteOnly));model.write("fixture weights");model.close();
        GenerationRuntime runtime;runtime.executable=QStringLiteral(DREAMSCAPES_PROCESS_FIXTURE);runtime.pythonExecutable=QStringLiteral(DREAMSCAPES_TEST_PYTHON);runtime.device="cpu";runtime.imageExtent=64;runtime.steps=1;
        auto controller=std::make_unique<GenerationController>(runtime);QVERIFY(controller->connectStorage(root.path()));QVERIFY(!controller->enqueue("held process close").isEmpty());controller->setForeground(true);
        QFile file(root.filePath("Models/.held-process.json"));QTRY_VERIFY(file.exists()&&file.size()>0);QVERIFY(file.open(QIODevice::ReadOnly));const auto record=QJsonDocument::fromJson(file.readAll()).object();file.close();
        const auto pid=quint64(record.value("pid").toDouble());QVERIFY(pid>1);QVERIFY(!gone(pid));
        const auto directory=record.value("runtime").toString();QVERIFY(QFileInfo::exists(directory));
        const auto arguments=record.value("request").toObject().value("arguments").toArray();const auto position=std::find(arguments.constBegin(),arguments.constEnd(),QJsonValue(QStringLiteral("--work-dir")));const int workIndex=int(std::distance(arguments.constBegin(),position));QVERIFY(workIndex+1<arguments.size());const auto work=arguments[workIndex+1].toString();QVERIFY(QFileInfo::exists(work));
        QElapsedTimer clock;clock.start();controller.reset();const auto elapsed=clock.nsecsElapsed();QVERIFY(elapsed<250000000);
        QTRY_VERIFY(gone(pid));QTRY_VERIFY(!QFileInfo::exists(directory));QTRY_VERIFY(!QFileInfo::exists(work));QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
        qInfo()<<"PERF controller_process_close_ns="<<elapsed;
    }
    void cancelledNativeInputDoesNotInvokeTheProvider() {
        dreamscapes::NativeGenerationInput input;input.state=std::make_shared<dreamscapes::NativeGenerationState>();input.state->cancelled=true;bool invoked=false;
        input.generate=[&](const auto &,const auto &,const auto &,const auto &,const auto &){invoked=true;return iiLocalDiffusion::NativeGenerationResult{};};
        const auto result=dreamscapes::runNativeGeneration(std::move(input));QVERIFY(result.cancelled);QVERIFY(!invoked);
    }
    void cancellingObserverCanRemoveTheController() {
        QTemporaryDir root(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/cancel-observer-close-XXXXXX.societycontainer"));QVERIFY(root.isValid());QVERIFY(iiSocietyContainer::SocietyDrive::create(root.path()));QFile model(root.filePath("Models/model.safetensors"));QVERIFY(model.open(QIODevice::WriteOnly));model.write("fixture weights");model.close();
        const auto entered=std::make_shared<std::atomic_bool>(false);GenerationRuntime runtime;runtime.nativeInference=true;runtime.steps=1;runtime.imageExtent=64;
        runtime.nativeGenerate=[entered](const auto &,const auto &,const auto &cancelled,const auto &,const auto &){*entered=true;while(!cancelled.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));iiLocalDiffusion::NativeGenerationResult result;result.cancelled=true;return result;};
        auto controller=std::make_unique<GenerationController>(runtime);QVERIFY(controller->connectStorage(root.path()));const auto id=controller->enqueue("cancel then remove");QVERIFY(!id.isEmpty());controller->setForeground(true);QTRY_VERIFY(entered->load());
        connect(controller.get(),&GenerationController::inferenceStatusChanged,this,[&]{if(controller&&controller->inferenceStatus().value("state")=="cancelling")controller.reset();});
        auto *target=controller.get();QVERIFY(target->cancel(id));QVERIFY(!controller);
    }
    // This is last: quit closes process admission for this application lifetime.
    void applicationShutdownRetiresOwnedProcesses() {
        GenerationProcess process;QByteArray bytes;connect(&process,&GenerationProcess::outputAvailable,this,[&](const QByteArray &data){bytes+=data;});launch(process,{"hold"});QTRY_VERIFY(bytes.contains("ready"));const auto pid=process.processId();QVERIFY(pid>1&&!gone(pid));
        QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(),"aboutToQuit",Qt::DirectConnection));const bool reapedBeforeQuitReturned=gone(pid);
        // Also clean up on the old implementation when the red check fails.
        process.cancel();QTRY_VERIFY(gone(pid));QVERIFY(reapedBeforeQuitReturned);
    }
};
QTEST_GUILESS_MAIN(GenerationProcessTests)
#include "tst_generationprocess.moc"
