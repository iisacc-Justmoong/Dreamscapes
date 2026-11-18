#include "NativeBrushTool.h"
#include <Document/CanvasSampling.h>
#include <iiColorManager>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <stdexcept>

namespace dreamscapes::editor {
using namespace iiSharedCanvas;
using ii::core::ErrorCode;
struct BrushToolModel::Gesture final {
    std::shared_ptr<const Document> source;
    std::uint64_t generation=0,revision=0,device=0;
    std::int64_t frame=0;
    std::string layer;
    CanvasRegion region;
    AffineTransform transform;
    double left=0,top=0,displayWidth=1,displayHeight=1,lastTime=0;
};
BrushToolModel::BrushToolModel(std::shared_ptr<native::DocumentSession> document):document_(std::move(document)) {
    if (!document_) throw std::invalid_argument("Brush tool requires a document");
}
std::shared_ptr<BrushToolModel> BrushToolModel::create(std::shared_ptr<native::DocumentSession> document,
    ii::runtime::EventLoop &loop,std::shared_ptr<FrameRenderExecutor> executor) {
    auto model=std::shared_ptr<BrushToolModel>(new BrushToolModel(std::move(document)));
    model->start(loop,std::move(executor));return model;
}
void BrushToolModel::start(ii::runtime::EventLoop &loop,std::shared_ptr<FrameRenderExecutor> executor) {
    const std::weak_ptr<BrushToolModel> weak=shared_from_this();
    queue_=BrushQueue::create(loop,std::move(executor),[weak](BrushQueueUpdate update) {
        if (auto model=weak.lock()) model->update(std::move(update));
    });
    auto changed=[weak](const auto &,std::uint64_t){if (auto model=weak.lock()) {model->cancel();model->synchronize();}};
    generation_=document_->documentGeneration.observe(changed);
    selection_=document_->selectedLayer.observe(changed);
    frameObserver_=document_->frame.observe(changed);
    revision_=document_->revision.observe([weak](std::uint64_t value,std::uint64_t) {
        if (auto model=weak.lock()) {
            if (model->committing_ && value==model->commitRevision_) return;
            model->cancel();model->synchronize();
        }
    });
    synchronize();
}
BrushToolModel::~BrushToolModel() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
ii::core::Result<void> BrushToolModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Brush tool requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Brush tool is closed");return {};
}
void BrushToolModel::synchronize() {
    if (!writable()) return;
    const auto *doc=document_->document();const auto *layer=doc && document_->hasCurrentLayerSelection()?
        findLayer(*doc,document_->selectedLayer.get()):nullptr;
    const auto at=document_->frame.get();
    const auto *asset=layer && at>=0?resolveAssetAt(*doc,*layer,static_cast<FrameIndex>(at)):nullptr;
    const bool bitmap=layer && (std::holds_alternative<StaticBitmapLayer>(*layer) || std::holds_alternative<DynamicBitmapLayer>(*layer)) &&
        asset && std::holds_alternative<RasterAsset>(*asset);
    const auto sample=bitmap?sampleLayerAt(*doc,*layer,static_cast<FrameIndex>(at)):LayerSample{};
    const auto determinant=sample.transform.m11*sample.transform.m22-sample.transform.m12*sample.transform.m21;
    const bool supported=bitmap && sample.visible && std::isfinite(determinant) && std::abs(determinant)>1e-12;
    (void)availability.set(!doc?"Open a canvas first":!supported?"Select a visible bitmap layer":!frame_?"Waiting for canvas preview":std::string{});
    (void)available.set(visible_ && supported && bool(frame_));
}
void BrushToolModel::setVisible(bool value) {
    if (!writable() || visible_==value) return;visible_=value;if (!value) cancel();synchronize();
}
void BrushToolModel::setViewport(double w,double h) {
    if (!writable() || !std::isfinite(w) || !std::isfinite(h) || w<=0 || h<=0 || w>8192 || h>8192) return;
    if (w!=width.get() || h!=height.get()) cancel();(void)width.set(w);(void)height.set(h);synchronize();
}
void BrushToolModel::setFrame(std::shared_ptr<const ii::gui::RasterImage> image) {
    if (!writable()) return;
    if (!waitingFrame_ && !committing_ && (!image || !frame_ || image->width()!=frame_->width() || image->height()!=frame_->height())) cancel();
    frame_=std::move(image);if (waitingFrame_ && frame_) acknowledgeFrame();synchronize();
}
ii::core::Result<void> BrushToolModel::setBrush(const BitmapBrush &brush) {
    if (auto valid=writable();!valid) return valid;
    BitmapEditor validator;if (!validator.setBrush(brush)) return ii::core::fail(ErrorCode::invalid_argument,validator.lastError());
    if (brush_.argb!=brush.argb) {
        char color[10];
        if ((brush.argb>>24)==255) std::snprintf(color,sizeof color,"#%06X",brush.argb&0xffffffU);
        else std::snprintf(color,sizeof color,"#%06X%02X",brush.argb&0xffffffU,brush.argb>>24);
        (void)colorText.set(color);
    }
    brush_=brush;(void)size.set(brush.size);(void)hardness.set(brush.hardness*100);
    (void)opacity.set(brush.opacity*100);(void)flow.set(brush.flow*100);return {};
}
ii::core::Result<void> BrushToolModel::setSize(double value) {auto next=brush_;next.size=value;return setBrush(next);}
ii::core::Result<void> BrushToolModel::setHardness(double value) {auto next=brush_;next.hardness=value/100;return setBrush(next);}
ii::core::Result<void> BrushToolModel::setOpacity(double value) {auto next=brush_;next.opacity=value/100;return setBrush(next);}
ii::core::Result<void> BrushToolModel::setFlow(double value) {auto next=brush_;next.flow=value/100;return setBrush(next);}
ii::core::Result<void> BrushToolModel::setColor(std::string value) {
    if (auto valid=writable();!valid) return valid;
    const auto parsed=iiColorManager::parseHexValue(value);
    if (!parsed) {(void)status.set("Enter a valid hex color");return ii::core::fail(ErrorCode::invalid_argument,status.get());}
    const auto byte=[](double channel){return static_cast<std::uint32_t>(std::lround(std::clamp(channel,0.0,1.0)*255));};
    brush_.argb=(byte(parsed->a)<<24)|(byte(parsed->r)<<16)|(byte(parsed->g)<<8)|byte(parsed->b);
    (void)colorText.set(std::move(value));(void)status.set({});return {};
}
void BrushToolModel::cancel() {
    if (!writable()) return;if (queue_) queue_->cancel();gesture_.reset();request_=0;waitingFrame_=false;
    (void)drawing.set(false);(void)busy.set(false);(void)previewImage.set({});(void)cursor.set({});
}
void BrushToolModel::acknowledgeFrame() {
    if (!writable() || !waitingFrame_) return;waitingFrame_=false;(void)previewImage.set({});(void)busy.set(false);
}
ii::core::Result<bool> BrushToolModel::input(const ii::gui::WidgetInput &event) {
    if (auto valid=writable();!valid) return std::unexpected(valid.error());
    using Kind=ii::gui::WidgetInputKind;
    if (event.kind==Kind::focus_lost || event.kind==Kind::pointer_cancel || (event.kind==Kind::key_down && event.key==ii::gui::WidgetKey::escape)) {
        const bool had=bool(gesture_);cancel();return had;
    }
    if (event.kind==Kind::pointer_leave && !gesture_) {(void)cursor.set({});return false;}
    if (!event.local_position || !std::isfinite(event.local_position->x) || !std::isfinite(event.local_position->y)) return false;
    const auto position=*event.local_position;
    if (!visible_ || !available.get()) return false;
    if (event.kind==Kind::pointer_down) {
        if (busy.get() || gesture_ || (event.buttons && !(event.buttons&1))) return false;
        auto source=document_->snapshot();const auto layerId=document_->selectedLayer.get();
        const auto *layer=source?findLayer(*source,layerId):nullptr;if (!layer || !frame_) return false;
        const auto region=documentViewRegion(*source);const auto sampled=sampleLayerAt(*source,*layer,static_cast<FrameIndex>(document_->frame.get()));
        if (region.extent.width<=0 || region.extent.height<=0 || !sampled.visible) return false;
        const auto scale=std::min(width.get()/frame_->width(),height.get()/frame_->height());
        const auto dw=frame_->width()*scale,dh=frame_->height()*scale;
        const auto left=(width.get()-dw)/2,top=(height.get()-dh)/2;
        if (!(scale>0) || position.x<left || position.y<top || position.x>left+dw || position.y>top+dh) return false;
        auto gesture=std::make_unique<Gesture>();gesture->source=source;gesture->generation=document_->documentGeneration.get();
        gesture->revision=document_->revision.get();gesture->frame=document_->frame.get();gesture->layer=layerId;
        gesture->device=event.device_id;gesture->region=region;gesture->transform=sampled.transform;
        gesture->left=left;gesture->top=top;gesture->displayWidth=dw;gesture->displayHeight=dh;
        const auto worldX=region.origin.x+(position.x-left)*region.extent.width/dw;
        const auto worldY=region.origin.y+(position.y-top)*region.extent.height/dh;
        const auto &t=gesture->transform;const auto det=t.m11*t.m22-t.m12*t.m21;
        if (!std::isfinite(det) || std::abs(det)<=1e-12) return false;
        const auto x=worldX-t.translationX,y=worldY-t.translationY;
        StrokePoint point{.position={(t.m22*x-t.m21*y)/det,(-t.m12*x+t.m11*y)/det},
            .pressure=event.pressure,.time=std::max(0.0,event.timestamp),.tiltX=event.tilt_x,.tiltY=event.tilt_y,
            .rotationRadians=event.rotation*std::numbers::pi/180};
        gesture->lastTime=point.time;auto settings=brush_;if (event.device==ii::gui::PointerDevice::eraser) settings.eraser=true;
        auto id=queue_->begin({source,gesture->generation,gesture->revision,layerId,static_cast<FrameIndex>(gesture->frame),settings,width.get(),height.get()},point);
        if (!id) {(void)status.set(id.error().message);return std::unexpected(id.error());}
        request_=*id;gesture_=std::move(gesture);(void)busy.set(true);(void)drawing.set(true);(void)status.set({});
    } else if (gesture_ && event.device_id==gesture_->device && (event.kind==Kind::pointer_move || event.kind==Kind::pointer_up)) {
        auto &g=*gesture_;const auto worldX=g.region.origin.x+(position.x-g.left)*g.region.extent.width/g.displayWidth;
        const auto worldY=g.region.origin.y+(position.y-g.top)*g.region.extent.height/g.displayHeight;
        const auto &t=g.transform;const auto det=t.m11*t.m22-t.m12*t.m21;
        const auto x=worldX-t.translationX,y=worldY-t.translationY;
        StrokePoint point{.position={(t.m22*x-t.m21*y)/det,(-t.m12*x+t.m11*y)/det},
            .pressure=event.pressure,.time=std::max(g.lastTime,event.timestamp),.tiltX=event.tilt_x,.tiltY=event.tilt_y,
            .rotationRadians=event.rotation*std::numbers::pi/180};
        g.lastTime=point.time;
        auto result=event.kind==Kind::pointer_up?queue_->finish(request_,point):queue_->move(request_,point);
        if (!result) {const auto message=result.error().message;cancel();(void)status.set(message);return std::unexpected(result.error());}
        if (event.kind==Kind::pointer_up) (void)drawing.set(false);
    } else if (event.kind!=Kind::pointer_move) return false;
    const auto *doc=document_->document();const auto *layer=doc?findLayer(*doc,document_->selectedLayer.get()):nullptr;
    if (layer && frame_) {
        const auto region=documentViewRegion(*doc);const auto t=sampleLayerAt(*doc,*layer,static_cast<FrameIndex>(document_->frame.get())).transform;
        const auto scale=std::min(width.get()/frame_->width(),height.get()/frame_->height());
        const auto sx=frame_->width()*scale/region.extent.width,sy=frame_->height()*scale/region.extent.height;
        const auto radius=brush_.size*.5*std::sqrt(std::abs((t.m11*t.m22-t.m12*t.m21)*sx*sy));
        if (std::isfinite(radius)) (void)cursor.set({true,position.x,position.y,radius});
    }
    return bool(gesture_);
}
void BrushToolModel::update(BrushQueueUpdate value) {
    if (!writable() || !gesture_ || value.requestId!=request_) return;
    if (!value.error.empty()) {auto message=std::move(value.error);cancel();(void)status.set(std::move(message));return;}
    (void)previewImage.set(std::move(value.image));
    if (!gesture_ || value.requestId!=request_ || !value.complete) return;
    if (!value.prepared) {cancel();(void)status.set("Brush stroke has no result");return;}
    const auto generation=gesture_->generation,revision=gesture_->revision;
    const auto layer=gesture_->layer;const auto frame=gesture_->frame;
    committing_=true;commitRevision_=revision+1;
    auto result=document_->commitBrushStroke(std::move(*value.prepared));committing_=false;
    if (!result) {auto message=result.error().message;cancel();(void)status.set(message);return;}
    if (!gesture_ || document_->documentGeneration.get()!=generation || document_->revision.get()!=revision+1 ||
        document_->selectedLayer.get()!=layer || document_->frame.get()!=frame) {cancel();return;}
    gesture_.reset();request_=0;waitingFrame_=true;(void)drawing.set(false);(void)status.set("Brush stroke applied");
}
void BrushToolModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Brush tool requires its owner thread");
    if (!active_) return;cancel();active_=false;visible_=false;
    generation_={};revision_={};selection_={};frameObserver_={};if (queue_) queue_->close();queue_.reset();frame_.reset();
    (void)available.set(false);
}
}
