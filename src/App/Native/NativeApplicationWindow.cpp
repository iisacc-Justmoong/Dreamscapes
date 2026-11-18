#include "NativeApplicationWindow.h"
#include <iiGUI>
#include <exception>
#include <thread>
#include <utility>

namespace dreamscapes::native {
namespace {
using ii::core::Error;
using ii::core::ErrorCode;
}
struct NativeApplicationWindow::Impl final {
    explicit Impl(ii::gui::HostOptions value, Factory builder, Callbacks handlers)
        : options(std::move(value)), factory(std::move(builder)), callbacks(std::move(handlers)) {}
    const std::thread::id owner = std::this_thread::get_id();
    ii::gui::HostOptions options;
    Factory factory;
    Callbacks callbacks;
    std::shared_ptr<ii::gui::Host> host;
    std::optional<Content> content;
    std::optional<ii::gui::WidgetMount> mount;
    std::optional<Error> error;
    bool started = false, standalone = false, retired = false, finished = false;
};

NativeApplicationWindow::NativeApplicationWindow(ii::gui::HostOptions options, Factory factory, Callbacks callbacks)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(factory), std::move(callbacks))) {}
std::shared_ptr<NativeApplicationWindow> NativeApplicationWindow::create(ii::gui::HostOptions options,
                                                                          Factory factory, Callbacks callbacks) {
    return std::shared_ptr<NativeApplicationWindow>(
        new NativeApplicationWindow(std::move(options), std::move(factory), std::move(callbacks)));
}
NativeApplicationWindow::~NativeApplicationWindow() = default;

ii::core::Result<void> NativeApplicationWindow::prepare() {
    auto &state = *impl_;
    if (state.owner != std::this_thread::get_id())
        return ii::core::fail(ErrorCode::wrong_executor, "Native application window requires its owner thread");
    if (state.started || state.finished)
        return ii::core::fail(ErrorCode::busy, "Native application window is single use");
    if (!state.factory)
        return ii::core::fail(ErrorCode::invalid_argument, "Native application window requires a content factory");
    state.error.reset();
    try {
        state.host = std::make_shared<ii::gui::Host>(state.options);
        const std::weak_ptr<NativeApplicationWindow> weak = shared_from_this();
        auto configured = state.host->set_failure_handler([weak](const Error &error) {
            if (auto self = weak.lock()) self->fail(error);
        });
        if (!configured) { state.host.reset(); return std::unexpected(configured.error()); }
    } catch (...) {
        state.host.reset();
        return ii::core::fail(ErrorCode::failed, "Cannot create native application window", std::current_exception());
    }
    return {};
}

ii::core::Result<void> NativeApplicationWindow::attach(ii::runtime::Application &application) {
    auto &state=*impl_;
    if (state.owner!=std::this_thread::get_id())
        return ii::core::fail(ErrorCode::wrong_executor,"Native application window requires its owner thread");
    if (state.started || state.finished)
        return ii::core::fail(ErrorCode::busy,"Native application window is single use");
    if (!state.factory)
        return ii::core::fail(ErrorCode::invalid_argument,"Native application window requires a content factory");
    const auto self=shared_from_this();
    const std::weak_ptr<NativeApplicationWindow> weak=self;
    auto configured=application.configure_atomically([self,weak](ii::runtime::Application &owner) -> ii::core::Result<void> {
        auto &value=*self->impl_;
        auto host=ii::gui::attach_application(owner,value.options,{
            .resized=[weak](ii::gui::Size size) {if (auto window=weak.lock()) window->resized(size);},
            .close_requested=[weak] {if (auto window=weak.lock()) return window->mayClose();return true;},
            .closed=[weak] {if (auto window=weak.lock()) window->retired();}});
        if (!host) return std::unexpected(host.error());
        auto failure=(*host)->set_failure_handler([weak](const Error &error) {
            if (auto window=weak.lock()) window->fail(error);
        });
        if (!failure) return std::unexpected(failure.error());
        value.host=std::move(*host);value.started=true;value.standalone=true;
        return owner.attach({"Dreamscapes.window",
            [self](ii::runtime::Application &) -> ii::core::Result<void> {
                self->opened();
                if (self->impl_->error) return std::unexpected(*self->impl_->error);
                if (!self->impl_->content)
                    return ii::core::fail(ErrorCode::failed,"Native application content did not open");
                return {};
            },
            [self]() -> ii::core::Result<void> {self->finish();return {};}});
    });
    if (!configured) {state.started=false;state.standalone=false;state.host.reset();}
    return configured;
}

ii::core::Result<void> NativeApplicationWindow::showExternal() {
    if (auto result = prepare(); !result) return result;
    auto &state = *impl_;
    state.standalone = false;
    const auto self = shared_from_this();
    const std::weak_ptr<NativeApplicationWindow> weak = self;
    state.started = true;
    auto result = state.host->show_external({
        .opened = [weak] { if (auto owner = weak.lock()) owner->opened(); },
        .resized = [weak](ii::gui::Size size) { if (auto owner = weak.lock()) owner->resized(size); },
        .close_requested = [weak] { if (auto owner = weak.lock()) return owner->mayClose(); return true; },
        .closed = [self] { self->retired(); }
    });
    if (!result) {
        state.started = false;
        state.host.reset();
        return std::unexpected(result.error());
    }
    return {};
}

ii::core::Result<void> NativeApplicationWindow::run() {
    if (auto result = prepare(); !result) return result;
    auto &state = *impl_;
    state.standalone = true;
    const auto self = shared_from_this();
    const std::weak_ptr<NativeApplicationWindow> weak = self;
    state.started = true;
    auto result = state.host->run({
        .opened = [weak] { if (auto owner = weak.lock()) owner->opened(); },
        .resized = [weak](ii::gui::Size size) { if (auto owner = weak.lock()) owner->resized(size); },
        .close_requested = [weak] { if (auto owner = weak.lock()) return owner->mayClose(); return true; },
        .closed = [weak] { if (auto owner = weak.lock()) owner->retired(); }
    });
    if (!result && !state.retired) {
        state.started = false;
        state.host.reset();
        return std::unexpected(result.error());
    }
    if (!result && !state.error) state.error = result.error();
    const auto error = state.error;
    finish();
    if (error) return std::unexpected(*error);
    return {};
}

void NativeApplicationWindow::opened() {
    auto &state = *impl_;
    if (!state.started || state.retired || !state.host) return;
    try {
        auto loop = state.host->event_loop();
        if (!loop) { fail(loop.error()); return; }
        auto content = state.factory(loop->get());
        if (!content) { fail(content.error()); return; }
        if (!content->build) { fail({ErrorCode::invalid_argument, "Native window content has no widget builder"}); return; }
        state.content = std::move(*content);
        auto mount = state.host->mount_widgets(state.content->build);
        if (!mount) { fail(mount.error()); return; }
        state.mount = std::move(*mount);
        auto snapshot = state.host->snapshot();
        if (!snapshot) { fail(snapshot.error()); return; }
        resized(snapshot->size);
        if (state.error || state.retired) return;
        if (auto ready = state.callbacks.ready) ready();
    } catch (...) {
        fail({ErrorCode::failed, "Cannot mount native application view", std::current_exception()});
    }
}

void NativeApplicationWindow::resized(ii::gui::Size size) {
    auto &state=*impl_;
    if (!state.started || state.retired || !state.content || !state.content->resize) return;
    // Window systems may report a zero-sized surface during minimization.
    // Keep the last usable viewport until the window is restored.
    if (size.width<=0 || size.height<=0) return;
    try {
        if (auto changed=state.content->resize(size); !changed) fail(changed.error());
    } catch (...) {
        fail({ErrorCode::failed,"Cannot resize native application view",std::current_exception()});
    }
}

bool NativeApplicationWindow::mayClose() {
    auto &state = *impl_;
    if (state.error) return true;
    try { return !state.callbacks.closeRequested || state.callbacks.closeRequested(); }
    catch (...) { return false; }
}
ii::core::Result<void> NativeApplicationWindow::close() {
    auto &state = *impl_;
    if (state.owner != std::this_thread::get_id())
        return ii::core::fail(ErrorCode::wrong_executor, "Native application window requires its owner thread");
    if (state.retired || state.finished) return {};
    if (!state.started || !state.host)
        return ii::core::fail(ErrorCode::closed, "Native application window is not open");
    // A close policy may veto the request. The view stays usable in that case.
    return state.host->request_close();
}
ii::core::Result<void> NativeApplicationWindow::beginWindowDrag() {
    auto &state=*impl_;
    if (state.owner!=std::this_thread::get_id())
        return ii::core::fail(ErrorCode::wrong_executor,"Native application window requires its owner thread");
    if (!state.started || state.retired || state.finished || !state.host)
        return ii::core::fail(ErrorCode::closed,"Native application window is not open");
    return state.host->begin_window_drag();
}
ii::core::Result<void> NativeApplicationWindow::chooseFile(ii::gui::FileDialogOptions options,
                                                           ii::gui::FileDialogCompletion completion) {
    auto &state = *impl_;
    if (state.owner != std::this_thread::get_id())
        return ii::core::fail(ErrorCode::wrong_executor, "Native application window requires its owner thread");
    if (!state.started || state.retired || state.finished || !state.host)
        return ii::core::fail(ErrorCode::closed, "Native application window is not open");
    return state.host->choose_file(std::move(options), std::move(completion));
}
void NativeApplicationWindow::retired() {
    auto &state = *impl_;
    if (state.retired) return;
    state.retired = true;
    if (!state.standalone) finish();
}
void NativeApplicationWindow::finish() {
    auto &state = *impl_;
    if (state.finished) return;
    state.finished = true;
    state.mount.reset();
    state.content.reset();
    state.host.reset();
    state.factory = {};
    auto callback = std::move(state.callbacks.closed);
    state.callbacks = {};
    // Native callbacks must not unwind through the platform event loop.
    if (callback) try { callback(); } catch (...) {}
}
void NativeApplicationWindow::fail(Error error) {
    auto &state = *impl_;
    if (state.error || state.finished) return;
    state.error = std::move(error);
    auto callback = state.callbacks.failed;
    if (callback) try { callback(*state.error); } catch (...) {}
    (void)close();
}
std::optional<ii::gui::WidgetMount> NativeApplicationWindow::widgets() const {
    if (impl_->owner != std::this_thread::get_id() || impl_->retired || impl_->finished) return {};
    return impl_->mount;
}
} // namespace dreamscapes::native
