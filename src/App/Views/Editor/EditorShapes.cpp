#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QScopedValueRollback>
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
        const int sides = std::clamp(int(number(v, 7, 3)), 3, 64); const bool star = toggle(v, 9);
        const double inner = number(v, 5) > 0 ? std::clamp(number(v, 5) / 100, 0.0, 1.0) : 0.5;
        const int count = sides * (star ? 2 : 1);
        for (int i = 0; i < count; ++i) { const double angle = i * 2 * std::numbers::pi / count - std::numbers::pi / 2, k = star && i % 2 ? inner : 1;
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
QPainterPath EditorCanvas::elementCreationPath() const {
    return m_creatingElement ? elementPath(elementCreationBounds(), m_toolValues) : QPainterPath{};
}
QVariantList EditorCanvas::elementCandidates() const {
    QVariantList result;
    if (!documentReady()) return result;
    const auto active = selectedLayerId();
    for (auto it = document()->layers.crbegin(); it != document()->layers.crend(); ++it) {
        const auto &layer = *it;
        const auto &properties = layerProperties(layer);
        const auto id = QString::fromStdString(properties.id);
        if (!belongsToActiveArtboard(layer) || properties.id == backgroundLayerId()
            || m_textOrigins.contains(id)) continue;
        const bool known = m_elementBounds.contains(id) && m_elementSettings.contains(id);
        if (!known && contentKind(layer) != ContentKind::Vector) continue;
        const bool selected = m_selectedElements.contains(id);
        const auto bounds = elementLocalBounds(id);
        const bool unlocked = !liveStrokeActive() && !m_pixelLocks.contains(id) && !m_positionLocks.contains(id);
        result.append(QVariantMap{{"id", id}, {"name", QString::fromStdString(properties.name)},
            {"shape", known ? m_elementSettings.value(id).value("selector", "Rectangle").toString() : QStringLiteral("Vector")},
            {"selected", selected}, {"active", selected && active == id},
            {"editable", known && unlocked},
            {"resizable", unlocked && bounds.width() > 0 && bounds.height() > 0 && elementWorldTransform(id).isInvertible()}});
    }
    return result;
}
int EditorCanvas::elementSelectionCount() const {
    int count = 0;
    for (const auto &candidate : elementCandidates()) if (candidate.toMap().value("selected").toBool()) ++count;
    return count;
}
QVariantMap EditorCanvas::selectedElementSettings() const {
    const auto id = selectedLayerId();
    if (!documentReady() || !m_selectedElements.contains(id)) return {};
    const auto *layer = findLayer(*document(), id.toStdString());
    if (!layer || !belongsToActiveArtboard(*layer)) return {};
    const auto bounds = elementLocalBounds(id);
    if (bounds.width() <= 0 || bounds.height() <= 0) return {};
    auto settings = m_elementSettings.value(id);
    if (settings.isEmpty()) settings["selector"] = "Vector";
    const auto transform = elementWorldTransform(id);
    settings["field-0"] = QVariantList{bounds.width() * std::hypot(transform.m11(), transform.m12()),
        bounds.height() * std::hypot(transform.m21(), transform.m22())};
    settings["field-14"] = layerProperties(*layer).opacity * 100;
    const auto blendMode = layerProperties(*layer).blendMode;
    settings["field-15"] = blendMode == RasterBlendMode::Multiply ? "Multiply" : blendMode == RasterBlendMode::Screen ? "Screen" : "Normal";
    return settings;
}
void EditorCanvas::restoreSelectedElementSettings() {
    const auto settings = selectedElementSettings();
    if (settings.isEmpty()) return;
    m_settings["elements"] = settings;
    if (m_tool == "elements") m_toolValues = settings;
    if (m_elementPatterns.contains(selectedLayerId())) m_patternImage = m_elementPatterns.value(selectedLayerId());
}
void EditorCanvas::synchronizeElementSelection(bool replaceWithActive) {
    if (m_changingElementSelection) return;
    QSet<QString> available;
    for (const auto &candidate : elementCandidates()) available.insert(candidate.toMap().value("id").toString());
    m_selectedElements.intersect(available);
    const auto active = selectedLayerId();
    if (m_tool != "elements" || !available.contains(active)) m_selectedElements.clear();
    else if (replaceWithActive || !m_selectedElements.contains(active)) m_selectedElements = {active};
    if (replaceWithActive) restoreSelectedElementSettings();
    emit elementSelectionChanged();
}
bool EditorCanvas::applyElementSelection(const QSet<QString> &ids, const QString &preferred) {
    if (!documentReady()) return fail(tr("Open a canvas first."));
    if (liveStrokeActive()) return fail(tr("Finish the current stroke before changing element selection."));
    cancelElementTransform();
    QSet<QString> available;
    const auto candidates = elementCandidates();
    for (const auto &candidate : candidates) available.insert(candidate.toMap().value("id").toString());
    for (const auto &id : ids) if (!available.contains(id)) return fail(tr("Choose an element on the current artboard."));
    QString active = ids.contains(preferred) ? preferred : ids.contains(selectedLayerId()) ? selectedLayerId() : QString{};
    if (active.isEmpty()) for (const auto &candidate : candidates) {
        const auto id = candidate.toMap().value("id").toString();
        if (ids.contains(id)) { active = id; break; }
    }
    {
        QScopedValueRollback<bool> guard(m_changingElementSelection, true);
        const auto previous = m_selectedElements;
        m_selectedElements = ids;
        if (active.isEmpty()) clearSelection();
        else if (!selectLayer(active)) { m_selectedElements = previous; return false; }
        restoreSelectedElementSettings();
    }
    clearError();
    emit elementSelectionChanged();
    emit toolStateChanged();
    return true;
}
bool EditorCanvas::selectElement(const QString &id) { return applyElementSelection({id}, id); }
bool EditorCanvas::setElementSelected(const QString &id, bool checked, bool multiple) {
    bool found = false;
    for (const auto &candidate : elementCandidates()) found |= candidate.toMap().value("id").toString() == id;
    if (!found) return fail(tr("Choose an element on the current artboard."));
    auto selected = multiple ? m_selectedElements : QSet<QString>{};
    if (checked) selected.insert(id); else selected.remove(id);
    return applyElementSelection(selected, checked ? id : selectedLayerId());
}
bool EditorCanvas::selectAllElements() {
    QSet<QString> selected;
    for (const auto &candidate : elementCandidates()) selected.insert(candidate.toMap().value("id").toString());
    return applyElementSelection(selected, selectedLayerId());
}
bool EditorCanvas::invertElementSelection() {
    QSet<QString> selected;
    for (const auto &candidate : elementCandidates()) {
        const auto id = candidate.toMap().value("id").toString();
        if (!m_selectedElements.contains(id)) selected.insert(id);
    }
    return applyElementSelection(selected);
}
bool EditorCanvas::clearElementSelection() { return applyElementSelection({}); }
bool EditorCanvas::createElement(const QRectF &requested) {
    const auto bounds = requested.normalized();
    if (!documentReady() || !std::isfinite(bounds.x()) || !std::isfinite(bounds.y()) || bounds.width() <= 0 || bounds.height() <= 0) return fail(tr("Draw an element with a positive finite size."));
    const QColor color(text(m_toolValues, 16, "#8B7CFF")), stroke(text(m_toolValues, 17, "#FFFFFF"));
    if (!color.isValid() || !stroke.isValid()) return fail(tr("Choose valid element colors."));
    const auto assetId = unique("element.asset."), layerId = unique("element.layer.");
    auto values = m_toolValues;
    if (!QStringList{"Rectangle", "Ellipse", "Polygon", "Line / Arrow"}.contains(values.value("selector").toString()))
        values["selector"] = "Rectangle";
    const auto pattern = m_patternImage;
    const bool changingSelection = m_changingElementSelection;
    QScopedValueRollback<bool> guard(m_changingElementSelection, true);
    const auto fill = text(values, 2, "Solid");
    if (fill == "Image" && pattern.isNull()) return fail(tr("Choose an image for the element fill."));
    auto vector = shapeAsset(editingExtent(), assetId, bounds, values);
    std::optional<RasterLayer> rasterFill;
    if (fill == "Gradient" || fill == "Image") {
        QImage image(editingWidth(), editingHeight(), QImage::Format_ARGB32); image.fill(Qt::transparent);
        QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen);
        if (fill == "Gradient") { QLinearGradient gradient(bounds.topLeft(), bounds.bottomRight()); gradient.setColorAt(0, color); gradient.setColorAt(1, color.lighter(170)); painter.setBrush(gradient); }
        else painter.setBrush(QBrush(pattern));
        painter.drawPath(elementPath(bounds, values)); painter.end();
        rasterFill = raster(image);
    }
    if (!commit([&](Document &d) {
        LayerProperties p{layerId, values.value("selector", "Rectangle").toString().toStdString()};
        p.opacity = std::clamp(number(values, 14, 100) / 100, 0.0, 1.0); p.blendMode = blend(text(values, 15));
        if (rasterFill) {
            Document outlines; outlines.extent = editingExtent(); outlines.assets.emplace_back(vector); outlines.layers.emplace_back(StaticVectorLayer{{"outline", "Outline"}, StaticSource{assetId}});
            const auto rendered = renderFrame(outlines, 0); if (!rendered.ok()) return false;
            auto image = editorTools::image(*rasterFill); QPainter paint(&image); paint.drawImage(0, 0, editorTools::image(rendered.pixels)); paint.end();
            d.assets.emplace_back(RasterAsset{assetId, raster(image)}); d.layers.emplace_back(StaticBitmapLayer{p, StaticSource{assetId}});
        } else { d.assets.emplace_back(std::move(vector)); d.layers.emplace_back(StaticVectorLayer{p, StaticSource{assetId}}); }
        return true;
    })) return false;
    const auto id = QString::fromStdString(layerId); m_elementBounds[id] = bounds; m_elementSettings[id] = values;
    if (fill == "Image") m_elementPatterns[id] = pattern;
    updateElementHistoryMetadata();
    m_changingElementSelection = changingSelection;
    return selectLayer(id);
}
bool EditorCanvas::updateElement() {
    const auto id = selectedLayerId(); if (!m_elementBounds.contains(id)) return true;
    const auto reject = [this](const QString &message) {
        restoreSelectedElementSettings();
        emit elementSelectionChanged();
        return fail(message);
    };
    if (liveStrokeActive()) return reject(tr("Finish the current stroke before editing an element."));
    if (m_pixelLocks.contains(id) || m_positionLocks.contains(id)) return reject(tr("Unlock the selected element before editing its geometry."));
    const auto values = m_toolValues;
    const auto pattern = m_elementPatterns.value(id);
    auto bounds = m_elementBounds[id]; const auto size = values.value("field-0").toList();
    if (m_lastEditedField == "field-0") {
        if (size.size() != 2) return reject(tr("Enter the element width and height."));
        const double width = size[0].toDouble(), height = size[1].toDouble();
        if (!std::isfinite(width) || !std::isfinite(height) || width < 1 || height < 1 || width > 65536 || height > 65536)
            return reject(tr("Element dimensions must be from 1 to 65536 pixels."));
        const auto transform = elementWorldTransform(id);
        const auto sx = std::hypot(transform.m11(), transform.m12()), sy = std::hypot(transform.m21(), transform.m22());
        if (sx < 1e-9 || sy < 1e-9) return reject(tr("The selected element transform cannot be resized."));
        bounds.setSize({width / sx, height / sy});
    }
    const auto *layer = findLayer(*document(), id.toStdString()); if (!layer) return false;
    const auto sourceValue = layerSource(*layer); const auto *source = std::get_if<StaticSource>(&sourceValue); if (!source) return reject(tr("Select a static element."));
    const auto assetId = source->assetId;
    const bool changingSelection = m_changingElementSelection;
    QScopedValueRollback<bool> guard(m_changingElementSelection, true);
    if (!commit([&](Document &d) {
        auto *asset = findAsset(d, assetId); if (!asset) return false;
        auto *selected = findLayer(d, id.toStdString()); const auto priorProperties = layerProperties(*selected);
        const auto fill = text(values, 2, "Solid");
        auto geometry = shapeAsset(editingExtent(), assetId, bounds, values);
        if (fill == "Gradient" || fill == "Image") {
            if (fill == "Image" && pattern.isNull()) return false;
            QImage pixels(editingWidth(), editingHeight(), QImage::Format_ARGB32); pixels.fill(Qt::transparent); QPainter painter(&pixels); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(Qt::NoPen);
            if (fill == "Gradient") { const QColor color(text(values, 16, "#8B7CFF")); QLinearGradient gradient(bounds.topLeft(), bounds.bottomRight()); gradient.setColorAt(0, color); gradient.setColorAt(1, color.lighter(170)); painter.setBrush(gradient); } else painter.setBrush(QBrush(pattern));
            painter.drawPath(elementPath(bounds, values)); painter.end();
            Document outlines; outlines.extent = editingExtent(); outlines.assets.emplace_back(geometry); outlines.layers.emplace_back(StaticVectorLayer{{"outline", "Outline"}, StaticSource{assetId}});
            const auto rendered = renderFrame(outlines, 0); if (!rendered.ok()) return false;
            QPainter outline(&pixels); outline.drawImage(0, 0, image(rendered.pixels)); outline.end(); *asset = RasterAsset{assetId, raster(pixels)}; *selected = StaticBitmapLayer{priorProperties, StaticSource{assetId}};
        } else { *asset = geometry; *selected = StaticVectorLayer{priorProperties, StaticSource{assetId}}; }
        auto &properties = layerProperties(*findLayer(d, id.toStdString()));
        properties.opacity = std::clamp(number(values, 14, 100) / 100, 0.0, 1.0); properties.blendMode = blend(text(values, 15));
        return true;
    })) return reject(tr("Select a vector element to edit its geometry, or create a new element with the chosen fill."));
    m_elementBounds[id] = bounds; m_elementSettings[id] = values;
    if (text(values, 2) == "Image") m_elementPatterns[id] = pattern;
    updateElementHistoryMetadata();
    if (!selectLayer(id)) return false;
    m_changingElementSelection = changingSelection;
    if (!changingSelection) synchronizeElementSelection(false);
    return true;
}
