#pragma once

#include "NativeHomeRecentFilesSession.h"
#include "App/Native/NativePresentationSurface.h"
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>
#include <vector>

class QQuickItem;
class QQuickWindow;
namespace dreamscapes {
namespace native { class NativePanelOverlays; class NativePanelRegion; }
namespace home {
class NativeHomeRecentFilesPresenter final : public QObject {
    Q_OBJECT
  public:
    using Surface = native::NativePresentationSurface;
    using Factory = std::function<std::shared_ptr<Surface>(std::shared_ptr<HomeRecentFilesModel>,
        QPointer<NativeHomeRecentFilesSession>, Surface::Callbacks)>;
    NativeHomeRecentFilesPresenter(QQuickWindow *, Factory, QObject *parent = nullptr);
    ~NativeHomeRecentFilesPresenter() override;
    bool valid() const noexcept { return valid_; }
    void begin();
    void shutdown(std::function<void()> done);
  protected:
    bool eventFilter(QObject *, QEvent *) override;
  private slots:
    void synchronize();
  private:
    bool show(QRectF);
    void hide();
    void select(QString);
    void filter(QString);
    void open(QVariant);
    void browse();
    void ready();
    void failed();
    void closed();
    void restartForLanguage();
    void clearSession();
    void finishShutdown();
    QPointer<QQuickWindow> window_;
    QPointer<QQuickItem> panel_;
    Factory factory_;
    std::unique_ptr<native::NativePanelRegion> region_;
    std::unique_ptr<native::NativePanelOverlays> overlays_;
    std::unique_ptr<NativeHomeRecentFilesSession> session_;
    std::shared_ptr<HomeRecentFilesModel> model_;
    std::shared_ptr<Surface> surface_;
    std::shared_ptr<bool> synchronizing_ = std::make_shared<bool>(false);
    std::vector<std::function<void()>> completion_;
    bool valid_ = false, started_ = false, active_ = false, surfaceReady_ = false, failed_ = false;
    bool stopping_ = false, restarting_ = false, destroying_ = false;
};
} // namespace home
} // namespace dreamscapes
