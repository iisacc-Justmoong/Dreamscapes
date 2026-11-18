#pragma once

#include "NativeHomeRecentFiles.h"
#include <LVUI>
#include <iiGUI>
#include <memory>
#include <utility>

namespace dreamscapes::home {
using RecentFilesNavigation = lvui::native::CollectionNavigationBinding<ii::gui::BasicCollectionNavigation>;
class RecentFilesNavigationBinding;
[[nodiscard]] ii::core::Result<void> buildHomeRecentFiles(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<HomeRecentFilesModel>,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images = {});
}

class NativeHomeRecentFilesNavigationScope final {
    std::shared_ptr<dreamscapes::home::RecentFilesNavigation> value_;
  public:
    dreamscapes::home::RecentFilesNavigation::StateSource &state;
    NativeHomeRecentFilesNavigationScope();
    std::shared_ptr<dreamscapes::home::RecentFilesNavigation> shared() const { return value_; }
    void configure(lvui::native::CollectionNavigationOptions options) { value_->configure(options); }
    bool is_selected(std::string_view key) const { return value_->is_selected(key); }
    template<class... Args> auto focus_at(Args &&...args) { return value_->focus_at(std::forward<Args>(args)...); }
    template<class... Args> auto command(Args &&...args) { return value_->command(std::forward<Args>(args)...); }
};

class NativeHomeRecentFilesBackend final {
  public:
    using Model = dreamscapes::home::HomeRecentFilesModel;
    std::shared_ptr<Model> model;
    ii::core::Observable<std::vector<dreamscapes::home::RecentFileRow>> &rows;
    ii::core::Observable<std::string> &heading, &viewAll, &fileColumn, &specificationsColumn, &updatedColumn, &stateColumn;
    ii::core::Observable<std::string> &countSummary, &selectionSummary, &emptyMessage, &filterLabel;
    ii::core::Observable<std::string> &allTypesLabel, &canvasLabel, &imageLabel, &videoLabel, &documentLabel;
    ii::core::Observable<bool> &showFilter, &showUpdated, &showSpecifications, &showEmpty;
    ii::core::Observable<double> &viewportWidth, &viewportHeight;
    NativeHomeRecentFilesBackend();
    explicit NativeHomeRecentFilesBackend(std::shared_ptr<Model>);
    void bindNavigation(std::shared_ptr<dreamscapes::home::RecentFilesNavigation>);
    void attachMenu(ii::gui::Widget menu, ii::gui::Widget anchor);
    void openFilter();
    void filterAt(int index) { (void)model->filterAt(index); }
    void sizeChanged(double width, double height) { model->sizeChanged(width, height); }
    void open(std::string token) { (void)model->openToken(token); }
    void browse() { model->browse(); }
  private:
    std::shared_ptr<dreamscapes::home::RecentFilesNavigationBinding> navigation_;
    ii::gui::Widget menu_, anchor_;
};
