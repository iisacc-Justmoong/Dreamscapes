#pragma once
#include <functional>
#include <memory>

namespace dreamscapes::native {
// Implementations own a single external surface. A successful start retains
// its completion until the surface has actually retired. The callback reports
// an asynchronous presentation failure; it may be delivered synchronously.
class NativeOverlaySurface {
public:
    virtual ~NativeOverlaySurface() = default;
    virtual bool start(std::function<void(bool failed)> closed) = 0;
    virtual void requestClose() noexcept = 0;
};

// Owner-thread state machine. Input changes merely schedule one reconciliation;
// no idle timer, polling, worker or native surface exists without a request.
class NativeOverlayCoordinator final {
public:
    using Task = std::function<void()>;
    using Factory = std::function<std::shared_ptr<NativeOverlaySurface>()>;
    using Scheduler = std::function<void(Task)>;
    NativeOverlayCoordinator(Factory, Scheduler, Task fallback);
    ~NativeOverlayCoordinator();
    NativeOverlayCoordinator(const NativeOverlayCoordinator&) = delete;
    NativeOverlayCoordinator& operator=(const NativeOverlayCoordinator&) = delete;
    void setWanted(bool);
    // Completes only after a live surface's close callback. Repeated calls are
    // allowed; no new surface is admitted once shutdown starts.
    void shutdown(Task completed = {});
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
