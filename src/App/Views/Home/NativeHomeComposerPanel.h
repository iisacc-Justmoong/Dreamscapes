#pragma once
#include "NativeHomeComposerLayout.h"
#include "NativeHomeComposerPrompt.h"
#include <iiGUI>
#include <array>
#include <memory>
#include <thread>

namespace dreamscapes::home {
struct HomeComposerPanelSources final {
    std::array<std::shared_ptr<ii::gui::NativeViewSource>, 6> top;
    std::array<std::shared_ptr<ii::gui::NativeViewSource>, 9> settings;
    std::shared_ptr<ii::gui::NativeViewSource> attachments;
    static constexpr std::size_t quickLabelCount = 7;
    std::array<std::shared_ptr<ii::core::Observable<std::string>>, quickLabelCount> quickLabels;
    std::shared_ptr<ii::gui::NativeViewSource> inlineNegative;
    std::shared_ptr<ii::core::Observable<double>> inlineNegativeHeight;
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
    [[nodiscard]] ii::core::Result<void> setFontFamily(std::string family);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool compactControls() const noexcept { return spec_.compactControls; }
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
    ii::core::ObservableSubscription snapshotObservation_, promptObservation_, negativePromptObservation_;
    std::array<ii::core::ObservableSubscription, HomeComposerPanelSources::quickLabelCount> quickLabelObservations_;
    std::array<std::string, HomeComposerPanelSources::quickLabelCount> measuredLabels_;
    std::array<double, HomeComposerPanelSources::quickLabelCount> measuredWidths_{};
    std::string fontFamily_;
    double width_ = 0, measuredError_ = 0;
    bool layoutVideo_ = false;
    bool active_ = true, publishing_ = false, pending_ = false;
};
} // namespace dreamscapes::home
