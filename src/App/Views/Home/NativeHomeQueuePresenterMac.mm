#import <AppKit/AppKit.h>
#include "NativeHomeQueuePresenterMac.h"
#include "NativeHomeQueuePresenter.h"
#include "NativeHomeQueueView.h"
#include "App/ApplicationShutdownCoordinator.h"
#include "App/Native/NativeEmbeddedSurface.h"
#include "App/ViewModels/WorkspaceSession.h"
#include <QDebug>
#include <QFile>
#include <QPointer>
#include <QQuickWindow>
#include <QThread>
#include <memory>
#include <utility>

namespace dreamscapes::home {
namespace {
ii::gui::Rect nativeRect(QRectF frame) {
    return {static_cast<float>(frame.x()), static_cast<float>(frame.y()),
            static_cast<float>(frame.width()), static_cast<float>(frame.height())};
}
class MacSurface final : public native::NativePresentationSurface {
  public:
    MacSurface(QQuickWindow *window, std::shared_ptr<HomeQueueModel> model, std::string svg, Callbacks callbacks)
        : window_(window) {
        NSView *parent = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
        const auto failed = callbacks.failed;
        surface_ = native::NativeEmbeddedSurface::create((__bridge void *)parent,
            [model = std::move(model), svg = std::move(svg)](ii::runtime::EventLoop &, ii::runtime::Runtime *)
                -> ii::core::Result<native::NativeEmbeddedSurface::Content> {
                return native::NativeEmbeddedSurface::Content{
                    .build = [model, svg](ii::gui::WidgetBuilder &builder) {
                        return buildHomeQueue(builder, {}, model, svg);
                    }};
            }, {}, {.ready = std::move(callbacks.ready),
                .failed = [failed](const std::string &message) {
                    qWarning().noquote() << "Native home queue:" << QString::fromStdString(message);
                    if (failed) failed();
                },
                .closed = std::move(callbacks.closed)});
    }
    bool start(QRectF frame) override {
        return surface_ && bool(surface_->start(nativeRect(frame))) && bool(surface_->setVisible(false));
    }
    bool show(QRectF frame) override {
        return surface_ && bool(surface_->setFrame(nativeRect(frame))) && bool(surface_->setVisible(true));
    }
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
bool installNativeHomeQueuePresenter(QObject *root, QObject *lifetime, ApplicationShutdownCoordinator *shutdown) {
    auto *window = qobject_cast<QQuickWindow *>(root);
    auto *workspace = root ? root->findChild<WorkspaceSession *>(QStringLiteral("nativeWorkspaceSession")) : nullptr;
    if (!window || !workspace || !lifetime || !shutdown || shutdown->isClosing() ||
        window->thread() != QThread::currentThread() || lifetime->thread() != QThread::currentThread() ||
        shutdown->thread() != QThread::currentThread()) return false;
    try {
        const QPointer<QQuickWindow> nativeWindow(window);
        auto presenter = std::make_unique<NativeHomeQueuePresenter>(window, workspace,
            [nativeWindow](auto model, auto callbacks) -> std::shared_ptr<native::NativePresentationSurface> {
                if (!nativeWindow) return {};
                QFile indicator(QStringLiteral(":/native-home-queue/queue-ready.svg"));
                if (!indicator.open(QIODevice::ReadOnly)) return {};
                return std::make_shared<MacSurface>(nativeWindow, std::move(model), indicator.readAll().toStdString(), std::move(callbacks));
            }, lifetime);
        if (!presenter->valid()) return false;
        const QPointer<NativeHomeQueuePresenter> guard(presenter.get());
        shutdown->add([guard](std::function<void()> done) { if (guard) guard->shutdown(std::move(done)); else if (done) done(); });
        presenter->begin(); presenter.release(); return true;
    } catch (...) { return false; }
}
} // namespace dreamscapes::home
