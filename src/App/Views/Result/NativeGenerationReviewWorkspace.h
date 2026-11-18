#pragma once
#include "NativeReviewGalleryView.h"
#include "NativeReviewQueueView.h"
#include "App/Views/Failure/NativeFailureDetails.h"
#include <iiSDKCore>
#include <iiGUI>
#include <functional>
#include <memory>

namespace dreamscapes::review {
class GenerationReviewWorkspace final : public std::enable_shared_from_this<GenerationReviewWorkspace> {
  public:
    using Source=std::shared_ptr<ii::gui::NativeViewSource>;
    static std::shared_ptr<GenerationReviewWorkspace> create(std::shared_ptr<Session>,std::shared_ptr<QueueModel>,
        std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>>,
        std::shared_ptr<failure::FailureDetailsModel>,GalleryConfigure,std::function<void()> back);
    void sizeChanged(double,double,double queueSize,double inspectorSize,double minimumResults,double gap);
    void back();
    Source queueView,galleryView,inspectorView,failureView;
    std::shared_ptr<failure::FailureDetailsModel> failure;
    ii::core::Observable<double> width{1},height{1},queueWidth{1},galleryWidth{1},inspectorWidth{1};
    ii::core::Observable<bool> showQueue{false},showInspector{false};
  private:
    std::function<void()> back_;
};
[[nodiscard]] ii::core::Result<void> buildGenerationReviewWorkspace(ii::gui::WidgetBuilder &,ii::gui::Widget,
    std::shared_ptr<GenerationReviewWorkspace>);
}

class NativeGenerationReviewWorkspaceBackend final {
  public:
    explicit NativeGenerationReviewWorkspaceBackend(std::shared_ptr<dreamscapes::review::GenerationReviewWorkspace> value)
        :model(std::move(value)),width(model->width),height(model->height),queueWidth(model->queueWidth),
         galleryWidth(model->galleryWidth),inspectorWidth(model->inspectorWidth),showQueue(model->showQueue),
         showInspector(model->showInspector),failureOpen(model->failure->open),queueView(model->queueView),
         galleryView(model->galleryView),inspectorView(model->inspectorView),failureView(model->failureView) {}
    std::function<void(double,double)> sizeHandler(double queue,double inspector,double minimum,double gap);
    void back() {model->back();}
    std::shared_ptr<dreamscapes::review::GenerationReviewWorkspace> model;
    ii::core::Observable<double> &width,&height,&queueWidth,&galleryWidth,&inspectorWidth;
    ii::core::Observable<bool> &showQueue,&showInspector,&failureOpen;
    dreamscapes::review::GenerationReviewWorkspace::Source queueView,galleryView,inspectorView,failureView;
};
