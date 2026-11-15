#include "NativePanelOverlays.h"
#include <QEvent>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickItem>
#include <QTimer>
#include <utility>

namespace dreamscapes::native {
NativePanelOverlays::NativePanelOverlays(QObject *root, std::function<void(bool)> changed, QObject *parent)
    : QObject(parent), root_(root), changed_(std::move(changed)) {
    if (root_) root_->installEventFilter(this);
    scan();
}
bool NativePanelOverlays::eventFilter(QObject *object, QEvent *event) {
    if (object == root_ && (event->type() == QEvent::ChildAdded || event->type() == QEvent::ChildRemoved)) scheduleScan();
    return QObject::eventFilter(object, event);
}
void NativePanelOverlays::scheduleScan() {
    if (scheduled_) return;
    scheduled_ = true;
    QTimer::singleShot(0, this, [this] { scheduled_ = false; scan(); });
}
void NativePanelOverlays::scan() {
    for (const auto &connection : connections_) disconnect(connection);
    connections_.clear(); watched_.clear();
    if (root_) {
        auto objects = root_->findChildren<QObject *>();
        objects.prepend(root_);
        const auto slot = metaObject()->method(metaObject()->indexOfSlot("refresh()"));
        for (auto *object : objects) {
            // Popup content can be attached to the window overlay after this
            // scan, even when its QObject owner is deep in another view.
            // Observe only overlay child changes rather than every list row.
            if (object->inherits("QQuickOverlay")) {
                if (auto *item = qobject_cast<QQuickItem *>(object))
                    connections_.push_back(connect(item, &QQuickItem::childrenChanged, this, [this] { scheduleScan(); }));
            }
            const auto *meta = object->metaObject();
            const bool modal = object->objectName() == QLatin1String("failureDetailsModal") ||
                (meta->indexOfProperty("open") >= 0 && meta->indexOfProperty("contentComponent") >= 0 &&
                 meta->indexOfProperty("showIcon") >= 0 && meta->indexOfProperty("title") >= 0);
            const char *name = modal ? "open" : object->inherits("QQuickPopup") ? "visible" : nullptr;
            if (!name) continue;
            const auto index = meta->indexOfProperty(name);
            if (index < 0) continue;
            const auto property = meta->property(index);
            if (property.metaType().id() != QMetaType::Bool || !property.hasNotifySignal()) continue;
            watched_.push_back({object, name});
            connections_.push_back(QObject::connect(object, property.notifySignal(), this, slot));
            connections_.push_back(connect(object, &QObject::destroyed, this, [this] { scheduleScan(); }));
        }
    }
    refresh();
}
void NativePanelOverlays::refresh() {
    bool value = false;
    for (const auto &watched : watched_) if (watched.object && watched.object->property(watched.property.constData()).toBool()) {
        value = true; break;
    }
    if (!initialized_ || blocked_ != value) {
        initialized_ = true; blocked_ = value;
        if (changed_) changed_(value);
    }
}
} // namespace dreamscapes::native
