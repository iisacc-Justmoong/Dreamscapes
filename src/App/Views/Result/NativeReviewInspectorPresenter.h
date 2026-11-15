#pragma once
#include "NativeReviewInspectorViewModel.h"
#include "NativeReviewMedia.h"
#include "App/Native/NativePresentationSurface.h"
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>
#include <vector>

class QQuickItem;
class QQuickWindow;
class GenerationReviewModel;
namespace dreamscapes::native { class NativePanelRegion; class NativePanelOverlays; }
namespace dreamscapes::review {
// Temporary host for an LVUI inspector while the application root is Qt Quick.
class NativeReviewInspectorPresenter final : public QObject {
    Q_OBJECT
  public:
    using Surface = native::NativePresentationSurface;
    using Factory = std::function<std::shared_ptr<Surface>(std::shared_ptr<InspectorViewModel>,std::shared_ptr<ReviewMediaModel>,Surface::Callbacks)>;
    using PlaybackFactory = std::function<std::shared_ptr<ReviewPlaybackPort>()>;
    NativeReviewInspectorPresenter(QQuickWindow *,GenerationReviewModel *,Factory,PlaybackFactory={},QObject *parent=nullptr);
    ~NativeReviewInspectorPresenter() override;
    [[nodiscard]] bool valid() const noexcept { return valid_; }
    void begin();
    void shutdown(std::function<void()> done);
  protected:
    bool eventFilter(QObject *,QEvent *) override;
  private:
    bool show(std::size_t,QRectF);
    void hide(std::size_t);
    bool prepare(std::size_t);
    void dispatch(const char *);
    void observeFields();
    void scheduleMediaUpdate();
    void refreshMedia();
    void mediaFailure(std::string);
    void updateAvailability();
    void ready();
    void failed();
    void closed();
    void restartForLanguage();
    void clearSession();
    void finishShutdown();
    QPointer<QQuickWindow> window_;
    QPointer<GenerationReviewModel> review_;
    Factory factory_;
    PlaybackFactory playbackFactory_;
    std::vector<QPointer<QQuickItem>> panels_,previews_;
    std::vector<std::unique_ptr<native::NativePanelRegion>> regions_;
    std::unique_ptr<native::NativePanelOverlays> overlays_;
    std::shared_ptr<ReviewInspectorModel> inspector_;
    std::shared_ptr<InspectorViewModel> model_;
    std::shared_ptr<ReviewMediaModel> media_;
    std::shared_ptr<Surface> surface_;
    ii::core::ObservableSubscription videoObservation_,previewObservation_,mediaObservation_,fieldsObservation_;
    std::vector<ii::core::ObservableSubscription> fieldObservations_;
    std::vector<std::function<void()>> completion_;
    std::size_t active_ = static_cast<std::size_t>(-1);
    std::string loadedMedia_;
    bool loadedVideo_=false,playbackReady_=false,mediaPending_=false,mediaFailed_=false;
    bool valid_=false,started_=false,surfaceReady_=false,failed_=false;
    bool stopping_=false,restarting_=false,destroying_=false;
};
} // namespace dreamscapes::review
