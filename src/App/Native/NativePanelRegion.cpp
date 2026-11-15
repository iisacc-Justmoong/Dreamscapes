#include "NativePanelRegion.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <utility>

namespace dreamscapes::native {
namespace {
bool finite(const QRectF &value) {
    return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.width()) &&
           std::isfinite(value.height()) && value.width() > 0 && value.height() > 0 &&
           value.width() <= 8192 && value.height() <= 8192 &&
           std::abs(value.x()) <= 32767 && std::abs(value.y()) <= 32767;
}
bool contains(const QRectF &outer, const QRectF &inner) {
    constexpr qreal tolerance = 0.01;
    return outer.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(inner);
}
}
NativePanelRegion::NativePanelRegion(QQuickWindow *window, QQuickItem *item, Surface surface, QObject *parent)
    : QObject(parent), window_(window), item_(item), surface_(std::move(surface)) {
    if (window_) {
        connect(window_, &QWindow::visibleChanged, this, [this] { schedule(); });
        connect(window_, &QWindow::widthChanged, this, [this] { schedule(); });
        connect(window_, &QWindow::heightChanged, this, [this] { schedule(); });
        connect(window_, &QObject::destroyed, this, [this] { close(); });
    }
    watchAncestors();
}
NativePanelRegion::~NativePanelRegion() { close(); }
void NativePanelRegion::watchAncestors() {
    for (const auto &connection : connections_) disconnect(connection);
    connections_.clear();
    for (auto *item = item_.data(); item; item = item->parentItem()) {
        auto changed = [this] { schedule(); };
        connections_.push_back(connect(item, &QQuickItem::xChanged, this, changed));
        connections_.push_back(connect(item, &QQuickItem::yChanged, this, changed));
        connections_.push_back(connect(item, &QQuickItem::widthChanged, this, changed));
        connections_.push_back(connect(item, &QQuickItem::heightChanged, this, changed));
        connections_.push_back(connect(item, &QQuickItem::visibleChanged, this, changed));
        connections_.push_back(connect(item, &QQuickItem::enabledChanged, this, changed));
        connections_.push_back(connect(item, &QQuickItem::opacityChanged, this, changed));
        connections_.push_back(connect(item, &QQuickItem::rotationChanged, this, changed));
        connections_.push_back(connect(item, &QQuickItem::scaleChanged, this, changed));
        connections_.push_back(connect(item, &QQuickItem::clipChanged, this, changed));
        connections_.push_back(connect(item, &QQuickItem::parentChanged, this, [this] { watchAncestors(); schedule(); }));
        connections_.push_back(connect(item, &QObject::destroyed, this, [this] { schedule(); }));
    }
}
void NativePanelRegion::setReady(bool value) {
    if (ready_ == value) return;
    ready_ = value;
    if (!value) deactivate();
    schedule();
}
void NativePanelRegion::setBlocked(bool value) {
    if (blocked_ == value) return;
    blocked_ = value;
    if (value) deactivate();
    schedule();
}
void NativePanelRegion::schedule() {
    if (closed_ || scheduled_) return;
    scheduled_ = true;
    QTimer::singleShot(0, this, [this] { scheduled_ = false; update(); });
}
void NativePanelRegion::deactivate() {
    if (!active_) return;
    active_ = false;
    const QPointer<NativePanelRegion> self(this);
    if (surface_.hide) surface_.hide();
    if (!self) return;
    if (item_) {
        if (item_->opacity() == 0) item_->setOpacity(1);
        if (!item_->isEnabled()) item_->setEnabled(true);
    }
}
void NativePanelRegion::update() {
    if (closed_ || !ready_ || blocked_ || !window_ || !window_->isVisible() || !item_ ||
        item_->window() != window_ || !item_->isVisible() || !surface_.show) { deactivate(); return; }
    const auto rect = item_->mapRectToScene(item_->boundingRect());
    if (!finite(rect) || !contains(QRectF(0, 0, window_->width(), window_->height()), rect)) { deactivate(); return; }
    for (auto *node = item_.data(); node; node = node->parentItem()) {
        if (node->rotation() != 0 || node->scale() != 1 ||
            (node != item_ && (!node->isEnabled() || node->opacity() != 1)) ||
            (node == item_ && !active_ && (!node->isEnabled() || node->opacity() != 1)) ||
            (node->clip() && !contains(node->mapRectToScene(node->boundingRect()), rect))) {
            deactivate(); return;
        }
    }
    if (active_ && frame_ == rect) return;
    const QPointer<NativePanelRegion> self(this);
    const auto shown = surface_.show(rect);
    if (!self) return;
    if (!shown) { deactivate(); return; }
    frame_ = rect;
    if (!active_) {
        active_ = true;
        item_->setOpacity(0);
        item_->setEnabled(false);
    }
}
void NativePanelRegion::close() {
    if (closed_) return;
    closed_ = true;
    deactivate();
    for (const auto &connection : connections_) disconnect(connection);
    connections_.clear();
    surface_ = {};
}
} // namespace dreamscapes::native
