#pragma once

#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::home {
struct HomeSidebarDefinition final {
    std::string key, title, icon;
    bool divider = false;
};
enum class HomeSidebarState { normal, selected, inactive };
struct HomeSidebarRow final {
    std::string key, label, accessible, icon;
    bool divider = false, selected = false;
    HomeSidebarState state = HomeSidebarState::normal;
    bool operator==(const HomeSidebarRow &) const = default;
};
class HomeSidebarModel final : public std::enable_shared_from_this<HomeSidebarModel> {
  public:
    using Handler = std::function<void(std::string)>;
    static std::shared_ptr<HomeSidebarModel> create(std::vector<HomeSidebarDefinition>);
    ~HomeSidebarModel();
    HomeSidebarModel(const HomeSidebarModel &) = delete;
    HomeSidebarModel &operator=(const HomeSidebarModel &) = delete;
    [[nodiscard]] ii::core::Result<void> setSelected(std::string);
    [[nodiscard]] ii::core::Result<void> setCompact(bool);
    [[nodiscard]] ii::core::Result<void> activate(const std::string &);
    void setHandler(Handler);
    void sizeChanged(double width, double height);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    ii::core::Observable<std::vector<HomeSidebarRow>> rows;
    ii::core::Observable<std::string> selected;
    ii::core::Observable<bool> compact{false};
    ii::core::Observable<double> viewportWidth{0}, viewportHeight{0};
  private:
    explicit HomeSidebarModel(std::vector<HomeSidebarDefinition>);
    [[nodiscard]] ii::core::Result<void> writable() const;
    void publish();
    const std::thread::id owner_ = std::this_thread::get_id();
    std::vector<HomeSidebarDefinition> definitions_;
    std::string current_;
    Handler handler_;
    bool compact_ = false, active_ = true, updating_ = false;
};
} // namespace dreamscapes::home
