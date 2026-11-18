#pragma once

#include "NativeGenerationQueue.h"
#include <iiSDKCore>
#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dreamscapes::generation {
// One worker request. All methods run on the process-owning worker thread.
// Progress and result records are accepted only from stdout; stderr and other
// output are retained as bounded diagnostic evidence.
class WorkerProtocol final {
  public:
    static constexpr std::size_t maximumRequest=1024*1024;
    static constexpr std::size_t maximumLine=65536;
    static constexpr std::size_t maximumDiagnostics=16384;
    explicit WorkerProtocol(std::string id,QueueProgressCallback);
    [[nodiscard]] static ii::core::Result<std::string> request(std::string_view id,const std::vector<std::string> &arguments);
    void receive(ii::process::Stream,std::string_view bytes);
    [[nodiscard]] QueueCompletion finish(ii::process::Exit);
  private:
    struct Input {std::string line;bool oversized=false;};
    void line(ii::process::Stream,std::string_view);
    void diagnostic(std::string_view);
    void failure(std::string);
    const std::string id_;
    QueueProgressCallback progress_;
    std::array<Input,2> streams_;
    std::string diagnostics_,protocolError_,resultError_;
    std::optional<std::int64_t> resultExit_;
    bool ready_=false,result_=false,resultOk_=false,finished_=false;
};

// The transport is synchronous and must stop invoking receive before it
// returns. This permits deterministic tests without launching a process.
using WorkerReceiver=std::function<void(ii::process::Stream,std::string_view)>;
using WorkerTransport=std::function<ii::core::Result<ii::process::Exit>(std::string_view,std::stop_token,const WorkerReceiver &)>;
[[nodiscard]] QueueCompletion executeWorker(std::string_view id,const std::vector<std::string> &arguments,
    const QueueCancellation &,QueueProgressCallback,const WorkerTransport &);
// The returned transport owns only the child it starts, and never invokes a shell.
[[nodiscard]] WorkerTransport processWorkerTransport(std::filesystem::path executable,std::filesystem::path workingDirectory={});
// A command may include an interpreter and a script, without invoking a shell.
[[nodiscard]] WorkerTransport processWorkerTransport(std::vector<std::string> command,std::filesystem::path workingDirectory={});

struct WorkerStaging {
    virtual ~WorkerStaging()=default;
    // May be called again if cancellation happens between inference and publication.
    // Called once even on worker failure or cancellation, before publication.
    virtual void workerFinished(QueueCompletion &) noexcept {}
};
struct WorkerJobPlan final {
    std::string id;
    std::vector<std::string> arguments;
    vm::Value context;
    // Provider-owned staging lifetime. Destruction must not remove public outputs.
    std::shared_ptr<WorkerStaging> staging;
};
struct WorkerGenerationServices final {
    std::function<ii::core::Result<WorkerJobPlan>(const QueueRequest &,const QueueCancellation &)> prepare;
    WorkerTransport transport;
    // Each callback follows an independently committed output; a later failure
    // must not remove or conceal earlier commits.
    std::function<ii::core::Result<void>(const WorkerJobPlan &,const QueueCancellation &,const QueueOutputCallback &)> publish;
};
[[nodiscard]] QueueRunner makeWorkerGenerationRunner(WorkerGenerationServices);
}
