#include "NativeApplicationLifecycle.h"
#include <algorithm>
#include <exception>
#include <utility>

namespace dreamscapes::native {
ii::core::Result<void> NativeApplicationLifecycle::add(std::string name, Handler handler) {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id())
        return ii::core::fail(ErrorCode::wrong_executor, "Application lifecycle requires its owner thread");
    if (phase_ != Phase::accepting)
        return ii::core::fail(ErrorCode::closed, "Application shutdown has started");
    if (name.empty() || !handler)
        return ii::core::fail(ErrorCode::invalid_argument, "Shutdown handler requires a name and callback");
    if (std::ranges::any_of(handlers_, [&](const auto &entry) { return entry.name == name; }))
        return ii::core::fail(ErrorCode::already_exists, "Duplicate shutdown handler: " + name);
    try { handlers_.push_back({std::move(name), std::move(handler)}); }
    catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot register shutdown handler", std::current_exception()); }
    return {};
}
ii::core::Result<void> NativeApplicationLifecycle::stop() {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id())
        return ii::core::fail(ErrorCode::wrong_executor, "Application lifecycle requires its owner thread");
    if (phase_ == Phase::stopping)
        return ii::core::fail(ErrorCode::busy, "Application shutdown is already in progress");
    if (phase_ == Phase::stopped)
        return failure_ ? ii::core::Result<void>{std::unexpected(*failure_)} : ii::core::Result<void>{};
    phase_ = Phase::stopping;
    for (const auto &entry : handlers_) {
        try { entry.handler(); }
        catch (...) {
            if (!failure_) failure_ = ii::core::Error{
                ErrorCode::failed, "Application shutdown handler failed: " + entry.name, std::current_exception()};
        }
    }
    handlers_.clear();
    phase_ = Phase::stopped;
    return failure_ ? ii::core::Result<void>{std::unexpected(*failure_)} : ii::core::Result<void>{};
}
} // namespace dreamscapes::native
