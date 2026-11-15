#pragma once

#include "NativeReviewQueue.h"
#include "App/Native/NativePresentationSurface.h"
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <functional>
#include <memory>
#include <vector>

class GenerationReviewModel;
class QQuickItem;
class QQuickWindow;
namespace dreamscapes {
class WorkspaceSession;
namespace native { class NativePanelOverlays; class NativePanelRegion; }
namespace review {
class NativeReviewQueueSession;

using NativeReviewQueuePresentationSurface = native::NativePresentationSurface;

class NativeReviewQueuePresenter final : public QObject {
  public:
    using Surface = NativeReviewQueuePresentationSurface;
    using Factory = std::function<std::shared_ptr<Surface>(std::shared_ptr<QueueModel>, Surface::Callbacks)>;
    NativeReviewQueuePresenter(QQuickWindow *, WorkspaceSession *, GenerationReviewModel *, Factory, QObject *parent = nullptr);
    ~NativeReviewQueuePresenter() override;
    bool valid() const noexcept { return valid_; }
    void begin();
    void shutdown(std::function<void()> done);
  protected:
    bool eventFilter(QObject *, QEvent *) override;
  private:
    bool show(std::size_t, QRectF);
    void hide(std::size_t);
    void dispatch(const char *, const QString &);
    void ready();
    void failed();
    void closed();
    void restartForLanguage();
    void finishShutdown();
    QPointer<QQuickWindow> window_;
    QPointer<WorkspaceSession> workspace_;
    QPointer<GenerationReviewModel> review_;
    Factory factory_;
    std::vector<QPointer<QQuickItem>> queues_;
    std::vector<std::unique_ptr<native::NativePanelRegion>> regions_;
    std::unique_ptr<native::NativePanelOverlays> overlays_;
    std::unique_ptr<NativeReviewQueueSession> session_;
    std::shared_ptr<Surface> surface_;
    std::vector<std::function<void()>> completion_;
    std::size_t active_ = static_cast<std::size_t>(-1);
    bool valid_ = false, started_ = false, surfaceReady_ = false, failed_ = false, stopping_ = false, restarting_ = false;
    bool destroying_ = false;
};
} // namespace review
} // namespace dreamscapes
