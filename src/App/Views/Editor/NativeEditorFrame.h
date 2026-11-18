#pragma once
#include "App/Native/NativeDocumentSession.h"
#include <Render/AsyncFrameRenderQueue.h>
#include <iiGUI>
#include <iiSDKCore>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::editor {
// The host owns the runtime and must keep it alive until this executor and its
// accepted tasks have retired.
class RuntimeFrameExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:
    explicit RuntimeFrameExecutor(ii::runtime::Runtime &runtime) : runtime_(runtime) {}
    void post(std::function<void()> task) override;
    [[nodiscard]] std::size_t maxConcurrency() const noexcept override;
  private:
    ii::runtime::Runtime &runtime_;
};

class FramePresenter final : public std::enable_shared_from_this<FramePresenter> {
  public:
    static std::shared_ptr<FramePresenter> create(std::shared_ptr<native::DocumentSession>,
        ii::runtime::EventLoop &,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>);
    ~FramePresenter();
    [[nodiscard]] ii::core::Result<void> setVisible(bool);
    [[nodiscard]] ii::core::Result<void> setViewport(double,double);
    void close();
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> image;
    ii::core::Observable<bool> busy{false},current{false};
    ii::core::Observable<std::string> status;
  private:
    struct Bridge;
    FramePresenter(std::shared_ptr<native::DocumentSession>,ii::runtime::EventLoop::Sender,
                   std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>);
    void start();
    void request();
    void documentChanged();
    void deliver();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::runtime::EventLoop::Sender sender_;
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<iiSharedCanvas::AsyncFrameRenderQueue> queue_;
    ii::runtime::TaskToken wake_;
    ii::core::ObservableSubscription frameObserver_,revisionObserver_,identityObserver_;
    std::uint64_t request_=0;
    double width_=1,height_=1;
    bool visible_=false,active_=true;
};
}
