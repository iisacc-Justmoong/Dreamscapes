#include "App/Native/NativePanelOverlays.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QQmlComponent>
#include <QQmlEngine>
#include <iostream>
#include <stdexcept>

class Modal final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool open READ open WRITE setOpen NOTIFY openChanged)
  public:
    using QObject::QObject;
    bool open() const { return open_; }
    void setOpen(bool value) { if (open_ != value) { open_ = value; emit openChanged(); } }
  signals:
    void openChanged();
  private:
    bool open_ = false;
};
class ModalItem final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool open READ open WRITE setOpen NOTIFY openChanged)
  public:
    bool open() const { return open_; }
    void setOpen(bool value) { if (open_ != value) { open_ = value; emit openChanged(); } }
  signals:
    void openChanged();
  private:
    bool open_ = false;
};
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void settle() { for (int i = 0; i != 5; ++i) QCoreApplication::processEvents(); }
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        QObject root;
        Modal existing(&root); existing.setObjectName("failureDetailsModal");
        bool blocked = true;
        dreamscapes::native::NativePanelOverlays overlays(&root, [&](bool value) { blocked = value; });
        settle(); check(!blocked, "closed modal blocked the queue");
        existing.setOpen(true); settle(); check(blocked, "open modal did not block the queue");
        existing.setOpen(false); settle(); check(!blocked, "closed modal left the queue blocked");
        auto *later = new Modal(&root); later->setObjectName("failureDetailsModal");
        settle(); later->setOpen(true); settle(); check(blocked, "dynamically added modal was not tracked");
        delete later; settle(); check(!blocked, "destroyed modal left the queue blocked");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import QtQuick.Controls
            ApplicationWindow { width: 800; height: 600; property var testOverlay: Overlay.overlay }
        )QML", QUrl());
        auto qmlRoot = std::unique_ptr<QObject>(component.create());
        if (!qmlRoot) throw std::runtime_error(component.errorString().toStdString());
        auto *overlay = qobject_cast<QQuickItem *>(qmlRoot->property("testOverlay").value<QObject *>());
        check(overlay && overlay->inherits("QQuickOverlay"), "real Qt overlay was not created");
        QObject nested(qmlRoot.get());
        bool qmlBlocked = false;
        dreamscapes::native::NativePanelOverlays qmlOverlays(qmlRoot.get(), [&](bool value) { qmlBlocked = value; });
        auto *nestedModal = new ModalItem;
        nestedModal->setParent(&nested); nestedModal->setParentItem(overlay);
        nestedModal->setObjectName("failureDetailsModal"); settle();
        nestedModal->setOpen(true); settle();
        check(qmlBlocked, "modal attached dynamically to the overlay was not tracked");
        delete nestedModal; settle(); check(!qmlBlocked, "removed overlay modal left the queue blocked");
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
#include "tst_native_review_queue_overlays.moc"
