#pragma once
#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <iiSDKCore/Runtime/EventLoop.h>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::native {
struct LicenseDecision final {
    bool connected=false,licensed=false;
    std::string code,subject;
};
class LicenseProbe {
  public:
    using Completion=std::function<void(LicenseDecision)>;
    virtual ~LicenseProbe()=default;
    virtual ii::core::Result<void> start(Completion)=0;
    virtual void stop() noexcept=0;
};

// A single immutable per-process decision; completion is always marshalled to
// the owner event loop, including when a probe completes synchronously.
class LaunchLicense final : public std::enable_shared_from_this<LaunchLicense> {
  public:
    static std::shared_ptr<LaunchLicense> create(ii::runtime::EventLoop &,std::shared_ptr<LicenseProbe>);
    ~LaunchLicense();
    [[nodiscard]] ii::core::Result<void> start();
    void close();
    ii::core::Observable<bool> checking{false},decided{false},connected{false},licensed{false},gateVisible{false};
    ii::core::Observable<std::string> code,subject;
  private:
    struct Bridge;
    LaunchLicense(ii::runtime::EventLoop::Sender,std::shared_ptr<LicenseProbe>);
    void complete(LicenseDecision);
    void deliver();
    void retireBridge() noexcept;
    const std::thread::id owner_=std::this_thread::get_id();
    ii::runtime::EventLoop::Sender sender_;
    std::shared_ptr<LicenseProbe> probe_;
    std::shared_ptr<Bridge> bridge_;
    ii::runtime::TaskToken wake_;
    bool active_=true,started_=false,decisionMade_=false;
};
}
