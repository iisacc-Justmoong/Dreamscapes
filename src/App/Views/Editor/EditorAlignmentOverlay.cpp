#include "EditorAlignmentOverlay.h"
#include "EditorToolUtils.h"
#include <iiSharedCanvas/Document/CanvasSampling.h>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGRendererInterface>
#include <algorithm>
#include <cmath>

namespace A = dreamscapes::alignment;
namespace {
A::Rect rect(QRectF r) {
    return {r.x(), r.y(), r.width(), r.height()};
}
QSGGeometryNode *batch(const std::vector<QPointF> &points, QColor color, bool dots = false) {
    if (points.empty())
        return nullptr;
    auto *n = new QSGGeometryNode;
    auto *g = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), int(points.size()));
    g->setDrawingMode(dots ? QSGGeometry::DrawPoints : QSGGeometry::DrawLines);
    g->setLineWidth(dots ? 2 : 1);
    auto *v = g->vertexDataAsPoint2D();
    for (std::size_t i = 0; i < points.size(); ++i)
        v[i].set(float(points[i].x()), float(points[i].y()));
    auto *m = new QSGFlatColorMaterial;
    m->setColor(color);
    n->setGeometry(g);
    n->setMaterial(m);
    n->setFlag(QSGNode::OwnsGeometry);
    n->setFlag(QSGNode::OwnsMaterial);
    return n;
}
} // namespace
class AlignmentSoftwareSurface : public QQuickPaintedItem {
  public:
    explicit AlignmentSoftwareSurface(EditorAlignmentOverlay *owner)
        : QQuickPaintedItem(owner), m_owner(owner) {
        setAcceptedMouseButtons(Qt::NoButton);
        setAntialiasing(true);
    }
    void paint(QPainter *painter) override {
        if (m_owner->family() == "channel") {
            auto *canvas = m_owner->canvas();
            if (!canvas || !canvas->documentReady()) return;
            const auto display = canvas->viewOptions()->display();
            const auto channel = display.value("channel").toString();
            if (channel == "RGB" || display.value("outlines").toBool()) return;
            // Reuse the SDK's resident viewport tiles. Software-only channel
            // inspection does not re-render the complete IISC document.
            QImage image(QSize(int(std::ceil(width())), int(std::ceil(height()))), QImage::Format_ARGB32);
            if (image.isNull()) return;
            image.fill(Qt::transparent);
            { QPainter source(&image); canvas->paint(&source); }
            const auto topLeft = QPointF(canvas->panX() + (canvas->editingOrigin().x() - canvas->canvasOriginX()) * canvas->zoom(),
                                         canvas->panY() + (canvas->editingOrigin().y() - canvas->canvasOriginY()) * canvas->zoom());
            const auto extent = QRectF(topLeft, QSizeF(canvas->editingWidth() * canvas->zoom(),
                                                       canvas->editingHeight() * canvas->zoom()));
            const auto region = extent.toAlignedRect().intersected(image.rect());
            for (int y = region.top(); y <= region.bottom(); ++y) {
                auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int x = region.left(); x <= region.right(); ++x) {
                    const auto p = pixels[x];
                    const auto component = channel == "Red" ? qRed(p) : channel == "Green" ? qGreen(p)
                                           : channel == "Blue" ? qBlue(p) : qAlpha(p);
                    pixels[x] = qRgba(component, component, component, channel == "Alpha" ? 255 : qAlpha(p));
                }
            }
            painter->setClipRect(extent);
            painter->drawImage(QPointF(), image);
            return;
        }
        // The software scene graph does not render custom QSG geometry. Reuse
        // the same clipped, bounded line batches without a second geometry path.
        auto *root = m_owner->renderGeometry();
        for (auto *child = root->firstChild(); child; child = child->nextSibling()) {
            const auto *node = static_cast<QSGGeometryNode *>(child);
            const auto *g = node->geometry();
            const auto *v = g->vertexDataAsPoint2D();
            const auto *material = static_cast<QSGFlatColorMaterial *>(node->material());
            painter->setPen(QPen(material->color(), g->lineWidth()));
            if (g->drawingMode() == QSGGeometry::DrawPoints)
                for (int i = 0; i < g->vertexCount(); ++i)
                    painter->drawPoint(QPointF(v[i].x, v[i].y));
            else
                for (int i = 1; i < g->vertexCount(); i += 2)
                    painter->drawLine(QPointF(v[i - 1].x, v[i - 1].y), QPointF(v[i].x, v[i].y));
        }
        delete root;
    }

  private:
    EditorAlignmentOverlay *m_owner;
};
EditorAlignmentOverlay::EditorAlignmentOverlay(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
    setClip(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    connect(this, &QQuickItem::widthChanged, this, &EditorAlignmentOverlay::refresh);
    connect(this, &QQuickItem::heightChanged, this, &EditorAlignmentOverlay::refresh);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *window) {
        if (m_family != "guide-input" && !m_software &&
            (qEnvironmentVariable("QT_QUICK_BACKEND") == "software" ||
             (window && window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software))) {
            m_software = new AlignmentSoftwareSurface(this);
        }
        refresh();
    });
}
void EditorAlignmentOverlay::refresh() {
    if (m_software) {
        m_software->setSize(size());
        static_cast<QQuickPaintedItem *>(m_software)->update();
    }
    update();
    emit labelsChanged();
}
void EditorAlignmentOverlay::setCanvas(EditorCanvas *canvas) {
    if (m_canvas == canvas)
        return;
    if (m_canvas) {
        disconnect(m_canvas, nullptr, this, nullptr);
        disconnect(m_canvas->viewOptions(), nullptr, this, nullptr);
    }
    m_canvas = canvas;
    if (canvas) {
        connect(canvas->viewOptions(), &EditorViewOptions::changed, this, &EditorAlignmentOverlay::refresh);
        connect(canvas->viewOptions(), &EditorViewOptions::guidesChanged, this,
                &EditorAlignmentOverlay::refresh);
        connect(canvas, &EditorCanvas::alignmentFeedbackChanged, this, &EditorAlignmentOverlay::refresh);
        connect(canvas, &EditorCanvas::viewportChanged, this, &EditorAlignmentOverlay::refresh);
        connect(canvas, &EditorCanvas::documentInfoChanged, this, &EditorAlignmentOverlay::refresh);
        connect(canvas, &EditorCanvas::elementSelectionChanged, this, &EditorAlignmentOverlay::refresh);
        connect(canvas, &EditorCanvas::activeArtboardChanged, this, &EditorAlignmentOverlay::refresh);
        connect(canvas, &EditorCanvas::renderCompleted, this, &EditorAlignmentOverlay::refresh);
        connect(canvas, &EditorCanvas::smoothRenderingChanged, this, &EditorAlignmentOverlay::refresh);
        connect(canvas, &QObject::destroyed, this, [this] {
            m_canvas = nullptr;
            refresh();
            emit canvasChanged();
        });
    }
    refresh();
    emit canvasChanged();
}
void EditorAlignmentOverlay::setFamily(const QString &family) {
    if (m_family == family)
        return;
    m_family = family;
    setFlag(ItemHasContents, family != "guide-input");
    if (family == "guide-input" && m_software) { delete m_software; m_software = nullptr; }
    else if (family != "guide-input" && !m_software && window() &&
             (qEnvironmentVariable("QT_QUICK_BACKEND") == "software" ||
              window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software))
        m_software = new AlignmentSoftwareSurface(this);
    refresh();
    emit familyChanged();
}
void EditorAlignmentOverlay::setPreview(bool preview) {
    if (m_preview == preview)
        return;
    m_preview = preview;
    setAcceptedMouseButtons(preview ? Qt::NoButton : Qt::LeftButton);
    refresh();
    emit previewChanged();
}
QSGNode *EditorAlignmentOverlay::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    delete old;
    return m_software ? nullptr : renderGeometry();
}
QSGNode *EditorAlignmentOverlay::renderGeometry() {
    auto *root = new QSGNode;
    if (!m_canvas || !m_canvas->documentReady() || width() <= 0 || height() <= 0)
        return root;
    const auto options = m_canvas->viewOptions();
    const auto values = m_family == "grid"     ? options->grid()
                        : m_family == "guides" ? options->guides()
                                               : options->snap();
    if (!m_preview && m_family != "snap" && m_family != "outlines" && !values.value("enabled").toBool())
        return root;
    const auto map = [&](A::Point p) {
        return m_preview ? QPointF(p.x * width() / 1024, p.y * height() / (m_family == "guides" ? 768 : 256))
                         : m_canvas->elementItemPosition({p.x, p.y});
    };
    const auto view = m_preview ? A::Rect{0, 0, 1024, m_family == "guides" ? 768. : 256.}
                                : rect(QRectF(m_canvas->elementWorldPosition({0, 0}),
                                              m_canvas->elementWorldPosition({width(), height()})));
    auto reference = m_preview ? view : rect(m_canvas->viewReference(m_family));
    std::vector<A::Line> lines;
    std::vector<QPointF> minor, major;
    QColor color(values.value("color", "#548AF7").toString());
    color.setAlphaF(color.alphaF() * values.value("opacity", 70).toDouble() / 100);
    bool dots = false, dashed = false;
    if (m_family == "grid") {
        auto geometry =
            options->gridGeometry(reference, m_canvas->specification().value("ppi", 300).toDouble());
        if (m_preview &&
            (geometry.type == A::GridType::Rectangular || geometry.type == A::GridType::Isometric ||
             geometry.type == A::GridType::Pixel || geometry.type == A::GridType::Baseline)) {
            geometry.spacingX *= 4;
            geometry.spacingY *= 4;
            geometry.origin.x += 32;
            geometry.origin.y += 16;
        }
        if (!m_preview && geometry.type == A::GridType::Pixel &&
            m_canvas->zoom() * 100 < values.value("reveal-at").toDouble())
            return root;
        if (reference.width <= 0 || reference.height <= 0)
            return root;
        dots = values.value("style") == "Dots";
        if (dots) {
            for (const auto &dot : A::gridDots(view, geometry, m_preview ? 1 : m_canvas->zoom())) {
                const auto p = map(dot.position);
                auto &points = dot.major && values.value("major").toBool() ? major : minor;
                points.push_back(p);
            }
        } else
            lines = A::gridLines(view, geometry, m_preview ? 1 : m_canvas->zoom());
        if (values.value("axes").toBool()) {
            const std::vector<QPointF> axes{map({geometry.origin.x, view.y}), map({geometry.origin.x, view.y + view.height}),
                                          map({view.x, geometry.origin.y}), map({view.x + view.width, geometry.origin.y})};
            if (auto *node = batch(axes, color)) root->appendChildNode(node);
        }
    } else if (m_family == "guides") {
        if (m_preview)
            lines = A::guideLines(
                reference,
                options->guideGeometry(reference, m_canvas->specification().value("ppi", 300).toDouble()));
        else
            for (const auto &g : options->placedGuides())
                lines.push_back(g.line);
        dashed = values.value("style") == "Dashed";
    } else if (m_family == "snap") {
        if (m_preview) {
            const auto box = [&](double x) {
                lines.push_back({{x, 70}, {x + 180, 70}, false});
                lines.push_back({{x + 180, 70}, {x + 180, 186}, false});
                lines.push_back({{x + 180, 186}, {x, 186}, false});
                lines.push_back({{x, 186}, {x, 70}, false});
            };
            box(210);
            box(510);
            if (values.value("layer-centers").toBool())
                lines.push_back({{90, 128}, {934, 128}, true});
            if (values.value("layer-edges").toBool())
                lines.push_back({{210, 32}, {210, 224}, true});
        } else
            for (const auto &v : m_canvas->alignmentFeedback()) {
                const auto item = v.toMap();
                if (item.contains("a")) {
                    minor.push_back(item.value("a").toPointF());
                    minor.push_back(item.value("b").toPointF());
                }
            }
        color = QColor("#548AF7");
    } else if (m_family == "outlines") {
        if (!options->display().value("outlines").toBool() &&
            options->display().value("composite") != "Outlines")
            return root;
        for (const auto &layer : m_canvas->document()->layers) {
            if (!m_canvas->belongsToActiveArtboard(layer)) continue;
            const auto sampled = iiSharedCanvas::sampleLayerAt(*m_canvas->document(), layer, m_canvas->frame());
            if (!sampled.visible || sampled.opacity <= 0) continue;
            const auto *asset = iiSharedCanvas::resolveAssetAt(*m_canvas->document(), layer, m_canvas->frame());
            if (!asset) continue;
            const auto id = QString::fromStdString(iiSharedCanvas::layerProperties(layer).id);
            QPainterPath outline;
            if (const auto *vector = std::get_if<iiSharedCanvas::VectorAsset>(asset)) {
                if (m_canvas->elementLocalBounds(id).isEmpty()) continue;
                for (const auto &path : vector->paths) outline.addPath(editorTools::vectorPainterPath(path));
            } else outline.addRect(editorTools::assetBounds(*asset, m_canvas->document()->infiniteCanvas.chunkSize));
            const auto transform = m_canvas->elementWorldTransform(id);
            QTransform screen; screen.scale(m_canvas->zoom(), m_canvas->zoom());
            const auto polygons = outline.toSubpathPolygons(transform * screen);
            for (const auto &polygon : polygons) for (int i = 1; i < polygon.size() && lines.size() < 30000; ++i) {
                const auto a = polygon[i - 1] / m_canvas->zoom(), b = polygon[i] / m_canvas->zoom();
                lines.push_back({{a.x(), a.y()}, {b.x(), b.y()}, true});
            }
        }
        color = QColor("#B9BDC5");
    }
    for (const auto l : lines) {
        if (minor.size() + major.size() > 60000)
            break;
        auto a = map(l.a), b = map(l.b);
        const auto clipped = A::clipped({{a.x(), a.y()}, {b.x(), b.y()}, l.major}, {0, 0, width(), height()});
        if (!clipped)
            continue;
        a = {clipped->a.x, clipped->a.y};
        b = {clipped->b.x, clipped->b.y};
        auto &points = l.major && values.value("major", true).toBool() ? major : minor;
        if (dashed) {
            const auto length = QLineF(a, b).length();
            const auto count = std::min(1024, int(length / 10) + 1);
            for (int i = 0; i < count && points.size() < 30000; ++i) {
                const auto t = std::min(1., i * 10. / std::max(1., length));
                const auto p = a + (b - a) * t;
                points.push_back(p);
                points.push_back(a + (b - a) * std::min(1., t + 5 / std::max(1., length)));
            }
        } else {
            points.push_back(a);
            points.push_back(b);
        }
    }
    auto subdued = color;
    if (m_family == "grid")
        subdued.setAlphaF(color.alphaF() * .45);
    if (auto *n = batch(minor, subdued, dots))
        root->appendChildNode(n);
    if (auto *n = batch(major, color, dots))
        root->appendChildNode(n);
    return root;
}
QVariantList EditorAlignmentOverlay::labels() const {
    if (!m_canvas || m_preview)
        return {};
    if (m_family == "snap")
        return m_canvas->alignmentFeedback();
    QVariantList out;
    if (m_family != "guides" || !m_canvas->viewOptions()->guides().value("labels").toBool() ||
        !m_canvas->viewOptions()->guides().value("enabled").toBool())
        return out;
    int index = 0;
    const double inset = m_canvas->viewOptions()->display().value("rulers").toBool() ? 22 : 0;
    for (const auto &g : m_canvas->viewOptions()->placedGuides()) {
        ++index;
        const auto a = m_canvas->elementItemPosition({g.line.a.x, g.line.a.y}),
                   b = m_canvas->elementItemPosition({g.line.b.x, g.line.b.y});
        const auto visible = A::clipped({{a.x(), a.y()}, {b.x(), b.y()}, true}, {inset, inset, width() - inset, height() - inset});
        if (visible && out.size() < 64) {
            const auto values = m_canvas->viewOptions()->guides();
            QColor color(values.value("color").toString()); color.setAlphaF(color.alphaF() * values.value("opacity").toDouble() / 100);
            out.append(QVariantMap{{"label", QString::number(index)}, {"guide", true}, {"color", color},
                                   {"position", QPointF(visible->a.x + 4, visible->a.y + 4)}});
        }
    }
    return out;
}
void EditorAlignmentOverlay::mousePressEvent(QMouseEvent *event) {
    if (!m_canvas || m_preview || (m_family != "guides" && m_family != "guide-input") ||
        m_canvas->viewOptions()->guides().value("locked").toBool() ||
        !m_canvas->viewOptions()->guides().value("enabled").toBool() ||
        m_canvas->viewOptions()->guides().value("opacity").toDouble() <= 0 ||
        QColor(m_canvas->viewOptions()->guides().value("color").toString()).alpha() == 0) {
        event->ignore();
        return;
    }
    int index = 0;
    for (const auto &g : m_canvas->viewOptions()->placedGuides()) {
        const auto a = m_canvas->elementItemPosition({g.line.a.x, g.line.a.y}),
                   b = m_canvas->elementItemPosition({g.line.b.x, g.line.b.y});
        const auto q =
            A::closestPoint({event->position().x(), event->position().y()}, {{a.x(), a.y()}, {b.x(), b.y()}});
        if (QLineF(event->position(), {q.x, q.y}).length() <= 5) {
            m_dragGuide = index;
            m_last = m_canvas->elementWorldPosition(event->position());
            forceActiveFocus();
            event->accept();
            return;
        }
        ++index;
    }
    event->ignore();
}
void EditorAlignmentOverlay::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragGuide < 0 || !m_canvas) {
        event->ignore();
        return;
    }
    const auto p = m_canvas->elementWorldPosition(event->position()), delta = p - m_last;
    m_canvas->viewOptions()->moveGuide(m_dragGuide, {delta.x(), delta.y()});
    m_last = p;
    event->accept();
}
void EditorAlignmentOverlay::mouseReleaseEvent(QMouseEvent *event) {
    if (m_dragGuide >= 0) {
        mouseMoveEvent(event);
        event->accept();
    } else
        event->ignore();
}
void EditorAlignmentOverlay::keyPressEvent(QKeyEvent *event) {
    if (m_canvas && m_dragGuide >= 0 &&
        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        m_canvas->viewOptions()->removeGuide(m_dragGuide)) {
        m_dragGuide = -1;
        event->accept();
    } else
        event->ignore();
}
