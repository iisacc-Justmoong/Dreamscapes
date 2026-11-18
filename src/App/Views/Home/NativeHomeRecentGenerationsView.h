#pragma once

#include "NativeHomeRecentGenerations.h"
#include <LVUI>
#include <iiGUI>
#include <memory>
#include <utility>

namespace dreamscapes::home {
using RecentGenerationsNavigation = lvui::native::CollectionNavigationBinding<ii::gui::BasicCollectionNavigation>;
class RecentGenerationsNavigationBinding;
[[nodiscard]] ii::core::Result<void> buildHomeRecentGenerations(ii::gui::WidgetBuilder &, ii::gui::Widget parent,
    std::shared_ptr<HomeRecentGenerationsModel>,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images = {});
}
class NativeHomeRecentGenerationsNavigationScope final {
    std::shared_ptr<dreamscapes::home::RecentGenerationsNavigation> value_;
  public:
    dreamscapes::home::RecentGenerationsNavigation::StateSource &state;
    NativeHomeRecentGenerationsNavigationScope();
    std::shared_ptr<dreamscapes::home::RecentGenerationsNavigation> shared() const { return value_; }
    void configure(lvui::native::CollectionNavigationOptions options) { value_->configure(options); }
    bool is_selected(std::string_view key) const { return value_->is_selected(key); }
    template<class... Args> auto focus_at(Args &&...args) { return value_->focus_at(std::forward<Args>(args)...); }
    template<class... Args> auto command(Args &&...args) { return value_->command(std::forward<Args>(args)...); }
};
class NativeHomeRecentGenerationsBackend final {
  public:
    using Model = dreamscapes::home::HomeRecentGenerationsModel;
    std::shared_ptr<Model> model;
    ii::core::Observable<std::vector<dreamscapes::home::RecentGenerationRow>> &rows;
    ii::core::Observable<std::string> &heading, &viewAll, &selectLabel, &editLabel, &emptyMessage, &countSummary;
    ii::core::Observable<bool> &showEmpty, &canSelect, &selectionMode, &canEdit;
    ii::core::Observable<double> &viewportWidth, &viewportHeight, &itemWidth;
    NativeHomeRecentGenerationsBackend();
    explicit NativeHomeRecentGenerationsBackend(std::shared_ptr<Model>);
    void bindNavigation(std::shared_ptr<dreamscapes::home::RecentGenerationsNavigation>);
    void sizeChanged(double width, double height) { model->sizeChanged(width, height); }
    void focus(std::string token) { (void)model->focusToken(token); }
    void activate(std::string token) { (void)model->activate(token); }
    void toggleMode() { (void)model->toggleMode(); }
    void edit() { model->editSelection(); }
    void browse() { model->browse(); }
  private:
    std::shared_ptr<dreamscapes::home::RecentGenerationsNavigationBinding> navigation_;
};
