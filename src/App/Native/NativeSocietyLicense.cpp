#include "NativeSocietyLicense.h"
#include <iiAccountManager/SocietySession.h>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace dreamscapes::native {
namespace {
using ii::core::ErrorCode;
using namespace iisacc::accounts::native;
class SocietyProbe final : public LicenseProbe,public std::enable_shared_from_this<SocietyProbe> {
    struct DeviceMailbox final {
        std::mutex mutex;
        std::optional<AppDevice> device;
        ii::runtime::EventLoop::Sender sender;
        ii::runtime::TaskToken wake;
        bool ready=false,closed=false;
    };
  public:
    SocietyProbe(ii::runtime::EventLoop &loop,std::function<bool(ii::runtime::Task)> worker,std::string version)
        :loop_(loop),sender_(loop.sender()),worker_(std::move(worker)),version_(std::move(version)) {
        if (!worker_ || version_.empty()) throw std::invalid_argument("Society license requires a worker and version");
    }
    ii::core::Result<void> start(Completion completion) override {
        if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Society license requires its owner thread");
        if (started_ || !completion) return ii::core::fail(ErrorCode::invalid_argument,"Society license check is unavailable");
        started_=true;active_=true;completion_=std::move(completion);
        const std::weak_ptr<SocietyProbe> weak=shared_from_this();const auto version=version_;
        auto admitted=sender_.on_signal([weak] {if (auto self=weak.lock()) self->deliverDevice();});
        if (!admitted) return ii::core::fail(ErrorCode::busy,"Cannot receive device identity");
        wake_=admitted.token;mailbox_=std::make_shared<DeviceMailbox>();mailbox_->sender=sender_;mailbox_->wake=wake_;
        auto detect=[mailbox=mailbox_,version] {
            std::optional<AppDevice> device;
            try {device=detected_device("com.iisacc.dreamscapes",version,"");} catch (...) {}
            {
                std::lock_guard lock(mailbox->mutex);
                if (mailbox->closed || mailbox->ready) return;
                mailbox->device=std::move(device);mailbox->ready=true;
            }
            (void)mailbox->sender.signal(mailbox->wake,ii::runtime::Clock::now());
        };
#if defined(__APPLE__) && TARGET_OS_IPHONE
        // UIKit device queries require the main thread; the SDK request itself
        // still performs IPC asynchronously.
        if (!sender_.post(std::move(detect))) return ii::core::fail(ErrorCode::failed,"Cannot schedule device detection");
#else
        if (!worker_(std::move(detect))) return ii::core::fail(ErrorCode::busy,"Cannot schedule device detection");
#endif
        return {};
    }
    void stop() noexcept override {
        active_=false;completion_={};
        retireMailbox();
        if (session_) {
            session_->changed={};
            try {session_->cancel();session_.reset();} catch (...) {session_.reset();}
        }
    }
    ~SocietyProbe() override {stop();}
  private:
    void retireMailbox() noexcept {
        try {if (mailbox_) {std::lock_guard lock(mailbox_->mutex);mailbox_->closed=true;mailbox_->device.reset();}}
        catch (...) {}
        (void)sender_.cancel(wake_);
    }
    void deliverDevice() {
        if (!active_ || !mailbox_) return;
        std::optional<AppDevice> device;
        {std::lock_guard lock(mailbox_->mutex);if (!mailbox_->ready) return;device=std::move(mailbox_->device);}
        retireMailbox();deviceReady(std::move(device));
    }
    void finish(LicenseDecision result) {
        if (!active_ || delivered_) return;
        delivered_=true;auto callback=completion_;if (callback) callback(std::move(result));
    }
    void deviceReady(std::optional<AppDevice> device) {
        if (!active_ || delivered_) return;
        if (!device || device->id.size()!=64) {finish({false,false,"device_identity_unavailable",{}});return;}
        try {
            session_=std::make_unique<SocietySession>(loop_,SocietySessionOptions{.product_id="dreamscapes",.device=std::move(*device)});
            const std::weak_ptr<SocietyProbe> weak=shared_from_this();
            session_->changed=[weak](SocietySessionSnapshot value) {
                if (auto self=weak.lock();self && !value.checking)
                    self->finish({value.connected,value.licensed,std::move(value.code),std::move(value.subject)});
            };
            session_->refresh(true);
        } catch (...) {finish({false,false,"society_unavailable",{}});}
    }
    const std::thread::id owner_=std::this_thread::get_id();
    ii::runtime::EventLoop &loop_;
    ii::runtime::EventLoop::Sender sender_;
    std::function<bool(ii::runtime::Task)> worker_;
    std::string version_;
    Completion completion_;
    std::shared_ptr<DeviceMailbox> mailbox_;
    ii::runtime::TaskToken wake_;
    std::unique_ptr<SocietySession> session_;
    bool started_=false,active_=false,delivered_=false;
};
}
std::shared_ptr<LicenseProbe> societyLicenseProbe(ii::runtime::EventLoop &loop,
    std::function<bool(ii::runtime::Task)> worker,std::string version) {
    return std::make_shared<SocietyProbe>(loop,std::move(worker),std::move(version));
}
}
