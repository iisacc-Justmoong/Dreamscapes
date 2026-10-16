#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <QBitmap>
#include <QCursor>
#include <QPainter>
#include <QPainterPathStroker>
#include <QRegion>
#include <cmath>
#include <numbers>

using namespace editorTools;

QTransform EditorCanvas::feedbackTransform() const {
    const auto origin = editingOrigin();
    return QTransform(zoom(), 0, 0, zoom(), panX() + (origin.x() - canvasOriginX()) * zoom(),
        panY() + (origin.y() - canvasOriginY()) * zoom());
}
QRectF EditorCanvas::elementCreationBounds() const {
    auto bounds = QRectF(m_elementStart, m_elementCurrent).normalized();
    if (toggle(m_toolValues, 3)) bounds.setHeight(bounds.width());
    return bounds;
}
QPainterPath EditorCanvas::gestureFeedbackPath() const {
    if (!documentReady()) return {};
    if (m_creatingElement) return elementCreationPath();
    if (backgroundMaskActive()) {
        const auto tool = m_toolValues.value("maskTool", "Brush").toString();
        const auto &points = tool == "Polygon" ? m_maskPolygon : m_maskGesturePoints;
        if (points.isEmpty()) return {};
        QPainterPath path;
        const auto bounds = QRectF(points.first(), points.last()).normalized();
        if (tool == "Rectangle") path.addRect(bounds);
        else if (tool == "Ellipse") path.addEllipse(bounds);
        else if (tool == "Lasso" || tool == "Polygon" || tool == "Gradient") {
            path.moveTo(points.first());
            for (int i = 1; i < points.size(); ++i) path.lineTo(points[i]);
            if (tool == "Polygon" && m_feedbackPointerInside) path.lineTo(editingPosition(m_feedbackPointer));
            if (tool == "Lasso" && points.size() > 2) path.closeSubpath();
        }
        return path;
    }
    if (!m_gestureActive || m_gesture.isEmpty()) return {};
    if (m_tool == "masking") {
        const auto mode = m_toolValues.value("selector", "Brush").toString();
        if (QStringList{"Subject", "Region", "Range"}.contains(mode)) return {};
        if (mode == "Linear" && m_gesture.size() > 1) {
            const auto bounds = QRectF(m_gesture.first(), m_gesture.last()).normalized();
            const double angle = number(m_toolValues, 4) * std::numbers::pi / 180;
            const QPointF direction(std::cos(angle), std::sin(angle)), perpendicular(-direction.y(), direction.x());
            const double span = std::max(1., bounds.width() * std::abs(direction.x()) + bounds.height() * std::abs(direction.y()));
            const auto start = bounds.center() - direction * span / 2, end = bounds.center() + direction * span / 2;
            QPainterPath guide; guide.moveTo(start); guide.lineTo(end);
            for (const auto point : {start, end}) { guide.moveTo(point - perpendicular * (4 / zoom())); guide.lineTo(point + perpendicular * (4 / zoom())); }
            return guide;
        }
    }
    if (m_tool == "select" || m_tool == "masking") return selectionGesturePath(m_gesture);
    QPainterPath stroke; stroke.moveTo(m_gesture.first());
    for (int i = 1; i < m_gesture.size(); ++i) stroke.lineTo(m_gesture[i]);
    QPainterPathStroker outline; outline.setWidth(std::max(1.0, number(m_toolValues, 0, 84)));
    outline.setCapStyle(Qt::RoundCap); outline.setJoinStyle(Qt::RoundJoin);
    return outline.createStroke(stroke);
}
QPainterPath EditorCanvas::brushFeedbackPath() const {
    if (!documentReady() || !m_feedbackPointerInside || !isVisible() || m_navigatingCanvas) return {};
    const auto point = editingPosition(m_feedbackPointer);
    if (!infiniteCanvas() && !QRectF(0, 0, editingWidth(), editingHeight()).contains(point)) return {};
    double diameter = 0;
    QString shape = "Round";
    bool layerSpace = false;
    if (m_tool == "brush") {
        diameter = number(m_toolValues, 4, 8) / (toggle(m_toolValues, 6) ? zoom() : 1);
        shape = text(m_toolValues, 7, "Round"); layerSpace = rasterLayerSelected();
    } else if (m_tool == "eraser") {
        diameter = number(m_toolValues, 0, 8);
        layerSpace = m_toolValues.value("selector", "Pixel").toString() == "Pixel";
    } else if (m_tool == "retouch" && m_toolValues.value("selector").toString() != "Face Refine") {
        diameter = number(m_toolValues, 0, 84); layerSpace = true;
    } else if (m_tool == "masking" && m_toolValues.value("selector", "Brush").toString() == "Brush") {
        diameter = number(m_toolValues, 0, 84);
    } else if (backgroundMaskActive() && QStringList{"Brush", "Eraser"}.contains(m_toolValues.value("maskTool", "Brush").toString())) {
        diameter = m_toolValues.value("maskSize", 40).toDouble();
    }
    if (!(diameter > 0) || !std::isfinite(diameter)) return {};
    const double radius = diameter / 2;
    QPainterPath tip;
    if (shape == "Square") tip.addRect({-radius, -radius, diameter, diameter});
    else if (shape == "Diamond") {
        tip.moveTo(0, -radius); tip.lineTo(radius, 0); tip.lineTo(0, radius); tip.lineTo(-radius, 0); tip.closeSubpath();
    } else tip.addEllipse(QPointF{}, radius, radius);
    const auto layer = layerSpace ? elementWorldTransform(selectedLayerId()) : QTransform{};
    QTransform screen(layer.m11() * zoom(), layer.m12() * zoom(), layer.m21() * zoom(), layer.m22() * zoom(),
        m_feedbackPointer.x(), m_feedbackPointer.y());
    return screen.map(tip);
}
QVariantMap EditorCanvas::interactionFeedback() const {
    const auto cursor = brushFeedbackPath();
    const auto gesture = gestureFeedbackPath();
    return {{"tool", m_tool}, {"mode", backgroundMaskActive() ? m_toolValues.value("maskTool", "Brush") : m_toolValues.value("selector")},
        {"pointerVisible", !cursor.isEmpty()}, {"cursorBounds", cursor.boundingRect()},
        {"blocked", m_pixelLocks.contains(selectedLayerId())}, {"gestureActive", m_gestureActive || m_creatingElement || m_maskGestureActive || !m_maskPolygon.empty()},
        {"gestureBounds", feedbackTransform().map(gesture).boundingRect()}, {"gesturePoints", m_gestureActive ? m_gesture.size() : m_maskGesturePoints.size()},
        {"selectionBounds", feedbackTransform().map(m_selectionOutline).boundingRect()},
        {"pointer", m_feedbackPointer}};
}
void EditorCanvas::notifyInteractionFeedback() {
    if (m_tool != "elements" && !m_navigatingCanvas) {
        if (!brushFeedbackPath().isEmpty()) setCursor(m_pixelLocks.contains(selectedLayerId()) ? Qt::ForbiddenCursor : Qt::BlankCursor);
        else if (m_feedbackPointerInside && (m_tool == "select" || m_tool == "masking" || backgroundMaskActive())) setCursor(Qt::CrossCursor);
        else unsetCursor();
    }
    emit interactionFeedbackChanged();
}
void EditorCanvas::updatePointerFeedback(const QPointF &position, bool inside) {
    m_feedbackPointer = position;
    m_feedbackPointerInside = inside && boundingRect().contains(position);
    notifyInteractionFeedback();
}
void EditorCanvas::cancelInteractionGesture() {
    if (liveStrokeActive()) {
        cancelStroke();
        if (m_strokeAuthorship) { m_canvas.authorship = *m_strokeAuthorship; refresh(); }
        m_strokeAuthorship.reset(); m_strokePixels = {}; m_historySuspended = false;
    }
    if (m_gestureActive && m_historySuspended && m_tool == "retouch") {
        m_historySuspended = false; recordHistory();
    }
    m_gestureActive = false; m_gesture.clear(); m_creatingElement = false;
    m_primaryPointerDown = false;
    m_navigatingCanvas = false;
    cancelBackgroundMaskGesture();
    emit elementTransformChanged();
    notifyInteractionFeedback();
}
void EditorCanvas::updateSelectionOutline() {
    m_selectionOutline = {};
    if (m_selection.alpha.empty()) { notifyInteractionFeedback(); return; }
    const auto width = int(m_selection.extent.width), height = int(m_selection.extent.height);
    QImage mono(width, height, QImage::Format_MonoLSB); mono.fill(0);
    mono.setColor(0, qRgb(255, 255, 255)); mono.setColor(1, qRgb(0, 0, 0));
    for (int y = 0; y < height; ++y) {
        auto *row = mono.scanLine(y);
        for (int x = 0; x < width; ++x)
            if (m_selection.alpha[std::size_t(y) * width + x] >= 128) row[x / 8] |= 1 << (x % 8);
    }
    m_selectionOutline.addRegion(QRegion(QBitmap::fromImage(mono)));
    m_selectionOutline = m_selectionOutline.simplified();
    notifyInteractionFeedback();
}
void EditorCanvas::paintInteractionFeedback(QPainter *painter, qreal dashOffset) const {
    if (!painter || !documentReady()) return;
    painter->save(); painter->setRenderHint(QPainter::Antialiasing);
    const auto mapping = feedbackTransform();
    const auto ants = [&](const QPainterPath &path) {
        if (path.isEmpty()) return;
        painter->save();
        // Pixel-aligned ants keep black and white distinct on either backdrop.
        painter->setRenderHint(QPainter::Antialiasing, false);
        QPen pen(Qt::white, 2); painter->setPen(pen); painter->setBrush(Qt::NoBrush); painter->drawPath(path);
        pen.setColor(QColor("#202020")); pen.setWidthF(1); pen.setDashPattern({4, 4}); pen.setDashOffset(dashOffset);
        painter->setPen(pen); painter->drawPath(path);
        painter->restore();
    };
    painter->save();
    if (!infiniteCanvas()) painter->setClipRect(mapping.mapRect(QRectF(0, 0, editingWidth(), editingHeight())));
    if (!backgroundMaskActive()) ants(mapping.map(m_selectionOutline));
    const auto gesture = mapping.map(gestureFeedbackPath());
    if (!gesture.isEmpty()) {
        const bool trail = m_tool == "retouch" || m_tool == "eraser" || (m_tool == "masking" && m_toolValues.value("selector", "Brush").toString() == "Brush");
        if (m_creatingElement || trail) painter->fillPath(gesture, QColor(64, 150, 243, 35));
        ants(gesture);
    }
    painter->restore();
    const auto cursor = brushFeedbackPath();
    if (!cursor.isEmpty()) {
        painter->setBrush(Qt::NoBrush); painter->setPen(QPen(QColor("#202020"), 3)); painter->drawPath(cursor);
        painter->setPen(QPen(Qt::white, 1)); painter->drawPath(cursor);
        if (cursor.boundingRect().width() < 8 || m_tool == "retouch"
            || cursor.boundingRect().width() > width() || cursor.boundingRect().height() > height()) {
            QPainterPath center; center.moveTo(m_feedbackPointer + QPointF(-4, 0)); center.lineTo(m_feedbackPointer + QPointF(4, 0));
            center.moveTo(m_feedbackPointer + QPointF(0, -4)); center.lineTo(m_feedbackPointer + QPointF(0, 4));
            painter->setPen(QPen(QColor("#202020"), 3)); painter->drawPath(center);
            painter->setPen(QPen(Qt::white, 1)); painter->drawPath(center);
        }
    }
    painter->restore();
}
