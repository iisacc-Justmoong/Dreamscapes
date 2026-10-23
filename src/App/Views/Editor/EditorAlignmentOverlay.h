#pragma once
#include "EditorCanvas.h"
#include <QPointer>
#include <QQuickItem>

class EditorAlignmentOverlay : public QQuickItem {
    friend class AlignmentSoftwareSurface;
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *canvas READ canvas WRITE setCanvas NOTIFY canvasChanged)
    Q_PROPERTY(QString family READ family WRITE setFamily NOTIFY familyChanged)
    Q_PROPERTY(bool preview READ preview WRITE setPreview NOTIFY previewChanged)
    Q_PROPERTY(QVariantList labels READ labels NOTIFY labelsChanged)
  public:
    explicit EditorAlignmentOverlay(QQuickItem *parent = nullptr);
    EditorCanvas *canvas() const {
        return m_canvas;
    }
    void setCanvas(EditorCanvas *);
    QString family() const {
        return m_family;
    }
    void setFamily(const QString &);
    bool preview() const {
        return m_preview;
    }
    void setPreview(bool);
    QVariantList labels() const;
  signals:
    void canvasChanged();
    void familyChanged();
    void previewChanged();
    void labelsChanged();

  protected:
    QSGNode *updatePaintNode(QSGNode *, UpdatePaintNodeData *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    QPointer<EditorCanvas> m_canvas;
    QString m_family = "grid";
    bool m_preview = false;
    int m_dragGuide = -1;
    QPointF m_last;
    QQuickItem *m_software = nullptr;
    QSGNode *renderGeometry();
    void refresh();
};
