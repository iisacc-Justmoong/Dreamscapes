#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <iiSharedCanvas/Document/CanvasSampling.h>
#include <QHoverEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QPainterPathStroker>
#include <QScopedValueRollback>
#include <array>
#include <cmath>
#include <numbers>

using namespace iiSharedCanvas;
namespace {
QTransform matrix(const AffineTransform &t) {
    return {t.m11, t.m12, t.m21, t.m22, t.translationX, t.translationY};
}
AffineTransform affine(const QTransform &t) {
    return {t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()};
}
QPainterPath footprint(const VectorAsset &asset) {
    QPainterPath result;
    for (const auto &source : asset.paths) {
        const auto path = editorTools::vectorPainterPath(source);
        if (source.fill && qAlpha(source.fill->argb)) result = result.united(path);
        if (source.stroke && qAlpha(source.stroke->paint.argb) && source.stroke->width > 0) {
            QPainterPathStroker stroke; stroke.setWidth(source.stroke->width);
            result = result.united(stroke.createStroke(path));
        }
    }
    return result;
}
constexpr std::array<const char *, 8> handleIds{"nw", "n", "ne", "e", "se", "s", "sw", "w"};
std::array<QPointF, 8> handlePoints(QRectF r) {
    return {r.topLeft(), {r.center().x(), r.top()}, r.topRight(), {r.right(), r.center().y()},
        r.bottomRight(), {r.center().x(), r.bottom()}, r.bottomLeft(), {r.left(), r.center().y()}};
}
bool usable(QRectF r) {
    return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width())
        && std::isfinite(r.height()) && r.width() > 0 && r.height() > 0;
}
QSizeF dimensions(QRectF bounds, const QTransform &transform) {
    return {bounds.width() * std::hypot(transform.m11(), transform.m12()),
        bounds.height() * std::hypot(transform.m21(), transform.m22())};
}
QTransform around(QPointF anchor, double sx, double sy, double degrees = 0) {
    QTransform result;
    result.translate(anchor.x(), anchor.y()); result.rotate(degrees); result.scale(sx, sy);
    result.translate(-anchor.x(), -anchor.y());
    return result;
}
}

QRectF EditorCanvas::elementLocalBounds(const QString &id) const {
    if (!documentReady()) return {};
    const auto *layer = findLayer(*document(), id.toStdString());
    if (!layer || !belongsToActiveArtboard(*layer)) return {};
    if (m_elementBounds.contains(id)) return m_elementBounds.value(id);
    const auto *asset = resolveAssetAt(*document(), *layer, frame());
    const auto *vector = asset ? std::get_if<VectorAsset>(asset) : nullptr;
    return vector ? footprint(*vector).boundingRect() : QRectF{};
}
QTransform EditorCanvas::elementWorldTransform(const QString &id) const {
    const auto *layer = documentReady() ? findLayer(*document(), id.toStdString()) : nullptr;
    return layer ? matrix(sampleLayerAt(*document(), *layer, frame()).transform) : QTransform{};
}
QPointF EditorCanvas::elementWorldPosition(const QPointF &itemPosition) const {
    return {(itemPosition.x() - panX()) / zoom() + canvasOriginX(),
        (itemPosition.y() - panY()) / zoom() + canvasOriginY()};
}
QPointF EditorCanvas::elementItemPosition(const QPointF &worldPosition) const {
    return {panX() + (worldPosition.x() - canvasOriginX()) * zoom(),
        panY() + (worldPosition.y() - canvasOriginY()) * zoom()};
}
QVariantMap EditorCanvas::elementTransform() const {
    if (m_tool != "elements" || !documentReady()) return {};
    const auto pointsFor = [&](QRectF bounds, const QTransform &transform) {
        QVariantList points;
        for (const auto &point : {bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()})
            points.append(elementItemPosition(transform.map(point)));
        return points;
    };
    if (m_creatingElement) {
        const auto bounds = elementCreationBounds();
        const auto origin = editingOrigin();
        return {{"visible", true}, {"creating", true}, {"points", pointsFor(bounds, QTransform::fromTranslate(origin.x(), origin.y()))}};
    }
    const auto id = selectedLayerId();
    const auto *layer = findLayer(*document(), id.toStdString());
    const auto bounds = elementLocalBounds(id);
    if (!m_selectedElements.contains(id) || !layer || !usable(bounds)
        || !sampleLayerAt(*document(), *layer, frame()).visible) return {};
    const auto transform = elementWorldTransform(id);
    const auto size = dimensions(bounds, transform);
    const bool editable = !liveStrokeActive() && !m_pixelLocks.contains(id) && !m_positionLocks.contains(id)
        && transform.isInvertible() && size.width() >= 1 && size.height() >= 1;
    QVariantList handles, selections;
    const auto localHandles = handlePoints(bounds);
    for (std::size_t i = 0; i < localHandles.size(); ++i)
        handles.append(QVariantMap{{"id", handleIds[i]}, {"point", elementItemPosition(transform.map(localHandles[i]))}});
    const auto top = elementItemPosition(transform.map(localHandles[1]));
    const auto center = elementItemPosition(transform.map(bounds.center()));
    auto outward = top - center;
    const auto length = std::hypot(outward.x(), outward.y());
    if (length > 0) outward *= 26 / length;
    const auto rotate = top + outward;
    for (const auto &other : m_selectedElements) {
        if (other == id) continue;
        const auto *secondary = findLayer(*document(), other.toStdString());
        const auto secondaryBounds = elementLocalBounds(other);
        if (secondary && usable(secondaryBounds) && sampleLayerAt(*document(), *secondary, frame()).visible)
            selections.append(QVariantMap{{"id", other}, {"points", pointsFor(secondaryBounds, elementWorldTransform(other))}});
    }
    const auto corner = transform.map(bounds.topLeft()) - editingOrigin();
    return {{"visible", true}, {"id", id}, {"name", QString::fromStdString(layerProperties(*layer).name)},
        {"editable", editable}, {"dragging", !m_elementTransformHandle.isEmpty()},
        {"width", size.width()}, {"height", size.height()}, {"x", corner.x()}, {"y", corner.y()},
        {"rotation", std::atan2(transform.m12(), transform.m11()) * 180 / std::numbers::pi},
        {"points", pointsFor(bounds, transform)}, {"handles", handles}, {"rotate", rotate},
        {"rotateStem", top}, {"center", center}, {"selections", selections}};
}
QString EditorCanvas::elementHandleAt(const QPointF &position) const {
    const auto state = elementTransform();
    if (!state.value("editable").toBool() || state.value("creating").toBool()) return {};
    const auto near = [&](QPointF point) { const auto delta = position - point; return std::hypot(delta.x(), delta.y()) <= 9; };
    if (near(state.value("rotate").toPointF())) return "rotate";
    // Corners win over edge handles when a very small object makes them overlap.
    const auto handles = state.value("handles").toList();
    for (int parity = 0; parity < 2; ++parity) for (int i = parity; i < handles.size(); i += 2)
        if (near(handles[i].toMap().value("point").toPointF())) return handles[i].toMap().value("id").toString();
    return {};
}
QString EditorCanvas::elementAt(const QPointF &position) const {
    const auto world = elementWorldPosition(position);
    for (const auto &entry : elementCandidates()) {
        const auto candidate = entry.toMap(); const auto id = candidate.value("id").toString();
        const auto *layer = findLayer(*document(), id.toStdString());
        if (!layer || !sampleLayerAt(*document(), *layer, frame()).visible) continue;
        const auto transform = elementWorldTransform(id);
        if (!transform.isInvertible()) continue;
        const auto point = transform.inverted().map(world);
        const auto *asset = resolveAssetAt(*document(), *layer, frame());
        if (const auto *vector = asset ? std::get_if<VectorAsset>(asset) : nullptr) {
            const auto area = footprint(*vector);
            if (area.contains(point)) return id;
            // Thin lines stay selectable at every zoom without selecting empty corners.
            QPainterPathStroker tolerance;
            tolerance.setWidth(6 / std::max(0.001, zoom() * std::max(std::hypot(transform.m11(), transform.m12()), std::hypot(transform.m21(), transform.m22()))));
            if (tolerance.createStroke(area).contains(point)) return id;
        } else if (const auto *raster = asset ? std::get_if<RasterAsset>(asset) : nullptr) {
            if (point.x() >= 0 && point.y() >= 0 && point.x() < raster->pixels.width && point.y() < raster->pixels.height) {
                const auto x = std::size_t(point.x()), y = std::size_t(point.y());
                if (qAlpha(raster->pixels.pixels[y * raster->pixels.width + x])) return id;
            }
        }
    }
    return {};
}
bool EditorCanvas::beginElementTransform(const QString &handle, const QPointF &position, int) {
    if (!std::isfinite(position.x()) || !std::isfinite(position.y())) return false;
    if (m_tool != "elements" || !m_elementTransformHandle.isEmpty() || m_creatingElement) return false;
    const auto state = elementTransform();
    if (!state.value("editable").toBool()) return false;
    if (handle != "move" && handle != "rotate"
        && std::none_of(handleIds.begin(), handleIds.end(), [&](const char *id) { return handle == id; })) return false;
    m_elementTransformId = selectedLayerId(); m_elementTransformHandle = handle;
    resetAlignmentGesture();
    const auto *layer = findLayer(m_canvas, m_elementTransformId.toStdString());
    m_elementTransformBase = matrix(layerProperties(*layer).transform);
    m_elementTransformSample = elementWorldTransform(m_elementTransformId);
    m_elementTransformBounds = elementLocalBounds(m_elementTransformId);
    m_elementTransformStart = elementWorldPosition(position);
    m_elementTransformPosition = position;
    m_elementTransformChanged = false;
    m_historySuspended = true;
    emit elementTransformChanged();
    return true;
}
bool EditorCanvas::updateElementTransform(const QPointF &position, int modifiers) {
    if (m_elementTransformHandle.isEmpty() || !documentReady()) return false;
    auto *layer = findLayer(m_canvas, m_elementTransformId.toStdString());
    if (!layer || selectedLayerId() != m_elementTransformId || m_pixelLocks.contains(m_elementTransformId)
        || m_positionLocks.contains(m_elementTransformId)) { cancelElementTransform(); return false; }
    auto point = elementWorldPosition(position);
    if (!std::isfinite(point.x()) || !std::isfinite(point.y())) return false;
    m_elementTransformPosition = position;
    const bool shift = modifiers & Qt::ShiftModifier, alt = modifiers & Qt::AltModifier;
    const auto bounds = m_elementTransformBounds;
    const auto sample = m_elementTransformSample;
    const auto snapOptions = m_viewOptions->snap();
    if (!m_explicitElementResize && m_elementTransformHandle != "move" && m_elementTransformHandle != "rotate") point = snapAlignment(point);
    QTransform delta;
    if (m_elementTransformHandle == "move") {
        auto offset = point - m_elementTransformStart;
        if (shift) { if (std::abs(offset.x()) >= std::abs(offset.y())) offset.setY(0); else offset.setX(0); }
        const auto original = sample.mapRect(bounds);
        const auto corrected = snapAlignment(original.topLeft() + offset, original.translated(offset)) - (original.topLeft() + offset);
        offset += corrected;
        if (shift) { if (std::abs(point.x()-m_elementTransformStart.x()) >= std::abs(point.y()-m_elementTransformStart.y())) offset.setY(0); else offset.setX(0); }
        delta = QTransform::fromTranslate(offset.x(), offset.y());
    } else if (m_elementTransformHandle == "rotate") {
        const auto center = sample.map(bounds.center());
        const auto start = m_elementTransformStart - center, finish = point - center;
        if (std::hypot(finish.x(), finish.y()) < 0.001) return true;
        auto angle = (std::atan2(finish.y(), finish.x()) - std::atan2(start.y(), start.x())) * 180 / std::numbers::pi;
        if (shift) angle = std::round(angle / 15) * 15;
        else if(snapOptions.value("enabled").toBool() && snapOptions.value("angle").toBool()) angle = dreamscapes::alignment::increment(angle, snapOptions.value("angle-step").toDouble());
        delta = around(center, 1, 1, angle);
    } else {
        const auto handle = m_elementTransformHandle;
        const bool horizontal = handle.contains('w') || handle.contains('e');
        const bool vertical = handle.contains('n') || handle.contains('s');
        const auto handlePoint = handlePoints(bounds)[std::distance(handleIds.begin(), std::find_if(handleIds.begin(), handleIds.end(), [&](const char *id) { return handle == id; }))];
        const auto anchor = alt ? bounds.center() : QPointF(horizontal ? (handle.contains('w') ? bounds.right() : bounds.left()) : bounds.center().x(),
            vertical ? (handle.contains('n') ? bounds.bottom() : bounds.top()) : bounds.center().y());
        const auto localDelta = sample.inverted().map(point) - sample.inverted().map(m_elementTransformStart);
        double sx = horizontal ? (handlePoint.x() + localDelta.x() - anchor.x()) / (handlePoint.x() - anchor.x()) : 1;
        double sy = vertical ? (handlePoint.y() + localDelta.y() - anchor.y()) / (handlePoint.y() - anchor.y()) : 1;
        const bool constrain = !m_explicitElementResize && horizontal && vertical && (shift || editorTools::toggle(m_toolValues, 3) || m_viewOptions->display().value("constrain").toBool());
        if (!m_explicitElementResize && snapOptions.value("enabled").toBool() && snapOptions.value("scale").toBool()) {
            const auto step = snapOptions.value("scale-step").toDouble()/100;
            if(horizontal)sx=1+dreamscapes::alignment::increment(sx-1,step);
            if(vertical)sy=1+dreamscapes::alignment::increment(sy-1,step);
        }
        const auto originalSize = dimensions(bounds, sample);
        const QSizeF requestedSize{std::abs(sx)*originalSize.width(),std::abs(sy)*originalSize.height()};
        const auto matchedSize = m_explicitElementResize ? requestedSize : snapAlignmentSize(requestedSize);
        if(horizontal)sx=std::copysign(matchedSize.width()/originalSize.width(),sx);
        if(vertical)sy=std::copysign(matchedSize.height()/originalSize.height(),sy);
        if (constrain) {
            const auto factor = std::abs(sx - 1) >= std::abs(sy - 1) ? sx : sy;
            sx = sy = factor;
        }
        const auto size = dimensions(bounds, sample);
        const auto clampScale = [](double scale, double extent) { return std::copysign(std::clamp(std::abs(scale), 1 / extent, 65536 / extent), scale); };
        sx = clampScale(sx, size.width()); sy = clampScale(sy, size.height());
        delta = sample.inverted() * around(anchor, sx, sy) * sample;
    }
    auto baseWorld = m_elementTransformBase;
    QPointF origin;
    const auto &properties = layerProperties(*layer);
    if (properties.artboardId) if (const auto *board = findArtboard(*document(), *properties.artboardId))
        origin = {qreal(board->region.origin.x), qreal(board->region.origin.y)};
    baseWorld.setMatrix(baseWorld.m11(), baseWorld.m12(), 0, baseWorld.m21(), baseWorld.m22(), 0,
        baseWorld.dx() + origin.x(), baseWorld.dy() + origin.y(), 1);
    auto result = baseWorld * delta;
    result.setMatrix(result.m11(), result.m12(), 0, result.m21(), result.m22(), 0, result.dx() - origin.x(), result.dy() - origin.y(), 1);
    if (!result.isInvertible()) return false;
    if (matrix(properties.transform) == result) return true;
    layerProperties(*layer).transform = affine(result);
    m_elementTransformChanged = result != m_elementTransformBase;
    restoreSelectedElementSettings();
    // A preview redraws without document authorship, persistence, or Undo entries.
    // The accepted transform is persisted once at release.
    refreshPreview();
    emit elementSelectionChanged(); emit elementTransformChanged();
    return true;
}
bool EditorCanvas::endElementTransform(const QPointF &position, int modifiers) {
    if (m_elementTransformHandle.isEmpty()) return false;
    if (!updateElementTransform(position, modifiers) || m_elementTransformHandle.isEmpty()) { cancelElementTransform(); return false; }
    const auto id = m_elementTransformId;
    auto *layer = findLayer(m_canvas, id.toStdString());
    const auto result = layerProperties(*layer).transform;
    const auto changed = m_elementTransformChanged;
    layerProperties(*layer).transform = affine(m_elementTransformBase);
    m_elementTransformHandle.clear(); m_elementTransformId.clear(); m_elementTransformChanged = false;
    m_historySuspended = false;
    resetAlignmentGesture();
    if (!changed) { restoreSelectedElementSettings(); emit elementSelectionChanged(); emit elementTransformChanged(); return true; }
    const bool accepted = commit([&](Document &draft) {
        auto *target = findLayer(draft, id.toStdString());
        if (!target) return false;
        layerProperties(*target).transform = result; return true;
    });
    restoreSelectedElementSettings();
    if (!accepted) {
        QScopedValueRollback<bool> guard(m_historySuspended, true);
        refreshPreview();
    }
    emit elementSelectionChanged(); emit elementTransformChanged();
    return accepted;
}
void EditorCanvas::cancelElementTransform() {
    resetAlignmentGesture();
    if (m_elementTransformHandle.isEmpty()) return;
    if (documentReady()) if (auto *layer = findLayer(m_canvas, m_elementTransformId.toStdString()))
        layerProperties(*layer).transform = affine(m_elementTransformBase);
    m_elementTransformHandle.clear(); m_elementTransformId.clear(); m_elementTransformChanged = false;
    if (documentReady()) { restoreSelectedElementSettings(); refreshPreview(); }
    m_historySuspended = false;
    emit elementSelectionChanged(); emit elementTransformChanged();
}
bool EditorCanvas::resizeSelectedElement(const QVariant &value) {
    const auto size = value.toList();
    if (size.size() != 2) return fail(tr("Enter the element width and height."));
    const double width = size[0].toDouble(), height = size[1].toDouble();
    if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 || width > 65536 || height > 65536)
        return fail(tr("Element dimensions must be from 1 to 65536 pixels."));
    const auto state = elementTransform();
    if (!state.value("editable").toBool()) return fail(tr("Unlock the selected element before resizing it."));
    const auto handles = state.value("handles").toList();
    const auto start = handles[4].toMap().value("point").toPointF();
    if (!beginElementTransform("se", start)) return false;
    // Numeric dimensions are explicit; the presenter's ratio control already
    // supplies the other dimension when proportions are constrained.
    m_toolValues["field-3"] = false;
    const auto bounds = m_elementTransformBounds;
    const auto sample = m_elementTransformSample;
    const auto target = sample.map(bounds.topLeft() + QPointF(bounds.width() * width / state.value("width").toDouble(), bounds.height() * height / state.value("height").toDouble()));
    const QScopedValueRollback<bool> explicitResize(m_explicitElementResize, true);
    const bool accepted = endElementTransform(elementItemPosition(target));
    restoreSelectedElementSettings();
    return accepted;
}
void EditorCanvas::updateElementCursor(const QPointF &point) {
    if (m_tool != "elements") { unsetCursor(); return; }
    const auto handle = elementHandleAt(point);
    if (handle == "rotate") { setCursor(Qt::CrossCursor); return; }
    if (!handle.isEmpty()) {
        double angle = elementTransform().value("rotation").toDouble();
        if (handle == "n" || handle == "s") angle += 90;
        else if (handle == "nw" || handle == "se") angle += 45;
        else if (handle == "ne" || handle == "sw") angle -= 45;
        const int direction = (int(std::round(angle / 45)) % 4 + 4) % 4;
        constexpr Qt::CursorShape cursors[]{Qt::SizeHorCursor, Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor};
        setCursor(cursors[direction]); return;
    }
    setCursor(elementAt(point).isEmpty() ? Qt::CrossCursor : Qt::SizeAllCursor);
}
void EditorCanvas::hoverMoveEvent(QHoverEvent *event) {
    updateElementCursor(event->position()); updatePointerFeedback(event->position()); CanvasItem::hoverMoveEvent(event);
}
void EditorCanvas::hoverEnterEvent(QHoverEvent *event) {
    updateElementCursor(event->position()); updatePointerFeedback(event->position()); CanvasItem::hoverEnterEvent(event);
}
void EditorCanvas::hoverLeaveEvent(QHoverEvent *event) {
    updatePointerFeedback(event->position(), false); if (!m_navigatingCanvas) unsetCursor(); CanvasItem::hoverLeaveEvent(event);
}
bool EditorCanvas::elementKeyEvent(QKeyEvent *event) {
    if (m_tool != "elements") return false;
    if (event->key() == Qt::Key_Escape) {
        if (!m_elementTransformHandle.isEmpty()) cancelElementTransform();
        else if (m_creatingElement) { m_creatingElement = false; emit elementTransformChanged(); }
        else clearElementSelection();
        return true;
    }
    if (!m_elementTransformHandle.isEmpty() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter))
        return endElementTransform(m_elementTransformPosition);
    if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier)) return false;
    QPointF delta;
    const int amount = event->modifiers() & Qt::ShiftModifier ? 10 : 1;
    if (event->key() == Qt::Key_Left) delta.setX(-amount);
    else if (event->key() == Qt::Key_Right) delta.setX(amount);
    else if (event->key() == Qt::Key_Up) delta.setY(-amount);
    else if (event->key() == Qt::Key_Down) delta.setY(amount);
    else return false;
    const auto state = elementTransform();
    if (!state.value("editable").toBool()) return false;
    const auto start = state.value("center").toPointF();
    return beginElementTransform("move", start) && endElementTransform(start + delta * zoom());
}
