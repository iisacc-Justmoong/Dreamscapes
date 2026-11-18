#include "App/Generation/NativeFailureDiagnostics.h"
#include <iiSDKCore>
#include <iostream>
#include <stdexcept>

using namespace dreamscapes;
using namespace dreamscapes::generation;
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
int main() {
    try {
        QueueJob job("job-1",{"Image","model-id",vm::Value(vm::Object{{"prompt",vm::Value("한글 prompt")}}),"Model A"});
        job.state.set(QueueState::Failed);
        job.progress.set({"loading",11,0});
        job.error.set("generic failure");
        job.evidence.set({"get sd version from file failed","causal worker log",9,false});
        auto report=nativeFailureReport(job);
        auto details=vm::text(vm::get(report,"details"));
        check(vm::text(vm::get(report,"category"))=="model-identification" &&
            details.find("Progress: 11 / total unknown")!=std::string::npos &&
            details.find("Worker exit: 9")!=std::string::npos &&
            details.find("Deadline expired: no")!=std::string::npos &&
            details.find("causal worker log")!=std::string::npos &&
            details.find("generic failure")!=std::string::npos &&
            details.find("한글 prompt")!=std::string::npos,
            "native report did not project observed evidence and model identification");
        job.evidence.set({"unknown error",{},std::nullopt,std::nullopt});
        report=nativeFailureReport(job);details=vm::text(vm::get(report,"details"));
        check(vm::text(vm::get(report,"category"))=="unclassified" &&
            details.find("Worker exit: not reported")!=std::string::npos &&
            details.find("Deadline expired: not reported")!=std::string::npos,
            "unreported status was treated as an observed outcome");
        QueueDiagnosticEvidence windowsExit;
        windowsExit.originalError="worker failed";windowsExit.exitCode=std::int64_t{3221225477};
        job.evidence.set(windowsExit);report=nativeFailureReport(job);
        check(vm::text(vm::get(report,"details")).find("Worker exit: 3221225477")!=std::string::npos,
            "a Windows worker exit code was narrowed to a signed integer");
        job.evidence.set({std::string(8191,'a')+"한글 long error",{},std::nullopt,true});
        report=nativeFailureReport(job);details=vm::text(vm::get(report,"details"));
        check(ii::core::valid_utf8(details) && details.find("FULL PROVIDER ERROR")!=std::string::npos &&
            details.find("한글 long error")!=std::string::npos &&
            vm::text(vm::get(report,"category"))=="no-progress-timeout",
            "long multibyte error or timeout evidence was lost");
        job.state.set(QueueState::Canceled);job.error.set("Recoverable output");
        details=vm::text(vm::get(nativeFailureReport(job),"details"));
        check(details.find("CANCELED")!=std::string::npos && details.find("Recoverable output")!=std::string::npos &&
            details.find("Job: job-1")!=std::string::npos && details.find("Stage: loading")!=std::string::npos &&
            details.find("FAILURE SUMMARY")==std::string::npos,"cancellation was mislabeled as a failure");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
