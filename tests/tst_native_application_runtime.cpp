#include "App/Native/NativeApplicationRuntime.h"
#include "App/Native/NativeApplicationLifecycle.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace std::chrono_literals;
using dreamscapes::native::NativeApplicationRuntime;
using dreamscapes::native::NativeApplicationLifecycle;
using dreamscapes::native::RuntimeLane;

void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void good(ii::core::Result<void> value) {
    if (!value) throw std::runtime_error(value.error().message);
}
void lifecycle(bool failDriver) {
    ii::runtime::Application app({}, {.workers={.workers=4,.capacity=32}});
    auto releaseGeneration=std::make_shared<std::promise<void>>();
    auto releasePreferences=std::make_shared<std::promise<void>>();
    auto generationGate=releaseGeneration->get_future().share();
    auto preferencesGate=releasePreferences->get_future().share();
    std::promise<void> generationEntered,preferencesEntered,audioEntered,publicationEntered,generalEntered;
    auto generationReady=generationEntered.get_future();
    auto preferencesReady=preferencesEntered.get_future();
    auto audioReady=audioEntered.get_future();
    auto publicationReady=publicationEntered.get_future();
    auto generalReady=generalEntered.get_future();
    std::atomic<int> stopped=0,saved=0,events=0,lateGeneration=0;
    auto runtime=NativeApplicationRuntime::create(app,[&] {
        if (stopped.fetch_add(1)==0) {releaseGeneration->set_value();releasePreferences->set_value();}
    });
    check(&runtime->eventLoop()==&app.event_loop(),"window and application event loops differ");
    check(&runtime->workers()==&app.workers(),"worker runtime is not owned by the application");
    good(runtime->attach());
    auto duplicate=runtime->attach();
    check(!duplicate && duplicate.error().code==ii::core::ErrorCode::already_exists,"runtime was attached twice");
    good(app.set_driver({[&](const ii::runtime::ApplicationStart &start) -> ii::core::Result<void> {
        if (auto started=start(); !started) return started;
        check(runtime->post(RuntimeLane::generation,[&] {generationEntered.set_value();generationGate.wait();}),
              "generation work was not admitted");
        check(runtime->post(RuntimeLane::generation,[&] {++lateGeneration;}),"second generation task was not admitted");
        check(runtime->post(RuntimeLane::audio,[&] {audioEntered.set_value();generationGate.wait();}),
              "audio work was not admitted");
        check(runtime->post(RuntimeLane::publication,[&] {publicationEntered.set_value();generationGate.wait();}),
              "publication work was not admitted");
        check(runtime->post(RuntimeLane::general,[&] {generalEntered.set_value();}),"general work was not admitted");
        check(runtime->post(RuntimeLane::preferences,[&] {preferencesEntered.set_value();preferencesGate.wait();}),
              "preferences work was not admitted");
        check(runtime->post(RuntimeLane::preferences,[&] {++saved;}),"queued preference write was not admitted");
        check(generationReady.wait_for(3s)==std::future_status::ready,"generation worker did not start");
        check(audioReady.wait_for(3s)==std::future_status::ready,"audio lane did not start");
        check(publicationReady.wait_for(3s)==std::future_status::ready,"publication lane did not start");
        check(generalReady.wait_for(3s)==std::future_status::ready,"long operations starved general work");
        check(preferencesReady.wait_for(3s)==std::future_status::ready,"preferences worker did not start");
        check(bool(app.event_loop().post([&] {++events;})),"Core event was not accepted");
        (void)app.event_loop().poll();
        if (failDriver) throw std::runtime_error("fixture driver failure");
        return {};
    }, [] -> ii::core::Result<void> {return {};}}));
    const auto result=app.exec();
    check(failDriver ? !result : bool(result),"driver result was not preserved");
    check(stopped==1,"shutdown was not delivered exactly once before workers joined");
    check(saved==1,"accepted preference write was lost during shutdown");
    check(lateGeneration==0,"queued generation started after shutdown began");
    check(events==1,"Core event loop did not deliver its event");
    check(!runtime->post(RuntimeLane::general,[] {}),"work was accepted after shutdown");
    check(app.state()==ii::runtime::ApplicationState::stopped,"Core application did not stop");
}
void cleanupFailure() {
    ii::runtime::Application app;
    NativeApplicationLifecycle lifecycle;
    int retired=0;
    good(lifecycle.add("failing service", [] { throw std::runtime_error("fixture"); }));
    good(lifecycle.add("remaining service", [&] { ++retired; }));
    auto runtime=NativeApplicationRuntime::create(app,[&] {
        auto stopped=lifecycle.stop();
        if (!stopped) throw std::runtime_error(stopped.error().message);
    });
    good(runtime->attach());
    good(app.set_driver({[](const ii::runtime::ApplicationStart &start) { return start(); },
                         [] -> ii::core::Result<void> { return {}; }}));
    const auto result=app.exec();
    check(!result && retired==1 && app.state()==ii::runtime::ApplicationState::stopped,
          "Core did not finish remaining cleanup after a service failure");
    check(dreamscapes::native::describeApplicationError(result.error()).find("failing service")!=std::string::npos,
          "Core lost the failed lifecycle handler name");
}
}
int main() {
    try {
        const ii::core::Error error{ii::core::ErrorCode::failed,"Native startup failed",
                                   std::make_exception_ptr(std::runtime_error("fixture cause"))};
        check(dreamscapes::native::describeApplicationError(error)=="Native startup failed: fixture cause",
              "native callback failure lost its underlying cause");
        lifecycle(false);lifecycle(true);cleanupFailure();
    }
    catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
