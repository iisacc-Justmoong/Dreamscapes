#import <AppKit/AppKit.h>
#include "App/Views/Result/NativeReviewQueueSurface.h"
#include <QCoreApplication>
#include <QUrl>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class T> T take(ii::core::Result<T> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
    return std::move(*value);
}
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
void stop(NSApplication *app) {
    [app stop:nil];
    [app postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
        modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:YES];
}
void waitUntil(BOOL (^ready)(void), dispatch_block_t continuation) {
    if (ready()) { continuation(); return; }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        waitUntil(ready, continuation);
    });
}
bool hasPreviewImage(const ii::gui::WidgetMount &mount) {
    const auto nodes = mount.snapshot();
    if (!nodes) return false;
    for (const auto &node : *nodes) {
        if (node.id != "livePreview" && !node.id.ends_with("/livePreview")) continue;
        const auto widget = mount.find(node.id);
        if (!widget) return false;
        const auto scene = widget->snapshot_scene();
        if (!scene) return false;
        for (const auto &layer : scene->layers)
            for (const auto &image : layer.images) if (image.image) return true;
    }
    return false;
}
struct State final {
    std::shared_ptr<dreamscapes::review::NativeReviewQueueSurface> surface;
    std::shared_ptr<dreamscapes::review::QueueModel> model;
    __strong NSWindow *window = nil;
    __strong NSView *parent = nil;
    NSUInteger originalSubviews = 0;
    std::string address, error;
    int ready = 0, closed = 0;
    std::vector<std::string> selections;
};
}

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (argc != 2) return 1;
    @autoreleasepool {
        using namespace dreamscapes;
        NSApplication *app = NSApplication.sharedApplication;
        [app setActivationPolicy:NSApplicationActivationPolicyProhibited];
        auto state = std::make_shared<State>();
        state->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 800, 700)
            styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        state->window.releasedWhenClosed = NO;
        state->parent = state->window.contentView;
        state->originalSubviews = state->parent.subviews.count;
        state->address = QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])).toString(QUrl::FullyEncoded).toStdString();
        state->model = review::QueueModel::create(std::make_shared<review::Session>());
        const std::weak_ptr<State> weak = state;
        state->model->setSelectionHandler([weak](const std::string &id) {
            if (auto current = weak.lock()) current->selections.push_back(id);
        });
        review::QueueSnapshot snapshot{.jobs = vm::Value(vm::Array{vm::Value(vm::Object{
            {"id", vm::Value("job-one")}, {"state", vm::Value("running")}, {"mediaType", vm::Value("Image")}})}),
            .inference = vm::Value(vm::Object{{"step", vm::Value(1)}, {"total", vm::Value(2)}}),
            .previewJobId = "job-one", .previewSource = state->address};
        if (!state->model->setSnapshot(snapshot)) return 1;
        state->surface = review::NativeReviewQueueSurface::create((__bridge void *)state->parent, state->model, {
            .ready = [weak] { if (auto current = weak.lock()) ++current->ready; },
            .failed = [weak, app](const std::string &message) {
                if (auto current = weak.lock()) current->error = message;
                stop(app);
            },
            .closed = [weak, app] {
                if (auto current = weak.lock()) ++current->closed;
                stop(app);
            }});
        dispatch_async(dispatch_get_main_queue(), ^{
            try {
                good(state->surface->start({20, 30, 260, 500}));
                waitUntil(^BOOL { return state->ready != 0; }, ^{
                    try {
                        check(state->ready == 1 && state->parent.subviews.count == state->originalSubviews + 1,
                              "native queue surface was not mounted in the borrowed parent");
                        NSView *region = state->parent.subviews.lastObject;
                        check(std::abs(region.frame.size.width - 260) < 0.5 && std::abs(region.frame.size.height - 500) < 0.5,
                              "native queue surface did not use the requested size");
                        const auto expectedY = state->parent.isFlipped ? NSMinY(state->parent.bounds) + 30
                            : NSMaxY(state->parent.bounds) - 30 - 500;
                        check(std::abs(region.frame.origin.x - (NSMinX(state->parent.bounds) + 20)) < 0.5 &&
                              std::abs(region.frame.origin.y - expectedY) < 0.5,
                              "native queue surface did not translate top-left coordinates");
                        good(state->surface->setVisible(false));
                        check(region.hidden, "the native region stayed visible while hidden");
                        check(!state->surface->setFrame({0, 0, -1, 100}) && std::abs(region.frame.size.width - 260) < 0.5,
                              "invalid geometry changed the native region");
                        good(state->surface->setFrame({40, 50, 280, 420}));
                        good(state->surface->setVisible(true));
                        check(!region.hidden && std::abs(region.frame.size.width - 280) < 0.5 &&
                              std::abs(region.frame.size.height - 420) < 0.5, "native region did not resize or reappear");
                        bool rejected = false;
                        std::thread other([&] { rejected = !state->surface->setVisible(false) && !state->surface->close(); });
                        other.join();
                        check(rejected && !region.hidden, "the surface accepted mutation from another thread");
                        auto mounted = state->surface->widgets();
                        check(mounted.has_value(), "native queue widgets were not exposed after mount");
                        auto all = take(mounted->find("allJobs"));
                        good(all.activate());
                        check(state->selections == std::vector<std::string>{""}, "All jobs did not reach the native model");
                        waitUntil(^BOOL {
                            auto widgets = state->surface->widgets();
                            return widgets && hasPreviewImage(*widgets);
                        }, ^{
                            good(state->surface->close());
                        });
                    } catch (const std::exception &error) { state->error = error.what(); stop(app); }
                });
            } catch (const std::exception &error) { state->error = error.what(); stop(app); }
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            if (!state->closed && state->error.empty()) { state->error = "native queue surface timed out"; stop(app); }
        });
        [app run];
        if (!state->error.empty()) { std::cerr << state->error << '\n'; return 1; }
        if (state->closed != 1 || state->parent.subviews.count != state->originalSubviews) {
            std::cerr << "native queue surface did not retire its borrowed view\n"; return 1;
        }
        [state->window close];
    }
}
