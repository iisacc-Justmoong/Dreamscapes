#include "NativeMaskLasso.h"
#include "NativeEditorFrame.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dreamscapes::editor {
using namespace iiSharedCanvas;using ii::core::ErrorCode;
MaskLassoModel::MaskLassoModel(std::shared_ptr<native::DocumentSession> document,std::shared_ptr<MaskPanelModel> panel)
    :document_(std::move(document)),panel_(std::move(panel)) {
    if (!document_ || !panel_) throw std::invalid_argument("Mask lasso requires a document and mask panel");
}
std::shared_ptr<MaskLassoModel> MaskLassoModel::create(std::shared_ptr<native::DocumentSession> document,std::shared_ptr<MaskPanelModel> panel) {
    auto value=std::shared_ptr<MaskLassoModel>(new MaskLassoModel(std::move(document),std::move(panel)));value->start();return value;
}
void MaskLassoModel::start() {
    const std::weak_ptr<MaskLassoModel> weak=shared_from_this();
    auto changed=[weak](const auto &,std::uint64_t){if (auto self=weak.lock()) self->cancel();};
    selected_=document_->selectedLayer.observe(changed);revision_=document_->revision.observe(changed);
    frameObserver_=document_->frame.observe(changed);
    generation_=document_->documentGeneration.observe([weak](const auto &,std::uint64_t){if (auto self=weak.lock()) {
        self->frame_.reset();self->cancel();(void)self->status.set({});
    }});
}
ii::core::Result<void> MaskLassoModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Mask lasso requires its owner thread");return {};
}
void MaskLassoModel::setVisible(bool value) {if (!writable()) return;visible_=value;if (!value) cancel();}
void MaskLassoModel::setViewport(double w,double h) {
    if (!writable() || !std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||w>8192||h>8192) return;
    if (w!=width.get() || h!=height.get()) cancel();(void)width.set(w);(void)height.set(h);
}
void MaskLassoModel::setFrame(std::shared_ptr<const ii::gui::RasterImage> image) {
    if (!writable()) return;
    if (!image || !frame_ || image->width()!=frame_->width() || image->height()!=frame_->height()) cancel();
    frame_=std::move(image);
}
void MaskLassoModel::cancel() {if (!writable()) return;gesture_.reset();(void)preview.set({});}
ii::core::Result<bool> MaskLassoModel::input(const ii::gui::WidgetInput &event) {
    if (auto valid=writable();!valid) return std::unexpected(valid.error());
    using Kind=ii::gui::WidgetInputKind;
    if (event.kind==Kind::focus_lost || event.kind==Kind::pointer_cancel || (event.kind==Kind::key_down && event.key==ii::gui::WidgetKey::escape)) {
        const bool had=gesture_.has_value();cancel();return had;
    }
    if (event.local_position && (!std::isfinite(event.local_position->x)||!std::isfinite(event.local_position->y)))
        return ii::core::fail(ErrorCode::invalid_argument,"Lasso coordinates must be finite");
    if (!visible_ || !frame_ || !document_->document() || !event.local_position) return false;
    const Point point{event.local_position->x,event.local_position->y};
    if (event.kind==Kind::pointer_down) {
        if (gesture_ || (event.buttons && !(event.buttons&1)) || !panel_->canLasso.get() || !document_->hasCurrentLayerSelection()) return false;
        const auto region=documentViewRegion(*document_->document());
        if (region.extent.width<=0 || region.extent.height<=0 || !frame_->width() || !frame_->height()) return false;
        const auto scale=std::min(width.get()/frame_->width(),height.get()/frame_->height());
        const auto w=frame_->width()*scale,h=frame_->height()*scale,left=(width.get()-w)/2,top=(height.get()-h)/2;
        if (!std::isfinite(scale) || scale<=0 || point.x<left || point.y<top || point.x>left+w || point.y>top+h) return false;
        gesture_=Gesture{document_->selectedLayer.get(),document_->documentGeneration.get(),document_->revision.get(),event.device_id,
            document_->frame.get(),left,top,w,h,region,{point}};
        (void)status.set({});(void)panel_->status.set({});if (gesture_) (void)preview.set({true,gesture_->points});return true;
    }
    if (!gesture_ || gesture_->device!=event.device_id || (event.kind!=Kind::pointer_move && event.kind!=Kind::pointer_up)) return false;
    auto &g=*gesture_;const Point bounded{std::clamp(point.x,g.left,g.left+g.width),std::clamp(point.y,g.top,g.top+g.height)};
    const auto last=g.points.back();
    if (std::hypot(bounded.x-last.x,bounded.y-last.y)>=2) {
        if (g.points.size()==4096) {cancel();(void)status.set("Lasso path is too long");(void)panel_->status.set(status.get());return true;}
        g.points.push_back(bounded);
    }
    (void)preview.set({true,g.points});if (!gesture_ || event.kind==Kind::pointer_move) return true;
    const auto complete=std::move(*gesture_);gesture_.reset();(void)preview.set({});
    if (!visible_ || complete.generation!=document_->documentGeneration.get() || complete.revision!=document_->revision.get() ||
        complete.frame!=document_->frame.get() || !document_->hasCurrentLayerSelection() || document_->selectedLayer.get()!=complete.layer) return true;
    std::vector<Point> world;world.reserve(complete.points.size());
    for (const auto p:complete.points) world.push_back({complete.region.origin.x+(p.x-complete.left)*complete.region.extent.width/complete.width,
        complete.region.origin.y+(p.y-complete.top)*complete.region.extent.height/complete.height});
    if (auto result=panel_->createLasso(std::move(world));!result) (void)status.set(result.error().message);
    return true;
}
}
