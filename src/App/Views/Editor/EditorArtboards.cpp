#include "EditorCanvas.h"
#include <QWheelEvent>
#include <QScopedValueRollback>
#include <algorithm>
using namespace iiSharedCanvas;

const Artboard *EditorCanvas::activeArtboard() const {
    return documentReady() ? findArtboard(*document(), m_activeArtboard) : nullptr;
}
int EditorCanvas::activeArtboardIndex() const {
    if (documentReady()) for (int i = 0; i < int(document()->artboards.size()); ++i)
        if (document()->artboards[i].id == m_activeArtboard) return i;
    return 0;
}
CanvasExtent EditorCanvas::editingExtent() const { return activeArtboard() ? activeArtboard()->region.extent : CanvasExtent{canvasWidth(), canvasHeight()}; }
int EditorCanvas::editingWidth() const { return editingExtent().width; }
int EditorCanvas::editingHeight() const { return editingExtent().height; }
QPointF EditorCanvas::editingOrigin() const {
    if (const auto *board = activeArtboard()) return {qreal(board->region.origin.x), qreal(board->region.origin.y)};
    return {qreal(canvasOriginX()), qreal(canvasOriginY())};
}
bool EditorCanvas::belongsToActiveArtboard(const Layer &layer) const {
    return !activeArtboard() || layerProperties(layer).artboardId == m_activeArtboard;
}
FrameRenderResult EditorCanvas::renderEditingFrame(const Document &value) const {
    return activeArtboard() ? renderArtboard(value, frame(), m_activeArtboard) : renderFrame(value, frame());
}
std::string EditorCanvas::backgroundLayerId() const {
    return activeArtboard() ? m_activeArtboard + ":background.layer" : "canvas.background.layer";
}
bool EditorCanvas::selectArtboard(int index, bool reveal) {
    if (liveStrokeActive()) return fail(tr("Finish the current stroke before changing artboards."));
    if (!documentReady() || document()->artboards.empty()) return index == 0;
    if (index < 0 || index >= int(document()->artboards.size())) return fail(tr("Choose an existing artboard."));
    const auto id = document()->artboards[index].id;
    setRenderArtboardId(m_pagePresentation && !infiniteCanvas() && document()->artboards.size() > 1
        ? QString::fromStdString(id) : QString{});
    const bool changed = id != m_activeArtboard;
    if (changed) {
        m_activeArtboard = id; clearSelection(); clearAreaSelection();
        m_clippingKey.clear(); m_clippingImage = QUrl{}; m_eraserOverlay = QUrl{};
        for (auto layer = document()->layers.crbegin(); layer != document()->layers.crend(); ++layer)
            if (belongsToActiveArtboard(*layer)) { selectLayer(QString::fromStdString(layerProperties(*layer).id)); break; }
        emit activeArtboardChanged(); emit documentInfoChanged();
    }
    if ((reveal || (changed && m_pagePresentation && !infiniteCanvas() && document()->artboards.size() > 1)) && width() > 0) {
        if (m_pagePresentation && !infiniteCanvas() && document()->artboards.size() > 1) {
            fitToView();
            clearError();
            return true;
        }
        const auto *board = activeArtboard();
        const qreal left = (board->region.origin.x - canvasOriginX()) * zoom();
        const qreal extent = board->region.extent.width * zoom();
        qreal target = panX();
        if (left + target < 0 || extent > width()) target = -left;
        else if (left + extent + target > width()) target = width() - left - extent;
        setPanX(std::clamp(target, std::min(qreal{0}, width() - canvasWidth() * zoom()), qreal{0}));
    }
    clearError(); return true;
}
QPointF EditorCanvas::editingPosition(const QPointF &position) const {
    return QPointF((position.x() - panX()) / zoom() + canvasOriginX(),
                   (position.y() - panY()) / zoom() + canvasOriginY()) - editingOrigin();
}
bool EditorCanvas::selectArtboardAt(const QPointF &position) {
    if (!documentReady() || document()->artboards.empty()) return true;
    const QPointF world{(position.x() - panX()) / zoom() + canvasOriginX(), (position.y() - panY()) / zoom() + canvasOriginY()};
    if (m_pagePresentation && !infiniteCanvas() && document()->artboards.size() > 1) {
        const auto *board = activeArtboard();
        if (!board) return false;
        const auto &region = board->region;
        return QRectF(region.origin.x, region.origin.y, region.extent.width, region.extent.height).contains(world);
    }
    for (int i = int(document()->artboards.size()) - 1; i >= 0; --i) {
        const auto &r = document()->artboards[i].region;
        if (QRectF(r.origin.x, r.origin.y, r.extent.width, r.extent.height).contains(world)) return selectArtboard(i, false);
    }
    return false; // Workspace gaps receive no paint or element input.
}
void EditorCanvas::fitToView() {
    if (!documentReady() || document()->artboards.size() < 2) {
        setRenderArtboardId({});
        CanvasItem::fitToView();
        return;
    }
    if (width() <= 0 || height() <= 0) return;
    if (!activeArtboard()) selectArtboard(activeArtboardIndex(), false);
    if (m_pagePresentation && !infiniteCanvas()) {
        const auto extent = editingExtent();
        if (extent.width <= 0 || extent.height <= 0) return;
        const auto scale = std::min(width() * 0.70 / extent.width, height() * 0.80 / extent.height);
        // Set the camera as one operation; all coordinates remain in the native document.
        QScopedValueRollback<bool> guard(m_constrainingViewport, true);
        setZoom(scale);
        const auto actualScale = zoom();
        const auto offset = (editingOrigin() - QPointF(canvasOriginX(), canvasOriginY())) * actualScale;
        setPanX((width() - extent.width * actualScale) / 2 - offset.x());
        setPanY((height() - extent.height * actualScale) / 2 - offset.y());
        return;
    }
    const auto &boards = document()->artboards;
    const int index = activeArtboardIndex(), neighbor = index + 1 < int(boards.size()) ? index + 1 : index - 1;
    const auto &a = boards[index].region, &b = boards[neighbor].region;
    const qreal span = std::max(a.origin.x + a.extent.width, b.origin.x + b.extent.width) - std::min(a.origin.x, b.origin.x);
    // Keep a neighboring board visible at Fit; the remaining row can overflow.
    setZoom(std::min((height() - std::min(qreal{64}, height() / 4)) / canvasHeight(), width() / std::max(qreal{1}, span)));
    setPanY((height() - canvasHeight() * zoom()) / 2);
    selectArtboard(activeArtboardIndex());
}
void EditorCanvas::scrollHorizontally(qreal pixels) { if (!liveStrokeActive()) setPanX(panX() - pixels); }
void EditorCanvas::wheelEvent(QWheelEvent *event) {
    if (documentReady() && !document()->artboards.empty() && !(event->modifiers() & Qt::ControlModifier)) {
        const auto pixel = event->pixelDelta(), angle = event->angleDelta();
        const qreal delta = pixel.x() ? pixel.x() : (event->modifiers() & Qt::ShiftModifier) ? (pixel.y() ? pixel.y() : angle.y() / 2.0) : angle.x() / 2.0;
        if (delta != 0) { scrollHorizontally(-delta); event->accept(); return; }
    }
    CanvasItem::wheelEvent(event);
}
void EditorCanvas::resizeEditingCanvas(Document &value, CanvasExtent extent) {
    if (activeArtboard()) {
        findArtboard(value, m_activeArtboard)->region.extent = extent;
        // Keep the automatically imported row ordered and separated after resizing.
        if (value.artboards.size() > 1) for (std::size_t i = activeArtboardIndex() + 1; i < value.artboards.size(); ++i) {
            auto &board = value.artboards[i]; const auto &previous = value.artboards[i - 1];
            if (board.region.origin.y == previous.region.origin.y)
                board.region.origin.x = previous.region.origin.x + previous.region.extent.width + 64;
        }
        value.extent = value.artboards.front().region.extent;
    } else value.extent = extent;
}
