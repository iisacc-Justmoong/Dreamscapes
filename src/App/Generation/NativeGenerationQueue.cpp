#include "NativeGenerationQueue.h"
#include <iiSDKCore>
#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace dreamscapes::generation {
using ii::core::ErrorCode;
namespace {
constexpr std::size_t reportLimit=65536;
constexpr std::string_view omitted="\n[... report truncated ...]\n";
std::string preserveEnds(std::string_view value,std::size_t limit,bool utf8) {
    if (value.size()<=limit) return std::string(value);
    const auto half=(limit-omitted.size())/2;
    auto prefix=half,suffix=value.size()-(limit-omitted.size()-half);
    if (utf8) {
        while (prefix && (static_cast<unsigned char>(value[prefix])&0xc0)==0x80) --prefix;
        while (suffix<value.size() && (static_cast<unsigned char>(value[suffix])&0xc0)==0x80) ++suffix;
    }
    return std::string(value.substr(0,prefix))+std::string(omitted)+std::string(value.substr(suffix));
}
std::string safeFailure(std::string_view value) {
    auto bounded=preserveEnds(value,reportLimit,false);
    auto utf16=ii::core::to_utf16(bounded,ii::core::TextPolicy::replace);
    if (!utf16) return "Generation failed; the report could not be decoded";
    for (auto &character:*utf16) if (character==u'\0') character=u'\ufffd';
    auto utf8=ii::core::to_utf8(*utf16,ii::core::TextPolicy::replace);
    if (!utf8) return "Generation failed; the report could not be decoded";
    return preserveEnds(*utf8,reportLimit,true);
}
}

struct Queue::Bridge final {
    struct Event final {
        enum class Kind { Progress, Output, Finished } kind;
        std::string id;
        QueueProgress progress;
        vm::Value output;
        QueueCompletion completion;
    };
    struct Run final {bool accepting=true;}; // protected by mutex
    std::mutex mutex;
    std::vector<Event> events;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};

Queue::Queue(ii::runtime::EventLoop::Sender sender,Submit submit,QueueRunner runner)
    :sender_(std::move(sender)),submit_(std::move(submit)),runner_(std::move(runner)) {
    if (!submit_ || !runner_) throw std::invalid_argument("Generation queue requires a worker and provider");
}
std::shared_ptr<Queue> Queue::create(ii::runtime::EventLoop &loop,Submit submit,QueueRunner runner) {
    auto result=std::shared_ptr<Queue>(new Queue(loop.sender(),std::move(submit),std::move(runner)));
    result->start();return result;
}
void Queue::start() {
    const std::weak_ptr<Queue> weak=shared_from_this();
    const auto registered=sender_.on_signal([weak] {if (auto self=weak.lock()) self->deliver();});
    if (!registered) throw std::runtime_error("Cannot register generation queue completion");
    wake_=registered.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
}
Queue::~Queue() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
ii::core::Result<void> Queue::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Generation queue requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Generation queue is closed");
    return {};
}
ii::core::Result<Queue::Job> Queue::enqueue(QueueRequest request) {
    if (auto valid=writable();!valid) return std::unexpected(valid.error());
    if ((request.media!="Image" && request.media!="Video") || request.model.empty() || request.model.size()>4096 ||
        request.model.find('\0')!=std::string::npos || request.modelLabel.size()>4096 ||
        request.modelLabel.find('\0')!=std::string::npos || request.parameters.kind()!=ii::core::ValueKind::object)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid generation request");
    if (jobs.get().size()>=10000 || next_==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Generation queue is full");
    auto job=std::make_shared<QueueJob>(std::to_string(++next_),std::move(request));
    auto next=jobs.get();next.push_back(job);(void)jobs.set(std::move(next));
    if (active_) pump();
    return job;
}
ii::core::Result<void> Queue::cancel(std::string_view id) {
    if (auto valid=writable();!valid) return valid;
    const auto found=std::ranges::find_if(jobs.get(),[&](const auto &job){return job->id==id;});
    if (found==jobs.get().end()) return ii::core::fail(ErrorCode::invalid_argument,"Unknown generation job");
    const auto job=*found;
    if (job->state.get()==QueueState::Queued) {(void)job->state.set(QueueState::Canceled);if (active_) pump();return {};}
    if (running_==job && job->state.get()==QueueState::Running) {
        if (canceled_) canceled_->request();
        if (!launched_) {
            running_.reset();canceled_.reset();(void)job->state.set(QueueState::Canceled);
            if (active_) pump();
        }
    }
    return {};
}
void Queue::pump() {
    if (!active_ || pumping_) return;
    struct Reset final {bool &value;~Reset(){value=false;}} reset{pumping_};pumping_=true;
    while (active_ && !running_) {
        const auto found=std::ranges::find_if(jobs.get(),[](const auto &job){return job->state.get()==QueueState::Queued;});
        if (found==jobs.get().end()) return;
        auto job=*found;running_=job;canceled_=std::make_shared<QueueCancellation>();launched_=false;
        (void)job->state.set(QueueState::Running);
        if (!active_ || running_!=job) continue;
        auto bridge=bridge_;auto canceled=canceled_;auto runner=runner_;
        auto state=std::make_shared<Bridge::Run>();
        const auto publish=[bridge,state,id=job->id](Bridge::Event event) {
            event.id=id;
            {
                std::lock_guard lock(bridge->mutex);
                if (bridge->closed || !state->accepting) return;
                if (event.kind==Bridge::Event::Kind::Progress && !bridge->events.empty() &&
                    bridge->events.back().kind==Bridge::Event::Kind::Progress && bridge->events.back().id==id)
                    bridge->events.back()=std::move(event);
                else bridge->events.push_back(std::move(event));
            }
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        };
        auto task=[bridge,state,canceled,runner=std::move(runner),request=job->request,id=job->id,publish] {
            const QueueProgressCallback progress=[publish](QueueProgress value) {
                if (value.stage.empty() || value.stage.size()>128 || !ii::core::valid_utf8(value.stage) ||
                    std::ranges::any_of(value.stage,[](unsigned char c){return c<0x20 || c==0x7f;}) || value.completed<0 || value.total<0 ||
                    (value.total && value.completed>value.total)) return;
                Bridge::Event event;event.kind=Bridge::Event::Kind::Progress;event.progress=std::move(value);publish(std::move(event));
            };
            const QueueOutputCallback output=[publish](vm::Value value) {
                if (value.kind()!=ii::core::ValueKind::object) return;
                Bridge::Event event;event.kind=Bridge::Event::Kind::Output;event.output=std::move(value);publish(std::move(event));
            };
            QueueCompletion result;
            try {result=runner(request,*canceled,progress,output);}
            catch (const std::exception &error) {result={canceled->load()?QueueState::Canceled:QueueState::Failed,error.what()};}
            catch (...) {result={canceled->load()?QueueState::Canceled:QueueState::Failed,"Generation provider stopped unexpectedly"};}
            if (result.state!=QueueState::Completed && result.state!=QueueState::Failed && result.state!=QueueState::Canceled)
                result={QueueState::Failed,"Generation provider returned an invalid completion state"};
            if (result.state==QueueState::Failed && result.error.empty()) result.error="Generation failed without an explanation";
            if (result.state==QueueState::Completed) result.error.clear();
            else if (!result.error.empty()) result.error=safeFailure(result.error);
            if (!result.evidence.originalError.empty()) result.evidence.originalError=safeFailure(result.evidence.originalError);
            if (!result.evidence.workerLog.empty()) result.evidence.workerLog=safeFailure(result.evidence.workerLog);
            {
                std::lock_guard lock(bridge->mutex);
                if (bridge->closed) {state->accepting=false;return;}
                state->accepting=false;
                Bridge::Event event;event.kind=Bridge::Event::Kind::Finished;event.id=id;event.completion=std::move(result);
                bridge->events.push_back(std::move(event));
            }
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        };
        bool accepted=false;launched_=true;
        try {accepted=submit_(std::move(task));} catch (...) {accepted=false;}
        if (!active_) return;
        if (!accepted && running_==job) {
            {std::lock_guard lock(bridge->mutex);state->accepting=false;}
            running_.reset();canceled_.reset();launched_=false;
            (void)job->error.set("Generation worker is unavailable");
            if (active_) (void)job->state.set(QueueState::Failed);
        }
    }
}
void Queue::deliver() {
    if (!active_) return;
    std::vector<Bridge::Event> events;
    {std::lock_guard lock(bridge_->mutex);events.swap(bridge_->events);}
    for (auto &event:events) {
        if (!active_) return;
        const auto job=running_;
        if (!job || event.id!=job->id) continue;
        if (event.kind==Bridge::Event::Kind::Progress) (void)job->progress.set(std::move(event.progress));
        else if (event.kind==Bridge::Event::Kind::Output) {
            auto next=outputs.get();next.push_back({job->id,std::move(event.output)});(void)outputs.set(std::move(next));
        } else {
            (void)job->evidence.set(std::move(event.completion.evidence));
            if (!active_ || running_!=job) return;
            (void)job->error.set(std::move(event.completion.error));
            if (!active_ || running_!=job) return;
            (void)job->state.set(event.completion.state);
            if (!active_ || running_!=job) return;
            running_.reset();canceled_.reset();launched_=false;
            pump();
        }
    }
}
void Queue::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Generation queue requires its owner thread");
    if (!active_) return;
    active_=false;if (canceled_) canceled_->request();
    {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->events.clear();}
    (void)sender_.cancel(wake_);submit_={};runner_={};running_.reset();canceled_.reset();launched_=false;
    for (const auto &job:jobs.get()) if (job->state.get()==QueueState::Queued || job->state.get()==QueueState::Running)
        (void)job->state.set(QueueState::Canceled);
}
}
