#include "NativeBrushQueue.h"
#include <Render/FrameRenderer.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dreamscapes::editor {
using ii::core::ErrorCode;
using namespace iiSharedCanvas;
namespace {
constexpr std::size_t maximumQueuedSamples=4096,maximumSamplesPerBatch=64;
ii::core::Result<std::shared_ptr<const ii::gui::RasterImage>> renderPreview(
    const Document &document,FrameIndex frame,double width,double height) {
    const auto region=documentViewRegion(document);
    if (region.extent.width<=0 || region.extent.height<=0)
        return ii::core::fail(ErrorCode::invalid_argument,"The canvas has no visible area");
    constexpr double maximum=2048;
    const auto scale=std::min({1.0,maximum/region.extent.width,maximum/region.extent.height,
        std::max(1.0,width)/region.extent.width,std::max(1.0,height)/region.extent.height});
    const CanvasExtent extent{std::max(1,static_cast<int>(std::floor(region.extent.width*scale))),
                              std::max(1,static_cast<int>(std::floor(region.extent.height*scale)))};
    auto result=renderFrameRegion(document,frame,region,extent,
        extent==region.extent?RasterSampling::Nearest:RasterSampling::Smooth);
    if (!result.ok()) return ii::core::fail(ErrorCode::failed,
        result.message.empty()?"Cannot render the brush preview":result.message);
    const auto &pixels=result.pixels;
    if (pixels.width!=extent.width || pixels.height!=extent.height ||
        pixels.pixels.size()!=std::uint64_t(extent.width)*extent.height)
        return ii::core::fail(ErrorCode::failed,"The brush preview has invalid pixels");
    std::vector<std::uint8_t> rgba;rgba.reserve(pixels.pixels.size()*4);
    for (const auto value:pixels.pixels) {
        rgba.push_back(static_cast<std::uint8_t>(value>>16));rgba.push_back(static_cast<std::uint8_t>(value>>8));
        rgba.push_back(static_cast<std::uint8_t>(value));rgba.push_back(static_cast<std::uint8_t>(value>>24));
    }
    return ii::gui::rgba_image(static_cast<std::uint32_t>(extent.width),static_cast<std::uint32_t>(extent.height),std::move(rgba));
}
}
struct BrushQueue::Job final {
    struct Command final {StrokePoint point;bool finish=false;};
    std::mutex mutex;
    BrushQueueRequest request;
    std::deque<Command> commands;
    std::unique_ptr<BrushStroke> stroke;
    std::atomic_bool cancelled{false};
    std::uint64_t id=0;
    bool running=false,ending=false,finished=false;
};
struct BrushQueue::Bridge final {
    std::mutex mutex;
    std::optional<BrushQueueUpdate> pending;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};
BrushQueue::BrushQueue(ii::runtime::EventLoop::Sender sender,std::shared_ptr<FrameRenderExecutor> executor,Callback callback)
    :sender_(std::move(sender)),executor_(std::move(executor)),callback_(std::move(callback)) {
    if (!executor_ || !callback_) throw std::invalid_argument("Brush queue requires a worker and callback");
}
std::shared_ptr<BrushQueue> BrushQueue::create(ii::runtime::EventLoop &loop,std::shared_ptr<FrameRenderExecutor> executor,Callback callback) {
    auto value=std::shared_ptr<BrushQueue>(new BrushQueue(loop.sender(),std::move(executor),std::move(callback)));
    value->start();return value;
}
void BrushQueue::start() {
    const std::weak_ptr<BrushQueue> weak=shared_from_this();
    const auto signal=sender_.on_signal([weak]{if (auto self=weak.lock()) self->deliver();});
    if (!signal) throw std::runtime_error("Cannot register brush completion");
    wake_=signal.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
}
BrushQueue::~BrushQueue() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
ii::core::Result<void> BrushQueue::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Brush queue requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Brush queue is closed");
    return {};
}
ii::core::Result<std::uint64_t> BrushQueue::begin(BrushQueueRequest request,const StrokePoint &point) {
    if (auto valid=writable();!valid) return std::unexpected(valid.error());
    if (job_) return ii::core::fail(ErrorCode::busy,"A brush stroke is already active");
    if (!request.source || request.layerId.empty() || !std::isfinite(request.viewportWidth) || !std::isfinite(request.viewportHeight) ||
        request.viewportWidth<=0 || request.viewportHeight<=0 || request.viewportWidth>8192 || request.viewportHeight>8192)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid brush preview request");
    if (next_==std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range,"Brush request limit reached");
    try {
        auto job=std::make_shared<Job>();job->id=++next_;job->request=std::move(request);
        job->commands.push_back({point,false});job->running=true;job_=job;
        auto bridge=bridge_;executor_->post([job,bridge]{run(job,bridge);});
        return job->id;
    } catch (...) {
        if (job_) {job_->cancelled.store(true,std::memory_order_relaxed);job_.reset();}
        return ii::core::fail(ErrorCode::failed,"Brush worker is unavailable",std::current_exception());
    }
}
ii::core::Result<void> BrushQueue::move(std::uint64_t id,const StrokePoint &point) {return enqueue(id,point,false);}
ii::core::Result<void> BrushQueue::finish(std::uint64_t id,const StrokePoint &point) {return enqueue(id,point,true);}
ii::core::Result<void> BrushQueue::enqueue(std::uint64_t id,const StrokePoint &point,bool finishValue) {
    if (auto valid=writable();!valid) return valid;
    auto job=job_;
    if (!job || job->id!=id) return ii::core::fail(ErrorCode::closed,"Brush stroke is no longer active");
    bool schedule=false,overflow=false;
    try {
        {
            std::lock_guard lock(job->mutex);
            if (job->cancelled.load(std::memory_order_relaxed) || job->finished || job->ending)
                return ii::core::fail(ErrorCode::closed,"Brush stroke is no longer accepting input");
            if (job->commands.size()>=maximumQueuedSamples) overflow=true;
            else {
                job->commands.push_back({point,finishValue});job->ending=finishValue;
                if (!job->running) {job->running=true;schedule=true;}
            }
        }
        if (overflow) {cancel();return ii::core::fail(ErrorCode::busy,"Brush input exceeded the worker queue");}
        if (schedule) {auto bridge=bridge_;executor_->post([job,bridge]{run(job,bridge);});}
        return {};
    } catch (...) {cancel();return ii::core::fail(ErrorCode::failed,"Brush worker is unavailable",std::current_exception());}
}
void BrushQueue::publish(const std::shared_ptr<Job> &job,const std::shared_ptr<Bridge> &bridge,BrushQueueUpdate update) {
    if (job->cancelled.load(std::memory_order_relaxed)) return;
    {
        std::lock_guard lock(bridge->mutex);
        if (bridge->closed || job->cancelled.load(std::memory_order_relaxed)) return;
        if (bridge->pending && bridge->pending->requestId>update.requestId) return;
        bridge->pending=std::move(update);
    }
    (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
}
void BrushQueue::run(const std::shared_ptr<Job> &job,const std::shared_ptr<Bridge> &bridge) {
    auto fail=[&](std::string message) {
        {std::lock_guard lock(job->mutex);job->finished=true;job->running=false;job->commands.clear();}
        job->stroke.reset();publish(job,bridge,{.requestId=job->id,.error=std::move(message),.complete=true});
    };
    try {
        for (;;) {
            std::vector<Job::Command> batch;
            {
                std::lock_guard lock(job->mutex);
                if (job->cancelled.load(std::memory_order_relaxed)) {job->running=false;return;}
                const auto count=std::min(maximumSamplesPerBatch,job->commands.size());batch.reserve(count);
                for (std::size_t i=0;i<count;++i) {batch.push_back(std::move(job->commands.front()));job->commands.pop_front();}
                if (batch.empty()) {job->running=false;return;}
            }
            std::optional<PreparedBrushStroke> prepared;
            for (const auto &command:batch) {
                if (job->cancelled.load(std::memory_order_relaxed)) return;
                if (!job->stroke) {
                    const auto &request=job->request;
                    auto made=BrushStroke::create(request.source,request.generation,request.revision,request.layerId,request.frame,request.brush);
                    if (!made) {fail(made.error().message);return;}
                    job->stroke=std::move(*made);
                    auto started=job->stroke->begin(command.point);
                    if (!started) {fail(started.error().message);return;}
                } else if (command.finish) {
                    auto finished=job->stroke->finish(command.point);
                    if (!finished) {fail(finished.error().message);return;}
                    prepared=std::move(*finished);
                } else {
                    auto moved=job->stroke->move(command.point);
                    if (!moved) {fail(moved.error().message);return;}
                }
            }
            if (job->cancelled.load(std::memory_order_relaxed)) return;
            const auto &document=prepared?*prepared->candidate:job->stroke->document();
            auto image=renderPreview(document,job->request.frame,job->request.viewportWidth,job->request.viewportHeight);
            if (!image) {fail(image.error().message);return;}
            if (prepared) {
                {std::lock_guard lock(job->mutex);job->finished=true;job->running=false;}
                job->stroke.reset();
                publish(job,bridge,{.requestId=job->id,.image=std::move(*image),.prepared=std::move(prepared),.complete=true});
                return;
            }
            publish(job,bridge,{.requestId=job->id,.image=std::move(*image)});
        }
    } catch (...) {fail("Cannot process the brush stroke");}
}
void BrushQueue::deliver() {
    if (!writable()) return;
    std::optional<BrushQueueUpdate> update;
    {std::lock_guard lock(bridge_->mutex);update=std::move(bridge_->pending);bridge_->pending.reset();}
    if (!update || !job_ || update->requestId!=job_->id) return;
    if (update->complete) job_.reset();
    auto callback=callback_;if (callback) callback(std::move(*update));
}
void BrushQueue::cancel() {
    if (!writable()) return;
    auto retired=std::move(job_);if (!retired) return;
    retired->cancelled.store(true,std::memory_order_relaxed);
    {std::lock_guard lock(retired->mutex);retired->commands.clear();}
    if (executor_) try {executor_->post([retired=std::move(retired)] {});} catch (...) {}
}
void BrushQueue::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Brush queue requires its owner thread");
    if (!active_) return;
    cancel();active_=false;
    if (bridge_) {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->pending.reset();}
    (void)sender_.cancel(wake_);callback_={};executor_.reset();
}
}
