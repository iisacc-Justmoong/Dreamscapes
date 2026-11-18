#import <AppKit/AppKit.h>
#include "NativeHomeRecentGenerationsPresenterMac.h"
#include "NativeHomeRecentGenerationsPresenter.h"
#include "NativeHomeRecentGenerationsView.h"
#include "App/ApplicationShutdownCoordinator.h"
#include "App/Native/NativeEmbeddedSurface.h"
#include "App/Native/NativeFileImages.h"
#include <QDebug>
#include <QPointer>
#include <QQuickWindow>
#include <QThread>
#include <memory>
#include <utility>

namespace dreamscapes::home {
namespace {
ii::gui::Rect nativeRect(QRectF frame) {
    return {static_cast<float>(frame.x()), static_cast<float>(frame.y()), static_cast<float>(frame.width()), static_cast<float>(frame.height())};
}
class MacSurface final : public native::NativePresentationSurface {
  public:
    MacSurface(QQuickWindow *window, std::shared_ptr<HomeRecentGenerationsModel> model,
               QPointer<NativeHomeRecentGenerationsSession> session, Callbacks callbacks) : window_(window) {
        NSView *parent = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
        const auto failed = callbacks.failed;
        surface_ = native::NativeEmbeddedSurface::create((__bridge void *)parent,
            [model = std::move(model), session](ii::runtime::EventLoop &loop, ii::runtime::Runtime *workers)
                -> ii::core::Result<native::NativeEmbeddedSurface::Content> {
                if (!workers || !session) return ii::core::fail(ii::core::ErrorCode::failed, "Recent generation image workers unavailable");
                auto base = std::make_shared<native::NativeFileImages>(loop, *workers);
                auto images = std::make_shared<NativeHomeRecentGenerationsImages>(session, base);
                return native::NativeEmbeddedSurface::Content{
                    .build = [model, images](ii::gui::WidgetBuilder &builder) { return buildHomeRecentGenerations(builder, {}, model, images); },
                    .resources = images};
            }, {.workers = 1, .capacity = 16}, {.ready = std::move(callbacks.ready),
                .failed = [failed](const std::string &message) {
                    qWarning().noquote() << "Native recent generations:" << QString::fromStdString(message);
                    if (failed) failed();
                }, .closed = std::move(callbacks.closed)});
    }
    bool start(QRectF frame) override { return surface_ && bool(surface_->start(nativeRect(frame))) && bool(surface_->setVisible(false)); }
    bool show(QRectF frame) override { return surface_ && bool(surface_->setFrame(nativeRect(frame))) && bool(surface_->setVisible(true)); }
    void hide() override { if (surface_) (void)surface_->setVisible(false); }
    void close() override { if (surface_) (void)surface_->close(); }
    void restoreHostFocus() override {
        if (!window_) return;
        NSView *parent = (__bridge NSView *)reinterpret_cast<void *>(window_->winId());
        if (parent.window) [parent.window makeFirstResponder:parent];
    }
  private:
    QPointer<QQuickWindow> window_;
    std::shared_ptr<native::NativeEmbeddedSurface> surface_;
};
}
bool installNativeHomeRecentGenerationsPresenter(QObject *root, QObject *lifetime, ApplicationShutdownCoordinator *shutdown) {
    auto *window = qobject_cast<QQuickWindow *>(root);
    if (!window || !lifetime || !shutdown || shutdown->isClosing() || window->thread() != QThread::currentThread() ||
        lifetime->thread() != QThread::currentThread() || !shutdown->isOwnerThread()) return false;
    try {
        const QPointer<QQuickWindow> nativeWindow(window);
        auto presenter = std::make_unique<NativeHomeRecentGenerationsPresenter>(window,
            [nativeWindow](auto model, auto session, auto callbacks) -> std::shared_ptr<native::NativePresentationSurface> {
                if (!nativeWindow || !session) return {};
                return std::make_shared<MacSurface>(nativeWindow, std::move(model), session, std::move(callbacks));
            }, lifetime);
        if (!presenter->valid()) return false;
        const QPointer<NativeHomeRecentGenerationsPresenter> guard(presenter.get());
        if (!shutdown->add([guard](std::function<void()> done) { if (guard) guard->shutdown(std::move(done)); else if (done) done(); })) return false;
        presenter->begin(); presenter.release(); return true;
    } catch (...) { return false; }
}
} // namespace dreamscapes::home
