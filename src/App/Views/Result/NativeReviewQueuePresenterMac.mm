#import <AppKit/AppKit.h>
#include "NativeReviewQueuePresenterMac.h"
#include "NativeReviewQueuePresenter.h"
#include "NativeReviewQueueSurface.h"
#include "GenerationReviewModel.h"
#include "App/ApplicationShutdownCoordinator.h"
#include "App/ViewModels/WorkspaceSession.h"
#include <QDebug>
#include <QPointer>
#include <QQuickWindow>
#include <QThread>
#include <memory>
#include <utility>

namespace dreamscapes::review {
namespace {
ii::gui::Rect nativeRect(QRectF frame) {
    return {static_cast<float>(frame.x()), static_cast<float>(frame.y()),
            static_cast<float>(frame.width()), static_cast<float>(frame.height())};
}
class MacSurface final : public NativeReviewQueuePresentationSurface {
  public:
    MacSurface(QQuickWindow *window, std::shared_ptr<QueueModel> model, Callbacks callbacks) : window_(window) {
        NSView *parent = (__bridge NSView *)reinterpret_cast<void *>(window->winId());
        const auto failed = callbacks.failed;
        surface_ = NativeReviewQueueSurface::create((__bridge void *)parent, std::move(model), {
            .ready = std::move(callbacks.ready),
            .failed = [failed](const std::string &message) {
                qWarning().noquote() << "Native review queue:" << QString::fromStdString(message);
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
    std::shared_ptr<NativeReviewQueueSurface> surface_;
};
}
bool installNativeReviewQueuePresenter(QObject *root, QObject *lifetime, ApplicationShutdownCoordinator *shutdown) {
    auto *window = qobject_cast<QQuickWindow *>(root);
    auto *workspace = root ? root->findChild<WorkspaceSession *>(QStringLiteral("nativeWorkspaceSession")) : nullptr;
    auto *review = root ? root->findChild<GenerationReviewModel *>(QStringLiteral("generationReviewModel")) : nullptr;
    if (!window || !workspace || !review || !lifetime || !shutdown || shutdown->isClosing() ||
        window->thread() != QThread::currentThread() || lifetime->thread() != QThread::currentThread() ||
        !shutdown->isOwnerThread()) return false;
    try {
        const QPointer<QQuickWindow> nativeWindow(window);
        auto presenter = std::make_unique<NativeReviewQueuePresenter>(window, workspace, review,
            [nativeWindow](auto model, auto callbacks) -> std::shared_ptr<NativeReviewQueuePresentationSurface> {
                if (!nativeWindow) return {};
                return std::make_shared<MacSurface>(nativeWindow, std::move(model), std::move(callbacks));
            }, lifetime);
        if (!presenter->valid()) return false;
        const QPointer<NativeReviewQueuePresenter> guard(presenter.get());
        if (!shutdown->add([guard](std::function<void()> done) { if (guard) guard->shutdown(std::move(done)); else if (done) done(); })) return false;
        presenter->begin(); presenter.release(); return true;
    } catch (...) { return false; }
}
} // namespace dreamscapes::review
