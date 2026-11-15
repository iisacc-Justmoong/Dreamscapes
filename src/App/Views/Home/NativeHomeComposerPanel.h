#pragma once
#include "NativeHomeComposerLayout.h"
#include "NativeHomeComposerPrompt.h"
#include <iiGUI/NativeView.h>
#include <array>
#include <memory>
#include <thread>

namespace dreamscapes::home {
struct HomeComposerPanelSources final {
    std::array<std::shared_ptr<ii::gui::NativeViewSource>, 6> top;
    std::array<std::shared_ptr<ii::gui::NativeViewSource>, 9> settings;
    std::shared_ptr<ii::gui::NativeViewSource> attachments;
};
class HomeComposerPanelModel final : public std::enable_shared_from_this<HomeComposerPanelModel> {
  public:
    static std::shared_ptr<HomeComposerPanelModel> create(std::shared_ptr<HomeComposerModel>,
        std::shared_ptr<HomeComposerPromptModel>, HomeComposerPanelSources, HomeComposerLayoutSpec, HomeComposerLayoutMetrics);
    ~HomeComposerPanelModel();
    HomeComposerPanelModel(const HomeComposerPanelModel &) = delete;
    HomeComposerPanelModel &operator=(const HomeComposerPanelModel &) = delete;
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height);
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height, HomeComposerLayoutMetrics);
    [[nodiscard]] ii::core::Result<void> setErrorHeight(double height);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    const HomeComposerPanelSources sources;
    ii::core::Observable<double> viewportWidth{1}, contentWidth{1}, topHeight{1}, settingsHeight{1}, requestedHeight{1}, errorHeight{0};
    ii::core::Observable<bool> hasAttachments{false}, hasError{false};
    ii::core::Observable<std::string> error;
    ii::core::Observable<HomeComposerLayout> layout;
  private:
    HomeComposerPanelModel(std::shared_ptr<HomeComposerModel>, std::shared_ptr<HomeComposerPromptModel>,
                           HomeComposerPanelSources, HomeComposerLayoutSpec, HomeComposerLayoutMetrics);
    void start();
    [[nodiscard]] ii::core::Result<void> synchronize();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<HomeComposerModel> composer_;
    std::shared_ptr<HomeComposerPromptModel> prompt_;
    HomeComposerLayoutSpec spec_;
    HomeComposerLayoutMetrics metrics_;
    ii::core::ObservableSubscription snapshotObservation_, promptObservation_;
    double width_ = 0, height_ = 0, measuredError_ = 0;
    bool active_ = true, publishing_ = false, pending_ = false;
};
} // namespace dreamscapes::home
