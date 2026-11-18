#pragma once
#include "NativeHomeComposer.h"
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::home {
enum class HomeComposerActionRole { attach, paint, advanced, generate };
struct HomeComposerActionLabels final {
    std::string label, heading, accessible;
    std::function<std::string(int, bool)> generation;
};
class HomeComposerActionModel final : public std::enable_shared_from_this<HomeComposerActionModel> {
  public:
    static std::shared_ptr<HomeComposerActionModel> create(std::shared_ptr<HomeComposerModel>, HomeComposerActionRole,
                                                           HomeComposerActionLabels, bool fixedGenerateLabel = false);
    ~HomeComposerActionModel();
    HomeComposerActionModel(const HomeComposerActionModel &) = delete;
    HomeComposerActionModel &operator=(const HomeComposerActionModel &) = delete;
    [[nodiscard]] ii::core::Result<void> invoke();
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] HomeComposerActionRole role() const noexcept { return role_; }
    [[nodiscard]] bool isFor(const std::shared_ptr<HomeComposerModel> &owner) const noexcept { return active_ && composer_ == owner; }
    const bool icon, primary, showHeading;
    const std::string heading, accessible;
    ii::core::Observable<std::string> label;
    ii::core::Observable<bool> enabled{false};
    ii::core::Observable<double> viewportWidth{1}, viewportHeight{1};
  private:
    HomeComposerActionModel(std::shared_ptr<HomeComposerModel>, HomeComposerActionRole, HomeComposerActionLabels, bool);
    void start();
    void synchronize();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<HomeComposerModel> composer_;
    HomeComposerActionRole role_;
    HomeComposerActionLabels labels_;
    bool fixedGenerateLabel_ = false;
    ii::core::ObservableSubscription snapshotObservation_, commandsObservation_, canSubmitObservation_;
    bool active_ = true, publishing_ = false, pending_ = false;
};
} // namespace dreamscapes::home
