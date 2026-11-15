#import <AppKit/AppKit.h>
#include "NativeEmbeddedSurface.h"
#include <iiGUI/Host.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include <cmath>
#include <exception>
#include <thread>
#include <utility>

namespace dreamscapes::native {
namespace {
using ii::core::ErrorCode;
bool validFrame(ii::gui::Rect value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) &&
           std::isfinite(value.height) && value.width > 0 && value.height > 0 &&
           value.width <= 8192 && value.height <= 8192 && std::abs(value.x) <= 32767 && std::abs(value.y) <= 32767;
}
NSRect nativeFrame(NSView *parent, ii::gui::Rect frame) {
    const auto bounds = parent.bounds;
    const auto y = parent.isFlipped ? NSMinY(bounds) + frame.y : NSMaxY(bounds) - frame.y - frame.height;
    return NSMakeRect(NSMinX(bounds) + frame.x, y, frame.width, frame.height);
}
}
struct NativeEmbeddedSurface::Impl final {
    const std::thread::id owner = std::this_thread::get_id();
    __strong NSView *parent = nil;
    __strong NSView *region = nil;
    Factory factory;
    Options options;
    std::optional<Content> content;
    Callbacks callbacks;
    std::unique_ptr<ii::gui::Host> host;
    std::shared_ptr<ii::runtime::Runtime> workers;
    std::optional<ii::gui::WidgetMount> mount;
    bool started = false, closing = false, hostClosed = false, joining = false, joined = false;
    bool visible = true, failed = false, finished = false;
};

NativeEmbeddedSurface::NativeEmbeddedSurface(void *parent, Factory factory, Options options, Callbacks callbacks)
    : impl_(std::make_unique<Impl>()) {
    impl_->parent = (__bridge NSView *)parent;
    impl_->factory = std::move(factory);
    impl_->options = options;
    impl_->callbacks = std::move(callbacks);
}
std::shared_ptr<NativeEmbeddedSurface> NativeEmbeddedSurface::create(void *parent,
    Factory factory, Options options, Callbacks callbacks) {
    return std::shared_ptr<NativeEmbeddedSurface>(new NativeEmbeddedSurface(parent, std::move(factory), options, std::move(callbacks)));
}
NativeEmbeddedSurface::~NativeEmbeddedSurface() = default;

ii::core::Result<void> NativeEmbeddedSurface::start(ii::gui::Rect frame) {
    auto &state = *impl_;
    if (state.owner != std::this_thread::get_id() || !NSThread.isMainThread)
        return ii::core::fail(ErrorCode::wrong_executor, "Native embedded surface requires the main thread");
    if (state.started || state.closing) return ii::core::fail(ErrorCode::busy, "Native embedded surface is single use");
    if (!state.parent || ![state.parent isKindOfClass:NSView.class] || !state.parent.window ||
        !state.factory || state.options.workers > 16 || (state.options.workers && !state.options.capacity) || !validFrame(frame))
        return ii::core::fail(ErrorCode::invalid_argument, "Native embedded surface requires an attached parent, model and valid frame");
    try { @try {
        state.region = [[NSView alloc] initWithFrame:nativeFrame(state.parent, frame)];
        state.region.hidden = !state.visible;
        [state.parent addSubview:state.region positioned:NSWindowAbove relativeTo:nil];
        state.host = std::make_unique<ii::gui::Host>();
        const std::weak_ptr<NativeEmbeddedSurface> weak = shared_from_this();
        if (auto configured = state.host->set_failure_handler([weak](const ii::core::Error &error) {
                if (auto owner = weak.lock()) owner->fail(error.message);
            }); !configured) {
            [state.region removeFromSuperview]; state.region = nil; state.host.reset();
            return std::unexpected(configured.error());
        }
        const auto self = shared_from_this();
        state.started = true;
        auto attached = state.host->attach_external((__bridge void *)state.region, {
            .opened = [weak] { if (auto owner = weak.lock()) owner->opened(); },
            .closed = [self] { self->hostClosed(); }
        });
        if (!attached) {
            state.started = false;
            [state.region removeFromSuperview]; state.region = nil; state.host.reset();
            return std::unexpected(attached.error());
        }
        return {};
    } @catch (NSException *error) {
        [state.region removeFromSuperview]; state.region = nil; state.host.reset(); state.started = false;
        return ii::core::fail(ErrorCode::failed, error.reason ? error.reason.UTF8String : "Cannot attach native embedded surface");
    } } catch (...) {
        [state.region removeFromSuperview]; state.region = nil; state.host.reset(); state.started = false;
        return ii::core::fail(ErrorCode::failed, "Cannot attach native embedded surface", std::current_exception());
    }
}

void NativeEmbeddedSurface::opened() {
    auto &state = *impl_;
    if (state.closing || !state.host) { (void)close(); return; }
    try {
        auto loop = state.host->event_loop();
        if (!loop) { fail(loop.error().message); return; }
        if (state.options.workers)
            state.workers = std::make_shared<ii::runtime::Runtime>(ii::runtime::RuntimeOptions{
                .workers = state.options.workers, .capacity = state.options.capacity});
        auto content = state.factory(loop->get(), state.workers.get());
        if (!content) { fail(content.error().message); return; }
        if (!content->build) { fail("Native embedded surface requires a content builder"); return; }
        state.content = std::move(*content);
        auto mounted = state.host->mount_widgets(state.content->build);
        if (!mounted) { fail(mounted.error().message); return; }
        state.mount = *mounted;
        if (auto ready = state.callbacks.ready) ready();
    } catch (const std::exception &error) { fail(error.what()); }
    catch (...) { fail("Native embedded surface could not be mounted"); }
}

ii::core::Result<void> NativeEmbeddedSurface::setFrame(ii::gui::Rect frame) {
    auto &state = *impl_;
    if (state.owner != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Native embedded surface requires its owner thread");
    if (!state.started || state.closing || !state.region || !state.parent || !state.parent.window)
        return ii::core::fail(ErrorCode::closed, "Native embedded surface is not active");
    if (!validFrame(frame)) return ii::core::fail(ErrorCode::invalid_argument, "Invalid native embedded surface frame");
    @try {
        state.region.frame = nativeFrame(state.parent, frame);
        if (state.mount) return state.host->external_parent_resized();
        return {};
    } @catch (NSException *error) {
        return ii::core::fail(ErrorCode::failed, error.reason ? error.reason.UTF8String : "Cannot resize native embedded surface");
    }
}
ii::core::Result<void> NativeEmbeddedSurface::setVisible(bool visible) {
    auto &state = *impl_;
    if (state.owner != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Native embedded surface requires its owner thread");
    if (!state.started || state.closing || !state.region)
        return ii::core::fail(ErrorCode::closed, "Native embedded surface is not active");
    state.visible = visible;
    state.region.hidden = !visible;
    return {};
}
ii::core::Result<void> NativeEmbeddedSurface::close() {
    auto &state = *impl_;
    if (state.owner != std::this_thread::get_id())
        return ii::core::fail(ErrorCode::wrong_executor, "Native embedded surface requires its owner thread");
    if (state.finished || state.closing) return {};
    const auto keepAlive = shared_from_this();
    state.closing = true;
    if (state.region) state.region.hidden = YES;
    drainWorkers();
    return {};
}
void NativeEmbeddedSurface::drainWorkers() {
    auto &state = *impl_;
    if (state.joining || state.joined) return;
    if (!state.workers) { state.joined = true; requestHostClose(); return; }
    state.joining = true;
    const auto workers = state.workers;
    workers->request_stop(ii::runtime::ShutdownMode::cancel_pending);
    // A parent window can retire the host before the worker has drained. The
    // close callback is then the final host-owned reference, so the pending
    // join must keep the surface alive until its result reaches the UI thread.
    const auto self = shared_from_this();
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        const bool success = bool(workers->join());
        dispatch_async(dispatch_get_main_queue(), ^{ self->workersDrained(success); });
    });
}
void NativeEmbeddedSurface::workersDrained(bool success) {
    auto &state = *impl_;
    state.joining = false;
    if (!success && !state.failed) {
        state.failed = true;
        if (auto callback = state.callbacks.failed)
            try { callback("Native embedded workers could not be retired"); } catch (...) {}
    }
    if (!success && state.workers && state.workers->stats().state != ii::runtime::RuntimeState::stopped) {
        const auto self = shared_from_this();
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            self->drainWorkers();
        });
        return;
    }
    state.joined = true;
    requestHostClose();
}
void NativeEmbeddedSurface::requestHostClose() {
    auto &state = *impl_;
    if (state.hostClosed || !state.host) { finish(); return; }
    const std::weak_ptr<NativeEmbeddedSurface> weak = shared_from_this();
    auto request = [weak] {
        if (auto owner = weak.lock(); owner && owner->impl_->host)
            (void)owner->impl_->host->request_close();
    };
    if (!state.host->executor().post(request))
        dispatch_async(dispatch_get_main_queue(), ^{ request(); });
}
void NativeEmbeddedSurface::hostClosed() {
    auto &state = *impl_;
    state.hostClosed = true;
    state.closing = true;
    if (state.joined) finish();
    else drainWorkers();
}
void NativeEmbeddedSurface::finish() {
    auto &state = *impl_;
    if (state.finished || !state.joined || (state.host && !state.hostClosed)) return;
    state.finished = true;
    state.mount.reset(); state.content.reset(); state.host.reset(); state.workers.reset();
    if (state.region) [state.region removeFromSuperview];
    state.region = nil; state.parent = nil; state.factory = {};
    auto closed = std::move(state.callbacks.closed);
    state.callbacks = {};
    if (closed) closed();
}
void NativeEmbeddedSurface::fail(std::string message) {
    auto &state = *impl_;
    if (state.failed || state.finished) return;
    state.failed = true;
    auto callback = state.callbacks.failed;
    (void)close();
    if (callback) try { callback(message); } catch (...) {}
}
std::optional<ii::gui::WidgetMount> NativeEmbeddedSurface::widgets() const {
    if (impl_->owner != std::this_thread::get_id() || impl_->closing) return {};
    return impl_->mount;
}
} // namespace dreamscapes::native
