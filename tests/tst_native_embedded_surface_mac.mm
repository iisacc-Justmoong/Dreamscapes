#import <AppKit/AppKit.h>
#include "App/Native/NativeEmbeddedSurface.h"
#include "App/Views/Home/NativeHomeQueueView.h"
#include <QCoreApplication>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void stop(NSApplication *app) {
    [app stop:nil];
    [app postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
        modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:YES];
}
struct State final {
    std::shared_ptr<dreamscapes::native::NativeEmbeddedSurface> surface;
    std::shared_ptr<dreamscapes::home::HomeQueueModel> model;
    __strong NSWindow *window = nil;
    __strong NSView *parent = nil;
    NSUInteger originalSubviews = 0;
    std::string svg, error;
    std::weak_ptr<int> resource;
    int opened = 0, ready = 0, closed = 0, failures = 0;
};
void startFailure(const std::shared_ptr<State> &state, NSApplication *application) {
    const std::weak_ptr<State> weak = state;
    state->surface = dreamscapes::native::NativeEmbeddedSurface::create((__bridge void *)state->parent,
        [](ii::runtime::EventLoop &, ii::runtime::Runtime *) -> ii::core::Result<dreamscapes::native::NativeEmbeddedSurface::Content> {
            return ii::core::fail(ii::core::ErrorCode::failed, "fixture content failure");
        }, {}, {
            .ready = [weak] { if (auto s = weak.lock()) ++s->ready; },
            .failed = [weak](const std::string &message) {
                if (auto s = weak.lock()) { ++s->failures; if (message != "fixture content failure") s->error = message; }
            },
            .closed = [weak, application] { if (auto s = weak.lock()) ++s->closed; stop(application); }});
    if (!state->surface->start({5, 7, 450, 160})) { state->error = "failure fixture did not start"; stop(application); }
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) return 1;
    @autoreleasepool {
        NSApplication *application = NSApplication.sharedApplication;
        [application setActivationPolicy:NSApplicationActivationPolicyProhibited];
        auto state = std::make_shared<State>();
        std::ifstream file(argv[1]); state->svg.assign(std::istreambuf_iterator<char>(file), {});
        if (state->svg.empty()) return 1;
        state->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 800, 700)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        state->window.releasedWhenClosed = NO; state->parent = state->window.contentView;
        state->originalSubviews = state->parent.subviews.count;
        state->model = std::make_shared<dreamscapes::home::HomeQueueModel>();
        const std::weak_ptr<State> weak = state;
        state->model->setOpenQueueHandler([weak] { if (auto s = weak.lock()) ++s->opened; });
        state->surface = dreamscapes::native::NativeEmbeddedSurface::create((__bridge void *)state->parent,
            [weak](ii::runtime::EventLoop &, ii::runtime::Runtime *workers)
                -> ii::core::Result<dreamscapes::native::NativeEmbeddedSurface::Content> {
                if (workers) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "unexpected worker");
                auto s = weak.lock();
                if (!s) return ii::core::fail(ii::core::ErrorCode::closed, "test owner retired");
                auto resource = std::make_shared<int>(1); s->resource = resource;
                return dreamscapes::native::NativeEmbeddedSurface::Content{
                    .build = [model = s->model, svg = s->svg](ii::gui::WidgetBuilder &builder) {
                        return dreamscapes::home::buildHomeQueue(builder, {}, model, svg);
                    }, .resources = resource};
            }, {}, {
                .ready = [weak] {
                    if (auto s = weak.lock()) {
                        try {
                            ++s->ready;
                            check(!s->resource.expired(), "content resource was released while the surface was active");
                            auto mounted = s->surface->widgets();
                            check(mounted.has_value(), "home queue was not mounted");
                            auto button = mounted->find("openQueue");
                            check(bool(button) && bool(button->activate()) && s->opened == 1,
                                  "home action did not reach its model");
                            check(bool(s->surface->setFrame({8, 10, 350, 160})), "resize failed");
                            check(bool(s->surface->close()), "close failed");
                        } catch (const std::exception &error) { s->error = error.what(); stop(NSApplication.sharedApplication); }
                    }
                },
                .failed = [weak, application](const std::string &message) {
                    if (auto s = weak.lock()) s->error = message;
                    stop(application);
                },
                .closed = [weak, application] {
                    if (auto s = weak.lock()) {
                        ++s->closed;
                        auto retained = s;
                        dispatch_async(dispatch_get_main_queue(), ^{ startFailure(retained, application); });
                    }
                }});
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!state->surface->start({5, 7, 450, 160})) { state->error = "start failed"; stop(application); }
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            if (state->closed != 2 && state->error.empty()) { state->error = "surface timed out"; stop(application); }
        });
        [application run];
        if (!state->error.empty()) { std::cerr << state->error << '\n'; return 1; }
        if (state->ready != 1 || state->closed != 2 || state->failures != 1 || !state->resource.expired() ||
            state->parent.subviews.count != state->originalSubviews) {
            std::cerr << "surface did not retire cleanly\n"; return 1;
        }
        [state->window close];
    }
}
