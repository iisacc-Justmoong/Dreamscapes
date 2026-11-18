#pragma once

#include <iiSDKCore>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::native {
// The owner thread registers and retires application services. Core's
// Application controls when stop is called; this object does not own a loop.
class NativeApplicationLifecycle final {
  public:
    using Handler = std::function<void()>;
    NativeApplicationLifecycle() = default;
    NativeApplicationLifecycle(const NativeApplicationLifecycle &) = delete;
    NativeApplicationLifecycle &operator=(const NativeApplicationLifecycle &) = delete;

    [[nodiscard]] ii::core::Result<void> add(std::string name, Handler);
    [[nodiscard]] ii::core::Result<void> stop();
    [[nodiscard]] bool isClosing() const noexcept { return phase_ != Phase::accepting; }

  private:
    enum class Phase { accepting, stopping, stopped };
    struct Entry final { std::string name; Handler handler; };
    const std::thread::id owner_ = std::this_thread::get_id();
    Phase phase_ = Phase::accepting;
    std::vector<Entry> handlers_;
    std::optional<ii::core::Error> failure_;
};
} // namespace dreamscapes::native
