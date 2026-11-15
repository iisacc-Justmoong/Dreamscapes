#import <AppKit/AppKit.h>
#include "App/Native/NativeApplicationWindow.h"
#include "App/Views/Home/NativeHomeQueueView.h"
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using dreamscapes::native::NativeApplicationWindow;
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void stop(NSApplication *application) {
    [application stop:nil];
    [application postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
        modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:YES];
}
struct State final {
    std::shared_ptr<NativeApplicationWindow> window;
    std::weak_ptr<NativeApplicationWindow> previousWindow;
    std::shared_ptr<dreamscapes::home::HomeQueueModel> model;
    std::weak_ptr<int> resource;
    std::string svg, error;
    int ready = 0, openedQueue = 0, closed = 0, failed = 0, closeRequests = 0;
    bool permitClose = false;
};
void startFailure(const std::shared_ptr<State> &state, NSApplication *application) {
    if (!state->previousWindow.expired()) {
        state->error = "closed native window was retained";
        stop(application);
        return;
    }
    const std::weak_ptr<State> weak = state;
    state->window = NativeApplicationWindow::create({},
        [](ii::runtime::EventLoop &) -> ii::core::Result<NativeApplicationWindow::Content> {
            return ii::core::fail(ii::core::ErrorCode::failed, "fixture content failure");
        }, {.ready = [weak] { if (auto s = weak.lock()) ++s->ready; },
            .closeRequested = [] { return false; },
            .failed = [weak](const ii::core::Error &error) {
                if (auto s = weak.lock()) {
                    ++s->failed;
                    if (error.message != "fixture content failure") s->error = error.message;
                }
            },
            .closed = [weak, application] { if (auto s = weak.lock()) ++s->closed; stop(application); }});
    if (!state->window->showExternal()) { state->error = "failure fixture did not start"; stop(application); }
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) return 1;
    @autoreleasepool {
        NSApplication *application = NSApplication.sharedApplication;
        [application setActivationPolicy:NSApplicationActivationPolicyProhibited];
        auto state = std::make_shared<State>();
        std::ifstream file(argv[1]); state->svg.assign(std::istreambuf_iterator<char>(file), {});
        if (state->svg.empty()) return 1;
        state->model = std::make_shared<dreamscapes::home::HomeQueueModel>();
        const std::weak_ptr<State> weak = state;
        state->model->setOpenQueueHandler([weak] { if (auto s = weak.lock()) ++s->openedQueue; });
        state->window = NativeApplicationWindow::create({}, [weak](ii::runtime::EventLoop &)
            -> ii::core::Result<NativeApplicationWindow::Content> {
            auto s = weak.lock();
            if (!s) return ii::core::fail(ii::core::ErrorCode::closed, "test owner retired");
            auto resource = std::make_shared<int>(1); s->resource = resource;
            return NativeApplicationWindow::Content{
                .build = [model = s->model, svg = s->svg](ii::gui::WidgetBuilder &builder) {
                    return dreamscapes::home::buildHomeQueue(builder, {}, model, svg);
                }, .resources = resource};
        }, {
            .ready = [weak] {
                if (auto s = weak.lock()) {
                    try {
                        ++s->ready;
                        require(!s->resource.expired(), "native content resource was released early");
                        const auto duplicate = s->window->showExternal();
                        require(!duplicate && duplicate.error().code == ii::core::ErrorCode::busy,
                                "a running window accepted a second start");
                        auto standalone = NativeApplicationWindow::create({}, [](ii::runtime::EventLoop &)
                            -> ii::core::Result<NativeApplicationWindow::Content> {
                            return ii::core::fail(ii::core::ErrorCode::failed, "must not open");
                        });
                        const auto nestedLoop = standalone->run();
                        require(!nestedLoop && nestedLoop.error().code == ii::core::ErrorCode::busy,
                                "standalone window accepted an already-running application loop");
                        auto mount = s->window->widgets();
                        require(mount.has_value(), "LVUI home queue was not mounted");
                        auto button = mount->find("openQueue");
                        require(bool(button) && bool(button->activate()) && s->openedQueue == 1,
                                "LVUI action did not reach its native view model");
                        std::optional<ii::core::ErrorCode> otherThreadError;
                        std::thread wrongThread([&] {
                            const auto result = s->window->close();
                            if (!result) otherThreadError = result.error().code;
                        });
                        wrongThread.join();
                        require(otherThreadError == ii::core::ErrorCode::wrong_executor,
                                "window accepted a command from another thread");
                        require(bool(s->window->close()) && s->closeRequests == 1 && s->window->widgets(),
                                "the close policy did not preserve a vetoed window");
                        s->permitClose = true;
                        require(bool(s->window->close()), "approved close failed");
                        s->previousWindow = s->window;
                        s->window.reset();
                        require(!s->previousWindow.expired(), "window died before deferred close completed");
                    } catch (const std::exception &error) { s->error = error.what(); stop(NSApplication.sharedApplication); }
                }
            },
            .closeRequested = [weak] {
                if (auto s = weak.lock()) { ++s->closeRequests; return s->permitClose; }
                return true;
            },
            .failed = [weak, application](const ii::core::Error &error) {
                if (auto s = weak.lock()) s->error = error.message;
                stop(application);
            },
            .closed = [weak, application] {
                if (auto s = weak.lock()) {
                    ++s->closed;
                    auto retained = s;
                    dispatch_async(dispatch_get_main_queue(), ^{ startFailure(retained, application); });
                }
            }});
        const auto unopenedClose = state->window->close();
        if (unopenedClose || unopenedClose.error().code != ii::core::ErrorCode::closed) return 1;
        const auto tooEarly = state->window->showExternal();
        if (tooEarly || tooEarly.error().code != ii::core::ErrorCode::closed) return 1;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!state->window->showExternal()) { state->error = "native window did not start"; stop(application); }
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            if (state->closed != 2 && state->error.empty()) { state->error = "window timed out"; stop(application); }
        });
        [application run];
        if (!state->error.empty()) { std::cerr << state->error << '\n'; return 1; }
        if (state->ready != 1 || state->closed != 2 || state->failed != 1 || !state->resource.expired() ||
            state->closeRequests != 2) {
            std::cerr << "native window did not preserve lifecycle boundaries\n"; return 1;
        }
    }
}
