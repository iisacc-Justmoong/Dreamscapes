#pragma once

#include <iiSDKCore>
#include <functional>
#include <memory>

namespace ii::runtime { class EventLoop; }

// Native owner-thread admission with asynchronous, exactly-once completions.
// The Core event loop must remain alive and dispatch until shutdown completes.
class ApplicationShutdownCoordinator final {
  public:
    using Completion = std::function<void()>;
    using Task = std::function<void(Completion)>;
    using Finished = std::function<void(ii::core::Result<void>)>;

    explicit ApplicationShutdownCoordinator(ii::runtime::EventLoop &);
    ~ApplicationShutdownCoordinator();
    ApplicationShutdownCoordinator(const ApplicationShutdownCoordinator &) = delete;
    ApplicationShutdownCoordinator &operator=(const ApplicationShutdownCoordinator &) = delete;

    [[nodiscard]] bool add(Task);
    [[nodiscard]] ii::core::Result<void> begin(Finished = {});
    [[nodiscard]] bool isClosing() const noexcept;
    [[nodiscard]] bool isOwnerThread() const noexcept;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
