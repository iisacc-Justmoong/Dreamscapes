#include "NativeGenerationReviewWorkspace.h"
#include "NativeReviewInspectorView.h"
#include "App/Views/Failure/NativeFailureDetailsView.h"
#include "NativeGenerationReviewWorkspace.generated.h"
#include <LVUI>
#include <iiGUI>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
std::shared_ptr<GenerationReviewWorkspace> GenerationReviewWorkspace::create(std::shared_ptr<Session> session,
    std::shared_ptr<QueueModel> queue,std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> images,
    std::shared_ptr<failure::FailureDetailsModel> failure,GalleryConfigure configure,std::function<void()> back) {
    if (!session || !queue || !failure) throw std::invalid_argument("Generation review requires its session, queue and failure view");
    auto model=std::make_shared<GenerationReviewWorkspace>();model->back_=std::move(back);model->failure=std::move(failure);
    model->queueView=ii::gui::compiled_view_source([queue,images](ii::gui::WidgetBuilder &builder) {
        return buildReviewQueue(builder,{},queue,images);
    });
    model->galleryView=ii::gui::compiled_view_source([session,configure=std::move(configure)](ii::gui::WidgetBuilder &builder) {
        return buildReviewGallery(builder,{},configure,session);
    });
    InspectorViewStrings labels{"INSPECTOR","Focused result","Preview","Expand","Save current","Unavailable","No result selected",
        [](std::size_t count){return std::to_string(count)+" selected";},[](std::string_view text){return std::string(text);},
        [](std::string_view text){return "Job · "+std::string(text);},[](std::string_view text){return "Original · "+std::string(text);}};
    const auto &tokens=lvui::default_style_tokens();
    const auto preview=tokens.number("Metric.size140"),minimum=tokens.number("Metric.size22"),
               maximum=tokens.number("Metric.size400"),longField=tokens.number("Metric.size280");
    if (!preview || !minimum || !maximum || !longField) throw std::runtime_error("LVUI inspector metrics are unavailable");
    auto inspector=ReviewInspectorModel::create(session);
    auto detail=InspectorViewModel::create(inspector,std::move(labels),{*preview,*minimum,*maximum,*longField});
    model->inspectorView=ii::gui::compiled_view_source([detail,images](ii::gui::WidgetBuilder &builder) {
        return buildReviewInspector(builder,{},detail,images);
    });
    model->failureView=ii::gui::compiled_view_source([failure=model->failure](ii::gui::WidgetBuilder &builder) {
        return dreamscapes::failure::buildFailureDetails(builder,{},failure);
    });
    return model;
}
void GenerationReviewWorkspace::sizeChanged(double w,double h,double queue,double inspector,double minimum,double gap) {
    if (!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(queue)||!std::isfinite(inspector)||
        !std::isfinite(minimum)||!std::isfinite(gap)||w<0||h<0||w>8192||h>8192||queue<=0||inspector<=0||minimum<=0||gap<0) return;
    const bool hasQueue=w>=queue+minimum+gap;
    const bool hasInspector=hasQueue && w>=queue+inspector+minimum+2*gap;
    const auto center=std::max(1.0,w-(hasQueue?queue+gap:0)-(hasInspector?inspector+gap:0));
    (void)width.set(w);(void)height.set(h);(void)queueWidth.set(queue);(void)inspectorWidth.set(inspector);
    (void)galleryWidth.set(center);(void)showQueue.set(hasQueue);(void)showInspector.set(hasInspector);
    if (w>0 && h>0) (void)failure->setViewport(w,h);
}
void GenerationReviewWorkspace::back() {if (auto action=back_) action();}
ii::core::Result<void> buildGenerationReviewWorkspace(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,
    std::shared_ptr<GenerationReviewWorkspace> model) {
    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Generation review workspace has no model");
    auto factories=lvui::native::BackendFactories{}.with<NativeGenerationReviewWorkspaceBackend>([model] {
        return NativeGenerationReviewWorkspaceBackend(model);
    });
    return lvui_generated::NativeGenerationReviewWorkspace::build(builder,parent,[](auto &){},nullptr,factories);
}
}
std::function<void(double,double)> NativeGenerationReviewWorkspaceBackend::sizeHandler(double queue,double inspector,double minimum,double gap) {
    const std::weak_ptr<dreamscapes::review::GenerationReviewWorkspace> weak=model;
    return [weak,queue,inspector,minimum,gap](double width,double height) {
        if (auto owner=weak.lock()) owner->sizeChanged(width,height,queue,inspector,minimum,gap);
    };
}
