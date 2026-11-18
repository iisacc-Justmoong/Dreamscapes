#import <AppKit/AppKit.h>
#include "NativeReviewGridPresenterMac.h"
#include "NativeReviewGridPresenter.h"
#include "NativeReviewGridImages.h"
#include "NativeReviewGridView.h"
#include "GenerationReviewModel.h"
#include "App/ApplicationShutdownCoordinator.h"
#include "App/Native/NativeEmbeddedSurface.h"
#include "App/Native/NativeFileImages.h"
#include <QDebug>
#include <QPointer>
#include <QQuickWindow>
#include <QThread>
#include <memory>
#include <utility>

namespace dreamscapes::review {
namespace {
ii::gui::Rect nativeRect(QRectF frame) {
    return {static_cast<float>(frame.x()),static_cast<float>(frame.y()),
        static_cast<float>(frame.width()),static_cast<float>(frame.height())};
}
class MacSurface final : public NativeReviewGridSurface {
  public:
    MacSurface(QQuickWindow *window,std::shared_ptr<ReviewGridModel> grid,Callbacks callbacks) : window_(window) {
        NSView *parent=(__bridge NSView *)reinterpret_cast<void *>(window->winId());
        const auto failed=callbacks.failed;
        surface_=native::NativeEmbeddedSurface::create((__bridge void *)parent,
            [grid=std::move(grid)](ii::runtime::EventLoop &loop,ii::runtime::Runtime *workers)
                -> ii::core::Result<native::NativeEmbeddedSurface::Content> {
                if (!workers || !grid || !grid->active() || !grid->gallery() || !grid->gallery()->active())
                    return ii::core::fail(ii::core::ErrorCode::failed,"Review grid image workers unavailable");
                auto images=std::make_shared<native::NativeFileImages>(loop,*workers);
                auto binding=ReviewGridImages::create(grid->gallery(),std::move(images));
                return native::NativeEmbeddedSurface::Content{
                    .build=[grid](ii::gui::WidgetBuilder &builder) {return buildReviewGrid(builder,{},grid);},
                    .resources=std::move(binding)};
            },{.workers=1,.capacity=16},{.ready=std::move(callbacks.ready),
                .failed=[failed](const std::string &message) {
                    qWarning().noquote() << "Native review grid:" << QString::fromStdString(message);
                    if (failed) failed();
                },.closed=std::move(callbacks.closed)});
    }
    bool start(QRectF frame) override {return surface_ && bool(surface_->start(nativeRect(frame))) && bool(surface_->setVisible(false));}
    bool show(QRectF frame) override {return surface_ && bool(surface_->setFrame(nativeRect(frame))) && bool(surface_->setVisible(true));}
    void hide() override {if (surface_) (void)surface_->setVisible(false);}
    void close() override {if (surface_) (void)surface_->close();}
    void restoreHostFocus() override {
        if (!window_) return;
        NSView *parent=(__bridge NSView *)reinterpret_cast<void *>(window_->winId());
        if (parent.window) [parent.window makeFirstResponder:parent];
    }
    std::optional<double> scrollOffset() const override {
        auto mount=surface_?surface_->widgets():std::nullopt;
        if (!mount) return {};
        auto viewport=mount->find("gridResults/viewport");
        if (!viewport) return {};
        auto snapshot=viewport->snapshot();
        if (!snapshot) return {};
        return snapshot->scroll_offset;
    }
    bool setScrollOffset(double value) override {
        auto mount=surface_?surface_->widgets():std::nullopt;
        if (!mount || !mount->snapshot()) return false;
        auto viewport=mount->find("gridResults/viewport");
        return viewport && bool(viewport->set_scroll_offset(value));
    }
  private:
    QPointer<QQuickWindow> window_;
    std::shared_ptr<native::NativeEmbeddedSurface> surface_;
};
}
bool installNativeReviewGridPresenter(QObject *root,QObject *lifetime,ApplicationShutdownCoordinator *shutdown) {
    auto *window=qobject_cast<QQuickWindow *>(root);
    auto *review=root?root->findChild<GenerationReviewModel *>(QStringLiteral("generationReviewModel")):nullptr;
    if (!window || !review || !lifetime || !shutdown || shutdown->isClosing() ||
        window->thread()!=QThread::currentThread() || lifetime->thread()!=QThread::currentThread() ||
        !shutdown->isOwnerThread()) return false;
    try {
        const QPointer<QQuickWindow> nativeWindow(window);
        auto presenter=std::make_unique<NativeReviewGridPresenter>(window,review,
            [nativeWindow](auto grid,auto callbacks) -> std::shared_ptr<NativeReviewGridSurface> {
                if (!nativeWindow) return {};
                return std::make_shared<MacSurface>(nativeWindow,std::move(grid),std::move(callbacks));
            },lifetime);
        if (!presenter->valid()) return false;
        const QPointer<NativeReviewGridPresenter> guard(presenter.get());
        if (!shutdown->add([guard](std::function<void()> done){if (guard) guard->shutdown(std::move(done)); else if (done) done();})) return false;
        presenter->begin(); presenter.release(); return true;
    } catch (...) {return false;}
}
} // namespace dreamscapes::review
