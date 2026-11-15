#include "NativeReviewInspectorView.h"
#include "NativeReviewInspector.generated.h"
#include <stdexcept>
#include <utility>

NativeReviewInspectorBackend::NativeReviewInspectorBackend(std::shared_ptr<Model> value,
        std::shared_ptr<ii::gui::NativeViewSource> media)
    : model(std::move(value)), mediaView(std::move(media)), hasMediaView(bool(mediaView)), heading(model->strings.heading),
      previewLabel(model->strings.preview), expandLabel(model->strings.expand), saveLabel(model->strings.save),
      noResult(model->strings.noResult), fields(model->fields), selectionSummary(model->selectionSummary),
      previewSource(model->previewSource), jobText(model->jobText), originalText(model->originalText),
      canExpand(model->canExpand), canSave(model->canSave), jobVisible(model->jobVisible), noPreview(model->noPreview),
      viewportWidth(model->viewportWidth), viewportHeight(model->viewportHeight), previewHeight(model->previewHeight) {}
void NativeReviewInspectorBackend::sizeChanged(double width,double height) {
    if (auto result=model->setViewport(width,height); !result) throw std::runtime_error(result.error().message);
}
namespace dreamscapes::review {
ii::core::Result<void> buildReviewInspector(ii::gui::WidgetBuilder &widgets, ii::gui::Widget parent,
        std::shared_ptr<InspectorViewModel> model,
        std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images,
        std::shared_ptr<ii::gui::NativeViewSource> media) {
    if (!model || !model->active()) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Review inspector requires an active model");
    auto factories=lvui::native::BackendFactories{}.with<NativeReviewInspectorBackend>([model,media] {
        return NativeReviewInspectorBackend(model,media);
    });
    return lvui_generated::NativeReviewInspector::build(widgets,parent,[images=std::move(images)](auto &scope) {
        (void)scope.image_services->resolver.set(images);
    },nullptr,factories);
}
}
