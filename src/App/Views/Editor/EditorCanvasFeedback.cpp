#include "EditorCanvasFeedback.h"
#include <QPainter>
#include <QQuickWindow>
#include <cmath>

namespace {
class FeedbackSurface : public QQuickPaintedItem {
public:
    explicit FeedbackSurface(EditorCanvasFeedback *owner) : QQuickPaintedItem(owner), m_owner(owner) {
        setObjectName("canvasFeedbackSurface");
        setAcceptedMouseButtons(Qt::NoButton); setAcceptTouchEvents(false); setAcceptHoverEvents(false);
        setAntialiasing(true);
    }
    void paint(QPainter *painter) override {
        painter->save(); painter->translate(-x(), -y()); m_owner->paint(painter); painter->restore();
    }
private:
    EditorCanvasFeedback *m_owner;
};
}
EditorCanvasFeedback::EditorCanvasFeedback(QQuickItem *parent) : QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptTouchEvents(false);
    setAcceptHoverEvents(false);
    setClip(true);
    m_surface = new FeedbackSurface(this);
    m_ants.setInterval(100);
    connect(&m_ants, &QTimer::timeout, this, [this] {
        m_dashOffset = std::fmod(m_dashOffset + 1, 8);
        m_surface->update();
    });
    connect(this, &QQuickItem::visibleChanged, this, &EditorCanvasFeedback::refreshFeedback);
    connect(this, &QQuickItem::widthChanged, this, &EditorCanvasFeedback::refreshFeedback);
    connect(this, &QQuickItem::heightChanged, this, &EditorCanvasFeedback::refreshFeedback);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
        if (window) connect(window, &QWindow::visibilityChanged, this, &EditorCanvasFeedback::refreshFeedback, Qt::UniqueConnection);
        refreshFeedback();
    });
}
void EditorCanvasFeedback::setCanvas(EditorCanvas *canvas) {
    if (m_canvas == canvas) return;
    if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas;
    if (canvas) {
        connect(canvas, &EditorCanvas::interactionFeedbackChanged, this, &EditorCanvasFeedback::refreshFeedback);
        connect(canvas, &QQuickItem::visibleChanged, this, &EditorCanvasFeedback::refreshFeedback);
        connect(canvas, &QObject::destroyed, this, [this] { m_canvas = nullptr; refreshFeedback(); emit canvasChanged(); });
    }
    refreshFeedback();
    emit canvasChanged();
}
void EditorCanvasFeedback::refreshFeedback() {
    QRectF bounds;
    if (m_canvas && m_canvas->documentReady() && isVisible() && m_canvas->isVisible()) {
        const auto mapping = m_canvas->feedbackTransform();
        const auto guide = m_canvas->gestureFeedbackPath();
        if (!guide.isEmpty()) bounds = mapping.map(guide).boundingRect().adjusted(-3, -3, 3, 3);
        if (!m_canvas->backgroundMaskActive()) bounds |= mapping.map(m_canvas->m_selectionOutline).boundingRect();
        bounds |= m_canvas->brushFeedbackPath().boundingRect();
        if (!bounds.isEmpty()) {
            bounds = bounds.adjusted(-6, -6, 6, 6).intersected(boundingRect());
            // Reuse small textures across nearby cursor moves instead of allocating
            // a viewport-sized image or resizing the image on every pointer pixel.
            if (!bounds.isEmpty()) bounds = QRectF(
                QPointF(std::floor(bounds.left() / 32) * 32, std::floor(bounds.top() / 32) * 32),
                QPointF(std::ceil(bounds.right() / 32) * 32, std::ceil(bounds.bottom() / 32) * 32));
        }
    }
    m_surface->setPosition(bounds.topLeft());
    m_surface->setSize(bounds.size());
    m_surface->setVisible(!bounds.isEmpty());
    const bool animate = m_canvas && m_canvas->documentReady() && isVisible() && m_canvas->isVisible() && window()
        && window()->isVisible() && window()->visibility() != QWindow::Minimized
        && ((!m_canvas->backgroundMaskActive() && !m_canvas->m_selectionOutline.isEmpty()) || !m_canvas->gestureFeedbackPath().isEmpty());
    if (animate && !m_ants.isActive()) m_ants.start();
    else if (!animate) m_ants.stop();
    m_surface->update();
}
void EditorCanvasFeedback::paint(QPainter *painter) {
    if (m_canvas && m_canvas->isVisible()) m_canvas->paintInteractionFeedback(painter, m_dashOffset);
}
