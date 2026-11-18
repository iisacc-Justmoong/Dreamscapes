#pragma once
#include "NativeHomeComposer.h"
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::home {
enum class HomeComposerChoiceRole { media, model, ratio, resolution, count, duration, frameRate, preset };
enum class HomeComposerMenuState { normal, selected, inactive };
struct HomeComposerChoiceRow final {
    std::string key, label;
    bool enabled = true;
    HomeComposerMenuState state = HomeComposerMenuState::normal;
    bool operator==(const HomeComposerChoiceRow &) const = default;
};
class HomeComposerChoiceModel final : public std::enable_shared_from_this<HomeComposerChoiceModel> {
  public:
    static std::shared_ptr<HomeComposerChoiceModel> create(std::shared_ptr<HomeComposerModel>, HomeComposerChoiceRole,
                                                            std::string heading, std::string placeholder, bool showHeading = true);
    ~HomeComposerChoiceModel();
    HomeComposerChoiceModel(const HomeComposerChoiceModel &) = delete;
    HomeComposerChoiceModel &operator=(const HomeComposerChoiceModel &) = delete;
    [[nodiscard]] ii::core::Result<void> choose(std::string key);
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height);
    [[nodiscard]] ii::core::Result<void> setFontFamily(std::string family);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] HomeComposerChoiceRole role() const noexcept { return role_; }
    [[nodiscard]] bool isFor(const std::shared_ptr<HomeComposerModel> &owner) const noexcept { return active_ && composer_ == owner; }
    const std::string heading, placeholder;
    const bool showHeading;
    ii::core::Observable<std::string> value;
    ii::core::Observable<std::vector<HomeComposerChoiceRow>> choices;
    ii::core::Observable<bool> enabled{false};
    ii::core::Observable<double> viewportWidth{1}, viewportHeight{1}, menuItemWidth{1};
  private:
    struct Projection final {
        HomeComposerChoiceList choices;
        HomeComposerCommandKind command;
        std::string key, label;
        bool available = true;
        bool operator==(const Projection &) const = default;
    };
    HomeComposerChoiceModel(std::shared_ptr<HomeComposerModel>, HomeComposerChoiceRole, std::string, std::string, bool);
    [[nodiscard]] Projection project(const HomeComposerSnapshot &) const;
    void start();
    [[nodiscard]] ii::core::Result<void> synchronize();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<HomeComposerModel> composer_;
    HomeComposerChoiceRole role_;
    std::optional<Projection> published_;
    ii::core::ObservableSubscription snapshotObservation_, commandsObservation_;
    std::string fontFamily_;
    std::vector<std::string> measuredLabels_;
    bool measured_ = false;
    bool active_ = true, publishing_ = false, pending_ = false;
};
} // namespace dreamscapes::home
