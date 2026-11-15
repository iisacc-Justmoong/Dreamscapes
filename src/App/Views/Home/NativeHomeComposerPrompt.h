#pragma once
#include "NativeHomeComposer.h"
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::home {
enum class HomeComposerTextRole { prompt, negativePrompt };
class HomeComposerPromptModel final : public std::enable_shared_from_this<HomeComposerPromptModel> {
  public:
    static std::shared_ptr<HomeComposerPromptModel> create(std::shared_ptr<HomeComposerModel>, HomeComposerTextRole,
        std::string placeholder, std::string accessible, std::string clearAccessible, bool supportsClear);
    ~HomeComposerPromptModel();
    HomeComposerPromptModel(const HomeComposerPromptModel &) = delete;
    HomeComposerPromptModel &operator=(const HomeComposerPromptModel &) = delete;
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height, double gap, double buttonWidth);
    [[nodiscard]] ii::core::Result<void> setMeasuredHeight(double content, double padding, double minimum);
    [[nodiscard]] ii::core::Result<void> edit(std::string);
    [[nodiscard]] ii::core::Result<void> clear();
    [[nodiscard]] ii::core::Result<void> submit();
    [[nodiscard]] bool supportsSubmission() const noexcept { return role_ == HomeComposerTextRole::prompt; }
    [[nodiscard]] bool isFor(const std::shared_ptr<HomeComposerModel> &owner) const noexcept { return active_ && composer_ == owner; }
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    const std::string placeholder, accessible, clearAccessible;
    ii::core::Observable<std::string> text;
    ii::core::Observable<bool> clearVisible{false}, enabled{false};
    ii::core::Observable<double> viewportWidth{0}, viewportHeight{1}, editorWidth{1}, requestedHeight{0};
  private:
    HomeComposerPromptModel(std::shared_ptr<HomeComposerModel>, HomeComposerTextRole,
        std::string, std::string, std::string, bool);
    void start();
    void synchronize(const HomeComposerSnapshot &);
    void updateWidth();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<HomeComposerModel> composer_;
    HomeComposerTextRole role_;
    ii::core::ObservableSubscription observation_, commandsObservation_;
    double gap_ = 0, buttonWidth_ = 0;
    bool supportsClear_ = false, active_ = true;
};
} // namespace dreamscapes::home
