#include "NativeEditorFrame.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dreamscapes::editor {
using ii::core::ErrorCode;
void RuntimeFrameExecutor::post(std::function<void()> task) {
    if (!runtime_.post(std::move(task))) throw std::runtime_error("Frame renderer is busy or stopped");
}
std::size_t RuntimeFrameExecutor::maxConcurrency() const noexcept {
    try {return std::max<std::size_t>(1,runtime_.stats().workers);} catch (...) {return 1;}
}
struct FramePresenter::Bridge final {
    struct Result {std::uint64_t id=0; std::shared_ptr<const ii::gui::RasterImage> image; std::string error;};
    std::mutex mutex;
    std::weak_ptr<iiSharedCanvas::AsyncFrameRenderQueue> queue;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    std::optional<Result> completed;
    bool closed=false;
    void complete() {
        auto source=queue.lock();
        if (!source) return;
        auto result=source->takeCompleted();
        if (!result) return;
        Result delivery; delivery.id=result->requestId;
        if (result->status!=iiSharedCanvas::AsyncFrameRenderStatus::Rendered)
            delivery.error=result->message.empty()?"Canvas rendering failed":result->message;
        else if (!result->frame.ok())
            delivery.error=result->frame.message.empty()?"Canvas rendering failed":result->frame.message;
        else if (result->frame.tiles.size()!=1)
            delivery.error="Canvas renderer returned an unexpected tile count";
        else try {
            const auto &pixels=result->frame.tiles.front().pixels;
            std::vector<std::uint8_t> rgba;
            rgba.reserve(pixels.pixels.size()*4);
            for (const auto value:pixels.pixels) {
                rgba.push_back(static_cast<std::uint8_t>(value>>16));
                rgba.push_back(static_cast<std::uint8_t>(value>>8));
                rgba.push_back(static_cast<std::uint8_t>(value));
                rgba.push_back(static_cast<std::uint8_t>(value>>24));
            }
            auto converted=ii::gui::rgba_image(static_cast<std::uint32_t>(pixels.width),
                                               static_cast<std::uint32_t>(pixels.height),std::move(rgba));
            if (converted) delivery.image=std::move(*converted);
            else delivery.error=converted.error().message;
        } catch (const std::exception &error) {delivery.error=error.what();}
          catch (...) {delivery.error="Cannot prepare canvas pixels";}
        {
            std::lock_guard lock(mutex);
            if (closed) return;
            if (!completed || completed->id<delivery.id) completed=std::move(delivery);
        }
        (void)sender.signal(wake,ii::runtime::Clock::now());
    }
};
FramePresenter::FramePresenter(std::shared_ptr<native::DocumentSession> document,
    ii::runtime::EventLoop::Sender sender,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor)
    : document_(std::move(document)),sender_(std::move(sender)),executor_(std::move(executor)) {
    if (!document_ || !executor_) throw std::invalid_argument("Canvas renderer requires a document and executor");
}
std::shared_ptr<FramePresenter> FramePresenter::create(std::shared_ptr<native::DocumentSession> document,
    ii::runtime::EventLoop &loop,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor) {
    auto value=std::shared_ptr<FramePresenter>(new FramePresenter(std::move(document),loop.sender(),std::move(executor)));
    value->start(); return value;
}
void FramePresenter::start() {
    const std::weak_ptr<FramePresenter> weak=shared_from_this();
    auto admitted=sender_.on_signal([weak]{if (auto value=weak.lock()) value->deliver();});
    if (!admitted) throw std::runtime_error("Canvas renderer could not register its owner-thread notification");
    wake_=admitted.token;
    bridge_=std::make_shared<Bridge>(); bridge_->sender=sender_; bridge_->wake=wake_;
    const std::weak_ptr<Bridge> bridge=bridge_;
    queue_=std::make_shared<iiSharedCanvas::AsyncFrameRenderQueue>(executor_,
        [bridge]{if (auto value=bridge.lock()) value->complete();});
    bridge_->queue=queue_;
    identityObserver_=document_->documentGeneration.observe([weak](const auto &,std::uint64_t){
        if (auto value=weak.lock()) value->documentChanged();
    });
    auto changed=[weak](const auto &,std::uint64_t){if (auto value=weak.lock()) value->request();};
    frameObserver_=document_->frame.observe(changed); revisionObserver_=document_->visualRevision.observe(changed);
}
FramePresenter::~FramePresenter() {
    if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}
}
ii::core::Result<void> FramePresenter::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas renderer requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Canvas renderer is closed");
    return {};
}
ii::core::Result<void> FramePresenter::setVisible(bool value) {
    if (auto result=writable(); !result) return result;
    if (visible_==value) return {};
    visible_=value;
    if (visible_) request();
    else {queue_->cancel(); request_=0; (void)current.set(false); (void)busy.set(false);}
    return {};
}
ii::core::Result<void> FramePresenter::setViewport(double w,double h) {
    if (auto result=writable(); !result) return result;
    if (!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||w>8192||h>8192)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid canvas viewport");
    if (w==width_ && h==height_) return {};
    width_=w; height_=h; if (visible_) request(); return {};
}
void FramePresenter::documentChanged() {
    if (!active_) return;
    queue_->cancel(); request_=0;
    (void)current.set(false); (void)image.set({}); (void)busy.set(false); (void)status.set({});
}
void FramePresenter::request() {
    if (!active_ || !visible_ || !document_->hasDocument.get()) return;
    (void)current.set(false);
    if (!active_ || !visible_ || !document_->hasDocument.get()) return;
    auto snapshot=document_->snapshot();
    if (!snapshot) return;
    const auto region=iiSharedCanvas::documentViewRegion(*snapshot);
    if (region.extent.width<=0 || region.extent.height<=0) {
        (void)status.set("Canvas has no visible area"); (void)busy.set(false); return;
    }
    constexpr double maximum=2048;
    const auto scale=std::min({1.0,maximum/region.extent.width,maximum/region.extent.height,
                               std::max(1.0,width_)/region.extent.width,std::max(1.0,height_)/region.extent.height});
    const iiSharedCanvas::CanvasExtent output{std::max(1,static_cast<int>(std::floor(region.extent.width*scale))),
                                              std::max(1,static_cast<int>(std::floor(region.extent.height*scale)))};
    const auto sampling=output==region.extent?iiSharedCanvas::RasterSampling::Nearest:iiSharedCanvas::RasterSampling::Smooth;
    const auto id=queue_->request(std::move(snapshot),static_cast<iiSharedCanvas::FrameIndex>(document_->frame.get()),
                                  {{region,output,sampling}});
    if (!id) {(void)status.set("Canvas render request was rejected"); (void)busy.set(false); return;}
    request_=id; (void)busy.set(true);
    (void)status.set(image.get()?std::string{}:"Rendering canvas…");
}
void FramePresenter::deliver() {
    if (!active_) return;
    std::optional<Bridge::Result> result;
    {std::lock_guard lock(bridge_->mutex); result=std::move(bridge_->completed); bridge_->completed.reset();}
    if (!result || result->id!=request_ || !visible_) return;
    if (!result->error.empty()) {(void)busy.set(false);(void)status.set(result->error); return;}
    if (!result->image) {(void)busy.set(false);(void)status.set("Canvas renderer produced no image"); return;}
    const auto delivered=result->id;
    (void)image.set(std::move(result->image));
    if (!active_ || !visible_ || request_!=delivered) return;
    (void)current.set(true);
    if (!active_ || !visible_ || request_!=delivered) return;
    (void)busy.set(false); (void)status.set({});
}
void FramePresenter::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Canvas renderer requires its owner thread");
    if (!active_) return;
    active_=false; visible_=false; request_=0;
    frameObserver_={}; revisionObserver_={}; identityObserver_={};
    if (bridge_) {std::lock_guard lock(bridge_->mutex); bridge_->closed=true; bridge_->completed.reset();}
    (void)sender_.cancel(wake_); if (queue_) queue_->cancel(); queue_.reset(); executor_.reset();
    (void)current.set(false); (void)busy.set(false); (void)image.set({}); (void)status.set({});
}
}
