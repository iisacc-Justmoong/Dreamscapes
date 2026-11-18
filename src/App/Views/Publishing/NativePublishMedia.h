#pragma once
#include "App/Native/NativeDocumentSession.h"
#include <iiSocialIntegration>
#include <Render/AsyncFrameRenderQueue.h>
#include <iiSDKCore>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::publishing {
struct PublicationMediaLimits final {
    std::size_t maximumCanvases=256;
    std::uint64_t maximumTotalPixels=256ULL*1024*1024;
    std::uint64_t maximumTotalBytes=512ULL*1024*1024;
};
struct PublicationStaging;
struct PreparedPublicationMedia final {
    iiSocialIntegration::ReviewSource source;
    std::shared_ptr<PublicationStaging> staging;
    iiSharedCanvas::FrameIndex frame=0;
};
// Worker-only. The returned lease owns its private temporary files.
[[nodiscard]] ii::core::Result<PreparedPublicationMedia> preparePublicationMedia(
    std::shared_ptr<const iiSharedCanvas::Document>,iiSharedCanvas::FrameIndex,
    const std::filesystem::path &stagingRoot,std::function<bool()> cancelled={},PublicationMediaLimits limits={});

class PublicationMediaModel final : public std::enable_shared_from_this<PublicationMediaModel> {
  public:
    static std::shared_ptr<PublicationMediaModel> create(std::shared_ptr<native::DocumentSession>,
        ii::runtime::EventLoop &,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>,
        std::filesystem::path stagingRoot={},PublicationMediaLimits limits={});
    ~PublicationMediaModel();
    void setVisible(bool);
    void close();
    ii::core::Observable<std::shared_ptr<const PreparedPublicationMedia>> prepared;
    ii::core::Observable<bool> busy{false};
    ii::core::Observable<std::string> status{"Publication media unavailable"};
  private:
    struct Bridge;
    PublicationMediaModel(std::shared_ptr<native::DocumentSession>,ii::runtime::EventLoop::Sender,
        std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>,std::filesystem::path,PublicationMediaLimits);
    void start();
    void request();
    void metadataChanged();
    void schedule();
    void deliver();
    void retire(std::shared_ptr<const PreparedPublicationMedia>);
    [[nodiscard]] bool writable() const noexcept;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::runtime::EventLoop::Sender sender_;
    ii::runtime::TaskToken wake_;
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::filesystem::path stagingRoot_;
    PublicationMediaLimits limits_;
    ii::core::ObservableSubscription generation_,visual_,frame_,revision_;
    std::vector<iiSharedCanvas::CanvasPublicationMetadata> canvases_;
    std::uint64_t desired_=0,inFlight_=0;
    bool active_=true,visible_=false;
};
}
