#include "NativeHomeRecentFilesView.h"
#include "NativeHomeRecentFiles.generated.h"
#include <stdexcept>

namespace dreamscapes::home {
class RecentFilesNavigationBinding final : public std::enable_shared_from_this<RecentFilesNavigationBinding> {
  public:
    static std::shared_ptr<RecentFilesNavigationBinding> create(std::shared_ptr<HomeRecentFilesModel> model,
                                                                 std::shared_ptr<RecentFilesNavigation> navigation) {
        if (!model || !navigation) throw std::invalid_argument("Recent files navigation requires its model and state");
        auto result = std::shared_ptr<RecentFilesNavigationBinding>(new RecentFilesNavigationBinding(std::move(model), std::move(navigation)));
        result->start(); return result;
    }
    ~RecentFilesNavigationBinding() { close(); }
    void close() {
        if (!active_) return;
        active_ = false; current_.reset(); rows_.reset(); navigationState_.reset();
        model_.reset(); navigation_.reset();
    }
  private:
    RecentFilesNavigationBinding(std::shared_ptr<HomeRecentFilesModel> model, std::shared_ptr<RecentFilesNavigation> navigation)
        : model_(std::move(model)), navigation_(std::move(navigation)) {}
    void start() {
        const std::weak_ptr<RecentFilesNavigationBinding> weak = shared_from_this();
        current_ = model_->selectedKey.observe([weak](const auto &, std::uint64_t) { if (auto owner = weak.lock()) owner->syncFromModel(); });
        rows_ = model_->rows.observe([weak](const auto &, std::uint64_t) { if (auto owner = weak.lock()) owner->syncFromModel(); });
        syncFromModel();
        navigationState_ = navigation_->state.observe([weak](const auto &state, std::uint64_t) {
            if (auto owner = weak.lock()) owner->syncFromNavigation(state);
        });
    }
    void syncFromModel() {
        if (!active_ || syncing_ || !model_ || !model_->active()) return;
        struct Reset final { bool &value; ~Reset() { value = false; } } reset{syncing_}; syncing_ = true;
        const auto token = model_->tokenForKey(model_->selectedKey.get());
        lvui::native::CollectionSelectionState state;
        state.current_key = token; state.anchor_key = token;
        if (!token.empty()) state.selected_keys.push_back(token);
        (void)navigation_->set_state(std::move(state));
    }
    void syncFromNavigation(const lvui::native::CollectionSelectionState &state) {
        if (!active_ || syncing_ || !model_ || !model_->active() || state.current_key.empty()) return;
        bool accepted = false;
        {
            struct Reset final { bool &value; ~Reset() { value = false; } } reset{syncing_}; syncing_ = true;
            accepted = bool(model_->selectToken(state.current_key));
        }
        if (!accepted) syncFromModel();
    }
    std::shared_ptr<HomeRecentFilesModel> model_;
    std::shared_ptr<RecentFilesNavigation> navigation_;
    ii::core::ObservableSubscription current_, rows_, navigationState_;
    bool active_ = true, syncing_ = false;
};

ii::core::Result<void> buildHomeRecentFiles(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
    std::shared_ptr<HomeRecentFilesModel> model,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Recent files requires an active model");
    auto host = widgets.content_host(parent, "recentFilesHost");
    if (!host) return std::unexpected(host.error());
    if (auto flex = host->set_flex(1); !flex) return flex;
    return host->set_content([host = *host, model = std::move(model), images = std::move(images)](ii::gui::WidgetBuilder &builder) {
        using View = lvui_generated::NativeHomeRecentFiles;
        std::shared_ptr<View::Scope> scope;
        auto factories = lvui::native::BackendFactories{}.with<NativeHomeRecentFilesBackend>([model] {
            return NativeHomeRecentFilesBackend(model);
        });
        auto built = View::build(builder, {}, [&](auto &owner) {
            owner.backend_1.bindNavigation(owner.backend_0.shared());
            (void)owner.image_services->resolver.set(images);
        }, &scope, factories);
        if (!built) return built;
        return builder.on_publish([host, weak = std::weak_ptr(scope)] {
            auto owner = weak.lock(); if (!owner) return;
            auto menu = host.content_item("recentFilterMenu"), anchor = host.content_item("recentFilter");
            if (menu && anchor) owner->backend_1.attachMenu(*menu, *anchor);
        });
    });
}
} // namespace dreamscapes::home

NativeHomeRecentFilesNavigationScope::NativeHomeRecentFilesNavigationScope()
    : value_(std::make_shared<dreamscapes::home::RecentFilesNavigation>()), state(value_->state) {}
NativeHomeRecentFilesBackend::NativeHomeRecentFilesBackend()
    : NativeHomeRecentFilesBackend(dreamscapes::home::HomeRecentFilesModel::create()) {}
NativeHomeRecentFilesBackend::NativeHomeRecentFilesBackend(std::shared_ptr<Model> source)
    : model(std::move(source)), rows(model->rows), heading(model->heading), viewAll(model->viewAll), fileColumn(model->fileColumn),
      specificationsColumn(model->specificationsColumn), updatedColumn(model->updatedColumn), stateColumn(model->stateColumn),
      countSummary(model->countSummary), selectionSummary(model->selectionSummary), emptyMessage(model->emptyMessage),
      filterLabel(model->filterLabel), allTypesLabel(model->allTypesLabel), canvasLabel(model->canvasLabel), imageLabel(model->imageLabel),
      videoLabel(model->videoLabel), documentLabel(model->documentLabel), showFilter(model->showFilter), showUpdated(model->showUpdated),
      showSpecifications(model->showSpecifications), showEmpty(model->showEmpty), viewportWidth(model->viewportWidth), viewportHeight(model->viewportHeight) {}
void NativeHomeRecentFilesBackend::bindNavigation(std::shared_ptr<dreamscapes::home::RecentFilesNavigation> source) {
    navigation_ = dreamscapes::home::RecentFilesNavigationBinding::create(model, std::move(source));
}
void NativeHomeRecentFilesBackend::attachMenu(ii::gui::Widget menu, ii::gui::Widget anchor) {
    menu_ = std::move(menu); anchor_ = std::move(anchor);
}
void NativeHomeRecentFilesBackend::openFilter() {
    auto anchor = anchor_.snapshot();
    if (anchor && anchor->visible && anchor->enabled) (void)menu_.open_for(anchor_, 0, anchor->bounds.height);
}
