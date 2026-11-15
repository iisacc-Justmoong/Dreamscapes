#pragma once
#include "NativeHomeComposerPrompt.h"
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::home {
class HomeNegativePromptModel final : public std::enable_shared_from_this<HomeNegativePromptModel> {
  public:
    static std::shared_ptr<HomeNegativePromptModel> create(std::shared_ptr<HomeComposerPromptModel>,
                                                           std::string heading, std::string doneLabel);
    ~HomeNegativePromptModel();
    HomeNegativePromptModel(const HomeNegativePromptModel &) = delete;
    HomeNegativePromptModel &operator=(const HomeNegativePromptModel &) = delete;
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::shared_ptr<HomeComposerPromptModel> prompt() const { return prompt_; }
    const std::string heading, doneLabel;
    ii::core::Observable<bool> enabled{false};
    ii::core::Observable<double> viewportWidth{1}, viewportHeight{1};
  private:
    HomeNegativePromptModel(std::shared_ptr<HomeComposerPromptModel>, std::string, std::string);
    void start();
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<HomeComposerPromptModel> prompt_;
    ii::core::ObservableSubscription enabledObservation_;
    bool active_ = true;
};
} // namespace dreamscapes::home
