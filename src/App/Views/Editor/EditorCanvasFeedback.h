#pragma once
#include "EditorCanvas.h"
#include <QPointer>
#include <QQuickPaintedItem>
#include <QTimer>

// A presentation-only surface: input and document transactions stay in EditorCanvas.
class EditorCanvasFeedback : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *canvas READ canvas WRITE setCanvas NOTIFY canvasChanged)
public:
    explicit EditorCanvasFeedback(QQuickItem *parent = nullptr);
    EditorCanvas *canvas() const { return m_canvas; }
    void setCanvas(EditorCanvas *canvas);
    void paint(QPainter *painter);
signals:
    void canvasChanged();
private:
    void refreshFeedback();
    QPointer<EditorCanvas> m_canvas;
    QQuickPaintedItem *m_surface = nullptr;
    QTimer m_ants;
    qreal m_dashOffset = 0;
};
