#include "NativeFailureDiagnostics.h"
#include <iiJson>
#include <iiMultimodalAgent>
#include <iiSDKCore>

namespace dreamscapes::generation {
vm::Value nativeFailureReport(const QueueJob &job) {
    const auto progress=job.progress.get();
    const auto evidence=job.evidence.get();
    const auto model=job.request.modelLabel.empty()?job.request.model:job.request.modelLabel;
    if (job.state.get()==QueueState::Canceled) {
        std::string details="CANCELED\nGeneration was canceled. The following information was retained.\n\nOBSERVED FACTS\nJob: "+
            job.id+"\nOperation: "+job.request.media+"\nModel: "+model+"\nStage: "+
            (progress.stage.empty()?std::string("Not reported"):progress.stage)+"\nProgress: "+
            std::to_string(progress.completed)+" / "+(progress.total?std::to_string(progress.total):std::string("total unknown"))+
            "\n\nRECOVERY DETAILS\n";
        details+=job.error.get();
        if (const auto parameters=ii::json::stringify(job.request.parameters);parameters)
            details+="\n\nSUBMITTED SETTINGS\n"+*parameters;
        return vm::Value(vm::Object{{"details",vm::Value(std::move(details))}});
    }
    iiMultimodalAgent::diagnostics::FailureContext context;
    context.jobId=job.id;context.operation=job.request.media;context.stage=progress.stage;
    context.error=evidence.originalError.empty()?job.error.get():evidence.originalError;
    context.workerLog=evidence.workerLog;
    if (!progress.stage.empty()) {context.step=progress.completed;if (progress.total>0) context.total=progress.total;}
    if (evidence.exitCode) context.exitCode=*evidence.exitCode;
    context.timedOut=evidence.deadlineExpired.value_or(false);
    context.parameters["modelName"]=model;
    for (const auto &[key,value]:vm::object(job.request.parameters)) {
        if (auto serialized=ii::json::stringify(value);serialized) context.parameters[key]=std::move(*serialized);
    }
    auto report=iiMultimodalAgent::diagnostics::explain(context);
    if (!evidence.deadlineExpired) {
        constexpr std::string_view unknown="Deadline expired: no";
        if (const auto at=report.details.find(unknown);at!=std::string::npos)
            report.details.replace(at,unknown.size(),"Deadline expired: not reported");
    }
    // The SDK bounds individual fields; retain the complete already-bounded
    // provider error and submitted parameters for copying and investigation.
    if (context.error.size()>8192) report.details+="\nFULL PROVIDER ERROR\n"+context.error+"\n";
    if (job.error.get()!=context.error)
        report.details+="\nFULL PROVIDER DETAILS\n"+job.error.get()+"\n";
    if (const auto parameters=ii::json::stringify(job.request.parameters);parameters)
        report.details+="\nFULL SUBMITTED SETTINGS\n"+*parameters+"\n";
    // A bounded SDK tail may begin within a multibyte character.
    if (auto unicode=ii::core::to_utf16(report.details,ii::core::TextPolicy::replace);unicode) {
        for (auto &character:*unicode) if (character==u'\0') character=u'\ufffd';
        if (auto clean=ii::core::to_utf8(*unicode,ii::core::TextPolicy::replace);clean) report.details=std::move(*clean);
    }
    return vm::Value(vm::Object{{"details",vm::Value(std::move(report.details))},
        {"category",vm::Value(std::move(report.category))},{"summary",vm::Value(std::move(report.summary))},
        {"modelUnavailableReason",vm::Value("This report uses recorded evidence; no local language-model analysis was performed.")}});
}
}
