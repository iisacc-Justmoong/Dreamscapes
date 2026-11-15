#include "App/Native/NativeLaunchLicense.h"
#include "App/Native/NativeSocietyLicense.h"
#include <iiSDKCore/Runtime/EventLoop.h>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}

class FakeProbe final : public dreamscapes::native::LicenseProbe {
  public:
    ii::core::Result<void> start(Completion value) override {++starts;completion=std::move(value);return {};}
    void stop() noexcept override {++stops;}
    Completion completion;
    int starts=0,stops=0;
};

int main() {
    try {
        using namespace dreamscapes::native;
        ii::runtime::EventLoop loop;
        auto probe=std::make_shared<FakeProbe>();
        auto license=LaunchLicense::create(loop,probe);
        good(license->start());
        check(probe->starts==1 && license->checking.get() && !license->decided.get() &&
              !license->licensed.get() && !license->gateVisible.get(),
              "startup did not leave the workspace visible while checking");
        check(!license->start() && probe->starts==1,"license check ran more than once");
        probe->completion({true,true,"licensed","fixture-subject"});
        (void)loop.poll();
        check(license->decided.get() && license->licensed.get() && !license->checking.get() &&
              !license->gateVisible.get() && license->subject.get()=="fixture-subject",
              "a verified license did not become the session decision");
        probe->completion({false,false,"society_unavailable",{}});
        (void)loop.poll();
        check(license->licensed.get() && !license->gateVisible.get() && probe->starts==1,
              "a later callback revoked the granted launch session");
        auto deniedProbe=std::make_shared<FakeProbe>();
        auto denied=LaunchLicense::create(loop,deniedProbe);
        good(denied->start());
        deniedProbe->completion({false,false,"society_unavailable",{}});
        (void)loop.poll();
        check(denied->decided.get() && !denied->licensed.get() && denied->gateVisible.get() &&
              denied->code.get()=="society_unavailable","denial did not reveal the license gate");
        deniedProbe->completion({true,true,"licensed","late"});
        (void)loop.poll();
        check(!denied->licensed.get() && denied->gateVisible.get(),"a late reply replaced the first decision");
        auto retiredProbe=std::make_shared<FakeProbe>();
        auto retired=LaunchLicense::create(loop,retiredProbe);good(retired->start());retired->close();
        retiredProbe->completion({true,true,"licensed","late"});
        (void)loop.poll();
        check(!retired->decided.get() && !retired->licensed.get(),"a closed session accepted a late callback");
        ii::runtime::EventLoop fullLoop(2);
        auto fullProbe=std::make_shared<FakeProbe>();
        auto congested=LaunchLicense::create(fullLoop,fullProbe);good(congested->start());
        bool full=false;
        for (int i=0;i<8;++i) if (!fullLoop.post([]{})) {full=true;break;}
        check(full,"license test did not fill the event queue");
        fullProbe->completion({true,true,"licensed","fixture"});
        (void)fullLoop.poll();
        check(congested->decided.get() && congested->licensed.get(),
              "a full UI queue lost the one launch license decision");
        ii::runtime::EventLoop firstLoop;
        auto firstProbe=std::make_shared<FakeProbe>();
        auto first=LaunchLicense::create(firstLoop,firstProbe);good(first->start());
        firstProbe->completion({false,false,"society_unavailable",{}});
        firstProbe->completion({true,true,"licensed","late"});
        (void)firstLoop.poll();
        check(first->decided.get() && !first->licensed.get() && first->gateVisible.get(),
              "the first decision was overwritten before event delivery");
        ii::runtime::EventLoop unavailableLoop(1);
        check(bool(unavailableLoop.post([]{})),"could not fill the event loop");
        auto unavailableProbe=std::make_shared<FakeProbe>();
        auto unavailable=LaunchLicense::create(unavailableLoop,unavailableProbe);
        check(!unavailable->start() && unavailable->decided.get() && unavailable->gateVisible.get() &&
              !unavailable->licensed.get() && unavailableProbe->starts==0,
              "failure to reserve completion delivery did not fail closed");
        ii::runtime::EventLoop rejectedLoop;
        auto rejected=LaunchLicense::create(rejectedLoop,societyLicenseProbe(rejectedLoop,
            [](ii::runtime::Task){return false;},"test-version"));
        check(!rejected->start(),"rejected device dispatch was reported successful");
        (void)rejectedLoop.poll();
        check(rejected->decided.get() && rejected->gateVisible.get() &&
              rejected->code.get()=="verification_unavailable",
              "a rejected device worker left the launch license pending");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
