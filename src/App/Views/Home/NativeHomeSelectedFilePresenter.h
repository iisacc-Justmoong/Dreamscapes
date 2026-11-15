#pragma once

#include "NativeHomeSelectedFile.h"
#include "App/Native/NativePresentationSurface.h"
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <vector>

class QQuickItem;
class QQuickWindow;
namespace dreamscapes {
namespace native { class NativePanelOverlays; class NativePanelRegion; }
namespace home {
class NativeHomeSelectedFileSession;
class NativeHomeSelectedFilePresenter final : public QObject {
    Q_OBJECT
  public:
    using Surface = native::NativePresentationSurface;
    using Factory = std::function<std::shared_ptr<Surface>(std::shared_ptr<HomeSelectedFileModel>, Surface::Callbacks)>;
    NativeHomeSelectedFilePresenter(QQuickWindow *, Factory, QObject *parent = nullptr);
    ~NativeHomeSelectedFilePresenter() override;
    bool valid() const noexcept { return valid_; }
    void begin();
    void shutdown(std::function<void()> done);
  protected:
    bool eventFilter(QObject *, QEvent *) override;
  private slots:
    void synchronizeFile();
  private:
    bool show(QRectF);
    void hide();
    void openFile(QVariantMap);
    void ready();
    void failed();
    void closed();
    void restartForLanguage();
    void clearSession();
    void finishShutdown();
    QPointer<QQuickWindow> window_;
    QPointer<QQuickItem> selected_;
    Factory factory_;
    std::unique_ptr<native::NativePanelRegion> region_;
    std::unique_ptr<native::NativePanelOverlays> overlays_;
    std::unique_ptr<NativeHomeSelectedFileSession> session_;
    std::shared_ptr<HomeSelectedFileModel> model_;
    std::shared_ptr<Surface> surface_;
    std::vector<std::function<void()>> completion_;
    bool valid_ = false, started_ = false, active_ = false, surfaceReady_ = false, failed_ = false;
    bool stopping_ = false, restarting_ = false, destroying_ = false;
};
} // namespace home
} // namespace dreamscapes
