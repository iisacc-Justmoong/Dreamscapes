#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QTransform>
using namespace iiSharedCanvas;
using namespace editorTools;
RasterMask EditorCanvas::layerMask() const {
    const auto *pixels = selectedRasterPixels(); if (!pixels) return {};
    RasterMask mask{{pixels->width, pixels->height}, std::vector<std::uint8_t>(pixels->pixels.size(), 255)};
    if (m_selection.alpha.empty()) return mask;
    const auto *layer = findLayer(*document(), selectedLayerId().toStdString()); if (!layer) return {};
    const auto &t = layerProperties(*layer).transform; QTransform transform(t.m11, t.m12, t.m21, t.m22, t.translationX, t.translationY);
    for (int y = 0; y < pixels->height; ++y) for (int x = 0; x < pixels->width; ++x) {
        const auto p = transform.map(QPointF(x + 0.5, y + 0.5)); const int px = int(std::floor(p.x())), py = int(std::floor(p.y()));
        mask.alpha[y * pixels->width + x] = px >= 0 && py >= 0 && px < m_selection.extent.width && py < m_selection.extent.height ? m_selection.alpha[py * m_selection.extent.width + px] : 0;
    }
    return mask;
}
QUrl EditorCanvas::selectionOverlay() const { return m_selectionOverlay; }
void EditorCanvas::clearAreaSelection() { m_selection = {}; m_selectionOverlay = QUrl{}; invalidateToolPreview(); emit toolStateChanged(); }
void EditorCanvas::updateSelectionOverlay() {
    if (m_selection.alpha.empty()) { m_selectionOverlay = QUrl{}; emit toolStateChanged(); return; }
    QImage image(m_selection.extent.width, m_selection.extent.height, QImage::Format_ARGB32); image.fill(Qt::transparent);
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const auto alpha = m_selection.alpha[y * image.width() + x]; if (alpha) image.setPixel(x, y, (std::uint32_t(alpha / 3) << 24) | 0x008b7cff);
    }
    m_selectionOverlay = dataImage(image); emit toolStateChanged();
}
bool EditorCanvas::selectArea(const QVariantList &input) {
    if (!documentReady() || input.size() < 2) return fail(tr("Draw a selection in the canvas."));
    if (std::uint64_t(canvasWidth()) * canvasHeight() > 67108864) return fail(tr("This canvas exceeds the selection allocation limit."));
    QVector<QPointF> points; for (const auto &value : input) { const auto p = value.toPointF(); if (!std::isfinite(p.x()) || !std::isfinite(p.y())) return fail(tr("Selection coordinates must be finite.")); points.append(p); }
    const auto v = m_toolValues; const bool masking = m_tool == "masking"; const auto mode = v.value("selector", masking ? "Brush" : "Rectangle").toString();
    if (masking && (mode == "Subject" || mode == "Region")) return semanticSelection(v, false);
    if (masking && mode == "Range") {
        const auto rendered = renderFrame(*document(), frame()); if (!rendered.ok()) return fail(QString::fromStdString(rendered.message));
        const int px = std::clamp(int(points.first().x()), 0, canvasWidth() - 1), py = std::clamp(int(points.first().y()), 0, canvasHeight() - 1);
        const auto seed = rendered.pixels.pixels[py * canvasWidth() + px]; const auto kind = text(v, 18, "Luminance"); const double width = std::max(0.001, number(v, 20, 50) / 100), smoothness = number(v, 21, 54) / 100;
        if (kind == "Depth") return fail(tr("Choose a supplied depth layer to select a depth range."));
        RasterMask mask{{canvasWidth(), canvasHeight()}, std::vector<std::uint8_t>(rendered.pixels.pixels.size())};
        for (std::size_t i = 0; i < mask.alpha.size(); ++i) { const auto p = rendered.pixels.pixels[i]; const double distance = kind == "Color" ? std::sqrt(std::pow(qRed(p) - qRed(seed), 2) + std::pow(qGreen(p) - qGreen(seed), 2) + std::pow(qBlue(p) - qBlue(seed), 2)) / 441.67 : std::abs(qGray(p) - qGray(seed)) / 255.0;
            mask.alpha[i] = std::uint8_t(std::clamp((width - distance) / std::max(0.001, width * smoothness), 0.0, 1.0) * qAlpha(p)); }
        if (!m_selection.alpha.empty()) { const auto operation = text(v, 22, "Add"); auto combined = combineMasks(m_selection, mask, operation == "Intersect" ? MaskOperation::Intersect : operation == "Subtract" ? MaskOperation::Subtract : MaskOperation::Add); if (combined.ok()) mask = combined.mask; }
        m_selection = std::move(mask); updateSelectionOverlay(); return true;
    }
    QPainterPath path; auto bounds = QRectF(points.first(), points.last()).normalized();
    if (!masking && toggle(v, 5) && mode == "Rectangle") bounds = QRectF(points.first() - (points.last() - points.first()), points.last()).normalized();
    const auto ratio = text(v, 4, "Free"); if (!masking && mode == "Rectangle" && ratio != "Free") bounds.setHeight(bounds.width() / (ratio == "4:5" ? 0.8 : 1));
    if (mode == "Lasso" && points.size() > 2) {
        path.moveTo(points.first()); for (int i = 1; i < points.size(); ++i) { if (number(v, 1) > 0 && i + 1 < points.size()) path.quadTo(points[i], (points[i] + points[i + 1]) / 2); else path.lineTo(points[i]); }
        if (toggle(v, 3, true)) path.closeSubpath();
    } else if (mode == "Triangle") {
        if (toggle(v, 7)) { bounds.setLeft(std::round(bounds.left() / 8) * 8); bounds.setTop(std::round(bounds.top() / 8) * 8); }
        const std::array<QPointF, 3> vertices{QPointF(bounds.center().x(), bounds.top()), bounds.bottomRight(), bounds.bottomLeft()};
        const double rounding = std::clamp(number(v, 8) / 200, 0.0, 0.5);
        if (rounding == 0) { path.moveTo(vertices[0]); path.lineTo(vertices[1]); path.lineTo(vertices[2]); }
        else {
            path.moveTo(vertices[0] + (vertices[2] - vertices[0]) * rounding);
            for (int i = 0; i < 3; ++i) {
                const auto entry = vertices[i] + (vertices[(i + 2) % 3] - vertices[i]) * rounding;
                const auto exit = vertices[i] + (vertices[(i + 1) % 3] - vertices[i]) * rounding;
                path.lineTo(entry); path.quadTo(vertices[i], exit);
            }
        }
        path.closeSubpath();
    } else if (mode == "Radial") { const double roundness = std::max(0.05, number(v, 8, 100) / 100); bounds.setHeight(bounds.height() * roundness); path.addEllipse(bounds); }
    else if (masking && mode == "Brush") { QPainterPath stroke; stroke.moveTo(points.first()); for (int i = 1; i < points.size(); ++i) stroke.lineTo(points[i]); QPainterPathStroker stroker; stroker.setWidth(std::max(1.0, number(v, 0, 84))); stroker.setCapStyle(Qt::RoundCap); path = stroker.createStroke(stroke); }
    else path.addRoundedRect(bounds, mode == "Triangle" ? number(v, 8) : 0, mode == "Triangle" ? number(v, 8) : 0);
    if (!masking && (mode == "Rectangle" || mode == "Triangle")) {
        QTransform rotation; rotation.translate(bounds.center().x(), bounds.center().y()); rotation.rotate(number(v, 6)); rotation.translate(-bounds.center().x(), -bounds.center().y()); path = rotation.map(path);
    }
    QImage image(canvasWidth(), canvasHeight(), QImage::Format_ARGB32); image.fill(Qt::transparent); QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.fillPath(path, Qt::white); painter.end();
    RasterMask next{{canvasWidth(), canvasHeight()}, std::vector<std::uint8_t>(std::size_t(canvasWidth()) * canvasHeight())};
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) next.alpha[y * image.width() + x] = qAlpha(image.pixel(x, y));
    if (masking && mode == "Linear") {
        const double angle = number(v, 4) * std::numbers::pi / 180;
        const double span = std::max(1.0, bounds.width() * std::abs(std::cos(angle)) + bounds.height() * std::abs(std::sin(angle)));
        for (int y = 0; y < canvasHeight(); ++y) for (int x = 0; x < canvasWidth(); ++x) next.alpha[y * canvasWidth() + x] = std::clamp(int((0.5 + ((x - bounds.center().x()) * std::cos(angle) + (y - bounds.center().y()) * std::sin(angle)) / span) * 255), 0, 255);
    }
    const double feather = masking ? mode == "Radial" ? number(v, 9) / 100 * std::min(bounds.width(), bounds.height()) / 2 : mode == "Linear" ? number(v, 5) / 100 * std::min(bounds.width(), bounds.height()) / 2 : number(v, 1) / 100 * number(v, 0, 84) / 2 : number(v, 2);
    if (feather > 0) { auto softened = featherMask(next, std::min(256.0, feather)); if (!softened.ok()) return fail(QString::fromStdString(softened.error)); next = std::move(softened.mask); }
    if (masking && toggle(v, 6)) for (auto &alpha : next.alpha) alpha = 255 - alpha;
    const auto operation = masking ? text(v, 22, "Add") : text(v, 0, "New");
    if (!m_selection.alpha.empty() && operation != "New") { const auto op = operation == "Subtract" || (masking && text(v, 2) == "Erase") ? MaskOperation::Subtract : operation == "Intersect" ? MaskOperation::Intersect : MaskOperation::Add;
        auto combined = combineMasks(m_selection, next, op); if (!combined.ok()) return fail(QString::fromStdString(combined.error)); next = std::move(combined.mask); }
    if (masking) {
        if (toggle(v, 3) && mode == "Brush") { const auto rendered = renderFrame(*document(), frame()); if (rendered.ok()) { auto connected = floodMask(rendered.pixels, {int(points.first().x()), int(points.first().y())}, 0.12, true); if (connected.ok()) { auto intersection = combineMasks(next, connected.mask, MaskOperation::Intersect); if (intersection.ok()) next = intersection.mask; } } }
        const double contrast = number(v, 24) / 100; if (contrast > 0) for (auto &alpha : next.alpha) alpha = std::uint8_t(std::clamp((alpha / 255.0 - 0.5) * (1 + contrast * 3) + 0.5, 0.0, 1.0) * 255);
        if (number(v, 23) > 0) { auto softened = featherMask(next, std::min(256.0, number(v, 23))); if (softened.ok()) next = softened.mask; }
    }
    m_selection = std::move(next); invalidateToolPreview(); clearError(); updateSelectionOverlay(); return true;
}
bool EditorCanvas::applySelectionMask(const QVariantMap &values) {
    if (m_selection.alpha.empty()) return fail(tr("Create a selection or mask first."));
    if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("Unlock the selected layer's pixels first."));
    if (!rasterizeSelected()) return false;
    auto mask = layerMask(); const double density = std::clamp(number(values, 25, 100) / 100, 0.0, 1.0);
    for (auto &alpha : mask.alpha) alpha = std::uint8_t(std::lround(alpha * density));
    if (number(values, 26) > 0) { auto feathered = featherMask(mask, std::min(256.0, number(values, 26))); if (!feathered.ok()) return fail(QString::fromStdString(feathered.error)); mask = std::move(feathered.mask); }
    auto result = maskRaster(*selectedRasterPixels(), mask); if (!result.ok()) return fail(QString::fromStdString(result.error));
    invalidateToolPreview(); return replaceSelectedPixels(result.pixels);
}
bool EditorCanvas::semanticSelection(const QVariantMap &v, bool background) {
    const auto target = text(v, m_tool == "select" ? 13 : m_tool == "background" ? 10 : v.value("selector").toString() == "Region" ? 14 : 11, background ? "Background" : "Main");
    const double threshold = number(v, m_tool == "select" ? 15 : 9) / 100;
    for (const auto &layer : document()->layers) if (const auto *semantic = std::get_if<SemanticSegmentLayer>(&layer)) {
        const auto map = renderSemanticControlMap(*document(), semantic->properties.id, frame()); if (!map.ok()) continue;
        QSet<std::uint32_t> regions;
        for (const auto &region : semantic->segmentation.regions) {
            const auto *type = findSemanticClass(semantic->segmentation, region.classId); const auto name = QString::fromStdString(type ? type->name : region.name);
            const bool selected = target == "Main" || target == "Object" || target == "Part" || name.contains(target, Qt::CaseInsensitive);
            if (selected && (!region.confidence || *region.confidence >= threshold)) regions.insert(region.id);
        }
        if (regions.isEmpty()) continue;
        QImage mask(map.pixels.width, map.pixels.height, QImage::Format_ARGB32); mask.fill(Qt::transparent);
        for (int y = 0; y < mask.height(); ++y) for (int x = 0; x < mask.width(); ++x) if (regions.contains(map.regionIds[y * mask.width() + x])) mask.setPixel(x, y, 0xffffffff);
        QImage canvasMask(canvasWidth(), canvasHeight(), QImage::Format_ARGB32); canvasMask.fill(Qt::transparent);
        const auto &t = semantic->properties.transform;
        QPainter mapped(&canvasMask); mapped.setTransform(QTransform(t.m11, t.m12, t.m21, t.m22, t.translationX, t.translationY)); mapped.drawImage(QPointF(0, 0), mask); mapped.end(); mask = std::move(canvasMask);
        m_selection = {{canvasWidth(), canvasHeight()}, std::vector<std::uint8_t>(std::size_t(canvasWidth()) * canvasHeight())};
        for (int y = 0; y < mask.height(); ++y) for (int x = 0; x < mask.width(); ++x) m_selection.alpha[y * mask.width() + x] = qAlpha(mask.pixel(x, y));
        auto feather = featherMask(m_selection, std::min(256.0, number(v, m_tool == "select" ? 12 : 12)));
        if (feather.ok()) m_selection = std::move(feather.mask); updateSelectionOverlay(); clearError(); return true;
    }
    return fail(tr("Import a semantic segmentation layer containing the requested region. No detected subject data is present."));
}
bool EditorCanvas::retouchAt(const QPointF &position) {
    if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("Unlock the selected layer's pixels first."));
    if (!rasterizeSelected()) return false;
    if (m_toolValues.value("selector").toString() == "Clone" && !m_cloneReady) return fail(tr("Pick a clone source point first."));
    const auto *layer = findLayer(*document(), selectedLayerId().toStdString()); const auto &t = layerProperties(*layer).transform;
    const auto inverse = QTransform(t.m11, t.m12, t.m21, t.m22, t.translationX, t.translationY).inverted();
    const auto mappedPosition = inverse.map(position); const auto source = inverse.map(m_cloneSource); const auto start = inverse.map(m_retouchStart);
    auto pixels = *selectedRasterPixels(); const auto base = pixels; const auto v = m_toolValues;
    RasterLayer sampled = base;
    const bool sampleAll = toggle(v, 10);
    if (sampleAll) {
        const auto composite = renderFrame(*document(), frame());
        if (!composite.ok()) return fail(QString::fromStdString(composite.message));
        sampled = composite.pixels;
    }
    const QTransform toCanvas(t.m11, t.m12, t.m21, t.m22, t.translationX, t.translationY);
    const auto sample = [&](double x, double y) {
        const auto point = sampleAll ? toCanvas.map(QPointF(x, y)) : QPointF(x, y);
        const int sx = std::clamp(int(std::floor(point.x())), 0, sampled.width - 1);
        const int sy = std::clamp(int(std::floor(point.y())), 0, sampled.height - 1);
        return sampled.pixels[sy * sampled.width + sx];
    };
    const double radius = std::max(1.0, number(v, 0, 84) / 2); const auto mode = v.value("selector", "Heal").toString();
    if (m_pickCloneSource) { m_cloneSource = position; m_pickCloneSource = false; emit toolStateChanged(); return true; }
    auto mask = layerMask();
    const auto adaptation = text(v, 16, "Auto");
    const double angle = (mode == "Heal" && adaptation == "Color" ? 0 : number(v, 13)) * std::numbers::pi / 180, scale = std::max(0.01, number(v, 12, 100) / 100);
    const double feather = std::clamp(number(v, 8, 62) / 100, 0.0, 1.0);
    for (int y = std::max(0, int(mappedPosition.y() - radius)); y < std::min(pixels.height, int(mappedPosition.y() + radius + 1)); ++y)
        for (int x = std::max(0, int(mappedPosition.x() - radius)); x < std::min(pixels.width, int(mappedPosition.x() + radius + 1)); ++x) {
            const double dx = x - mappedPosition.x(), dy = y - mappedPosition.y(), distance = std::hypot(dx, dy); if (distance > radius) continue;
            int sx = int((source.x() + (toggle(v, 14) ? mappedPosition.x() - start.x() : 0)) + (dx * std::cos(angle) + dy * std::sin(angle)) / scale), sy = int((source.y() + (toggle(v, 14) ? mappedPosition.y() - start.y() : 0)) + (-dx * std::sin(angle) + dy * std::cos(angle)) / scale);
            if (mode != "Clone" && text(v, 9, "Auto") == "Auto") { sx = std::clamp(int(mappedPosition.x() + radius + 2), 0, pixels.width - 1); sy = y; }
            if (text(v, 9) == "Pattern") { sx = x % std::max(1, int(radius)); sy = y % std::max(1, int(radius)); }
            sx = std::clamp(sx, 0, pixels.width - 1); sy = std::clamp(sy, 0, pixels.height - 1);
            auto target = sample(sx, sy); const auto previous = base.pixels[y * pixels.width + x];
            if (mode == "Heal" && adaptation != "Rotation") {
                int dr = 0, dg = 0, db = 0, sr = 0, sg = 0, sb = 0, count = 0;
                for (int yy = -2; yy <= 2; ++yy) for (int xx = -2; xx <= 2; ++xx) { auto d = base.pixels[std::clamp(y + yy, 0, pixels.height - 1) * pixels.width + std::clamp(x + xx, 0, pixels.width - 1)], s = sample(sx + xx, sy + yy); dr += qRed(d); dg += qGreen(d); db += qBlue(d); sr += qRed(s); sg += qGreen(s); sb += qBlue(s); ++count; }
                const double amount = number(v, 17, 68) / 100; target = qRgba(std::clamp(int(qRed(target) + (dr - sr) / double(count) * amount), 0, 255), std::clamp(int(qGreen(target) + (dg - sg) / double(count) * amount), 0, 255), std::clamp(int(qBlue(target) + (db - sb) / double(count) * amount), 0, 255), qAlpha(target));
            }
            const double coverage = std::min(1.0, (1 - distance / radius) / std::max(0.01, feather)) * mask.alpha[y * pixels.width + x] / 255;
            auto mix = [&](int a, int b) { return int(std::lround(a + (b - a) * coverage)); };
            pixels.pixels[y * pixels.width + x] = qRgba(mix(qRed(previous), qRed(target)), mix(qGreen(previous), qGreen(target)), mix(qBlue(previous), qBlue(target)), qAlpha(previous));
        }
    invalidateToolPreview(); return replaceSelectedPixels(pixels);
}
