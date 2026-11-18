#include "NativeWorkerProtocol.h"
#include <iiJson>
#include <iiSDKCore>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace dreamscapes::generation {
namespace {
using ii::core::ErrorCode;
std::optional<std::int64_t> integer(const vm::Value &value) {
    if (const auto *v=std::get_if<std::int64_t>(&value.storage())) return *v;
    if (const auto *v=std::get_if<std::uint64_t>(&value.storage());v && *v<=std::uint64_t(std::numeric_limits<std::int64_t>::max()))
        return static_cast<std::int64_t>(*v);
    return {};
}
bool validId(std::string_view value) {
    return !value.empty() && value.size()<=128 && std::ranges::all_of(value,[](unsigned char c) {
        return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.' || c==':';
    });
}
std::string bounded(std::string text,std::size_t limit) {
    if (text.size()>limit) text.resize(limit);
    return text;
}
std::string safeText(std::string_view text,std::size_t limit,bool keepTail) {
    auto converted=ii::core::to_utf16(text,ii::core::TextPolicy::replace);
    if (!converted) return {};
    for (auto &character:*converted) if (character==u'\0') character=u'\ufffd';
    auto utf8=ii::core::to_utf8(*converted,ii::core::TextPolicy::replace);
    if (!utf8) return {};
    if (utf8->size()>limit) {
        if (keepTail) {
            auto begin=utf8->size()-limit;
            while (begin<utf8->size() && (static_cast<unsigned char>((*utf8)[begin])&0xc0)==0x80) ++begin;
            utf8->erase(0,begin);
        } else {
            auto end=limit;
            while (end && (static_cast<unsigned char>((*utf8)[end])&0xc0)==0x80) --end;
            utf8->resize(end);
        }
    }
    return std::move(*utf8);
}
}
WorkerProtocol::WorkerProtocol(std::string id,QueueProgressCallback progress)
    :id_(std::move(id)),progress_(std::move(progress)) {
    if (!validId(id_)) failure("Invalid worker request identity");
}
ii::core::Result<std::string> WorkerProtocol::request(std::string_view id,const std::vector<std::string> &arguments) {
    if (!validId(id)) return ii::core::fail(ErrorCode::invalid_argument,"Invalid worker request identity");
    if (arguments.size()>4096) return ii::core::fail(ErrorCode::invalid_argument,"Worker request has too many arguments");
    vm::Array values;values.reserve(arguments.size());
    std::size_t total=id.size();
    for (const auto &value:arguments) {
        if (value.find('\0')!=value.npos || !ii::core::valid_utf8(value) || value.size()>maximumRequest-total)
            return ii::core::fail(ErrorCode::invalid_argument,"Invalid or oversized worker argument");
        total+=value.size();values.emplace_back(value);
    }
    auto encoded=ii::json::stringify(vm::Value(vm::Object{{"schema",vm::Value("iild-worker-request-v1")},
        {"id",vm::Value(std::string(id))},{"arguments",vm::Value(std::move(values))}}));
    if (!encoded || encoded->size()>=maximumRequest)
        return ii::core::fail(ErrorCode::invalid_argument,"Worker request exceeds the protocol limit");
    encoded->push_back('\n');return std::move(*encoded);
}
void WorkerProtocol::diagnostic(std::string_view text) {
    if (text.size()>=maximumDiagnostics) diagnostics_.assign(text.substr(text.size()-maximumDiagnostics));
    else {
        if (diagnostics_.size()+text.size()>maximumDiagnostics)
            diagnostics_.erase(0,diagnostics_.size()+text.size()-maximumDiagnostics);
        diagnostics_.append(text);
    }
}
void WorkerProtocol::failure(std::string message) {if (protocolError_.empty()) protocolError_=std::move(message);}
void WorkerProtocol::receive(ii::process::Stream stream,std::string_view bytes) {
    if (finished_) return;
    auto &input=streams_[stream==ii::process::Stream::output?0:1];
    while (!bytes.empty()) {
        const auto end=bytes.find('\n');const auto part=bytes.substr(0,end);
        if (!input.oversized && part.size()<=maximumLine-input.line.size()) input.line.append(part);
        else {
            if (!input.oversized) {diagnostic(input.line);input.line.clear();input.oversized=true;}
            diagnostic(part);
        }
        if (end==bytes.npos) break;
        if (input.oversized) diagnostic("\n");
        else line(stream,input.line);
        input.line.clear();input.oversized=false;bytes.remove_prefix(end+1);
    }
}
void WorkerProtocol::line(ii::process::Stream stream,std::string_view text) {
    if (!text.empty() && text.back()=='\r') text.remove_suffix(1);
    if (stream==ii::process::Stream::error) {diagnostic(text);diagnostic("\n");return;}
    constexpr std::string_view ready="IILD_READY ",result="IILD_RESULT ";
    if (text.starts_with(ready)) {
        auto value=ii::json::parse(text.substr(ready.size()));
        if (!value || vm::text(vm::get(*value,"schema"))!="iild-worker-v1" || ready_ || result_)
            failure("Invalid worker readiness message");
        else ready_=true;
        return;
    }
    if (text.starts_with(result)) {
        auto value=ii::json::parse(text.substr(result.size()));
        if (!value || !ready_ || result_ || vm::text(vm::get(*value,"schema"))!="iild-worker-result-v1" ||
            vm::text(vm::get(*value,"id"))!=id_ || vm::get(*value,"ok").kind()!=ii::core::ValueKind::boolean ||
            !integer(vm::get(*value,"exit_code"))) {
            failure("Invalid or mismatched worker result");return;
        }
        result_=true;resultOk_=vm::boolean(vm::get(*value,"ok"));resultExit_=integer(vm::get(*value,"exit_code"));
        resultError_=bounded(vm::text(vm::get(*value,"error")),4096);return;
    }
    struct Marker {std::string_view prefix,schema;bool loading;};
    static constexpr Marker markers[]{
        {"IILD_MODEL_PROGRESS ","iild-model-progress-v1",true},
        {"IILD_NATIVE_PROGRESS ","iild-native-progress-v1",false},
        {"IILD_DIFFUSERS_PROGRESS ","iild-diffusers-progress-v1",false},
        {"IILD_VIDEO_PROGRESS ","iild-video-progress-v1",false}};
    for (const auto &marker:markers) if (text.starts_with(marker.prefix)) {
        auto value=ii::json::parse(text.substr(marker.prefix.size()));
        if (!value || !ready_ || result_ || vm::text(vm::get(*value,"schema"))!=marker.schema) return;
        const auto stage=marker.loading?std::string("loading"):vm::text(vm::get(*value,"stage"));
        const auto completed=integer(vm::get(*value,marker.loading?"completed_bytes":"step"));
        const auto total=integer(vm::get(*value,marker.loading?"total_bytes":"total"));
        if (stage.empty() || stage.size()>128 || std::ranges::any_of(stage,[](unsigned char c){return c<0x20 || c==0x7f;}) ||
            !completed || !total || *completed<0 || *total<0 ||
            (*total && *completed>*total)) return;
        if (progress_) progress_({stage,*completed,*total});
        return;
    }
    diagnostic(text);diagnostic("\n");
}
QueueCompletion WorkerProtocol::finish(ii::process::Exit exit) {
    if (finished_) return {QueueState::Failed,"Worker result was already consumed"};
    finished_=true;
    for (auto &input:streams_) if (!input.line.empty()) {diagnostic(input.line);input.line.clear();}
    if (exit.reason==ii::process::ExitReason::cancelled) return {QueueState::Canceled,{}};
    std::string error;
    if (!protocolError_.empty()) error=protocolError_;
    else if (exit.reason==ii::process::ExitReason::timed_out) error="The generation worker timed out";
    else if (exit.reason!=ii::process::ExitReason::exited) error="The generation worker terminated before completion";
    else if (exit.code!=0) error="The generation worker exited with code "+std::to_string(exit.code);
    else if (!ready_) error="The generation worker did not report readiness";
    else if (!result_) error="The generation worker exited without a matching result";
    else if (!resultOk_ || !resultExit_ || *resultExit_!=0)
        error=resultError_.empty()?"The generation worker reported failure":resultError_;
    else return {QueueState::Completed,{}};
    if (!resultError_.empty() && error!=resultError_) error+="\n"+resultError_;
    error=safeText(error,8192,false);
    QueueDiagnosticEvidence evidence;
    evidence.originalError=error;
    evidence.workerLog=safeText(diagnostics_,maximumDiagnostics,true);
    if (exit.reason==ii::process::ExitReason::exited) {
#ifdef _WIN32
        // Older iiSDKCore releases represented a Win32 DWORD as signed int.
        evidence.exitCode=static_cast<std::uint32_t>(exit.code);
#else
        evidence.exitCode=exit.code;
#endif
        evidence.deadlineExpired=false;
    } else if (exit.reason==ii::process::ExitReason::timed_out) evidence.deadlineExpired=true;
    if (!evidence.workerLog.empty()) error+="\n\nWORKER LOG (tail)\n"+evidence.workerLog;
    return {QueueState::Failed,std::move(error),std::move(evidence)};
}
QueueCompletion executeWorker(std::string_view id,const std::vector<std::string> &arguments,
    const QueueCancellation &cancellation,QueueProgressCallback progress,const WorkerTransport &transport) {
    if (cancellation.load()) return {QueueState::Canceled,{}};
    auto request=WorkerProtocol::request(id,arguments);
    if (!request) return {QueueState::Failed,request.error().message};
    if (!transport) return {QueueState::Failed,"Generation worker transport is unavailable"};
    WorkerProtocol protocol(std::string(id),std::move(progress));
    const WorkerReceiver receive=[&](ii::process::Stream stream,std::string_view bytes) {protocol.receive(stream,bytes);};
    try {
        auto result=transport(*request,cancellation.token(),receive);
        if (cancellation.load()) return {QueueState::Canceled,{}};
        if (result) return protocol.finish(*result);
        auto failed=protocol.finish({ii::process::ExitReason::owner_removed,0,0});
        failed.error="Generation worker communication failed: "+safeText(result.error().message,4096,false)+"\n"+failed.error;
        failed.evidence.originalError="Generation worker communication failed: "+safeText(result.error().message,4096,false)+"\n"+failed.evidence.originalError;
        return failed;
    } catch (const std::exception &error) {
        if (cancellation.load()) return {QueueState::Canceled,{}};
        auto failed=protocol.finish({ii::process::ExitReason::owner_removed,0,0});
        failed.error="Generation worker communication failed: "+safeText(error.what(),4096,false)+"\n"+failed.error;
        failed.evidence.originalError="Generation worker communication failed: "+safeText(error.what(),4096,false)+"\n"+failed.evidence.originalError;
        return failed;
    } catch (...) {
        if (cancellation.load()) return {QueueState::Canceled,{}};
        auto failed=protocol.finish({ii::process::ExitReason::owner_removed,0,0});
        failed.error="Generation worker communication failed\n"+failed.error;
        failed.evidence.originalError="Generation worker communication failed\n"+failed.evidence.originalError;
        return failed;
    }
}
WorkerTransport processWorkerTransport(std::filesystem::path executable,std::filesystem::path workingDirectory) {
    auto path=ii::core::path_to_utf8(executable);
    return processWorkerTransport(path?std::vector<std::string>{std::move(*path)}:std::vector<std::string>{},std::move(workingDirectory));
}
WorkerTransport processWorkerTransport(std::vector<std::string> command,std::filesystem::path workingDirectory) {
    return [command=std::move(command),workingDirectory=std::move(workingDirectory)](
        std::string_view request,std::stop_token stop,const WorkerReceiver &receive) -> ii::core::Result<ii::process::Exit> {
        if (stop.stop_requested()) return ii::process::Exit{ii::process::ExitReason::cancelled,0,0};
        if (command.empty()) return ii::core::fail(ErrorCode::invalid_argument,"Worker executable must be an absolute path");
        auto executable=ii::core::path_from_utf8(command.front());
        if (!executable || !executable->is_absolute() || (!workingDirectory.empty() && !workingDirectory.is_absolute()))
            return ii::core::fail(ErrorCode::invalid_argument,"Worker executable and working directory must be absolute paths");
        for (const auto &argument:command) if (!ii::core::valid_utf8(argument) || argument.find('\0')!=argument.npos)
            return ii::core::fail(ErrorCode::invalid_argument,"Worker command contains invalid text");
        ii::process::Launch launch;
        launch.arguments=command;launch.arguments.emplace_back("--worker");launch.working_directory=workingDirectory;
        launch.environment={{"IILD_WORKER_PROGRESS","1"},{"IILD_MODEL_VALIDATION","metadata"},{"PYTHONDONTWRITEBYTECODE","1"}};
        auto process=ii::process::Process::start(launch);
        if (!process) return std::unexpected(process.error());
        ii::process::CommunicationOptions options;
        options.stop=stop;options.output_limit=0;options.error_limit=0;
        options.receive=[&](ii::process::Stream stream,ii::core::ByteView bytes) {
            if (receive) receive(stream,{reinterpret_cast<const char *>(bytes.data()),bytes.size()});
        };
        auto result=process->communicate(ii::core::byte_view(request),std::move(options));
        if (!result) return std::unexpected(result.error());
        if (result->exit.reason!=ii::process::ExitReason::cancelled &&
            (result->input_closed_early || result->input_written!=request.size()))
            return ii::core::fail(ErrorCode::failed,"Worker closed its input before receiving the complete request");
        return result->exit;
    };
}
QueueRunner makeWorkerGenerationRunner(WorkerGenerationServices services) {
    return [services=std::move(services)](const QueueRequest &request,const QueueCancellation &cancellation,
        const QueueProgressCallback &progress,const QueueOutputCallback &output) -> QueueCompletion {
        if (cancellation.load()) return {QueueState::Canceled,{}};
        if (!services.prepare || !services.transport || !services.publish || !output)
            return {QueueState::Failed,"Generation worker services are unavailable"};
        try {
            auto plan=services.prepare(request,cancellation);
            if (cancellation.load()) {
                QueueCompletion canceled{QueueState::Canceled,{}};
                if (plan && plan->staging) plan->staging->workerFinished(canceled);
                return canceled;
            }
            if (!plan) return {QueueState::Failed,"Cannot prepare generation: "+safeText(plan.error().message,4096,false)};
            auto completed=executeWorker(plan->id,plan->arguments,cancellation,progress,services.transport);
            if (plan->staging) plan->staging->workerFinished(completed);
            if (completed.state!=QueueState::Completed) return completed;
            if (cancellation.load()) {
                QueueCompletion canceled{QueueState::Canceled,{}};
                if (plan->staging) plan->staging->workerFinished(canceled);
                return canceled;
            }
            std::size_t count=0;
            const QueueOutputCallback committed=[&](vm::Value value) {output(std::move(value));++count;};
            auto published=services.publish(*plan,cancellation,committed);
            if (cancellation.load()) {
                QueueCompletion canceled{QueueState::Canceled,published?std::string{}:safeText(published.error().message,4096,false)};
                if (!published && plan->staging) plan->staging->workerFinished(canceled);
                return canceled;
            }
            if (!published) return {QueueState::Failed,"Cannot publish generation: "+safeText(published.error().message,4096,false)};
            if (!count) return {QueueState::Failed,"The generation provider returned no committed output"};
            return {QueueState::Completed,{}};
        } catch (const std::exception &error) {
            if (cancellation.load()) return {QueueState::Canceled,{}};
            return {QueueState::Failed,"Generation provider failed: "+safeText(error.what(),4096,false)};
        } catch (...) {
            return cancellation.load()?QueueCompletion{QueueState::Canceled,{}}:
                QueueCompletion{QueueState::Failed,"Generation provider failed"};
        }
    };
}
}
