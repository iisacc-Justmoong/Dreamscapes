#include "EditorCanvas.h"
#include <QMatrix4x4>
#include <QSGTransformNode>

namespace {
// The SDK owns composition and its opaque scene node. The app owns when that
// completed presentation is replaced; retaining it allocates no extra bitmap.
class FramePresentationNode final : public QSGTransformNode {
public:
    quint64 epoch = 0;
    bool complete = false;
    qreal zoom = 1;
    qreal panX = 0;
    qreal panY = 0;
};
}

QSGNode *EditorCanvas::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) {
    auto *presentation = static_cast<FramePresentationNode *>(oldNode);
    if (!presentation) presentation = new FramePresentationNode;

    if (presentation->complete && presentation->epoch == m_presentationEpoch
        && documentReady() && residentTileCount() == 0) {
        // Preparing the next frame must not expose the empty decode cache.
        // Adjust the retained node's viewport without accessing its internals.
        QMatrix4x4 delta;
        delta.translate(float(panX()), float(panY()));
        delta.scale(float(zoom() / presentation->zoom));
        delta.translate(float(-presentation->panX), float(-presentation->panY));
        presentation->setMatrix(delta);
        return presentation;
    }

    auto *content = presentation->firstChild();
    if (content) presentation->removeChildNode(content);
    content = CanvasItem::updatePaintNode(content, data);
    if (content) presentation->appendChildNode(content);
    presentation->setMatrix(QMatrix4x4{});
    presentation->epoch = m_presentationEpoch;
    presentation->complete = documentReady() && residentTileCount() > 0;
    presentation->zoom = zoom();
    presentation->panX = panX();
    presentation->panY = panY();
    return presentation;
}
