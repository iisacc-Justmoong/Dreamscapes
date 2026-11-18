#pragma once
#include "NativeReviewMedia.h"
#include "App/Native/NativePreviewStatus.h"
#include <LVUI>
#include <iiGUI>
#include <functional>
#include <memory>

namespace dreamscapes::review {
[[nodiscard]] ii::core::Result<void> buildReviewMedia(ii::gui::WidgetBuilder &,ii::gui::Widget parent,
    std::shared_ptr<ReviewMediaModel>,std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images = {});
}
class NativeReviewMediaBackend final {
  public:
    using Model=dreamscapes::review::ReviewMediaModel;
    explicit NativeReviewMediaBackend(std::shared_ptr<Model>,std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>>);
    std::shared_ptr<Model> model;
    const std::string &noResult,&replayLabel;
    ii::core::Observable<std::string> &previewSource,&playLabel,&timeText,&error;
    ii::core::Observable<bool> &controlsVisible,&canControl,&showFrame,&noPreview,&hasError;
    ii::core::Observable<double> &viewportWidth,&viewportHeight,&previewHeight;
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> &frame;
    [[nodiscard]] std::string previewMessage() const;
    std::function<void(double,double)> sizeHandler(double controlsHeight,double gap);
    ii::core::Result<void> toggle() {return model->toggle();}
    ii::core::Result<void> replay() {return model->replay();}
  private:
    std::shared_ptr<const dreamscapes::native::PreviewStatusSource> previewStatus_;
};
