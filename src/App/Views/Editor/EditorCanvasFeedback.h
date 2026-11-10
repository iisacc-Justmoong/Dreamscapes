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
    Q_PROPERTY(QColor lightColor MEMBER m_lightColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor darkColor MEMBER m_darkColor NOTIFY paletteChanged)
    Q_PROPERTY(QColor gestureColor MEMBER m_gestureColor NOTIFY paletteChanged)
public:
    explicit EditorCanvasFeedback(QQuickItem *parent = nullptr);
    EditorCanvas *canvas() const { return m_canvas; }
    void setCanvas(EditorCanvas *canvas);
    void paint(QPainter *painter);
signals:
    void canvasChanged();
    void paletteChanged();
private:
    void refreshFeedback();
    QPointer<EditorCanvas> m_canvas;
    QColor m_lightColor, m_darkColor, m_gestureColor;
    QQuickPaintedItem *m_surface = nullptr;
    QTimer m_ants;
    qreal m_dashOffset = 0;
};
