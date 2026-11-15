#include "NativeHomeRecentGenerationsView.h"
#include "NativeHomeRecentGenerations.generated.h"
#include <stdexcept>

namespace dreamscapes::home {
class RecentGenerationsNavigationBinding final : public std::enable_shared_from_this<RecentGenerationsNavigationBinding> {
  public:
    static std::shared_ptr<RecentGenerationsNavigationBinding> create(std::shared_ptr<HomeRecentGenerationsModel> model,
                                                                       std::shared_ptr<RecentGenerationsNavigation> navigation) {
        if (!model || !navigation) throw std::invalid_argument("Recent generations navigation requires a model and state");
        auto result = std::shared_ptr<RecentGenerationsNavigationBinding>(new RecentGenerationsNavigationBinding(std::move(model), std::move(navigation)));
        result->start(); return result;
    }
    ~RecentGenerationsNavigationBinding() { current_.reset(); rows_.reset(); }
  private:
    RecentGenerationsNavigationBinding(std::shared_ptr<HomeRecentGenerationsModel> model,
                                       std::shared_ptr<RecentGenerationsNavigation> navigation)
        : model_(std::move(model)), navigation_(std::move(navigation)) {}
    void start() {
        const std::weak_ptr<RecentGenerationsNavigationBinding> weak = shared_from_this();
        current_ = model_->focusedKey.observe([weak](const auto &, std::uint64_t) { if (auto owner = weak.lock()) owner->sync(); });
        rows_ = model_->rows.observe([weak](const auto &, std::uint64_t) { if (auto owner = weak.lock()) owner->sync(); });
        sync();
    }
    void sync() {
        if (!model_ || !model_->active()) return;
        const auto token = model_->tokenForKey(model_->focusedKey.get());
        if (navigation_->state.get().current_key == token) return;
        lvui::native::CollectionSelectionState state;
        state.current_key = token; state.anchor_key = token;
        (void)navigation_->set_state(std::move(state));
    }
    std::shared_ptr<HomeRecentGenerationsModel> model_;
    std::shared_ptr<RecentGenerationsNavigation> navigation_;
    ii::core::ObservableSubscription current_, rows_;
};
ii::core::Result<void> buildHomeRecentGenerations(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
    std::shared_ptr<HomeRecentGenerationsModel> model,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Recent generations requires an active model");
    auto host = widgets.content_host(parent, "recentGenerationsHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([model = std::move(model), images = std::move(images)](ii::gui::WidgetBuilder &builder) {
        using View = lvui_generated::NativeHomeRecentGenerations;
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeRecentGenerationsBackend>([model] {
            return NativeHomeRecentGenerationsBackend(model);
        });
        return View::build(builder, {}, [&](auto &owner) {
            owner.backend_1.bindNavigation(owner.backend_0.shared());
            (void)owner.image_services->resolver.set(images);
        }, nullptr, factories);
    });
}
} // namespace dreamscapes::home
NativeHomeRecentGenerationsNavigationScope::NativeHomeRecentGenerationsNavigationScope()
    : value_(std::make_shared<dreamscapes::home::RecentGenerationsNavigation>()), state(value_->state) {}
NativeHomeRecentGenerationsBackend::NativeHomeRecentGenerationsBackend()
    : NativeHomeRecentGenerationsBackend(dreamscapes::home::HomeRecentGenerationsModel::create()) {}
NativeHomeRecentGenerationsBackend::NativeHomeRecentGenerationsBackend(std::shared_ptr<Model> source)
    : model(std::move(source)), rows(model->rows), heading(model->heading), viewAll(model->viewAll), selectLabel(model->selectLabel),
      editLabel(model->editLabel), emptyMessage(model->emptyMessage), countSummary(model->countSummary), showEmpty(model->showEmpty),
      canSelect(model->canSelect), selectionMode(model->selectionMode), canEdit(model->canEdit),
      viewportWidth(model->viewportWidth), viewportHeight(model->viewportHeight), itemWidth(model->itemWidth) {}
void NativeHomeRecentGenerationsBackend::bindNavigation(std::shared_ptr<dreamscapes::home::RecentGenerationsNavigation> source) {
    navigation_ = dreamscapes::home::RecentGenerationsNavigationBinding::create(model, std::move(source));
}
