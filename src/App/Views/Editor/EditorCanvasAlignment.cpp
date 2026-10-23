#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <QColor>
#include <algorithm>
#include <array>
#include <cmath>
#include <iiSharedCanvas/Document/CanvasSampling.h>

using namespace iiSharedCanvas;
namespace A = dreamscapes::alignment;
namespace {
A::Point point(QPointF p) { return {p.x(), p.y()}; }
QPointF qt(A::Point p) { return {p.x, p.y}; }
A::Rect rect(QRectF r) { return {r.x(), r.y(), r.width(), r.height()}; }
QTransform matrix(const AffineTransform &t) {
    return {t.m11, t.m12, t.m21, t.m22, t.translationX, t.translationY};
}
} // namespace
QRectF EditorCanvas::viewReference(const QString &family) const {
    const auto options = family == "grid" ? m_viewOptions->grid() : m_viewOptions->guides();
    const auto reference = options.value("reference").toString();
    if (reference == "Selection") {
        if (!m_selectionOutline.isEmpty())
            return m_selectionOutline.boundingRect().translated(editingOrigin());
        QRectF bounds;
        for (const auto &id : m_selectedElements)
            bounds |= elementWorldTransform(id).mapRect(elementLocalBounds(id));
        return bounds;
    }
    if (reference == "Artboard")
        return QRectF(editingOrigin(), QSizeF(editingWidth(), editingHeight()));
    return {qreal(canvasOriginX()), qreal(canvasOriginY()), qreal(canvasWidth()), qreal(canvasHeight())};
}
void EditorCanvas::resetAlignmentGesture() {
    m_alignmentLayersValid = false;
    m_previousAlignment = {};
    if (!m_alignmentFeedback.isEmpty()) {
        m_alignmentFeedback.clear();
        emit alignmentFeedbackChanged();
    }
}
void EditorCanvas::cacheAlignmentLayers() {
    if (m_alignmentLayersValid)
        return;
    m_alignmentLayers.clear();
    m_alignmentLayersValid = true;
    if (!documentReady())
        return;
    const auto options = m_viewOptions->snap();
    const bool visibleOnly = options.value("visible-only").toBool(),
               ignoreLocked = options.value("ignore-locked").toBool();
    if (!options.value("layer-edges").toBool() && !options.value("layer-centers").toBool() &&
        !options.value("anchors").toBool() && !options.value("baselines").toBool() &&
        !options.value("size").toBool() && !options.value("spacing").toBool())
        return;
    for (const auto &layer : document()->layers) {
        if (!belongsToActiveArtboard(layer))
            continue;
        const auto &p = layerProperties(layer);
        const auto id = QString::fromStdString(p.id);
        if (id == selectedLayerId() || m_selectedElements.contains(id))
            continue;
        const auto sampled = sampleLayerAt(*document(), layer, frame());
        if (visibleOnly && (!sampled.visible || sampled.opacity <= 0))
            continue;
        if (ignoreLocked && (m_positionLocks.contains(id) || m_pixelLocks.contains(id)))
            continue;
        const auto *asset = resolveAssetAt(*document(), layer, frame());
        if (!asset)
            continue;
        const auto transform = matrix(sampled.transform);
        QRectF bounds = elementLocalBounds(id);
        if (bounds.isEmpty()) bounds = editorTools::assetBounds(*asset, document()->infiniteCanvas.chunkSize);
        AlignmentLayer candidate{id, transform.mapRect(bounds), {}, {}};
        if (candidate.bounds.isEmpty())
            continue;
        if (options.value("anchors").toBool())
            if (const auto *vector = std::get_if<VectorAsset>(asset))
                for (const auto &path : vector->paths) {
                    for (const auto &command : path.commands) {
                        if (candidate.anchors.size() >= 1024)
                            break;
                        std::visit(
                            [&](const auto &c) {
                                using T = std::decay_t<decltype(c)>;
                                if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, LineTo>)
                                    candidate.anchors.push_back(
                                        point(transform.map(QPointF(c.point.x, c.point.y))));
                                else if constexpr (std::is_same_v<T, QuadraticTo> ||
                                                   std::is_same_v<T, CubicTo>)
                                    candidate.anchors.push_back(
                                        point(transform.map(QPointF(c.end.x, c.end.y))));
                            },
                            command);
                    }
                }
        if (options.value("baselines").toBool())
            for (const auto &line : m_textBaselines.value(id)) {
                const auto mapped = transform.map(line);
                candidate.baselines.push_back({point(mapped.p1()), point(mapped.p2()), true});
            }
        m_alignmentLayers.push_back(std::move(candidate));
    }
}
QPointF EditorCanvas::snapViewPosition(const QPointF &position) {
    return elementItemPosition(snapAlignment(elementWorldPosition(position)));
}
QPointF EditorCanvas::snapAlignment(const QPointF &world, const QRectF &moving) {
    const auto options = m_viewOptions->snap();
    if (!options.value("enabled").toBool() || !documentReady()) {
        resetAlignmentGesture();
        return world;
    }
    cacheAlignmentLayers();
    std::vector<A::Target> targets;
    std::vector<A::Point> anchors;
    if (moving.isEmpty())
        anchors.push_back(point(world));
    else
        for (const auto p :
             {moving.topLeft(), moving.topRight(), moving.bottomLeft(), moving.bottomRight(), moving.center(),
              QPointF(moving.center().x(), moving.top()), QPointF(moving.left(), moving.center().y())})
            anchors.push_back(point(p));
    const auto extent = QRectF(canvasOriginX(), canvasOriginY(), canvasWidth(), canvasHeight());
    const auto vertical = [&](double x, A::TargetKind kind = A::TargetKind::Geometry) {
        targets.push_back(
            {{{x, extent.top() - extent.height() * 2}, {x, extent.bottom() + extent.height() * 2}, true},
             kind});
    };
    const auto horizontal = [&](double y, A::TargetKind kind = A::TargetKind::Geometry) {
        targets.push_back(
            {{{extent.left() - extent.width() * 2, y}, {extent.right() + extent.width() * 2, y}, true},
             kind});
    };
    const auto boundsTargets = [&](QRectF r, bool edges, bool centers) {
        if (edges) {
            vertical(r.left());
            vertical(r.right());
            horizontal(r.top());
            horizontal(r.bottom());
        }
        if (centers) {
            vertical(r.center().x());
            horizontal(r.center().y());
        }
    };
    boundsTargets(extent, options.value("canvas-edges").toBool(), options.value("canvas-centers").toBool());
    for (const auto &entry : m_alignmentLayers) {
        boundsTargets(entry.bounds, options.value("layer-edges").toBool(),
                      options.value("layer-centers").toBool());
        for (const auto p : entry.anchors) {
            vertical(p.x);
            horizontal(p.y);
        }
        for (const auto &l : entry.baselines)
            targets.push_back({l, A::TargetKind::Geometry});
    }
    const auto guides = m_viewOptions->guides();
    if (options.value("guides").toBool() && guides.value("snap").toBool() &&
        (!options.value("visible-only").toBool() || (guides.value("enabled").toBool() && guides.value("opacity").toDouble() > 0 && QColor(guides.value("color").toString()).alpha() > 0)))
        for (const auto &g : m_viewOptions->placedGuides())
            targets.push_back({g.line, A::TargetKind::Guide});
    const auto gridVisibility = m_viewOptions->grid();
    const bool gridVisible = gridVisibility.value("enabled").toBool() && gridVisibility.value("opacity").toDouble() > 0 && QColor(gridVisibility.value("color").toString()).alpha() > 0 &&
        (gridVisibility.value("type") != "Pixel" || zoom() * 100 >= gridVisibility.value("reveal-at").toDouble());
    if (options.value("grid").toBool() && (!options.value("visible-only").toBool() || gridVisible)) {
        const auto reference = viewReference("grid");
        const auto geometry =
            m_viewOptions->gridGeometry(rect(reference), m_specification.value("ppi", 300).toDouble());
        if (reference.width() > 0 && reference.height() > 0) {
            const auto grid = m_viewOptions->grid();
            if (!grid.value("intersections").toBool()) {
                const auto radius = options.value("tolerance").toDouble() / std::max(.001, zoom());
                for (const auto p : anchors)
                    for (const auto l : A::gridSnapLines(p, geometry, radius))
                        targets.push_back({l, A::TargetKind::Grid,
                                           l.a.x == l.b.x && l.a.y == l.b.y});
            } else
                for (const auto p : anchors)
                    if (const auto next = A::gridPoint(p, geometry)) {
                        targets.push_back({{*next, *next, true}, A::TargetKind::Grid, true});
                    }
        }
    }
    for (const auto p : anchors) {
        if (options.value("pixel-edges").toBool()) {
            vertical(std::round(p.x), A::TargetKind::Pixel);
            horizontal(std::round(p.y), A::TargetKind::Pixel);
        }
        if (options.value("pixel-centers").toBool()) {
            vertical(std::round(p.x - .5) + .5, A::TargetKind::Pixel);
            horizontal(std::round(p.y - .5) + .5, A::TargetKind::Pixel);
        }
    }
    if (!moving.isEmpty() && options.value("spacing").toBool()) {
        // Neighbour gaps supply before / between / after placements. Limit to
        // the nearest 64 layers so a drag never performs an all-layer square scan.
        std::vector<const AlignmentLayer *> neighbours;
        neighbours.reserve(m_alignmentLayers.size());
        for (const auto &entry : m_alignmentLayers)
            neighbours.push_back(&entry);
        const auto count = std::min<std::size_t>(64, neighbours.size());
        std::partial_sort(neighbours.begin(), neighbours.begin() + std::ptrdiff_t(count), neighbours.end(),
                          [&](const auto *a, const auto *b) {
                              return QLineF(a->bounds.center(), moving.center()).length() <
                                     QLineF(b->bounds.center(), moving.center()).length();
                          });
        neighbours.resize(count);
        for (const bool x : {true, false}) {
            std::sort(neighbours.begin(), neighbours.end(), [&](const auto *a, const auto *b) {
                return x ? a->bounds.left() < b->bounds.left() : a->bounds.top() < b->bounds.top();
            });
            for (std::size_t i = 1; i < neighbours.size(); ++i) {
                const auto a = neighbours[i - 1]->bounds, b = neighbours[i]->bounds;
                const auto gap = x ? b.left() - a.right() : b.top() - a.bottom();
                if (gap < 0)
                    continue;
                const auto size = x ? moving.width() : moving.height();
                const std::array<double, 3> candidates{
                    x ? a.left() - gap - size : a.top() - gap - size, x ? b.right() + gap : b.bottom() + gap,
                    x ? (a.right() + b.left() - size) / 2 : (a.bottom() + b.top() - size) / 2};
                for (const auto v : candidates)
                    if (x)
                        vertical(v, A::TargetKind::Spacing);
                    else
                        horizontal(v, A::TargetKind::Spacing);
            }
        }
    }
    A::SnapSettings settings;
    settings.tolerance = options.value("tolerance").toDouble();
    settings.release = options.value("release").toDouble();
    settings.priority =
        QStringList{"Nearest", "Guides first", "Grid first"}.indexOf(options.value("priority").toString());
    settings.axes = QStringList{"Both", "Horizontal", "Vertical"}.indexOf(options.value("axes").toString());
    m_previousAlignment = A::snap(anchors, targets, settings, zoom(), &m_previousAlignment);
    QVariantList feedback;
    if (options.value("smart-guides").toBool() && guides.value("smart").toBool())
        for (const auto &target :
             {m_previousAlignment.x, m_previousAlignment.y, m_previousAlignment.diagonal})
            if (target) {
                const auto a = elementItemPosition(qt(target->line.a)),
                           b = elementItemPosition(qt(target->line.b));
                if (target->intersection) {
                    feedback.append(QVariantMap{
                        {"a", a - QPointF(4, 0)}, {"b", a + QPointF(4, 0)}, {"kind", int(target->kind)}});
                    feedback.append(QVariantMap{
                        {"a", a - QPointF(0, 4)}, {"b", a + QPointF(0, 4)}, {"kind", int(target->kind)}});
                } else
                    feedback.append(QVariantMap{{"a", a}, {"b", b}, {"kind", int(target->kind)}});
            }
    if (options.value("distances").toBool() && m_previousAlignment.snapped)
        feedback.append(QVariantMap{{"label", QString::number(std::hypot(m_previousAlignment.correction.x,
                                                                         m_previousAlignment.correction.y),
                                                              'f', 2) +
                                                  " px"},
                                    {"position", elementItemPosition(world) + QPointF(12, 12)}});
    if (feedback != m_alignmentFeedback) {
        m_alignmentFeedback = feedback;
        emit alignmentFeedbackChanged();
    }
    return world + qt(m_previousAlignment.correction);
}
QSizeF EditorCanvas::snapAlignmentSize(const QSizeF &size) {
    const auto s = m_viewOptions->snap();
    if (!s.value("enabled").toBool() || !s.value("size").toBool())
        return size;
    cacheAlignmentLayers();
    auto result = size;
    double dx = s.value("tolerance").toDouble() / zoom(), dy = dx;
    for (const auto &entry : m_alignmentLayers) {
        const auto x = std::abs(entry.bounds.width() - size.width()),
                   y = std::abs(entry.bounds.height() - size.height());
        if (s.value("axes") != "Vertical" && x < dx) {
            dx = x;
            result.setWidth(entry.bounds.width());
        }
        if (s.value("axes") != "Horizontal" && y < dy) {
            dy = y;
            result.setHeight(entry.bounds.height());
        }
    }
    return result;
}
bool EditorCanvas::createViewGuides() {
    if (!documentReady())
        return false;
    std::vector<EditorViewOptions::PlacedGuide> result;
    const auto settings = m_viewOptions->guides();
    const auto add = [&](QRectF r, const QString &id) {
        const auto reference = rect(r);
        for (const auto l : A::guideLines(
                 reference,
                 m_viewOptions->guideGeometry(reference, m_specification.value("ppi", 300).toDouble())))
            result.push_back({l, id});
    };
    if (settings.value("all-artboards").toBool() && !document()->artboards.empty())
        for (const auto &board : document()->artboards)
            add({qreal(board.region.origin.x), qreal(board.region.origin.y), qreal(board.region.extent.width),
                 qreal(board.region.extent.height)},
                QString::fromStdString(board.id));
    else
        add(viewReference("guides"), QString::fromStdString(m_activeArtboard));
    if (result.empty())
        return false;
    m_viewOptions->addGuides(std::move(result));
    return true;
}
QVariantList EditorCanvas::viewRulerTicks(bool horizontal, double extent) const {
    QVariantList ticks;
    if (!documentReady() || zoom() <= 0 || extent <= 0)
        return ticks;
    double step = 1;
    while (step * zoom() < 64 && step < 1e9)
        step *= 2;
    const auto pan = horizontal ? panX() : panY();
    const auto origin = horizontal ? canvasOriginX() : canvasOriginY();
    const auto start = std::floor((-pan / zoom() + origin) / step) * step;
    for (int i = 0; i < 512; ++i) {
        const auto value = start + i * step, position = pan + (value - origin) * zoom();
        if (position > extent)
            break;
        if (position >= 0)
            ticks.append(QVariantMap{{"position", position},
                                     {"spacing", step * zoom()},
                                     {"label", QString::number(value, 'g', 8)}});
    }
    return ticks;
}
