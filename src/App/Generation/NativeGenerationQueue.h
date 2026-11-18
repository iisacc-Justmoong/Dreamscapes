#pragma once

#include "App/ViewModels/Native/ViewValue.h"
#include <iiSDKCore>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace dreamscapes::generation {

enum class QueueState { Queued, Running, Completed, Failed, Canceled };
struct QueueRequest final {
    std::string media,model;
    vm::Value parameters;
    std::string modelLabel;
    vm::Value modelSource; // immutable identity captured when the user submits
};
struct QueueProgress final {
    std::string stage;
    std::int64_t completed=0,total=0; // zero total means unknown
    bool operator==(const QueueProgress &) const = default;
};
struct QueueDiagnosticEvidence final {
    std::string originalError,workerLog;
    std::optional<std::int64_t> exitCode;
    std::optional<bool> deadlineExpired;
    bool operator==(const QueueDiagnosticEvidence &) const = default;
};
struct QueueCompletion final {QueueState state=QueueState::Failed;std::string error;QueueDiagnosticEvidence evidence;};
struct QueueOutput final {
    std::string jobId;vm::Value entry;
    bool operator==(const QueueOutput &) const = default;
};
using QueueProgressCallback=std::function<void(QueueProgress)>;
using QueueOutputCallback=std::function<void(vm::Value)>;
class QueueCancellation final {
  public:
    void request() noexcept {canceled_.store(true);(void)stop_.request_stop();}
    [[nodiscard]] bool load() const noexcept {return canceled_.load();}
    [[nodiscard]] const std::atomic_bool &flag() const noexcept {return canceled_;}
    [[nodiscard]] std::stop_token token() const noexcept {return stop_.get_token();}
  private:
    std::atomic_bool canceled_{false};
    std::stop_source stop_;
};
// Runner and its callbacks execute on a worker. An output is reported only
// after its provider has successfully committed it; cancellation never removes
// an already committed output.
using QueueRunner=std::function<QueueCompletion(const QueueRequest &,const QueueCancellation &,
    const QueueProgressCallback &,const QueueOutputCallback &)>;

struct QueueJob final {
    const std::string id;
    const QueueRequest request;
    ii::core::Observable<QueueState> state{QueueState::Queued};
    ii::core::Observable<QueueProgress> progress;
    ii::core::Observable<std::string> error;
    ii::core::Observable<QueueDiagnosticEvidence> evidence;
    QueueJob(std::string identifier,QueueRequest value):id(std::move(identifier)),request(std::move(value)) {}
};

// Session-scoped, serial coordinator. It owns no GUI, document, model file,
// or engine. Providers and worker lifetimes are supplied by the application.
class Queue final : public std::enable_shared_from_this<Queue> {
  public:
    using Submit=std::function<bool(std::function<void()>)>;
    using Job=std::shared_ptr<QueueJob>;
    static std::shared_ptr<Queue> create(ii::runtime::EventLoop &,Submit,QueueRunner);
    ~Queue();
    [[nodiscard]] ii::core::Result<Job> enqueue(QueueRequest);
    [[nodiscard]] ii::core::Result<void> cancel(std::string_view);
    void close();
    ii::core::Observable<std::vector<Job>> jobs;
    ii::core::Observable<std::vector<QueueOutput>> outputs;
  private:
    struct Bridge;
    Queue(ii::runtime::EventLoop::Sender,Submit,QueueRunner);
    void start();
    void pump();
    void deliver();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    ii::runtime::EventLoop::Sender sender_;
    ii::runtime::TaskToken wake_;
    Submit submit_;
    QueueRunner runner_;
    std::shared_ptr<Bridge> bridge_;
    Job running_;
    std::shared_ptr<QueueCancellation> canceled_;
    std::uint64_t next_=0;
    bool active_=true,pumping_=false,launched_=false;
};
}
