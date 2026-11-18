#pragma once
#include "NativeBrushStroke.h"
#include <Render/AsyncFrameRenderQueue.h>
#include <iiGUI>
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace dreamscapes::editor {
struct BrushQueueRequest final {
    std::shared_ptr<const iiSharedCanvas::Document> source;
    std::uint64_t generation=0,revision=0;
    std::string layerId;
    iiSharedCanvas::FrameIndex frame=0;
    iiSharedCanvas::BitmapBrush brush;
    double viewportWidth=0,viewportHeight=0;
};
struct BrushQueueUpdate final {
    std::uint64_t requestId=0;
    std::shared_ptr<const ii::gui::RasterImage> image;
    std::optional<PreparedBrushStroke> prepared;
    std::string error;
    bool complete=false;
};
// Input methods and callbacks belong to the owner event loop. The injected
// executor serializes each gesture's pixel edits and composited previews away
// from that loop; no candidate document crosses threads while being mutated.
class BrushQueue final : public std::enable_shared_from_this<BrushQueue> {
  public:
    using Callback=std::function<void(BrushQueueUpdate)>;
    static std::shared_ptr<BrushQueue> create(ii::runtime::EventLoop &,
        std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>,Callback);
    ~BrushQueue();
    [[nodiscard]] ii::core::Result<std::uint64_t> begin(BrushQueueRequest,const StrokePoint &);
    [[nodiscard]] ii::core::Result<void> move(std::uint64_t,const StrokePoint &);
    [[nodiscard]] ii::core::Result<void> finish(std::uint64_t,const StrokePoint &);
    void cancel();
    void close();
  private:
    struct Job;
    struct Bridge;
    BrushQueue(ii::runtime::EventLoop::Sender,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>,Callback);
    void start();
    void deliver();
    [[nodiscard]] ii::core::Result<void> enqueue(std::uint64_t,const StrokePoint &,bool finish);
    [[nodiscard]] ii::core::Result<void> writable() const;
    static void run(const std::shared_ptr<Job> &,const std::shared_ptr<Bridge> &);
    static void publish(const std::shared_ptr<Job> &,const std::shared_ptr<Bridge> &,BrushQueueUpdate);
    const std::thread::id owner_=std::this_thread::get_id();
    ii::runtime::EventLoop::Sender sender_;
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor_;
    Callback callback_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<Job> job_;
    ii::runtime::TaskToken wake_;
    std::uint64_t next_=0;
    bool active_=true;
};
}
