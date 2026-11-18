#pragma once
#include <iiGUI>
#include <iiSDKCore>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::home {
class HomePresetGroupModel final : public std::enable_shared_from_this<HomePresetGroupModel> {
  public:
    static std::shared_ptr<HomePresetGroupModel> create(std::string heading,
        std::shared_ptr<ii::gui::NativeViewSource> choice, std::shared_ptr<ii::gui::NativeViewSource> save);
    ~HomePresetGroupModel();
    HomePresetGroupModel(const HomePresetGroupModel &) = delete;
    HomePresetGroupModel &operator=(const HomePresetGroupModel &) = delete;
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height, double gap, double saveWidth);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    const std::string heading;
    const std::shared_ptr<ii::gui::NativeViewSource> choice, save;
    ii::core::Observable<double> viewportWidth{1}, choiceWidth{1}, saveWidth{1};
  private:
    HomePresetGroupModel(std::string, std::shared_ptr<ii::gui::NativeViewSource>, std::shared_ptr<ii::gui::NativeViewSource>);
    const std::thread::id owner_ = std::this_thread::get_id();
    bool active_ = true;
};
} // namespace dreamscapes::home
