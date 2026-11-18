#include "App/Generation/NativeWorkerProtocol.h"
#include <iiJson>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
using namespace dreamscapes;
using namespace dreamscapes::generation;
using ii::process::Stream;
using ii::process::Exit;
using ii::process::ExitReason;
constexpr auto ready="IILD_READY {\"schema\":\"iild-worker-v1\",\"pid\":123}\n";
constexpr auto success="IILD_RESULT {\"schema\":\"iild-worker-result-v1\",\"id\":\"job-1\",\"ok\":true,\"exit_code\":0,\"error\":\"\"}\n";
struct TrackingStaging final : WorkerStaging {
    int calls=0;QueueState last=QueueState::Queued;
    std::function<void(QueueCompletion &)> onFinished;
    void workerFinished(QueueCompletion &result) noexcept override {++calls;last=result.state;if (onFinished) onFinished(result);}
};
}
int main() {
    try {
        const auto request=WorkerProtocol::request("job-1",{"--prompt","한글 \"test\"\nnext","--steps","10"});
        check(bool(request) && request->back()=='\n',"Worker request was not a JSON line");
        const auto parsed=ii::json::parse(*request);
        check(bool(parsed) && vm::text(vm::get(*parsed,"schema"))=="iild-worker-request-v1" &&
            vm::text(vm::get(*parsed,"id"))=="job-1" &&
            vm::text(vm::array(vm::get(*parsed,"arguments"))[1])=="한글 \"test\"\nnext",
            "Worker request lost its identity or escaped arguments");
        check(!WorkerProtocol::request("",{}) && !WorkerProtocol::request("id",{std::string("x\0y",3)}) &&
            !WorkerProtocol::request("id",{std::string(1024*1024,'x')}),"Invalid worker request was accepted");
        check(!WorkerProtocol::request("id",std::vector<std::string>(4097)),"Too many worker arguments were accepted");
        check(!WorkerProtocol::request("id",{std::string("\xff",1)}),"Invalid UTF-8 worker argument was accepted");

        std::vector<QueueProgress> events;
        WorkerProtocol protocol("job-1",[&](QueueProgress value){events.push_back(std::move(value));});
        protocol.receive(Stream::output,"IILD_RE");protocol.receive(Stream::output,std::string(ready).substr(7));
        protocol.receive(Stream::error,"First causal error\n");
        protocol.receive(Stream::output,"IILD_MODEL_PROGRESS {\"schema\":\"iild-model-progress-v1\",\"completed_bytes\":50,\"total_bytes\":100}\n");
        protocol.receive(Stream::output,"IILD_NATIVE_PROGRESS {\"schema\":\"iild-native-progress-v1\",\"stage\":\"denoising\",\"step\":1,\"total\":2}\n");
        protocol.receive(Stream::output,"IILD_NATIVE_PROGRESS {\"schema\":\"wrong\",\"stage\":\"denoising\",\"step\":2,\"total\":2}\n");
        protocol.receive(Stream::output,"IILD_NATIVE_PROGRESS {\"schema\":\"iild-native-progress-v1\",\"stage\":\"bad\\u0000stage\",\"step\":2,\"total\":2}\n");
        protocol.receive(Stream::output,std::string(success).substr(0,19));protocol.receive(Stream::output,std::string(success).substr(19));
        auto completed=protocol.finish(Exit{ExitReason::exited,0,0});
        check(completed.state==QueueState::Completed && events.size()==2 && events[0]==QueueProgress{"loading",50,100} &&
            events[1]==QueueProgress{"denoising",1,2},"Valid chunked worker events were not delivered accurately");

        WorkerProtocol failed("job-1",{});failed.receive(Stream::output,ready);
        failed.receive(Stream::error,std::string(20000,'a')+"\nFirst causal error\n");
        failed.receive(Stream::output,"IILD_RESULT {\"schema\":\"iild-worker-result-v1\",\"id\":\"job-1\",\"ok\":false,\"exit_code\":1,\"error\":\"cannot load\"}\n");
        auto rejected=failed.finish(Exit{ExitReason::exited,0,0});
        check(rejected.state==QueueState::Failed && rejected.error.find("cannot load")!=std::string::npos &&
            rejected.error.find("First causal error")!=std::string::npos && rejected.error.size()<18000,
            "Worker failure lost evidence or exceeded its diagnostic bound");
        check(rejected.evidence.originalError=="cannot load" &&
            rejected.evidence.workerLog.find("First causal error")!=std::string::npos &&
            rejected.evidence.exitCode && *rejected.evidence.exitCode==0 &&
            rejected.evidence.deadlineExpired && !*rejected.evidence.deadlineExpired,
            "Worker diagnostic evidence was not separated and recorded accurately");
        WorkerProtocol encoding("job-1",{});encoding.receive(Stream::output,ready);
        encoding.receive(Stream::error,std::string(16383,'x')+"한글"+std::string("\0\xff",2)+"\n");
        encoding.receive(Stream::output,"IILD_RESULT {\"schema\":\"iild-worker-result-v1\",\"id\":\"job-1\",\"ok\":false,\"exit_code\":1,\"error\":\"cannot load\"}\n");
        auto encodedFailure=encoding.finish(Exit{ExitReason::exited,0,0});
        check(ii::core::valid_utf8(encodedFailure.error) && encodedFailure.error.find('\0')==std::string::npos,
            "Worker diagnostics cannot be shown as selectable text");

        WorkerProtocol oversized("job-1",{});oversized.receive(Stream::output,ready);
        oversized.receive(Stream::error,std::string(WorkerProtocol::maximumLine+1000,'x')+"\n");
        oversized.receive(Stream::output,success);
        check(oversized.finish(Exit{ExitReason::exited,0,0}).state==QueueState::Completed,
            "An oversized log line corrupted the next protocol record");

        WorkerProtocol wrong("job-1",{});wrong.receive(Stream::output,ready);
        wrong.receive(Stream::output,"IILD_RESULT {\"schema\":\"iild-worker-result-v1\",\"id\":\"other\",\"ok\":true,\"exit_code\":0}\n");
        check(wrong.finish(Exit{ExitReason::exited,0,0}).state==QueueState::Failed,"A different job's result was accepted");
        WorkerProtocol missing("job-1",{});missing.receive(Stream::output,ready);
        check(missing.finish(Exit{ExitReason::exited,0,0}).state==QueueState::Failed,"Missing worker result was accepted");
        WorkerProtocol unready("job-1",{});unready.receive(Stream::output,success);
        check(unready.finish(Exit{ExitReason::exited,0,0}).state==QueueState::Failed,"Worker result before readiness was accepted");
        WorkerProtocol nonzero("job-1",{});nonzero.receive(Stream::output,ready);nonzero.receive(Stream::output,success);
        check(nonzero.finish(Exit{ExitReason::exited,9,0}).state==QueueState::Failed,"A crashing worker was marked complete");
        WorkerProtocol deadline("job-1",{});
        auto timedOut=deadline.finish(Exit{ExitReason::timed_out,0,0});
        check(timedOut.evidence.deadlineExpired && *timedOut.evidence.deadlineExpired && !timedOut.evidence.exitCode,
            "A real timeout was confused with an exit code or unreported status");
        WorkerProtocol canceled("job-1",{});canceled.receive(Stream::output,ready);canceled.receive(Stream::output,success);
        check(canceled.finish(Exit{ExitReason::cancelled,0,0}).state==QueueState::Canceled,"Cancellation was marked complete");

        QueueCancellation cancellation;bool receivedRequest=false;
        WorkerTransport fake=[&](std::string_view body,std::stop_token stop,const WorkerReceiver &receive) -> ii::core::Result<Exit> {
            receivedRequest=body.ends_with('\n') && !stop.stop_requested();
            receive(Stream::output,ready);receive(Stream::output,success);return Exit{ExitReason::exited,0,0};
        };
        check(executeWorker("job-1",{"--prompt","hello"},cancellation,{},fake).state==QueueState::Completed && receivedRequest,
            "Injected worker transport did not receive and complete the actual request");
        WorkerTransport broken=[](std::string_view,std::stop_token,const WorkerReceiver &receive) -> ii::core::Result<Exit> {
            receive(Stream::error,"Process pipe failed after this diagnostic\n");
            return ii::core::fail(ii::core::ErrorCode::failed,"pipe closed");
        };
        const auto interrupted=executeWorker("job-1",{},cancellation,{},broken);
        check(interrupted.state==QueueState::Failed && interrupted.error.find("pipe closed")!=std::string::npos &&
            interrupted.error.find("Process pipe failed")!=std::string::npos,"Transport failure discarded diagnostic evidence");
        check(interrupted.evidence.originalError.find("pipe closed")!=std::string::npos &&
            interrupted.evidence.workerLog.find("Process pipe failed")!=std::string::npos &&
            !interrupted.evidence.exitCode && !interrupted.evidence.deadlineExpired,
            "A transport failure invented process status or discarded its separate log");
        cancellation.request();receivedRequest=false;
        check(executeWorker("job-1",{},cancellation,{},fake).state==QueueState::Canceled && !receivedRequest,
            "Already canceled work entered the transport");
        QueueCancellation commandCancellation;
        const auto invalidCommand=processWorkerTransport(std::vector<std::string>{"relative-python","launcher.py"});
        const auto invalidLaunch=executeWorker("job-1",{},commandCancellation,{},invalidCommand);
        check(invalidLaunch.state==QueueState::Failed && invalidLaunch.error.find("absolute")!=std::string::npos,
            "relative executable command was accepted");

        QueueCancellation active;bool prepared=false,published=false;std::vector<vm::Value> committed;
        auto tracked=std::make_shared<TrackingStaging>();
        WorkerGenerationServices services;
        services.prepare=[&](const QueueRequest &input,const QueueCancellation &) -> ii::core::Result<WorkerJobPlan> {
            prepared=input.model=="model-a";
            return WorkerJobPlan{"job-1",{"--prompt","hello"},vm::Value(vm::Object{}),tracked};
        };
        services.transport=fake;
        services.publish=[&](const WorkerJobPlan &,const QueueCancellation &,const QueueOutputCallback &output) -> ii::core::Result<void> {
            published=true;output(vm::Value(vm::Object{{"path",vm::Value("/committed.png")}}));return {};
        };
        auto runner=makeWorkerGenerationRunner(services);
        auto outcome=runner({"Image","model-a",vm::Value(vm::Object{})},active,{},[&](vm::Value entry){committed.push_back(std::move(entry));});
        check(outcome.state==QueueState::Completed && prepared && published && committed.size()==1 &&
            tracked->calls==1 && tracked->last==QueueState::Completed,
            "Worker generation did not prepare, run and publish in order");
        services.transport=broken;published=false;committed.clear();
        outcome=makeWorkerGenerationRunner(services)({"Image","model-a",vm::Value(vm::Object{})},active,{},[&](vm::Value entry){committed.push_back(std::move(entry));});
        check(outcome.state==QueueState::Failed && !published && committed.empty() &&
            tracked->calls==2 && tracked->last==QueueState::Failed,"Failed inference was published as a result");
        services.transport=fake;services.publish=[](const WorkerJobPlan &,const QueueCancellation &,const QueueOutputCallback &output) -> ii::core::Result<void> {
            output(vm::Value(vm::Object{{"path",vm::Value("/first.png")}}));
            return ii::core::fail(ii::core::ErrorCode::failed,"Second output could not be published");
        };
        outcome=makeWorkerGenerationRunner(services)({"Image","model-a",vm::Value(vm::Object{})},active,{},[&](vm::Value entry){committed.push_back(std::move(entry));});
        check(outcome.state==QueueState::Failed && committed.size()==1 && outcome.error.find("Second output")!=std::string::npos,
            "Publication failure lost a previously committed output or reported success");
        services.publish=[](const WorkerJobPlan &,const QueueCancellation &,const QueueOutputCallback &) -> ii::core::Result<void> {return {};};
        outcome=makeWorkerGenerationRunner(services)({"Image","model-a",vm::Value(vm::Object{})},active,{},[&](vm::Value entry){committed.push_back(std::move(entry));});
        check(outcome.state==QueueState::Failed,"An empty publication was reported as a completed generation");
        QueueCancellation partialCancel;committed.clear();
        services.publish=[&](const WorkerJobPlan &,const QueueCancellation &,const QueueOutputCallback &output) -> ii::core::Result<void> {
            output(vm::Value(vm::Object{{"path",vm::Value("/first.png")}}));partialCancel.request();return {};
        };
        outcome=makeWorkerGenerationRunner(services)({"Image","model-a",vm::Value(vm::Object{})},partialCancel,{},[&](vm::Value entry){committed.push_back(std::move(entry));});
        check(outcome.state==QueueState::Canceled && committed.size()==1,"Cancellation discarded an already committed output");
        QueueCancellation afterWorker;published=false;
        tracked->onFinished=[&](QueueCompletion &result) {
            if (result.state==QueueState::Completed) afterWorker.request();
            if (result.state==QueueState::Canceled) result.error="Recoverable files: /fixture/output";
        };
        services.publish=[&](const auto &,const auto &,const auto &) -> ii::core::Result<void> {published=true;return {};};
        outcome=makeWorkerGenerationRunner(services)({"Image","model-a",vm::Value(vm::Object{})},afterWorker,{},[](auto){});
        check(outcome.state==QueueState::Canceled && !published && outcome.error.find("/fixture/output")!=std::string::npos,
            "cancellation after inference hid the recovery location");
        QueueCancellation duringPublication;committed.clear();tracked->onFinished={};
        services.publish=[&](const auto &,const auto &,const auto &output) -> ii::core::Result<void> {
            output(vm::Value(vm::Object{{"path",vm::Value("/first.png")}}));duringPublication.request();
            return ii::core::fail(ii::core::ErrorCode::cancelled,"Recoverable files: /fixture/remaining");
        };
        outcome=makeWorkerGenerationRunner(services)({"Image","model-a",vm::Value(vm::Object{})},duringPublication,{},
            [&](auto value){committed.push_back(std::move(value));});
        check(outcome.state==QueueState::Canceled && committed.size()==1 && outcome.error.find("/fixture/remaining")!=std::string::npos,
            "cancellation during publication lost a committed output or its recovery location");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
