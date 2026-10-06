#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <QPainter>
#include <QTransform>
using namespace iiSharedCanvas;
using namespace editorTools;

void EditorCanvas::invalidateToolPreview() { m_previewTool.clear(); m_previewLayer.clear(); m_touchedFields.clear(); m_previewPixels = {}; }
QVariantMap EditorCanvas::settingsFor(const QString &tool) const { return m_settings.value(tool); }
bool EditorCanvas::rasterizeSelected() {
    if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("Unlock the selected layer's pixels first."));
    if (selectedRasterPixels()) return true;
    const auto id = selectedLayerId(); const auto *layer = documentReady() ? findLayer(*document(), id.toStdString()) : nullptr;
    if (!layer) return fail(tr("Select a layer first."));
    const auto index = std::size_t(layer - document()->layers.data());
    auto single = *document(); for (auto &entry : single.layers) layerProperties(entry).visible = false;
    layerProperties(single.layers[index]).visible = true; layerProperties(single.layers[index]).opacity = 1;
    const auto rendered = renderFrame(single, frame()); if (!rendered.ok()) return fail(QString::fromStdString(rendered.message));
    const auto assetId = unique("rasterized.asset.");
    if (!commit([&](Document &d) { auto *selected = findLayer(d, id.toStdString()); auto properties = layerProperties(*selected); properties.transform = {};
        d.assets.emplace_back(RasterAsset{assetId, rendered.pixels}); *selected = BitmapLayer{properties, StaticSource{assetId}}; return true; })) return false;
    return selectLayer(id);
}
bool EditorCanvas::applyPixelTool(const QString &tool, const QString &field, const QVariant &value) {
    if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("The selected layer's pixels are locked."));
    if (!rasterizeSelected()) return false;
    if (m_previewTool != tool || m_previewLayer != selectedLayerId() || m_previewRevision != revision()) {
        m_previewPixels = *selectedRasterPixels(); m_previewTool = tool; m_previewLayer = selectedLayerId(); m_touchedFields.clear();
    }
    m_touchedFields.insert(field); auto v = m_settings.value(tool); v[field] = value; m_settings[tool] = v;
    const auto changed = [&](int n) { return m_touchedFields.contains(key(n)); };
    const auto n = [&](int id, double neutral = 0.0) { return changed(id) ? number(v, id) : neutral; };
    RasterProcessResult result{m_previewPixels, {}};
    if (tool == "color") {
        ColorAdjustment a;
        a.exposure = n(1); a.offset = n(2); a.protectHighlights = toggle(v, 3);
        a.contrast = n(4) / 100; a.pivot = number(v, 5, 50) / 100; a.linearLight = toggle(v, 6);
        a.highlights = n(8) / 100; a.highlightRange = number(v, 9, 72) / 100; a.rolloff = changed(8) || changed(10) ? number(v, 10) / 100 : 0;
        a.preserveColor = toggle(v, 11, true); a.shadows = n(12) / 100; a.shadowRange = number(v, 13, 38) / 100;
        a.blackProtection = number(v, 14) / 100; a.adaptive = toggle(v, 15);
        a.whites = n(16) / 100; a.whiteClip = number(v, 17, 100) / 100; a.blacks = n(20) / 100;
        a.blackClip = number(v, 21) / 100; a.liftBlacks = toggle(v, 22);
        const auto illuminant = text(v, 26, "Daylight"); const double reference = illuminant == "Tungsten" ? 3200 : illuminant == "Shade" ? 7500 : 5600;
        a.temperature = changed(25) || changed(26) || changed(24) ? text(v, 24, "Kelvin") == "Relative" ? number(v, 25) / 10000 : (number(v, 25, reference) - reference) / 6000 : 0;
        a.tint = n(28) / 100; a.skinProtection = toggle(v, 30) ? number(v, 33, 68) / 100 : 0;
        a.vibrance = n(32) / 100; a.lowSaturationBias = number(v, 34, 100) / 100; a.gamutLimit = toggle(v, 35, true);
        a.saturation = n(36) / 100; a.saturationChannel = text(v, 37) == "Red" ? 0 : text(v, 37) == "Blue" ? 2 : -1;
        a.channelSaturation = n(38) / 100; if (a.saturationChannel < 0) a.saturation += a.channelSaturation;
        a.colorize = toggle(v, 39) && (changed(39) || changed(42) || changed(43)); a.gradeHue = number(v, 42, 214) / 360;
        a.gradeBalance = n(43) / 100; a.gradeRange = text(v, 41) == "Shadows" ? 0 : text(v, 41) == "Highlights" ? 2 : 1;
        a.curveChannel = text(v, 45) == "Red" ? 0 : text(v, 45) == "Green" ? 1 : text(v, 45) == "Blue" ? 2 : -1;
        a.curve = v.value("curveAmount", 0).toDouble() / 100; a.smoothCurve = text(v, 46, "Smooth") == "Smooth";
        result = adjustRaster(m_previewPixels, a);
    } else if (tool == "effects") {
        auto apply = [&](RasterEffect e) { if (!result.ok()) return; result = effectRaster(result.pixels, e); };
        if (changed(1) || changed(2) || changed(3)) {
            ColorAdjustment preset; const auto type = text(v, 1, "Featured");
            const double amount = number(v, 2, 72) / 100;
            if (type == "B&W") preset.saturation = -amount;
            else { preset.temperature = (type == "Film" ? 0.4 : -0.2) * amount; preset.contrast = 0.2 * amount; preset.vibrance = 0.1 * amount; }
            preset.skinProtection = toggle(v, 3) ? 0.8 : 0; result = adjustRaster(result.pixels, preset);
        }
        if (changed(4) || changed(5) || changed(6) || changed(7)) { RasterEffect e; e.kind = text(v, 4, "Gaussian") == "Motion" ? RasterEffectKind::MotionBlur : text(v, 4) == "Lens" ? RasterEffectKind::LensBlur : RasterEffectKind::GaussianBlur; e.radius = std::min(256.0, number(v, 5, 18)); e.angle = number(v, 6); e.edgeAware = toggle(v, 7); apply(e); }
        if (changed(8) || changed(9) || changed(10) || changed(11)) { RasterEffect e; e.kind = RasterEffectKind::Texture; e.amount = number(v, 8) / 100; e.radius = std::max(1.0, number(v, 9, 42) / 10); e.threshold = toggle(v, 11) ? 0.2 : 0; apply(e); }
        if (changed(12) || changed(13) || changed(14) || changed(15)) { RasterEffect e; e.kind = RasterEffectKind::Clarity; e.amount = number(v, 12) / 100; e.radius = number(v, 13, 36); e.natural = toggle(v, 14); e.threshold = number(v, 15) / 100; apply(e); }
        if (changed(16) || changed(17) || changed(18)) { RasterEffect e; e.kind = RasterEffectKind::Dehaze; e.amount = number(v, 16) / 100; e.detail = number(v, 17, 58) / 100; apply(e); if (toggle(v, 18)) { ColorAdjustment a; a.vibrance = e.amount * 0.25; result = adjustRaster(result.pixels, a); } }
        if (changed(20) || changed(21) || changed(22) || changed(23)) { RasterEffect e; e.kind = RasterEffectKind::Vignette; e.amount = number(v, 20) / 100; e.midpoint = number(v, 21, 46) / 100; e.roundness = number(v, 22, 18) / 100; e.feather = number(v, 23, 72) / 100; apply(e); }
        if (changed(24) || changed(25) || changed(26) || changed(27)) { RasterEffect e; e.kind = RasterEffectKind::Grain; e.amount = number(v, 24) / 100 * (0.5 + number(v, 26, 58) / 100); e.scale = std::max(1.0, number(v, 25, 32) / 10); e.monochromatic = text(v, 27, "Film") != "Digital"; e.seed = 29; apply(e); }
        if (changed(28) || changed(29) || changed(30) || changed(31)) { RasterEffect e; e.kind = RasterEffectKind::Sharpen; e.amount = number(v, 28) / 100; e.radius = number(v, 29, 1); e.detail = number(v, 30, 28) / 100; e.threshold = number(v, 31, 42) / 100; apply(e); }
        if (changed(32) || changed(33) || changed(34) || changed(35)) { RasterEffect e; e.kind = text(v, 32, "Gaussian") == "Film" ? RasterEffectKind::Grain : RasterEffectKind::Noise; e.amount = number(v, 33) / 100; e.scale = number(v, 34, 1); e.monochromatic = toggle(v, 35, true); e.seed = text(v, 32) == "Poisson" ? 71 : 13; apply(e); }
        if (changed(36) || changed(37) || changed(38)) { RasterEffect e; e.kind = RasterEffectKind::LuminanceDenoise; e.amount = number(v, 36) / 100; e.radius = 1 + number(v, 38) / 25; e.detail = number(v, 37, 54) / 100; apply(e); }
        if (changed(40) || changed(41) || changed(42) || changed(43)) { RasterEffect e; e.kind = RasterEffectKind::ColorDenoise; e.amount = number(v, 40) / 100 * (toggle(v, 43, true) ? 1 : 0.5); e.radius = 1 + number(v, 42, 56) / 25; e.detail = number(v, 41, 48) / 100; apply(e); }
        if (changed(45) && number(v, 45) != 0) { RasterEffect e; e.kind = RasterEffectKind::LensDistortion; e.amount = number(v, 45) / 100; apply(e); }
        if (changed(46) && toggle(v, 46)) { RasterEffect e; e.kind = RasterEffectKind::ChromaticAberration; e.amount = -0.5; e.radius = 2; apply(e); }
        if (changed(47)) { RasterEffect e; e.kind = RasterEffectKind::Vignette; e.amount = number(v, 47) / 100; apply(e); }
    } else if (tool == "auto-enhance") {
        const double strength = number(v, field == "field-3" ? 1 : 5, 70) / 100;
        if (field == "field-3") {
            double mean = 0; for (auto p : m_previewPixels.pixels) mean += (0.2126 * qRed(p) + 0.7152 * qGreen(p) + 0.0722 * qBlue(p)) / 255;
            mean /= std::max<std::size_t>(1, m_previewPixels.pixels.size()); ColorAdjustment a;
            a.exposure = std::clamp(std::log2(0.5 / std::max(0.01, mean)), -2.0, 2.0) * strength; a.protectHighlights = toggle(v, 2);
            result = adjustRaster(result.pixels, a);
        } else if (field == "field-7") {
            double r = 0, g = 0, b = 0; for (auto p : m_previewPixels.pixels) { r += qRed(p); g += qGreen(p); b += qBlue(p); }
            const auto sum = std::max(1.0, r + g + b); ColorAdjustment a; a.temperature = (b - r) / sum * strength * 4; a.tint = (g - (r + b) / 2) / sum * strength * 4; a.skinProtection = toggle(v, 6) ? 0.8 : 0;
            result = adjustRaster(result.pixels, a);
        } else if (field == "field-11") { RasterEffect e; e.kind = RasterEffectKind::LuminanceDenoise; e.amount = number(v, 9, 44) / 100; e.radius = 2; result = effectRaster(result.pixels, e); e.kind = RasterEffectKind::Sharpen; e.amount = number(v, 8, 62) / 100; e.natural = toggle(v, 10); result = effectRaster(result.pixels, e); }
    }
    if (!result.ok()) return fail(QString::fromStdString(result.error));
    const auto mask = layerMask(); if (!mask.alpha.empty()) { result = blendRaster(m_previewPixels, result.pixels, mask); if (!result.ok()) return fail(QString::fromStdString(result.error)); }
    if (!replaceSelectedPixels(result.pixels)) return fail(lastError()); m_previewRevision = revision(); clearError(); emit toolStateChanged(); return true;
}
bool EditorCanvas::updateBackground() {
    const auto v = settingsFor("background"); const QColor color(text(v, 1, "#12131A")); if (!color.isValid()) return fail(tr("Enter a valid background hex color."));
    RasterFill f; f.firstArgb = color.rgba(); f.secondArgb = color.lighter(180).rgba(); f.opacity = std::clamp(number(v, 2, 100) / 100, 0.0, 1.0);
    f.kind = text(v, 0, "Solid") == "Linear" ? RasterFillKind::Linear : text(v, 0) == "Radial" ? RasterFillKind::Radial : RasterFillKind::Solid;
    auto filled = fillRaster(document()->extent, f); if (!filled.ok()) return fail(QString::fromStdString(filled.error));
    return commit([&](Document &d) { const std::string id = "canvas.background";
        if (auto *asset = findAsset(d, id)) *asset = RasterAsset{id, filled.pixels}; else d.assets.emplace_back(RasterAsset{id, filled.pixels});
        if (auto *layer = findLayer(d, "canvas.background.layer")) *layer = BitmapLayer{{"canvas.background.layer", "Background"}, StaticSource{id}};
        else d.layers.insert(d.layers.begin(), BitmapLayer{{"canvas.background.layer", "Background"}, StaticSource{id}}); return true; });
}
bool EditorCanvas::fillAt(const QPointF &position) {
    if (m_toolValues.value("selector").toString() == "Generative") {
        if (!documentReady()) return fail(tr("Open a canvas first."));
        const auto rendered = renderFrame(*document(), frame());
        if (!rendered.ok()) return fail(QString::fromStdString(rendered.message));
        const auto selected = floodMask(rendered.pixels, {int(std::floor(position.x())), int(std::floor(position.y()))}, 0.12, true);
        if (!selected.ok()) return fail(QString::fromStdString(selected.error));
        m_selection = selected.mask; updateSelectionOverlay(); clearError(); return true;
    }
    if (!documentReady()) return fail(tr("Open a canvas first."));
    if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("Unlock the selected layer's pixels first."));
    if (!rasterLayerSelected() && !addPaintLayer()) return false;
    const auto v = settingsFor("fill"); const auto mode = v.value("selector", "Solid").toString();
    if (mode == "Generative") return fail(tr("Use Generate fill to generate the selected region."));
    const QColor color(text(v, 1, "#8B7CFF")); if (!color.isValid()) return fail(tr("Enter a valid fill hex color."));
    RasterFill f; f.firstArgb = color.rgba(); f.secondArgb = color.lighter(180).rgba(); f.opacity = std::clamp(number(v, 2, 100) / 100, 0.0, 1.0);
    f.kind = mode == "Pattern" ? RasterFillKind::Pattern : mode == "Gradient" ? text(v, 4, "Linear") == "Radial" ? RasterFillKind::Radial : text(v, 4) == "Angular" ? RasterFillKind::Angular : RasterFillKind::Linear : RasterFillKind::Solid;
    f.angle = mode == "Pattern" ? number(v, 10) : number(v, 6); f.scale = std::max(1.0, number(v, 9, 82) / 4); f.dither = toggle(v, 7); f.seamless = toggle(v, 11, true);
    if (!selectedRasterPixels() && !rasterizeSelected()) return false;
    const auto *source = selectedRasterPixels(); if (!source) return fail(tr("Select a raster layer to fill."));
    auto filled = fillRaster({source->width, source->height}, f); if (!filled.ok()) return fail(QString::fromStdString(filled.error));
    auto mask = layerMask();
    if (m_selection.alpha.empty()) { const auto *layer = findLayer(*document(), selectedLayerId().toStdString()); const auto &t = layerProperties(*layer).transform;
        const auto point = QTransform(t.m11, t.m12, t.m21, t.m22, t.translationX, t.translationY).inverted().map(position);
        auto wand = floodMask(*source, {int(point.x()), int(point.y())}, 0.05, true); if (wand.ok()) mask = std::move(wand.mask); }
    QImage composited = image(*source); QPainter painter(&composited);
    const auto blendMode = text(v, 3, "Normal"); painter.setCompositionMode(blendMode == "Multiply" ? QPainter::CompositionMode_Multiply : blendMode == "Screen" ? QPainter::CompositionMode_Screen : blendMode == "Overlay" ? QPainter::CompositionMode_Overlay : QPainter::CompositionMode_SourceOver);
    painter.drawImage(0, 0, image(filled.pixels)); painter.end();
    auto blended = blendRaster(*source, raster(composited), mask); if (!blended.ok()) return fail(QString::fromStdString(blended.error));
    const auto result = replaceSelectedPixels(blended.pixels); invalidateToolPreview(); return result;
}
