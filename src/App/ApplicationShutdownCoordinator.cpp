#include "ApplicationShutdownCoordinator.h"
#include <iiSDKCore>
#include <atomic>
#include <exception>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

struct ApplicationShutdownCoordinator::State final {
    explicit State(ii::runtime::EventLoop &loop) : sender(loop.sender()) {}
    const std::thread::id owner=std::this_thread::get_id();
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    std::vector<Task> tasks;
    std::vector<Finished> waiters;
    std::atomic_size_t pending{0};
    std::optional<ii::core::Error> failure;
    std::atomic_bool closing{false};
    bool dispatching=false,finished=false;

    void complete() noexcept {
        if (pending.fetch_sub(1,std::memory_order_acq_rel)==1)
            (void)sender.signal(wake,ii::runtime::Clock::now());
    }
    void deliver() {
        if (finished || dispatching || pending.load(std::memory_order_acquire)!=0) return;
        finished=true;
        (void)sender.cancel(wake);
        auto callbacks=std::move(waiters);
        for (auto &callback:callbacks) if (callback) try {
            if (failure) callback(std::unexpected(*failure)); else callback({});
        } catch (...) {}
    }
};

ApplicationShutdownCoordinator::ApplicationShutdownCoordinator(ii::runtime::EventLoop &loop)
    :state_(std::make_shared<State>(loop)) {}
ApplicationShutdownCoordinator::~ApplicationShutdownCoordinator() {
    if (state_) (void)state_->sender.cancel(state_->wake);
}
bool ApplicationShutdownCoordinator::isOwnerThread() const noexcept {
    return state_->owner==std::this_thread::get_id();
}
bool ApplicationShutdownCoordinator::isClosing() const noexcept {return state_->closing;}
bool ApplicationShutdownCoordinator::add(Task task) {
    if (!isOwnerThread() || state_->closing || !task) return false;
    try {state_->tasks.push_back(std::move(task));return true;} catch (...) {return false;}
}
ii::core::Result<void> ApplicationShutdownCoordinator::begin(Finished callback) {
    using ii::core::ErrorCode;
    auto state=state_;
    if (!isOwnerThread()) return ii::core::fail(ErrorCode::wrong_executor,"Shutdown requires its owner thread");
    if (state->finished) {
        if (callback) try {if (state->failure) callback(std::unexpected(*state->failure));else callback({});} catch (...) {}
        return {};
    }
    if (callback) try {state->waiters.push_back(std::move(callback));}
        catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot register shutdown completion",std::current_exception());}
    if (state->closing) return {};
    if (state->tasks.empty()) {
        state->closing=true;
        state->deliver();
        return {};
    }
    const std::weak_ptr<State> weak=state;
    auto wake=state->sender.on_signal([weak] {if (auto owner=weak.lock()) owner->deliver();});
    if (!wake) {state->waiters.clear();return ii::core::fail(ErrorCode::busy,"Cannot reserve shutdown event");}
    state->wake=wake.token;
    state->closing=true;state->dispatching=true;
    auto tasks=std::move(state->tasks);
    state->pending.store(tasks.size(),std::memory_order_release);
    for (auto &task:tasks) {
        auto called=std::make_shared<std::atomic_bool>(false);
        auto done=[weak,called] {
            if (called->exchange(true,std::memory_order_acq_rel)) return;
            if (auto owner=weak.lock()) owner->complete();
        };
        try {task(done);}
        catch (...) {
            if (!state->failure) state->failure=ii::core::Error{ErrorCode::failed,"Shutdown task failed",std::current_exception()};
            done();
        }
    }
    state->dispatching=false;
    state->deliver();
    return {};
}
