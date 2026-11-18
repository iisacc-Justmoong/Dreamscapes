#include "NativeApplicationRuntime.h"
#include <utility>

namespace dreamscapes::native {
std::string describeApplicationError(const ii::core::Error &error) {
    auto result=error.message;
    if (!error.cause) return result;
    try {std::rethrow_exception(error.cause);}
    catch (const std::exception &cause) {result.append(": ").append(cause.what());}
    catch (...) {result.append(": native callback exception");}
    return result;
}
NativeApplicationRuntime::NativeApplicationRuntime(ii::runtime::Application &application, std::function<void()> onStop)
    : application_(application), generation_(application.workers()), publication_(application.workers()),
      audio_(application.workers()), preferences_({.workers=1,.capacity=2}), onStop_(std::move(onStop)) {}

std::shared_ptr<NativeApplicationRuntime> NativeApplicationRuntime::create(
    ii::runtime::Application &application, std::function<void()> onStop) {
    auto runtime=std::shared_ptr<NativeApplicationRuntime>(new NativeApplicationRuntime(application,std::move(onStop)));
    const std::weak_ptr<NativeApplicationRuntime> weak=runtime;
    runtime->stopCallback_=std::make_unique<std::stop_callback<std::function<void()>>>(
        application.stop_token(),[weak] {if (auto value=weak.lock()) value->requestStop();});
    return runtime;
}
NativeApplicationRuntime::~NativeApplicationRuntime() {requestStop();}

ii::core::Result<void> NativeApplicationRuntime::attach() {
    auto self=shared_from_this();
    return application_.attach_before_driver({"Dreamscapes.runtime",
        [](ii::runtime::Application &) -> ii::core::Result<void> {return {};},
        [self] {return self->finish();}});
}
bool NativeApplicationRuntime::post(RuntimeLane lane, ii::runtime::Task task) {
    if (!task || stopping_.load()) return false;
    switch (lane) {
    case RuntimeLane::general: return bool(application_.post(std::move(task)));
    case RuntimeLane::generation: return bool(generation_.post(std::move(task)));
    case RuntimeLane::publication: return bool(publication_.post(std::move(task)));
    case RuntimeLane::audio: return bool(audio_.post(std::move(task)));
    case RuntimeLane::preferences: return bool(preferences_.post(std::move(task)));
    }
    return false;
}
void NativeApplicationRuntime::requestStop() noexcept {
    if (stopping_.exchange(true)) return;
    try {generation_.request_close();} catch (...) {if (!stopFailure_) stopFailure_=std::current_exception();}
    try {publication_.request_close();} catch (...) {if (!stopFailure_) stopFailure_=std::current_exception();}
    try {audio_.request_close();} catch (...) {if (!stopFailure_) stopFailure_=std::current_exception();}
    try {if (onStop_) onStop_();} catch (...) {if (!stopFailure_) stopFailure_=std::current_exception();}
    try {preferences_.request_stop(ii::runtime::ShutdownMode::drain);}
    catch (...) {if (!stopFailure_) stopFailure_=std::current_exception();}
}
ii::core::Result<void> NativeApplicationRuntime::finish() {
    if (finished_) return {};
    requestStop();
    finished_=true;
    if (auto result=preferences_.join(); !result)
        return ii::core::fail(ii::core::ErrorCode::failed,"Cannot finish generation preferences worker");
    if (stopFailure_)
        return ii::core::fail(ii::core::ErrorCode::failed,"Cannot stop Dreamscapes services",stopFailure_);
    return {};
}
ii::runtime::EventLoop &NativeApplicationRuntime::eventLoop() const noexcept {return application_.event_loop();}
ii::runtime::Runtime &NativeApplicationRuntime::workers() const noexcept {return application_.workers();}
} // namespace dreamscapes::native
