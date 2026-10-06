#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPathStroker>
#include <numbers>
using namespace iiSharedCanvas;
using namespace editorTools;
namespace {
QPainterPath elementPath(QRectF r, const QVariantMap &v) {
    QPainterPath p; p.setFillRule(Qt::OddEvenFill); const auto shape = v.value("selector", "Rectangle").toString();
    if (shape == "Rectangle") { const double radius = std::clamp(number(v, 1), 0.0, std::min(r.width(), r.height()) / 2); p.addRoundedRect(r, radius * (1 + number(v, 8) / 200), radius * (1 + number(v, 8) / 200)); }
    else if (shape == "Ellipse") {
        const double sweep = number(v, 4, 360); if (sweep >= 360) p.addEllipse(r);
        else { p.moveTo(r.center()); p.arcTo(r, 0, sweep); p.closeSubpath(); }
        const auto inner = number(v, 5) / 100; if (inner > 0) p.addEllipse(QRectF(r.center() - QPointF(r.width() * inner / 2, r.height() * inner / 2), QSizeF(r.width() * inner, r.height() * inner)));
    } else if (shape == "Polygon") {
        const int sides = std::clamp(int(number(v, 7, 3)), 3, 64); const bool star = toggle(v, 9) && number(v, 5) > 0;
        const int count = sides * (star ? 2 : 1);
        for (int i = 0; i < count; ++i) { const double angle = i * 2 * std::numbers::pi / count - std::numbers::pi / 2, k = star && i % 2 ? number(v, 5) / 100 : 1;
            const QPointF point(r.center().x() + std::cos(angle) * r.width() / 2 * k, r.center().y() + std::sin(angle) * r.height() / 2 * k);
            if (i) p.lineTo(point); else p.moveTo(point);
        } p.closeSubpath();
        if (number(v, 8) > 0) { QPainterPathStroker round; round.setWidth(number(v, 8) / 20); round.setJoinStyle(Qt::RoundJoin); p = p.united(round.createStroke(p)); }
    } else if (shape == "Line / Arrow") { p.moveTo(r.topLeft()); p.lineTo(r.bottomRight()); }
    return p;
}
VectorAsset shapeAsset(CanvasExtent extent, const std::string &id, QRectF bounds, const QVariantMap &v) {
    auto p = elementPath(bounds, v); VectorAsset asset{id, extent, {}};
    const auto fill = text(v, 2, "Solid"); const QColor color(text(v, 16, "#8B7CFF")), stroke(text(v, 17, "#FFFFFF"));
    if (fill == "Solid" && v.value("selector").toString() != "Line / Arrow") asset.paths.push_back(nativePath(p, color.rgba()));
    const double width = std::clamp(number(v, 10, 4), 0.0, 2048.0);
    if (width > 0) {
        QPainterPathStroker s; s.setWidth(text(v, 6, "Center") == "Center" ? width : width * 2);
        s.setCapStyle(text(v, 11, "Round") == "Round" ? Qt::RoundCap : text(v, 11) == "Square" ? Qt::SquareCap : Qt::FlatCap);
        s.setJoinStyle(Qt::RoundJoin); auto outline = s.createStroke(p);
        if (v.value("selector").toString() != "Line / Arrow") {
            if (text(v, 6) == "Inside") outline = outline.intersected(p);
            if (text(v, 6) == "Outside") outline = outline.subtracted(p);
        }
        asset.paths.push_back(nativePath(outline, stroke.rgba()));
    }
    if (v.value("selector").toString() == "Line / Arrow") {
        for (int end = 0; end < 2; ++end) {
            const auto type = text(v, 12 + end, end ? "Arrow" : "None"); const auto at = end ? bounds.bottomRight() : bounds.topLeft();
            QPainterPath marker; const double size = std::max(4.0, width * 3);
            if (type == "Dot") marker.addEllipse(at, size / 2, size / 2);
            else if (type == "Arrow") { const double angle = std::atan2(bounds.height(), bounds.width()) + (end ? 0 : std::numbers::pi);
                marker.moveTo(at); marker.lineTo(at - QPointF(std::cos(angle - 0.5), std::sin(angle - 0.5)) * size);
                marker.lineTo(at - QPointF(std::cos(angle + 0.5), std::sin(angle + 0.5)) * size); marker.closeSubpath(); }
            if (!marker.isEmpty()) asset.paths.push_back(nativePath(marker, stroke.rgba()));
        }
    }
    return asset;
}
}
bool EditorCanvas::createElement(const QRectF &requested) {
    const auto bounds = requested.normalized();
    if (!documentReady() || !std::isfinite(bounds.x()) || !std::isfinite(bounds.y()) || bounds.width() <= 0 || bounds.height() <= 0) return fail(tr("Draw an element with a positive finite size."));
    const QColor color(text(m_toolValues, 16, "#8B7CFF")), stroke(text(m_toolValues, 17, "#FFFFFF"));
    if (!color.isValid() || !stroke.isValid()) return fail(tr("Choose valid element colors."));
    const auto assetId = unique("element.asset."), layerId = unique("element.layer.");
    const auto fill = text(m_toolValues, 2, "Solid");
    if (fill == "Image" && m_patternImage.isNull()) return fail(tr("Choose an image for the element fill."));
    auto vector = shapeAsset(document()->extent, assetId, bounds, m_toolValues);
    std::optional<RasterLayer> rasterFill;
    if (fill == "Gradient" || fill == "Image") {
        QImage image(canvasWidth(), canvasHeight(), QImage::Format_ARGB32); image.fill(Qt::transparent);
        QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen);
        if (fill == "Gradient") { QLinearGradient gradient(bounds.topLeft(), bounds.bottomRight()); gradient.setColorAt(0, color); gradient.setColorAt(1, color.lighter(170)); painter.setBrush(gradient); }
        else painter.setBrush(QBrush(m_patternImage));
        painter.drawPath(elementPath(bounds, m_toolValues)); painter.end();
        rasterFill = raster(image);
    }
    if (!commit([&](Document &d) {
        LayerProperties p{layerId, m_toolValues.value("selector", "Rectangle").toString().toStdString()};
        p.opacity = std::clamp(number(m_toolValues, 14, 100) / 100, 0.0, 1.0); p.blendMode = blend(text(m_toolValues, 15));
        if (rasterFill) {
            Document outlines; outlines.extent = d.extent; outlines.assets.emplace_back(vector); outlines.layers.emplace_back(StaticVectorLayer{{"outline", "Outline"}, StaticSource{assetId}});
            const auto rendered = renderFrame(outlines, 0); if (!rendered.ok()) return false;
            auto image = editorTools::image(*rasterFill); QPainter paint(&image); paint.drawImage(0, 0, editorTools::image(rendered.pixels)); paint.end();
            d.assets.emplace_back(RasterAsset{assetId, raster(image)}); d.layers.emplace_back(StaticBitmapLayer{p, StaticSource{assetId}});
        } else { d.assets.emplace_back(std::move(vector)); d.layers.emplace_back(StaticVectorLayer{p, StaticSource{assetId}}); }
        return true;
    })) return false;
    const auto id = QString::fromStdString(layerId); m_elementBounds[id] = bounds; m_elementSettings[id] = m_toolValues;
    return selectLayer(id);
}
bool EditorCanvas::updateElement() {
    const auto id = selectedLayerId(); if (!m_elementBounds.contains(id)) return true;
    auto bounds = m_elementBounds[id]; const auto size = m_toolValues.value("field-0").toList();
    if (m_lastEditedField == "field-0" && size.size() == 2) bounds.setSize({size[0].toDouble(), size[1].toDouble()});
    const auto *layer = findLayer(*document(), id.toStdString()); if (!layer) return false;
    const auto sourceValue = layerSource(*layer); const auto *source = std::get_if<StaticSource>(&sourceValue); if (!source) return fail(tr("Select a static element."));
    const auto assetId = source->assetId;
    if (!commit([&](Document &d) {
        auto *asset = findAsset(d, assetId); if (!asset) return false;
        auto *selected = findLayer(d, id.toStdString()); const auto priorProperties = layerProperties(*selected);
        const auto fill = text(m_toolValues, 2, "Solid");
        auto geometry = shapeAsset(d.extent, assetId, bounds, m_toolValues);
        if (fill == "Gradient" || fill == "Image") {
            if (fill == "Image" && m_patternImage.isNull()) return false;
            QImage pixels(d.extent.width, d.extent.height, QImage::Format_ARGB32); pixels.fill(Qt::transparent); QPainter painter(&pixels); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen);
            if (fill == "Gradient") { const QColor color(text(m_toolValues, 16, "#8B7CFF")); QLinearGradient gradient(bounds.topLeft(), bounds.bottomRight()); gradient.setColorAt(0, color); gradient.setColorAt(1, color.lighter(170)); painter.setBrush(gradient); } else painter.setBrush(QBrush(m_patternImage));
            painter.drawPath(elementPath(bounds, m_toolValues)); painter.end();
            Document outlines; outlines.extent = d.extent; outlines.assets.emplace_back(geometry); outlines.layers.emplace_back(StaticVectorLayer{{"outline", "Outline"}, StaticSource{assetId}});
            const auto rendered = renderFrame(outlines, 0); if (!rendered.ok()) return false;
            QPainter outline(&pixels); outline.drawImage(0, 0, image(rendered.pixels)); outline.end(); *asset = RasterAsset{assetId, raster(pixels)}; *selected = StaticBitmapLayer{priorProperties, StaticSource{assetId}};
        } else { *asset = geometry; *selected = StaticVectorLayer{priorProperties, StaticSource{assetId}}; }
        auto &properties = layerProperties(*findLayer(d, id.toStdString()));
        properties.opacity = std::clamp(number(m_toolValues, 14, 100) / 100, 0.0, 1.0); properties.blendMode = blend(text(m_toolValues, 15));
        return true;
    })) return fail(tr("Select a vector element to edit its geometry, or create a new element with the chosen fill."));
    m_elementBounds[id] = bounds; m_elementSettings[id] = m_toolValues; return selectLayer(id);
}
bool EditorCanvas::createText(const QPointF &position) {
    if (!documentReady()) return fail(tr("Open a canvas first."));
    const auto id = QString::fromStdString(unique("text.layer."));
    m_textOrigins[id] = position; m_textSettings[id] = m_toolValues;
    const auto assetId = unique("text.asset.");
    if (!commit([&](Document &d) { d.assets.emplace_back(VectorAsset{assetId, d.extent, {}}); d.layers.emplace_back(StaticVectorLayer{{id.toStdString(), "Text"}, StaticSource{assetId}}); return true; })) return false;
    if (!selectLayer(id)) return false; return updateText();
}
bool EditorCanvas::updateText() {
    const auto id = selectedLayerId(); if (!m_textOrigins.contains(id)) return true;
    const auto v = m_toolValues; const auto mode = v.value("selector", "Free Text").toString();
    QString content = text(v, 0, "Enter your text…");
    if (mode == "Footer" && text(v, 13) == "Page no.") content = QString::number(m_specification.value("pageNumber", 1).toInt());
    else if (mode == "Footer" && text(v, 13) == "Metadata") content = documentName();
    if (content.isEmpty()) return fail(tr("Enter text content."));
    QFont font(text(v, 1, "Pretendard") == "Serif" ? "serif" : text(v, 1) == "Mono" ? "monospace" : "Pretendard");
    double size = number(v, 2, 48); if (mode == "Header Title") size *= text(v, 7, "Display") == "H2" ? 0.6 : text(v, 7) == "H1" ? 0.8 : 1.0;
    font.setPixelSize(std::max(1, int(std::round(size)))); font.setLetterSpacing(QFont::AbsoluteSpacing, number(v, 8) / 10);
    const QFontMetricsF metrics(font); if (toggle(v, 9) && metrics.horizontalAdvance(content) > canvasWidth() - 16) font.setPixelSize(std::max(1, int(size * (canvasWidth() - 16) / metrics.horizontalAdvance(content))));
    QPainterPath textPath; int line = 0; for (const auto &row : content.split('\n')) textPath.addText(0, line++ * QFontMetricsF(font).lineSpacing(), font, row); auto bounds = textPath.boundingRect(); auto origin = m_textOrigins[id];
    const auto align = text(v, 3, "Left"); origin.rx() -= align == "Center" ? bounds.width() / 2 : align == "Right" ? bounds.width() : 0;
    if (mode == "Caption") origin.setY(text(v, 5, "Bottom") == "Top" ? 16 : text(v, 5) == "Center" ? canvasHeight() / 2 - bounds.height() / 2 : canvasHeight() - bounds.height() - 16);
    if (mode == "Footer") origin.setY(canvasHeight() - bounds.height() - number(v, 14, 32));
    textPath.translate(origin.x() - bounds.x(), origin.y() - bounds.y()); bounds = textPath.boundingRect();
    std::vector<VectorPath> paths;
    if (mode == "Callout" || mode == "Caption") {
        const double padding = number(v, 12, 20); auto bubbleBounds = bounds.adjusted(-padding, -padding, padding, padding); QPainterPath bubble;
        const auto style = text(v, 10, "Round"); if (style == "Sharp") bubble.addRect(bubbleBounds); else bubble.addRoundedRect(bubbleBounds, style == "Cloud" ? padding : padding / 2, style == "Cloud" ? padding : padding / 2);
        if (mode == "Callout") { const auto pointer = text(v, 11, "Left"); const auto point = pointer == "Bottom" ? bubbleBounds.bottomLeft() + QPointF(bubbleBounds.width() / 2, padding) : pointer == "Right" ? bubbleBounds.topRight() + QPointF(padding, bubbleBounds.height() / 2) : bubbleBounds.topLeft() + QPointF(-padding, bubbleBounds.height() / 2);
            QPainterPath triangle; triangle.moveTo(point); triangle.lineTo(bounds.center() + QPointF(-padding, 0)); triangle.lineTo(bounds.center() + QPointF(padding, 0)); triangle.closeSubpath(); bubble = bubble.united(triangle); }
        const auto alpha = mode == "Caption" ? std::clamp(int(number(v, 6, 72) * 2.55), 0, 255) : 210;
        paths.push_back(nativePath(bubble, std::uint32_t(alpha) << 24));
    }
    paths.push_back(nativePath(textPath, brushColor().rgba()));
    if (!commit([&](Document &d) { auto *layer = findLayer(d, id.toStdString()); const auto sourceValue = layerSource(*layer); const auto *source = std::get_if<StaticSource>(&sourceValue); if (!source) return false;
        auto *asset = std::get_if<VectorAsset>(findAsset(d, source->assetId)); if (!asset) return false; asset->paths = paths;
        layerProperties(*layer).name = ("Text: " + content.left(80)).toStdString();
        if (mode == "Caption") { const auto range = timeRange(text(v, 4)); if (range.first >= 0 && range.second > range.first) { const double rate = double(d.timeline.frameRate.numerator) / d.timeline.frameRate.denominator;
            const auto first = FrameIndex(range.first * rate), last = FrameIndex(std::ceil(range.second * rate) - 1); d.timeline.frameCount = std::max(d.timeline.frameCount, last + 1); layerProperties(*layer).frameRange = LayerFrameRange{first, last}; } }
        else layerProperties(*layer).frameRange.reset(); return true;
    })) return false;
    m_textSettings[id] = v; return true;
}
