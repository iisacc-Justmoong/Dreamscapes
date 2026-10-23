#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <QFont>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QPainterPathStroker>
#include <QPainter>
#include <QTextLayout>
#include <QTextOption>
#include <random>

using namespace iiSharedCanvas;
using namespace editorTools;
namespace {
double parameter(const QVariantMap &v, const char *key, double fallback) {
    const double value = v.value(key, fallback).toDouble();
    return std::isfinite(value) ? value : fallback;
}
QColor textColor(const QString &name, const QString &custom = "#FFFFFF") {
    QColor color(name == "Black" ? "#000000" : name == "Accent" ? "#0A84FF" : name == "Custom" ? custom : "#FFFFFF");
    return color.isValid() ? color : QColor(Qt::white);
}
QPainterPath bubblePath(const QRectF &r, const QVariantMap &v) {
    QPainterPath path;
    const auto style = text(v, 10, "Round");
    const double radius = parameter(v, "textCornerRadius", 12);
    if (style == "Sharp") path.addRect(r);
    else if (style == "Cloud") {
        path.addRoundedRect(r.adjusted(6, 6, -6, -6), radius, radius);
        const double diameter = std::max(12.0, std::min(r.width(), r.height()) / 2);
        const int count = std::max(3, int(std::ceil(r.width() / (diameter * 0.6))));
        for (int i = 0; i < count; ++i) {
            const double x = r.left() + (r.width() - diameter) * i / (count - 1);
            QPainterPath lobe; lobe.addEllipse(QRectF(x, r.top(), diameter, diameter));
            lobe.addEllipse(QRectF(x, r.bottom() - diameter, diameter, diameter)); path = path.united(lobe);
        }
    } else path.addRoundedRect(r, radius, radius);
    const auto direction = text(v, 11, "Left");
    if (direction == "None" || v.value("selector").toString() != "Callout") return path;
    const double length = parameter(v, "textPointerLength", 16), width = parameter(v, "textPointerWidth", 12);
    const double offset = std::clamp(parameter(v, "textPointerOffset", 50) / 100, 0.0, 1.0);
    QPointF center, vector;
    if (direction.contains("Top")) { center = {r.left() + r.width() * offset, r.top()}; vector = {0, -length}; }
    else if (direction.contains("Bottom")) { center = {r.left() + r.width() * offset, r.bottom()}; vector = {0, length}; }
    else { center = {direction == "Right" ? r.right() : r.left(), r.top() + r.height() * offset}; vector = {direction == "Right" ? length : -length, 0}; }
    if (direction.contains(' ') && direction.contains("left")) vector.rx() -= length;
    if (direction.contains(' ') && direction.contains("right")) vector.rx() += length;
    const QPointF tangent = vector.y() != 0 ? QPointF(width / 2, 0) : QPointF(0, width / 2);
    QPainterPath pointer; pointer.moveTo(center - tangent);
    const double round = parameter(v, "textPointerRoundness", 0) / 100;
    if (round > 0) pointer.quadTo(center + vector * (1 + round), center + tangent);
    else { pointer.lineTo(center + vector); pointer.lineTo(center + tangent); }
    pointer.closeSubpath(); return path.united(pointer);
}
}

bool EditorCanvas::createText(const QPointF &position) {
    if (!documentReady()) return fail(tr("Open a canvas first."));
    const auto id = QString::fromStdString(unique("text.layer."));
    m_textOrigins[id] = position; m_textSettings[id] = m_toolValues;
    const auto assetId = unique("text.asset.");
    if (!commit([&](Document &d) {
        d.assets.emplace_back(VectorAsset{assetId, editingExtent(), {}});
        d.layers.emplace_back(StaticVectorLayer{{id.toStdString(), "Text"}, StaticSource{assetId}}); return true;
    })) return false;
    if (!selectLayer(id)) return false;
    return updateText();
}

bool EditorCanvas::updateText() {
    const auto id = selectedLayerId();
    if (!m_textOrigins.contains(id)) return true;
    const auto v = m_toolValues;
    if (v.value("textBlend").toString() == "Soft light" || v.value("textBlend").toString() == "Difference") return fail(tr("This blend mode is unavailable in the current canvas renderer."));
    if (v.value("textNoisePattern").toString() == "Blue noise") return fail(tr("Blue noise is unavailable in the current canvas renderer."));
    if (v.value("textHorizontalConstraint").toString() == "Scale" || v.value("textVerticalConstraint").toString() == "Scale") return fail(tr("Proportional text constraints are unavailable in the current canvas renderer."));
    const auto usage = v.value("selector", "Free Text").toString();
    QString content = text(v, 0, "Enter your text…");
    if (usage == "Footer" && text(v, 13) == "Page no.") content = QString::number(m_specification.value("pageNumber", 1).toInt());
    else if (usage == "Footer" && text(v, 13) == "Metadata") content = documentName();
    // An empty editable text layer is valid while the user replaces its content.
    QFont font(text(v, 1, "Pretendard") == "Serif" ? "serif" : text(v, 1) == "Mono" ? "monospace" : "Pretendard");
    const auto weight = v.value("textWeight", "Medium").toString();
    font.setWeight(weight == "Bold" ? QFont::Bold : weight == "SemiBold" ? QFont::DemiBold : weight == "Regular" ? QFont::Normal : QFont::Medium);
    const double size = std::clamp(number(v, 2, 13), 1.0, 512.0);
    font.setPixelSize(int(std::round(size))); font.setLetterSpacing(QFont::AbsoluteSpacing, number(v, 8));
    QRectF reference(0, 0, editingWidth(), editingHeight());
    if (v.value("textReference").toString() == "Selection") {
        if (m_selection.alpha.empty()) return fail(tr("Create a selection to place text relative to it."));
        int left = m_selection.extent.width, right = -1, top = m_selection.extent.height, bottom = -1;
        for (int y = 0; y < m_selection.extent.height; ++y) for (int x = 0; x < m_selection.extent.width; ++x)
            if (m_selection.alpha[y * m_selection.extent.width + x]) { left = std::min(left, x); right = std::max(right, x); top = std::min(top, y); bottom = std::max(bottom, y); }
        if (right < left) return fail(tr("Create a selection to place text relative to it."));
        reference = QRectF(left, top, right - left + 1, bottom - top + 1);
    } else if (v.value("textReference").toString() == "Canvas" && activeArtboard()) {
        reference = QRectF(-editingOrigin(), QSizeF(canvasWidth(), canvasHeight()));
    }
    const double padding = number(v, 12, 20), availableWidth = std::max(1.0, reference.width() - padding * 2);
    QFontMetricsF metrics(font); double naturalWidth = 1;
    for (const auto &row : content.split('\n')) naturalWidth = std::max(naturalWidth, metrics.horizontalAdvance(row));
    if (toggle(v, 9, true) && naturalWidth > availableWidth && v.value("textWidthMode", "Auto").toString() == "Auto") {
        font.setPixelSize(std::max(1, int(std::floor(size * availableWidth / naturalWidth)))); metrics = QFontMetricsF(font); naturalWidth = 1;
        for (const auto &row : content.split('\n')) naturalWidth = std::max(naturalWidth, metrics.horizontalAdvance(row));
    }
    const auto widthMode = v.value("textWidthMode", "Auto").toString();
    double boxWidth = widthMode == "Fixed" ? parameter(v, "textBoxWidth", 320) : widthMode == "Fill" ? availableWidth : std::min(naturalWidth, availableWidth);
    if (v.value("textHorizontalConstraint").toString() == "Left + right") boxWidth = availableWidth;
    const auto alignment = text(v, 3, "Left"), direction = v.value("textDirection", "Auto").toString();
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setTextDirection(direction == "RTL" ? Qt::RightToLeft : direction == "LTR" ? Qt::LeftToRight : Qt::LayoutDirectionAuto);
    option.setAlignment(alignment == "Center" ? Qt::AlignHCenter : alignment == "Justify" ? Qt::AlignJustify : alignment == "Right" || (alignment == "Leading" && direction == "RTL") || (alignment == "Trailing" && direction != "RTL") ? Qt::AlignRight : Qt::AlignLeft);
    // QTextLayout uses U+2028 for explicit paragraph breaks and shapes each run.
    QString shaped = content; shaped.replace('\n', QChar::LineSeparator);
    QTextLayout textLayout(shaped, font); textLayout.setTextOption(option);
    double lineHeight = std::max(1.0, parameter(v, "textLineHeight", size));
    const auto linePreset = v.value("textLinePreset", "Token").toString();
    if (linePreset != "Token") lineHeight = size * linePreset.chopped(1).toDouble() / 100;
    textLayout.beginLayout(); int count = 0;
    for (;;) { auto line = textLayout.createLine(); if (!line.isValid()) break; line.setLineWidth(std::max(1.0, boxWidth)); line.setPosition({0, count++ * lineHeight}); }
    textLayout.endLayout();
    QPainterPath glyphs;
    for (const auto &run : textLayout.glyphRuns()) {
        const auto indexes = run.glyphIndexes(); const auto positions = run.positions(); const auto rawFont = run.rawFont();
        for (int i = 0; i < indexes.size(); ++i) { auto path = rawFont.pathForGlyph(indexes[i]); path.translate(positions[i]); glyphs.addPath(path); }
    }
    const double textHeight = std::max(lineHeight, count * lineHeight);
    const auto heightMode = v.value("textHeightMode", "Hug").toString();
    double boxHeight = heightMode == "Fixed" ? parameter(v, "textBoxHeight", 80) : heightMode == "Fill" ? std::max(1.0, reference.height() - padding * 2) : textHeight;
    if (v.value("textVerticalConstraint").toString() == "Top + bottom") boxHeight = std::max(1.0, reference.height() - padding * 2);
    const auto vertical = v.value("textVerticalAlignment", "Middle").toString();
    glyphs.translate(0, vertical == "Bottom" ? boxHeight - textHeight : vertical == "Middle" ? (boxHeight - textHeight) / 2 : 0);
    QPointF origin = m_textOrigins[id];
    const auto placement = text(v, 5, "Bottom");
    if (v.value("textLayoutEnabled", false).toBool()) {
        const bool left = placement.contains("left") || placement == "Left", right = placement.contains("right") || placement == "Right";
        origin.setX(left ? reference.left() + padding : right ? reference.right() - boxWidth - padding : reference.center().x() - boxWidth / 2);
        origin.setY(placement.startsWith("Top") ? reference.top() + padding : placement.startsWith("Bottom") ? reference.bottom() - boxHeight - number(v, 14, 32) : reference.center().y() - boxHeight / 2);
        origin += QPointF(parameter(v, "textOffsetX", 0), parameter(v, "textOffsetY", 32) - 32);
    } else {
        origin.rx() -= alignment == "Center" ? boxWidth / 2 : alignment == "Right" ? boxWidth : 0;
        if (usage == "Caption") origin.setY(placement == "Top" ? padding : placement == "Center" ? (editingHeight() - boxHeight) / 2 : editingHeight() - boxHeight - number(v, 14, 32));
        if (usage == "Footer") origin.setY(editingHeight() - boxHeight - number(v, 14, 32));
    }
    glyphs.translate(origin);
    m_textBaselines[id].clear();
    const auto verticalOffset = vertical == "Bottom" ? boxHeight-textHeight : vertical == "Middle" ? (boxHeight-textHeight)/2 : 0;
    for(int i=0;i<textLayout.lineCount();++i){const auto line=textLayout.lineAt(i);const auto y=origin.y()+verticalOffset+line.y()+line.ascent();m_textBaselines[id].append(QLineF(origin.x(),y,origin.x()+line.naturalTextWidth(),y));}
    // The box clips fixed-height text, preserving the user's editable string.
    if (heightMode == "Fixed") { QPainterPath clip; clip.addRect(QRectF(origin, QSizeF(boxWidth, boxHeight))); glyphs = glyphs.intersected(clip); }
    std::vector<VectorPath> paths;
    QPainterPath bubble;
    if (usage == "Caption" || usage == "Callout") {
        bubble = bubblePath(QRectF(origin, QSizeF(boxWidth, boxHeight)).adjusted(-padding, -padding, padding, padding), v);
        paths.push_back(nativePath(bubble, std::uint32_t(std::clamp(int(std::round(number(v, 6, 72) * 2.55)), 0, 255)) << 24));
    }
    const auto fill = v.contains("textFill") ? textColor(v.value("textFill").toString(), v.value("textCustomFill").toString()) : brushColor();
    QColor textFill = fill; textFill.setAlphaF(std::clamp(parameter(v, "textOpacity", 100) / 100, 0.0, 1.0));
    auto textPath = nativePath(glyphs, textFill.rgba());
    const double strokeWidth = parameter(v, "textStrokeWidth", 0);
    if (strokeWidth > 0) { auto stroke = textColor(v.value("textStrokeColor", "White").toString()); stroke.setAlphaF(std::clamp(parameter(v, "textStrokeOpacity", 100) / 100, 0.0, 1.0)); textPath.stroke = StrokeStyle{{stroke.rgba()}, strokeWidth}; }
    if (!glyphs.isEmpty()) paths.push_back(std::move(textPath));
    const double blur = parameter(v, "textBlur", 0), noise = parameter(v, "textNoise", 0), feather = parameter(v, "textFeather", 0);
    const bool shadow = v.value("textShadow", false).toBool();
    const bool rasterEffects = shadow || blur > 0 || noise > 0 || feather > 0 || parameter(v, "textSoftness", 0) > 0 || parameter(v, "textGamma", 1) != 1 || parameter(v, "textRangeMin", 0) != 0 || parameter(v, "textRangeMax", 100) != 100 || parameter(v, "textBrightness", 100) != 100 || parameter(v, "textContrast", 100) != 100 || parameter(v, "textSaturation", 100) != 100 || parameter(v, "textHue", 0) != 0;
    RasterLayer pixels;
    if (rasterEffects) {
        Document sample; sample.extent = editingExtent(); sample.assets.emplace_back(VectorAsset{"text-preview", editingExtent(), paths}); sample.layers.emplace_back(StaticVectorLayer{{"text-preview", "Text"}, StaticSource{"text-preview"}});
        const auto rendered = renderFrame(sample, 0); if (!rendered.ok()) return fail(tr("Text could not be rendered.")); pixels = rendered.pixels;
        if (shadow) {
            QPainterPath shadowPath = bubble.isEmpty() ? glyphs : bubble;
            const double spread = parameter(v, "textShadowSpread", 0);
            if (spread > 0) { QPainterPathStroker stroker; stroker.setWidth(spread * 2); shadowPath = shadowPath.united(stroker.createStroke(shadowPath)); }
            shadowPath.translate(parameter(v, "textShadowX", 0), parameter(v, "textShadowY", 4));
            sample.assets = {VectorAsset{"text-preview", editingExtent(), {nativePath(shadowPath, std::uint32_t(std::clamp(int(parameter(v, "textShadowOpacity", 24) * 2.55), 0, 255)) << 24)}}};
            auto shadowPixels = renderFrame(sample, 0).pixels;
            RasterEffect effect; effect.kind = RasterEffectKind::GaussianBlur; effect.radius = parameter(v, "textShadowBlur", 12); effect.amount = 1;
            if (effect.radius > 0) { const auto result = effectRaster(shadowPixels, effect); if (!result.ok()) return fail(QString::fromStdString(result.error)); shadowPixels = result.pixels; }
            auto combined = image(shadowPixels); QPainter painter(&combined); painter.drawImage(0, 0, image(pixels)); painter.end(); pixels = raster(combined);
        }
        const double radius = std::max({blur, feather, parameter(v, "textSoftness", 0) / 10});
        if (radius > 0) { RasterEffect effect; effect.kind = RasterEffectKind::GaussianBlur; effect.radius = radius; effect.amount = 1; auto result = effectRaster(pixels, effect); if (!result.ok()) return fail(QString::fromStdString(result.error)); pixels = std::move(result.pixels); }
        // Color/range controls are deterministic and affect document/export pixels.
        const double brightness = parameter(v, "textBrightness", 100) / 100, contrast = parameter(v, "textContrast", 100) / 100, saturation = parameter(v, "textSaturation", 100) / 100, gamma = std::max(0.01, parameter(v, "textGamma", 1));
        const double low = parameter(v, "textRangeMin", 0) / 100, high = std::max(low + 0.0001, parameter(v, "textRangeMax", 100) / 100), hue = parameter(v, "textHue", 0) / 360;
        const auto channel = v.value("textRangeChannel", "Luminance").toString();
        std::mt19937 random(std::uint32_t(parameter(v, "textNoiseSeed", 0))); std::uniform_real_distribution<double> uniform(-1, 1); std::normal_distribution<double> gaussian(0, 0.35);
        const int noiseScale = std::max(1, int(parameter(v, "textNoiseScale", 1))); double noiseValue = 0;
        for (int y = 0; y < pixels.height; ++y) for (int x = 0; x < pixels.width; ++x) {
            auto &pixel = pixels.pixels[y * pixels.width + x]; if (qAlpha(pixel) == 0) continue;
            QColor color = QColor::fromRgba(pixel); double h = color.hslHueF(), s = color.hslSaturationF(), l = color.lightnessF(), a = color.alphaF();
            color.setHslF(std::fmod(std::max(0.0, h) + hue + 1, 1.0), std::clamp(s * saturation, 0.0, 1.0), l, a);
            double r = color.redF(), g = color.greenF(), b = color.blueF();
            const auto adjust = [&](double c) { return std::pow(std::clamp(((c * brightness - 0.5) * contrast + 0.5), 0.0, 1.0), 1 / gamma); };
            r = adjust(r); g = adjust(g); b = adjust(b);
            if (channel == "Alpha") a = std::clamp((a - low) / (high - low), 0.0, 1.0);
            else if (channel == "Color") { r = std::clamp((r - low) / (high - low), 0.0, 1.0); g = std::clamp((g - low) / (high - low), 0.0, 1.0); b = std::clamp((b - low) / (high - low), 0.0, 1.0); }
            else { const double luminance = r * 0.2126 + g * 0.7152 + b * 0.0722; if (low > 0 || high < 1) a *= std::clamp((luminance - low) / (high - low), 0.0, 1.0); }
            if (noise > 0) {
                const auto pattern = v.value("textNoisePattern", "Uniform").toString();
                if (x % noiseScale == 0 || x == 0) noiseValue = pattern == "Gaussian" ? gaussian(random) : uniform(random);
                const double amount = noise / 100; r += noiseValue * amount; g += (v.value("textMonochrome", true).toBool() ? noiseValue : uniform(random)) * amount; b += (v.value("textMonochrome", true).toBool() ? noiseValue : uniform(random)) * amount;
            }
            pixel = QColor::fromRgbF(std::clamp(r, 0.0, 1.0), std::clamp(g, 0.0, 1.0), std::clamp(b, 0.0, 1.0), a).rgba();
        }
    }
    if (!commit([&](Document &d) {
        auto *layer = findLayer(d, id.toStdString()); if (!layer) return false;
        const auto sourceValue = layerSource(*layer); const auto *source = std::get_if<StaticSource>(&sourceValue); if (!source) return false;
        const auto assetId = source->assetId; auto properties = layerProperties(*layer);
        auto *asset = findAsset(d, assetId); if (!asset) return false;
        if (rasterEffects) { *asset = RasterAsset{assetId, pixels}; *layer = StaticBitmapLayer{properties, StaticSource{assetId}}; }
        else { *asset = VectorAsset{assetId, editingExtent(), paths}; *layer = StaticVectorLayer{properties, StaticSource{assetId}}; }
        auto &updated = layerProperties(*layer); updated.name = ("Text: " + content.left(80)).toStdString();
        const auto blendName = v.value("textBlend", "Normal").toString(); updated.blendMode = blend(blendName == "Soft light" ? "Soft Light" : blendName);
        if (usage == "Caption") { const auto range = timeRange(text(v, 4)); if (range.first >= 0 && range.second > range.first) { const double rate = double(d.timeline.frameRate.numerator) / d.timeline.frameRate.denominator; const auto first = FrameIndex(range.first * rate), last = FrameIndex(std::ceil(range.second * rate) - 1); d.timeline.frameCount = std::max(d.timeline.frameCount, last + 1); updated.frameRange = LayerFrameRange{first, last}; } }
        else updated.frameRange.reset(); return true;
    })) return false;
    m_textSettings[id] = v; return true;
}
