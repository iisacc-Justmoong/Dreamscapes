#pragma once
#include <iiSharedCanvas/QtAdapter/CanvasItem.h>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class EditorCanvas : public iiSharedCanvas::CanvasItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap specification READ specification NOTIFY specificationChanged)
public:
    explicit EditorCanvas(QQuickItem *parent = nullptr) : CanvasItem(parent) {}
    QVariantMap specification() const { return m_specification; }
    Q_INVOKABLE bool createCanvas(const QVariantMap &specification);
signals:
    void specificationChanged();
private:
    iiSharedCanvas::Document m_canvas;
    QVariantMap m_specification;
};
