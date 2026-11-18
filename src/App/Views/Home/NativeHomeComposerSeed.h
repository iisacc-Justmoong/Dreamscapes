#pragma once
#include "NativeHomeComposer.h"
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::home {
class HomeComposerSeedModel final : public std::enable_shared_from_this<HomeComposerSeedModel> {
  public:
    static std::shared_ptr<HomeComposerSeedModel> create(std::shared_ptr<HomeComposerModel>, std::string heading,
                                                          std::string randomLabel, std::string applyLabel, bool showHeading = true);
    ~HomeComposerSeedModel();
    HomeComposerSeedModel(const HomeComposerSeedModel &) = delete;
    HomeComposerSeedModel &operator=(const HomeComposerSeedModel &) = delete;
    [[nodiscard]] ii::core::Result<void> begin();
    [[nodiscard]] ii::core::Result<void> edit(std::string);
    [[nodiscard]] ii::core::Result<void> apply();
    [[nodiscard]] ii::core::Result<void> randomize();
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool isFor(const std::shared_ptr<HomeComposerModel> &owner) const noexcept { return active_ && composer_ == owner; }
    const std::string heading, randomLabel, applyLabel;
    const bool showHeading;
    ii::core::Observable<std::string> value, draft;
    ii::core::Observable<bool> enabled{false};
    ii::core::Observable<double> viewportWidth{1}, viewportHeight{1};
  private:
    HomeComposerSeedModel(std::shared_ptr<HomeComposerModel>, std::string, std::string, std::string, bool);
    void start();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<HomeComposerModel> composer_;
    ii::core::ObservableSubscription snapshotObservation_, commandsObservation_;
    bool active_ = true;
};
} // namespace dreamscapes::home
