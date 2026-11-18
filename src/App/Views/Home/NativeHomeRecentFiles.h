#pragma once

#include "App/ViewModels/Native/ViewCatalog.h"
#include <iiSDKCore>
#include <cstdint>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dreamscapes::home {
struct RecentFileRow final {
    std::string token, key, name, description, preview, dimensions, date, state;
    bool selected = false;
    bool operator==(const RecentFileRow &) const = default;
};
struct HomeRecentFilesStrings final {
    std::string empty{"No recent files"}, loading{"Reading Society…"}, noSelection{"No file selected"};
    std::string heading{"Recent files"}, viewAll{"View all"}, fileColumn{"FILE"}, specificationsColumn{"SPECIFICATIONS"};
    std::string updatedColumn{"UPDATED"}, stateColumn{"STATE"};
    std::array<std::string, 5> filters{"All types", "Canvas", "Image", "Video", "Document"};
    std::function<std::string(std::size_t)> count = [](std::size_t n) { return std::to_string(n) + " files"; };
    std::function<std::string(const std::string &)> selection = [](const auto &name) { return "1 selected · " + name; };
};
class HomeRecentFilesModel final : public std::enable_shared_from_this<HomeRecentFilesModel> {
  public:
    struct Handlers final {
        std::function<void(std::string)> select;
        std::function<void(std::string, vm::Value)> open;
        std::function<void()> browse;
        std::function<void(std::string)> filter;
    };
    static std::shared_ptr<HomeRecentFilesModel> create(HomeRecentFilesStrings = {});
    ~HomeRecentFilesModel();
    HomeRecentFilesModel(const HomeRecentFilesModel &) = delete;
    HomeRecentFilesModel &operator=(const HomeRecentFilesModel &) = delete;

    [[nodiscard]] ii::core::Result<void> setFiles(vm::Value, std::vector<std::string> previews, bool loading);
    [[nodiscard]] ii::core::Result<void> setAvailabilityMessage(std::string);
    [[nodiscard]] ii::core::Result<void> setFilter(std::string);
    [[nodiscard]] ii::core::Result<void> setSelectedKey(std::string);
    [[nodiscard]] ii::core::Result<void> selectToken(const std::string &);
    [[nodiscard]] ii::core::Result<void> openToken(const std::string &);
    [[nodiscard]] ii::core::Result<void> filterAt(int);
    [[nodiscard]] std::string tokenForKey(const std::string &) const;
    [[nodiscard]] std::optional<std::size_t> sourceIndexForToken(const std::string &) const;
    void sizeChanged(double width, double height);
    void setHandlers(Handlers);
    void browse();
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }
    ii::core::Observable<std::vector<RecentFileRow>> rows;
    ii::core::Observable<std::string> selectedKey, filter{"All types"}, emptyMessage, countSummary, selectionSummary;
    ii::core::Observable<std::string> heading, viewAll, fileColumn, specificationsColumn, updatedColumn, stateColumn;
    ii::core::Observable<std::string> filterLabel, allTypesLabel, canvasLabel, imageLabel, videoLabel, documentLabel;
    ii::core::Observable<bool> showFilter{false}, showUpdated{false}, showSpecifications{false}, showEmpty{true};
    ii::core::Observable<double> viewportWidth{0}, viewportHeight{0};

  private:
    explicit HomeRecentFilesModel(HomeRecentFilesStrings);
    [[nodiscard]] ii::core::Result<void> writable() const;
    [[nodiscard]] vm::Value query(vm::Read, std::vector<vm::Value>) const;
    [[nodiscard]] std::string key(const vm::Value &) const;
    [[nodiscard]] std::size_t indexForKey(const std::string &) const;
    [[nodiscard]] std::size_t indexForToken(const std::string &) const;
    void publish();
    const std::thread::id owner_ = std::this_thread::get_id();
    HomeRecentFilesStrings strings_;
    vm::ViewCatalog catalog_{{}};
    vm::Array files_;
    std::vector<std::string> previews_, tokens_;
    std::map<std::pair<std::string, std::size_t>, std::string> identity_;
    std::uint64_t nextToken_ = 0;
    std::string current_, currentFilter_{"All types"},availabilityMessage_;
    Handlers handlers_;
    bool loading_ = false, active_ = true, updating_ = false;
};
} // namespace dreamscapes::home
