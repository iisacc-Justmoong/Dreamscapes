#include "EditorCanvas.h"
#include <QScopedValueRollback>
#include <algorithm>

void EditorCanvas::constrainViewport() {
    if (m_constrainingViewport || !documentReady() || infiniteCanvas()
        || width() <= 0 || height() <= 0 || canvasWidth() <= 0 || canvasHeight() <= 0) return;
    const auto constrain = [](qreal pan, qreal viewport, qreal extent) {
        return extent <= viewport ? (viewport - extent) / 2
                                  : std::clamp(pan, viewport - extent, qreal{0});
    };
    const bool page = m_pagePresentation && document()->artboards.size() > 1 && activeArtboard();
    const auto offset = page ? (editingOrigin() - QPointF(canvasOriginX(), canvasOriginY())) * zoom() : QPointF{};
    const auto extent = page ? editingExtent() : iiSharedCanvas::CanvasExtent{canvasWidth(), canvasHeight()};
    const auto x = constrain(panX() + offset.x(), width(), extent.width * zoom()) - offset.x();
    const auto y = constrain(panY() + offset.y(), height(), extent.height * zoom()) - offset.y();
    QScopedValueRollback<bool> guard(m_constrainingViewport, true);
    setPanX(x);
    setPanY(y);
}

void EditorCanvas::setPagePresentation(bool enabled) {
    if (m_pagePresentation == enabled) return;
    m_pagePresentation = enabled;
    setRenderArtboardId(enabled && documentReady() && !infiniteCanvas()
        && document()->artboards.size() > 1 ? QString::fromStdString(m_activeArtboard) : QString{});
    emit pagePresentationChanged();
    fitToView();
}

void EditorCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    CanvasItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) constrainViewport();
}
