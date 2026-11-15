#include "NativeLaunchLicense.h"
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dreamscapes::native {
using ii::core::ErrorCode;
struct LaunchLicense::Bridge final {
    std::mutex mutex;
    std::optional<LicenseDecision> decision;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};
LaunchLicense::LaunchLicense(ii::runtime::EventLoop::Sender sender,std::shared_ptr<LicenseProbe> probe)
    :sender_(std::move(sender)),probe_(std::move(probe)) {
    if (!probe_) throw std::invalid_argument("Launch license requires a probe");
}
std::shared_ptr<LaunchLicense> LaunchLicense::create(ii::runtime::EventLoop &loop,std::shared_ptr<LicenseProbe> probe) {
    return std::shared_ptr<LaunchLicense>(new LaunchLicense(loop.sender(),std::move(probe)));
}
LaunchLicense::~LaunchLicense() {retireBridge();if (probe_) probe_->stop();}
ii::core::Result<void> LaunchLicense::start() {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"License session requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"License session is closed");
    if (started_) return ii::core::fail(ErrorCode::busy,"The launch license was already checked");
    const auto keep=shared_from_this();started_=true;(void)checking.set(true);
    if (!active_ || !probe_) return ii::core::fail(ErrorCode::cancelled,"License session is closed");
    const std::weak_ptr<LaunchLicense> weak=keep;
    auto admitted=sender_.on_signal([weak] {if (auto owner=weak.lock()) owner->deliver();});
    if (!admitted) {
        complete({false,false,"verification_unavailable",{}});
        return ii::core::fail(ErrorCode::busy,"Cannot receive the Society license decision");
    }
    wake_=admitted.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
    auto deliver=[bridge=bridge_](LicenseDecision result) {
        {
            std::lock_guard lock(bridge->mutex);
            if (bridge->closed || bridge->decision) return;
            bridge->decision=std::move(result);
        }
        (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
    };
    try {
        auto source=probe_;
        auto result=source->start(deliver);
        if (!result) deliver({false,false,"verification_unavailable",{}});
        return result;
    } catch (...) {
        deliver({false,false,"verification_unavailable",{}});
        return ii::core::fail(ErrorCode::failed,"Cannot start the Society license check",std::current_exception());
    }
}
void LaunchLicense::deliver() {
    if (!active_ || !bridge_) return;
    std::optional<LicenseDecision> value;
    {std::lock_guard lock(bridge_->mutex);value=std::move(bridge_->decision);bridge_->decision.reset();}
    if (value) complete(std::move(*value));
}
void LaunchLicense::retireBridge() noexcept {
    try {if (bridge_) {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->decision.reset();}}
    catch (...) {}
    (void)sender_.cancel(wake_);
}
void LaunchLicense::complete(LicenseDecision value) {
    if (owner_!=std::this_thread::get_id() || !active_ || decisionMade_) return;
    const auto keep=shared_from_this();decisionMade_=true;retireBridge();
    auto probe=std::exchange(probe_,{});if (probe) probe->stop();
    const bool granted=value.connected && value.licensed;
    (void)code.set(std::move(value.code));if (!active_) return;
    (void)connected.set(value.connected);if (!active_) return;
    (void)subject.set(value.connected?std::move(value.subject):std::string{});if (!active_) return;
    (void)licensed.set(granted);if (!active_) return;
    (void)checking.set(false);if (!active_) return;
    (void)decided.set(true);if (!active_) return;
    (void)gateVisible.set(!granted);
}
void LaunchLicense::close() {
    if (owner_!=std::this_thread::get_id() || !active_) return;
    active_=false;retireBridge();auto probe=std::exchange(probe_,{});if (probe) probe->stop();
    (void)checking.set(false);
}
}
