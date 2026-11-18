#pragma once
#include "App/Native/NativeDocumentSession.h"
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <thread>

namespace dreamscapes::editor {
class TimelinePlayback final : public std::enable_shared_from_this<TimelinePlayback> {
  public:
    using Now=std::function<ii::runtime::Clock::time_point()>;
    static std::shared_ptr<TimelinePlayback> create(std::shared_ptr<native::DocumentSession>,
        ii::runtime::EventLoop &,Now now=[] {return ii::runtime::Clock::now();});
    ~TimelinePlayback();
    [[nodiscard]] ii::core::Result<void> play();
    [[nodiscard]] ii::core::Result<void> pause();
    // Advances from monotonic time; exposed for deterministic scheduling and tests.
    [[nodiscard]] ii::core::Result<void> advance();
    [[nodiscard]] ii::runtime::Clock::time_point origin() const noexcept {return origin_;}
    void close();
    ii::core::Observable<bool> playing{false};
  private:
    TimelinePlayback(std::shared_ptr<native::DocumentSession>,ii::runtime::EventLoop::Sender,Now);
    void start();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::runtime::EventLoop::Sender sender_;
    Now now_;
    ii::runtime::TaskToken timer_;
    ii::runtime::Clock::time_point origin_;
    ii::core::ObservableSubscription generationObserver_,frameObserver_;
    std::uint64_t generation_=0;
    std::int64_t first_=0;
    std::uint32_t numerator_=0,denominator_=0;
    bool active_=true,internalSeek_=false;
};
}
