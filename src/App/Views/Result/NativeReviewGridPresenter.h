#pragma once
#include "App/Native/NativePresentationSurface.h"
#include "NativeReviewGrid.h"
#include <QObject>
#include <QPointer>
#include <QVariant>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class GenerationReviewModel;
class QQuickItem;
class QQuickWindow;
namespace dreamscapes::native {class NativePanelRegion; class NativePanelOverlays;}
namespace dreamscapes::review {
class NativeReviewGridSurface : public native::NativePresentationSurface {
  public:
    [[nodiscard]] virtual std::optional<double> scrollOffset() const=0;
    [[nodiscard]] virtual bool setScrollOffset(double)=0;
};
class NativeReviewGridPresenter final : public QObject {
    Q_OBJECT
  public:
    using Factory=std::function<std::shared_ptr<NativeReviewGridSurface>(std::shared_ptr<ReviewGridModel>,native::NativePresentationSurface::Callbacks)>;
    NativeReviewGridPresenter(QQuickWindow *,GenerationReviewModel *,Factory,QObject *parent=nullptr);
    ~NativeReviewGridPresenter() override;
    [[nodiscard]] bool valid() const noexcept {return valid_;}
    void begin();
    void shutdown(std::function<void()>);
  private slots:
    void refreshBlocked();
    void refreshSaveAvailability();
  private:
    bool eventFilter(QObject *,QEvent *) override;
    bool show(QRectF);
    void hide();
    void ready();
    void failed();
    void closed();
    void restartForLanguage();
    void finishShutdown();
    void clearSession();
    void captureContext(const ReviewContextState &);
    QVariantList originals(const std::vector<vm::Value> &) const;
    void deliver(const char *method,const std::vector<vm::Value> &,bool list);
    void save(const char *method,const std::vector<vm::Value> &);
    QPointer<QQuickWindow> window_;
    QPointer<QQuickItem> root_,gallery_;
    QPointer<GenerationReviewModel> review_;
    QPointer<QObject> fileExporter_,photoExporter_;
    Factory factory_;
    std::unique_ptr<native::NativePanelRegion> region_;
    std::unique_ptr<native::NativePanelOverlays> overlays_;
    std::shared_ptr<ReviewGalleryModel> galleryModel_;
    std::shared_ptr<ReviewGridModel> grid_;
    std::shared_ptr<NativeReviewGridSurface> surface_;
    ii::core::ObservableSubscription contextObservation_;
    std::vector<vm::Value> contextValues_;
    QVariantList contextOriginals_;
    std::uint64_t contextGeneration_=0;
    std::vector<std::function<void()>> completion_;
    bool valid_=false,started_=false,stopping_=false,restarting_=false,destroying_=false;
    bool surfaceReady_=false,failed_=false,overlayBlocked_=false,active_=false;
};
} // namespace dreamscapes::review
