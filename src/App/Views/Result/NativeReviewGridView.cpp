#include "NativeReviewGridView.h"
#include "NativeReviewGalleryView.h"
#include "NativeReviewGrid.generated.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

NativeReviewGridBackend::NativeReviewGridBackend(std::shared_ptr<dreamscapes::review::ReviewGridModel> value)
    : model(std::move(value)),gallery(model->gallery()),items(gallery->items),viewportWidth(model->viewportWidth),
      viewportHeight(model->viewportHeight),cellSize(model->cellSize),columns(model->columns),
      minimumCellWidth(model->minimumCellWidth) {}
void NativeReviewGridBackend::sizeChanged(double width,double height) {
    if (!model || !model->active()) return;
    if (auto result=model->setViewport(width,height); !result && result.error().code!=ii::core::ErrorCode::cancelled)
        throw std::runtime_error(result.error().message);
}
std::string NativeReviewGridBackend::tileTitle(const std::shared_ptr<dreamscapes::review::ReviewGalleryItem> &item) const {
    return item && model->active()?model->strings.title(item->ordinal.get(),item->mediaType.get(),item->jobId.get()):std::string{};
}
std::string NativeReviewGridBackend::previewMessage(std::string token) const {
    if (!model->active() || !gallery->active()) return {};
    const auto size=std::uint32_t(std::clamp(std::ceil(model->cellSize.get()),1.0,8192.0));
    const auto status=gallery->previewStatusForToken(token,size,size);
    if (!status || *status==dreamscapes::native::PreviewStatus::ready) return {};
    return *status==dreamscapes::native::PreviewStatus::loading?model->strings.loading:model->strings.unavailable;
}
namespace dreamscapes::review {
ii::core::Result<void> buildReviewGrid(ii::gui::WidgetBuilder &widgets,ii::gui::Widget parent,std::shared_ptr<ReviewGridModel> model) {
    if (!model || !model->active() || !model->gallery() || !model->gallery()->active())
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Review grid requires an active model and gallery");
    auto wrapper=widgets.vstack(parent,"review-grid-shortcuts",{.spacing=0,.alignment=ii::gui::StackAlignment::stretch});
    if (!wrapper) return std::unexpected(wrapper.error());
    if (auto flex=wrapper->set_flex(1); !flex) return flex;
    auto weak=std::make_shared<std::weak_ptr<ReviewGalleryModel>>(model->gallery());
    if (auto input=wrapper->set_input_handler(galleryShortcuts(weak)); !input) return input;
    auto factories=lvui::native::BackendFactories{}.with<NativeReviewGridBackend>([model] {return NativeReviewGridBackend(model);});
    return lvui_generated::NativeReviewGrid::build(widgets,*wrapper,[weak](auto &scope) {
        auto gallery=weak->lock();
        if (!gallery || !gallery->active()) throw std::logic_error("Review gallery retired during grid attachment");
        gallery->bindNavigation(scope.backend_0.shared());
        (void)scope.image_services->resolver.set(galleryImageResolver(gallery));
        scope.backend_0.setDetailHandler([weak] {if (auto owner=weak->lock(); owner && owner->active()) owner->detail();});
        scope.backend_0.setEditHandler([weak] {if (auto owner=weak->lock(); owner && owner->active()) owner->edit();});
    },nullptr,factories);
}
} // namespace dreamscapes::review
