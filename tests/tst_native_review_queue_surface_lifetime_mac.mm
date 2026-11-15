#import <AppKit/AppKit.h>
#include "App/Views/Result/NativeReviewQueueSurface.h"
#include <QCoreApplication>
#include <QUrl>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
void stop(NSApplication *app) {
    [app stop:nil];
    [app postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
        modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:YES];
}
struct State final {
    std::shared_ptr<dreamscapes::review::NativeReviewQueueSurface> surface;
    std::weak_ptr<dreamscapes::review::NativeReviewQueueSurface> retired;
    std::shared_ptr<dreamscapes::review::QueueModel> model;
    __strong NSWindow *window = nil;
    __strong NSView *parent = nil;
    __strong id closeObserver = nil;
    NSUInteger originalSubviews = 0;
    std::string address, error;
    int firstReady = 0, secondReady = 0, closed = 0, closeAttempt = 0, willClose = 0;
};
void openWindow(const std::shared_ptr<State> &state) {
    state->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 600, 500)
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
    state->window.releasedWhenClosed = NO;
    state->parent = state->window.contentView;
    state->originalSubviews = state->parent.subviews.count;
}
void second(const std::shared_ptr<State> &state, NSApplication *app) {
    using namespace dreamscapes;
    try {
        openWindow(state);
        const std::weak_ptr<State> weak = state;
        state->closeObserver = [NSNotificationCenter.defaultCenter addObserverForName:NSWindowWillCloseNotification
            object:state->window queue:nil usingBlock:^(NSNotification *) { if (auto current = weak.lock()) ++current->willClose; }];
        review::QueueSnapshot snapshot{.jobs = vm::Value(vm::Array{vm::Value(vm::Object{
            {"id", vm::Value("active")}, {"state", vm::Value("running")}, {"mediaType", vm::Value("Image")}})}),
            .previewJobId = "active", .previewSource = state->address};
        good(state->model->setSnapshot(snapshot));
        state->surface = review::NativeReviewQueueSurface::create((__bridge void *)state->parent, state->model, {
            .ready = [state] {
                ++state->secondReady;
                auto retained = state;
                dispatch_async(dispatch_get_main_queue(), ^{
                    retained->retired = retained->surface;
                    retained->surface.reset();
                    ++retained->closeAttempt;
                    [retained->window close];
                });
            },
            .failed = [state, app](const std::string &message) { state->error = message; stop(app); },
            .closed = [state, app] {
                ++state->closed;
                auto retained = state;
                NSApplication *application = app;
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (retained->secondReady != 1 || !retained->retired.expired() ||
                        retained->parent.subviews.count != retained->originalSubviews)
                        retained->error = "parent closure left a native review surface or preview worker alive";
                    if (retained->closeObserver) [NSNotificationCenter.defaultCenter removeObserver:retained->closeObserver];
                    retained->closeObserver = nil;
                    stop(application);
                });
            }});
        good(state->surface->start({5, 7, 260, 400}));
    } catch (const std::exception &error) { state->error = error.what(); stop(app); }
}
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (argc != 2) return 1;
    @autoreleasepool {
        using namespace dreamscapes;
        NSApplication *app = NSApplication.sharedApplication;
        [app setActivationPolicy:NSApplicationActivationPolicyProhibited];
        auto state = std::make_shared<State>();
        state->address = QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])).toString(QUrl::FullyEncoded).toStdString();
        state->model = review::QueueModel::create(std::make_shared<review::Session>());
        auto invalid = review::NativeReviewQueueSurface::create(nullptr, state->model);
        check(!invalid->start({0, 0, 100, 100}), "a missing native parent was accepted");
        openWindow(state);
        state->surface = review::NativeReviewQueueSurface::create((__bridge void *)state->parent, state->model, {
            .ready = [state] { ++state->firstReady; },
            .failed = [state, app](const std::string &message) { state->error = message; stop(app); },
            .closed = [state, app] {
                ++state->closed;
                if (state->firstReady || state->parent.subviews.count != state->originalSubviews) {
                    state->error = "closing before mount left a native surface or invoked ready"; stop(app); return;
                }
                state->surface.reset();
                [state->window close];
                auto retained = state;
                NSApplication *application = app;
                dispatch_async(dispatch_get_main_queue(), ^{ second(retained, application); });
            }});
        dispatch_async(dispatch_get_main_queue(), ^{
            try {
                good(state->surface->start({5, 7, 260, 400}));
                good(state->surface->close());
                good(state->surface->close());
            } catch (const std::exception &error) { state->error = error.what(); stop(app); }
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            if (state->closed != 2 && state->error.empty()) {
                state->error = "native surface retirement timed out: firstReady=" + std::to_string(state->firstReady) +
                    " secondReady=" + std::to_string(state->secondReady) + " closed=" + std::to_string(state->closed) +
                    " closeAttempt=" + std::to_string(state->closeAttempt) + " willClose=" + std::to_string(state->willClose) +
                    " subviews=" + std::to_string(state->parent.subviews.count);
                stop(app);
            }
        });
        [app run];
        if (!state->error.empty()) { std::cerr << state->error << '\n'; return 1; }
        if (state->closed != 2) { std::cerr << "native surface close callbacks were lost or duplicated\n"; return 1; }
    }
}
