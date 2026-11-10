#import <AppKit/AppKit.h>
#include "App/Generation/GenerationController.h"
#include "App/Generation/NativeFailurePresenter.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <memory>
#include <string>

namespace {
void stop(NSApplication *app) {
    [app stop:nil];
    [app postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
        modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:YES];
}
struct State {
    std::string error;
    int dismissed = 0, fallback = 0;
    NSUInteger originalSubviews = 0;
    __strong NSWindow *window = nil;
    __strong NSView *parent = nil;
    __strong id<NSWindowDelegate> windowDelegate = nil;
};
}

int main(int argc, char **argv) {
    QCoreApplication qt(argc, argv);
    @autoreleasepool {
        QTemporaryDir storage(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/native-failure-presenter-XXXXXX"));
        if (!storage.isValid()) return 1;
        GenerationRuntime runtime;
        runtime.modelPreferencesFile = storage.filePath("models.conf");
        runtime.failureReportsDirectory = storage.path();
        auto controller = std::make_unique<GenerationController>(runtime);
        auto presenter = std::make_unique<dreamscapes::failure::NativeFailurePresenter>();
        auto *controllerPtr = controller.get();
        auto *presenterPtr = presenter.get();
        NSApplication *app = NSApplication.sharedApplication;
        [app setActivationPolicy:NSApplicationActivationPolicyProhibited];
        auto state = std::make_shared<State>();
        state->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 900, 700)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        state->window.releasedWhenClosed = NO;
        state->parent = state->window.contentView;
        state->windowDelegate = (id<NSWindowDelegate>)[NSObject new];
        state->window.delegate = state->windowDelegate;
        state->originalSubviews = state->parent.subviews.count;
        QObject::connect(presenter.get(), &dreamscapes::failure::NativeFailurePresenter::dismissed,
            [&state](const QString &id) { if (id == "job-one") ++state->dismissed; });
        QObject::connect(presenter.get(), &dreamscapes::failure::NativeFailurePresenter::fallbackRequired,
            [&state](const QString &id) { if (id == "job-two") ++state->fallback; });
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!presenterPtr->openOnNativeParent(controllerPtr, (__bridge void *)state->parent, "job-one")) {
                state->error = "Native presenter refused a valid parent"; stop(app); return;
            }
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                if (state->parent.subviews.count != state->originalSubviews + 1 || state->window.delegate != state->windowDelegate) {
                    state->error = "Native failure view was not mounted in the borrowed window"; stop(app); return;
                }
                NSView *surface = state->parent.subviews.lastObject;
                [surface display];
                bool transparentMetal = false;
                for (NSView *child in surface.subviews)
                    if ([child.layer isKindOfClass:NSClassFromString(@"CAMetalLayer")] && !child.layer.opaque)
                        transparentMetal = true;
                if (!transparentMetal) {
                    state->error = "The native failure view obscured the owning window"; stop(app); return;
                }
                controllerPtr->failureExplained("job-one", {{"jobId", "job-one"}, {"details", "Generated diagnostic"}});
                auto model = presenterPtr->activeModel();
                if (!model || model->reportText() != "Generated diagnostic") {
                    state->error = "Controller evidence did not reach the mounted native view"; stop(app); return;
                }
                model->dismiss();
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                    if (state->dismissed != 1 || state->parent.subviews.count != state->originalSubviews || presenterPtr->activeModel()) {
                        state->error = "Dismissal did not retire the native surface"; stop(app); return;
                    }
                    if (!presenterPtr->openOnNativeParent(controllerPtr, (__bridge void *)state->parent, "job-two")) {
                        state->error = "Could not reopen the native failure view"; stop(app); return;
                    }
                    controllerPtr->failureExplained("job-two", {{"jobId", "job-two"}, {"details", QString(5000, QLatin1Char('x'))}});
                    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                        if (state->fallback != 1 || state->parent.subviews.count != state->originalSubviews || presenterPtr->activeModel()) {
                            state->error = "Long-report fallback failed or left the surface mounted"; stop(app); return;
                        }
                        if (!presenterPtr->openOnNativeParent(controllerPtr, (__bridge void *)state->parent, "job-three")) {
                            state->error = "Could not reopen the view before shutdown"; stop(app); return;
                        }
                        presenterPtr->shutdown([state, app, presenterPtr] {
                            if (state->parent.subviews.count != state->originalSubviews || presenterPtr->activeModel() ||
                                state->dismissed != 1 || state->fallback != 1)
                                state->error = "Shutdown did not retire the native view exactly once";
                            stop(app);
                        });
                    });
                });
            });
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 8 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            state->error = "Native presenter test timed out"; stop(app);
        });
        [app run];
        if (!state->error.empty()) { std::fprintf(stderr, "%s\n", state->error.c_str()); return 1; }
        return 0;
    }
}
