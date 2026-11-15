#include "NativeReviewMediaView.h"
#include "NativeReviewMedia.generated.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

NativeReviewMediaBackend::NativeReviewMediaBackend(std::shared_ptr<Model> value,
        std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images)
    : model(std::move(value)),noResult(model->strings.noResult),replayLabel(model->strings.replay),
      previewSource(model->previewSource),playLabel(model->playLabel),timeText(model->timeText),error(model->error),
      controlsVisible(model->controlsVisible),canControl(model->canControl),showFrame(model->showFrame),
      noPreview(model->noPreview),hasError(model->hasError),viewportWidth(model->viewportWidth),
      viewportHeight(model->viewportHeight),previewHeight(model->previewHeight),frame(model->frame),
      previewStatus_(std::dynamic_pointer_cast<const dreamscapes::native::PreviewStatusSource>(std::move(images))) {}
std::string NativeReviewMediaBackend::previewMessage() const {
    if (model->hasError.get()) return model->error.get();
    if (model->showFrame.get()) return {};
    const auto source=model->previewSource.get();
    if (source.empty()) return model->strings.noResult;
    if (!previewStatus_) return {};
    const auto width=std::uint32_t(std::clamp(std::ceil(model->viewportWidth.get()),1.0,8192.0));
    const auto height=std::uint32_t(std::clamp(std::ceil(model->previewHeight.get()),1.0,8192.0));
    using dreamscapes::native::PreviewStatus;
    switch (previewStatus_->previewStatus(source,width,height)) {
    case PreviewStatus::loading: return model->strings.loading;
    case PreviewStatus::failed: return model->strings.unavailable;
    case PreviewStatus::ready: return {};
    }
    return {};
}
std::function<void(double,double)> NativeReviewMediaBackend::sizeHandler(double controlsHeight,double gap) {
    const std::weak_ptr<Model> weak=model;
    return [weak,controlsHeight,gap](double width,double height) {
        if (auto value=weak.lock(); value && value->active())
            if (auto result=value->setViewport(width,height,controlsHeight,gap); !result) throw std::runtime_error(result.error().message);
    };
}
namespace dreamscapes::review {
ii::core::Result<void> buildReviewMedia(ii::gui::WidgetBuilder &widgets,ii::gui::Widget parent,
        std::shared_ptr<ReviewMediaModel> model,std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Review media requires an active model");
    auto factories=lvui::native::BackendFactories{}.with<NativeReviewMediaBackend>([model,images] {return NativeReviewMediaBackend(model,images);});
    return lvui_generated::NativeReviewMedia::build(widgets,parent,[images=std::move(images)](auto &scope) {
        (void)scope.image_services->resolver.set(images);
    },nullptr,factories);
}
}
