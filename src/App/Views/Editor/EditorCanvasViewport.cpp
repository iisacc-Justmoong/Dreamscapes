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
    const auto x = constrain(panX(), width(), canvasWidth() * zoom());
    const auto y = constrain(panY(), height(), canvasHeight() * zoom());
    QScopedValueRollback<bool> guard(m_constrainingViewport, true);
    setPanX(x);
    setPanY(y);
}

void EditorCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    CanvasItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) constrainViewport();
}
