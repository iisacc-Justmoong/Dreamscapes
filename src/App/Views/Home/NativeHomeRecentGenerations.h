#pragma once

#include "App/ViewModels/Native/ImageSelection.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::home {
struct RecentGenerationRow final {
    std::string token, key, name, preview, description, summary;
    bool selected = false;
    bool operator==(const RecentGenerationRow &) const = default;
};
struct HomeRecentGenerationsStrings final {
    std::string heading{"Recent generations"}, viewAll{"View all"}, select{"Select"}, cancel{"Cancel"};
    std::string empty{"No generated images yet"}, loading{"Reading Society…"};
    std::function<std::string(std::size_t)> count = [](std::size_t n) { return std::to_string(n) + " recent results"; };
    std::function<std::string(std::size_t)> edit = [](std::size_t n) { return "Edit (" + std::to_string(n) + ")"; };
};
class HomeRecentGenerationsModel final : public std::enable_shared_from_this<HomeRecentGenerationsModel> {
  public:
    struct Handlers final {
        std::function<void(std::string)> focus;
        std::function<void(std::string, vm::Value)> open;
        std::function<void(std::vector<std::string>)> edit;
        std::function<void()> browse;
        std::function<void(bool, std::vector<std::string>)> selection;
    };
    static std::shared_ptr<HomeRecentGenerationsModel> create(HomeRecentGenerationsStrings = {});
    ~HomeRecentGenerationsModel();
    HomeRecentGenerationsModel(const HomeRecentGenerationsModel &) = delete;
    HomeRecentGenerationsModel &operator=(const HomeRecentGenerationsModel &) = delete;

    [[nodiscard]] ii::core::Result<void> setFiles(vm::Value, std::vector<std::string> previews, bool loading);
    [[nodiscard]] ii::core::Result<void> setAvailabilityMessage(std::string);
    [[nodiscard]] ii::core::Result<void> setFocusedKey(std::string);
    [[nodiscard]] ii::core::Result<void> restoreSelection(bool mode, std::vector<std::string> imageKeys);
    [[nodiscard]] ii::core::Result<void> focusToken(const std::string &);
    [[nodiscard]] ii::core::Result<void> activate(const std::string &);
    [[nodiscard]] std::string tokenForKey(const std::string &) const;
    [[nodiscard]] std::optional<std::size_t> sourceIndexForToken(const std::string &) const;
    [[nodiscard]] ii::core::Result<void> toggleMode();
    void editSelection();
    void browse();
    void sizeChanged(double width, double height);
    void setHandlers(Handlers);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }

    ii::core::Observable<std::vector<RecentGenerationRow>> rows;
    ii::core::Observable<std::string> focusedKey, heading, viewAll, selectLabel, editLabel, emptyMessage, countSummary;
    ii::core::Observable<bool> showEmpty{true}, canSelect{false}, selectionMode{false}, canEdit{false};
    ii::core::Observable<std::size_t> selectedCount{0};
    ii::core::Observable<std::vector<std::string>> selectedImageKeys;
    ii::core::Observable<int> columns{1};
    ii::core::Observable<double> viewportWidth{0}, viewportHeight{0}, itemWidth{1};
  private:
    explicit HomeRecentGenerationsModel(HomeRecentGenerationsStrings);
    [[nodiscard]] ii::core::Result<void> writable() const;
    [[nodiscard]] vm::Value query(vm::Read, std::vector<vm::Value>) const;
    [[nodiscard]] std::string key(const vm::Value &) const;
    [[nodiscard]] std::size_t indexForKey(const std::string &) const;
    [[nodiscard]] std::size_t indexForToken(const std::string &) const;
    void send(const vm::ImageSelectionCommand &);
    void publish();
    const std::thread::id owner_ = std::this_thread::get_id();
    HomeRecentGenerationsStrings strings_;
    vm::ViewCatalog catalog_{{}};
    vm::ImageSelection selection_;
    vm::Array files_;
    std::vector<std::string> previews_, tokens_;
    std::map<std::pair<std::string, std::size_t>, std::string> identity_;
    std::uint64_t nextToken_ = 0;
    std::string current_,availabilityMessage_;
    Handlers handlers_;
    bool loading_ = false, active_ = true, updating_ = false;
};
} // namespace dreamscapes::home
