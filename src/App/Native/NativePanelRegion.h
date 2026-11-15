#pragma once

#include <QObject>
#include <QPointer>
#include <QRectF>
#include <functional>
#include <vector>

class QQuickItem;
class QQuickWindow;

namespace dreamscapes::native {
// Coordinates an embedded native surface with its temporary Qt host. Unsafe
// geometries and overlays leave the existing view in control.
class NativePanelRegion final : public QObject {
  public:
    struct Surface final {
        std::function<bool(QRectF)> show;
        std::function<void()> hide;
    };
    NativePanelRegion(QQuickWindow *, QQuickItem *, Surface, QObject *parent = nullptr);
    ~NativePanelRegion() override;
    void setReady(bool);
    void setBlocked(bool);
    void close();
  private:
    void watchAncestors();
    void schedule();
    void update();
    void deactivate();
    QPointer<QQuickWindow> window_;
    QPointer<QQuickItem> item_;
    Surface surface_;
    std::vector<QMetaObject::Connection> connections_;
    QRectF frame_;
    bool ready_ = false, blocked_ = false, active_ = false, scheduled_ = false, closed_ = false;
};
} // namespace dreamscapes::native
