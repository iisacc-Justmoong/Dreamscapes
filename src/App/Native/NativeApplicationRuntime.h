#pragma once

#include <iiSDKCore>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <stop_token>

namespace dreamscapes::native {
enum class RuntimeLane { general, generation, publication, audio, preferences };
[[nodiscard]] std::string describeApplicationError(const ii::core::Error &);

// The Core Application must outlive this bridge and its borrowed executors.
class NativeApplicationRuntime final : public std::enable_shared_from_this<NativeApplicationRuntime> {
  public:
    static std::shared_ptr<NativeApplicationRuntime> create(ii::runtime::Application &, std::function<void()> onStop = {});
    ~NativeApplicationRuntime();
    NativeApplicationRuntime(const NativeApplicationRuntime &) = delete;
    NativeApplicationRuntime &operator=(const NativeApplicationRuntime &) = delete;

    [[nodiscard]] ii::core::Result<void> attach();
    [[nodiscard]] bool post(RuntimeLane, ii::runtime::Task);
    void requestStop() noexcept;
    [[nodiscard]] ii::runtime::EventLoop &eventLoop() const noexcept;
    [[nodiscard]] ii::runtime::Runtime &workers() const noexcept;

  private:
    explicit NativeApplicationRuntime(ii::runtime::Application &, std::function<void()>);
    [[nodiscard]] ii::core::Result<void> finish();
    ii::runtime::Application &application_;
    ii::runtime::SerialQueue generation_, publication_, audio_;
    ii::runtime::Runtime preferences_;
    std::function<void()> onStop_;
    std::unique_ptr<std::stop_callback<std::function<void()>>> stopCallback_;
    std::atomic_bool stopping_{false};
    std::exception_ptr stopFailure_;
    bool finished_=false;
};
} // namespace dreamscapes::native
