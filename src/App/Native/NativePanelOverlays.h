#pragma once

#include <QObject>
#include <QPointer>
#include <functional>
#include <vector>

namespace dreamscapes::native {
// Tracks the existing host's modal and popup state while that host is still
// present. It prevents an embedded native surface from covering those views.
class NativePanelOverlays final : public QObject {
    Q_OBJECT
  public:
    NativePanelOverlays(QObject *root, std::function<void(bool)> changed, QObject *parent = nullptr);
  protected:
    bool eventFilter(QObject *, QEvent *) override;
  private slots:
    void refresh();
  private:
    void scan();
    void scheduleScan();
    struct Watched final { QPointer<QObject> object; QByteArray property; };
    QPointer<QObject> root_;
    std::function<void(bool)> changed_;
    std::vector<Watched> watched_;
    std::vector<QMetaObject::Connection> connections_;
    bool scheduled_ = false, initialized_ = false, blocked_ = false;
};
} // namespace dreamscapes::native
