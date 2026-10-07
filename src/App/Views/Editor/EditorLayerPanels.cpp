#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <array>
#include <cmath>
#include <map>
#include <type_traits>

using namespace iiSharedCanvas;
using namespace editorTools;

namespace {
bool dynamicLayer(const Layer &layer) {
    return std::visit([](const auto &entry) {
        using T = std::decay_t<decltype(entry)>;
        if constexpr (std::is_same_v<T, DynamicBitmapLayer> || std::is_same_v<T, DynamicVectorLayer> || std::is_same_v<T, VideoLayer>) return true;
        else if constexpr (requires { entry.source; }) return std::holds_alternative<KeyframedSource>(entry.source);
        else return false;
    }, layer);
}
QString contentLabel(const Layer &layer) {
    const auto kind = contentKind(layer);
    if (kind == ContentKind::IpAdapter) return QStringLiteral("Embedding");
    return (dynamicLayer(layer) ? QStringLiteral("Dynamic ") : QStringLiteral("Static "))
        + (kind == ContentKind::Vector ? QStringLiteral("vector") : QStringLiteral("bitmap"));
}
}

QVariantList EditorCanvas::layerHierarchy() const {
    QVariantList rows;
    if (!documentReady()) return rows;
    const auto append = [&](const Layer &layer, int depth) {
        const auto &p = layerProperties(layer);
        const auto id = QString::fromStdString(p.id);
        const auto kind = contentKind(layer);
        rows.append(QVariantMap{{"key", id}, {"layerId", id}, {"label", QString::fromStdString(p.name) + " · " + contentLabel(layer).toLower()},
            {"kind", kind == ContentKind::Vector ? "Vectorize" : "ImageGutter14X14"}, {"depth", depth}, {"group", false},
            {"visible", p.visible}, {"locked", m_pixelLocks.contains(id) || m_positionLocks.contains(id)},
            {"selected", id == selectedLayerId()}, {"showChevron", false}, {"expanded", true}});
    };
    for (auto board = document()->artboards.crbegin(); board != document()->artboards.crend(); ++board) {
        const auto id = QString::fromStdString(board->id);
        rows.append(QVariantMap{{"key", "group:" + id}, {"layerId", id}, {"label", QString::fromStdString(board->name)}, {"kind", "Folder"},
            {"group", true}, {"depth", 0}, {"visible", board->visible}, {"selected", false}, {"showChevron", true}, {"expanded", true}});
        for (auto layer = document()->layers.crbegin(); layer != document()->layers.crend(); ++layer)
            if (layerProperties(*layer).artboardId == board->id) append(*layer, 1);
    }
    for (auto layer = document()->layers.crbegin(); layer != document()->layers.crend(); ++layer)
        if (!layerProperties(*layer).artboardId) append(*layer, 0);
    return rows;
}

QVariantMap EditorCanvas::layerPanelInfo() const {
    QVariantMap info{{"dimensions", "—"}, {"profile", "Untagged RGB"}, {"bitDepth", "—"}, {"contentType", "—"},
        {"origin", "—"}, {"source", "—"}, {"canMerge", false}, {"canGroup", false}, {"canAdjust", false}, {"canReveal", false}};
    if (!documentReady()) return info;
    const auto *layer = findLayer(*document(), selectedLayerId().toStdString());
    if (!layer) return info;
    const auto *asset = resolveAssetAt(*document(), *layer, frame());
    const auto &properties = layerProperties(*layer);
    int width = canvasWidth(), height = canvasHeight();
    if (const auto *raster = asset ? std::get_if<RasterAsset>(asset) : nullptr) { width = raster->pixels.width; height = raster->pixels.height; info["bitDepth"] = "8 bit / channel"; }
    else if (const auto *vector = asset ? std::get_if<VectorAsset>(asset) : nullptr) { width = vector->viewport.width; height = vector->viewport.height; info["bitDepth"] = "Vector geometry"; }
    else if (const auto *video = asset ? std::get_if<VideoAsset>(asset) : nullptr; video && !video->frames.empty()) { width = video->frames.front().width; height = video->frames.front().height; info["bitDepth"] = "8 bit / channel"; }
    info["dimensions"] = QStringLiteral("%1 × %2 px").arg(width).arg(height);
    info["contentType"] = contentLabel(*layer);
    info["origin"] = asset ? "Embedded media" : "No frame source";
    info["source"] = asset ? QString::fromStdString(std::visit([](const auto &a) { return a.id; }, *asset)) : QStringLiteral("—");
    const bool unlocked = !m_pixelLocks.contains(selectedLayerId()) && !m_positionLocks.contains(selectedLayerId());
    info["canGroup"] = unlocked && !infiniteCanvas() && !properties.artboardId;
    info["canAdjust"] = unlocked && !dynamicLayer(*layer) && asset && (contentKind(*asset) == ContentKind::Raster);
    info["canReveal"] = !filePath().isEmpty() && QFileInfo::exists(filePath());
    const auto index = std::size_t(layer - document()->layers.data());
    if (index > 0) {
        const auto &below = document()->layers[index - 1];
        const auto &p = layerProperties(below);
        info["canMerge"] = unlocked && properties.visible && p.visible && !dynamicLayer(*layer) && !dynamicLayer(below)
            && properties.motion.empty() && p.motion.empty() && !properties.frameRange && !p.frameRange
            && !infiniteCanvas() && properties.artboardId == p.artboardId
            && !m_pixelLocks.contains(QString::fromStdString(p.id)) && !m_positionLocks.contains(QString::fromStdString(p.id));
    }
    return info;
}

QVariantMap EditorCanvas::layerHistogram(bool composite, const QString &channel) const {
    QVariantMap result{{"pixelCount", 0}, {"mean", 0.0}, {"standardDeviation", 0.0}};
    std::array<std::array<int, 256>, 3> bins{};
    qlonglong count = 0; double sum = 0, squares = 0;
    if (documentReady() && !infiniteCanvas() && (composite || !selectedLayerId().isEmpty())) {
        auto sampled = *document();
        if (!composite) {
            for (auto &layer : sampled.layers) layerProperties(layer).visible = layerProperties(layer).id == selectedLayerId().toStdString();
            for (auto &board : sampled.artboards) { board.backgroundArgb = 0; board.visible = true; }
        }
        const auto rendered = renderFrame(sampled, frame());
        if (rendered.ok()) for (const auto pixel : rendered.pixels.pixels) {
            if (!qAlpha(pixel)) continue;
            ++bins[0][qRed(pixel)]; ++bins[1][qGreen(pixel)]; ++bins[2][qBlue(pixel)]; ++count;
            const double value = channel == "Red" ? qRed(pixel) : channel == "Green" ? qGreen(pixel) : channel == "Blue" ? qBlue(pixel) : qGray(pixel);
            sum += value; squares += value * value;
        }
    }
    for (int index = 0; index < 3; ++index) { QVariantList values; values.reserve(256); for (const auto value : bins[index]) values.append(value); result[QStringList{"red", "green", "blue"}[index]] = values; }
    result["pixelCount"] = count;
    result["mean"] = count ? sum / count : 0;
    result["standardDeviation"] = count ? std::sqrt(std::max(0.0, squares / count - std::pow(sum / count, 2))) : 0;
    result["available"] = documentReady() && !infiniteCanvas() && (composite || !selectedLayerId().isEmpty());
    return result;
}

bool EditorCanvas::duplicateSelectedLayer() {
    const auto original = selectedLayerId().toStdString(), replacement = unique("layer.copy.");
    if (!documentReady() || !findLayer(*document(), original)) return fail(tr("Select a layer first."));
    if (!commit([&](Document &d) {
        const auto *source = findLayer(d, original); const auto index = source - d.layers.data(); auto copy = *source;
        layerProperties(copy).id = replacement; layerProperties(copy).name += " copy";
        std::map<std::string, std::string> copied;
        const auto cloneAsset = [&](std::string &id) {
            if (copied.contains(id)) { id = copied.at(id); return; }
            const auto *asset = findAsset(d, id); if (!asset) return;
            auto duplicate = *asset; const auto next = unique("asset.copy."); copied[id] = next;
            std::visit([&](auto &a) { a.id = next; }, duplicate); d.assets.emplace_back(std::move(duplicate)); id = next;
        };
        const auto cloneSource = [&](auto &content) {
            if constexpr (requires { content.assetId; }) cloneAsset(content.assetId);
            else for (auto &f : d.frames) { const auto old = f.keyframes; for (auto keyframe : old) if (keyframe.layerId == original) { keyframe.layerId = replacement; cloneAsset(keyframe.assetId); f.keyframes.emplace_back(std::move(keyframe)); } }
        };
        std::visit([&](auto &entry) { if constexpr (requires { entry.content; }) cloneSource(entry.content); else std::visit(cloneSource, entry.source); }, copy);
        d.layers.insert(d.layers.begin() + index + 1, std::move(copy)); return true;
    })) return false;
    return selectLayer(QString::fromStdString(replacement));
}

bool EditorCanvas::groupSelectedLayer() {
    if (!layerPanelInfo()["canGroup"].toBool()) return fail(tr("Select an unlocked, ungrouped layer on a finite canvas."));
    const auto id = unique("group."), selected = selectedLayerId().toStdString();
    return commit([&](Document &d) {
        DocumentEditor editor(d);
        if (!editor.insertArtboard({id, "Layer group", {{0, 0}, d.extent}, 0}).ok()) return false;
        return editor.setLayerArtboard(selected, id).ok();
    });
}
bool EditorCanvas::setLayerGroupVisible(const QString &id, bool visible) {
    return commit([&](Document &d) { DocumentEditor editor(d); return editor.setArtboardVisible(id.toStdString(), visible).ok(); });
}
bool EditorCanvas::mergeSelectedDown() {
    if (!layerPanelInfo()["canMerge"].toBool()) return fail(tr("Choose two visible, unlocked static layers in the same group."));
    const auto selected = selectedLayerId().toStdString(); QString target;
    if (!commit([&](Document &d) {
        const auto index = std::size_t(findLayer(d, selected) - d.layers.data());
        auto sampled = d; for (std::size_t n = 0; n < sampled.layers.size(); ++n) layerProperties(sampled.layers[n]).visible = n == index || n + 1 == index;
        for (auto &board : sampled.artboards) { board.backgroundArgb = 0; board.visible = true; }
        const auto rendered = renderFrame(sampled, frame()); if (!rendered.ok()) return false;
        auto properties = layerProperties(d.layers[index - 1]); target = QString::fromStdString(properties.id);
        properties.transform = {}; properties.opacity = 1; properties.blendMode = RasterBlendMode::SourceOver; properties.motion.clear(); properties.frameRange.reset();
        if (properties.artboardId) for (const auto &board : d.artboards) if (board.id == *properties.artboardId) { properties.transform.translationX = -board.region.origin.x; properties.transform.translationY = -board.region.origin.y; }
        const auto asset = unique("merge.asset."); d.assets.emplace_back(RasterAsset{asset, rendered.pixels});
        d.layers[index - 1] = StaticBitmapLayer{properties, StaticSource{asset}}; d.layers.erase(d.layers.begin() + index); return true;
    })) return false;
    return selectLayer(target);
}

bool EditorCanvas::beginLayerColorSample() {
    if (!documentReady() || infiniteCanvas()) return fail(tr("Open a finite canvas to sample a color."));
    m_layerColorPicking = true; emit toolStateChanged(); return true;
}
void EditorCanvas::cancelLayerColorSample() { if (!m_layerColorPicking) return; m_layerColorPicking = false; emit toolStateChanged(); }
bool EditorCanvas::sampleLayerColor(const QPointF &position) {
    if (!documentReady() || infiniteCanvas() || position.x() < 0 || position.y() < 0 || position.x() >= canvasWidth() || position.y() >= canvasHeight()) return fail(tr("Choose a point inside the canvas."));
    const auto rendered = renderFrame(*document(), frame()); if (!rendered.ok()) return fail(QString::fromStdString(rendered.message));
    m_layerSampleColor = QColor::fromRgba(rendered.pixels.pixels[int(position.y()) * canvasWidth() + int(position.x())]);
    m_layerColorValid = true; m_layerColorPicking = false; clearError(); emit toolStateChanged(); return true;
}
QVariantMap EditorCanvas::layerColorSample() const {
    return {{"valid", m_layerColorValid}, {"picking", m_layerColorPicking}, {"hex", m_layerColorValid ? m_layerSampleColor.name().toUpper() : QStringLiteral("No sample")},
        {"color", m_layerColorValid ? m_layerSampleColor : QColor(Qt::transparent)},
        {"rgb", m_layerColorValid ? QStringLiteral("RGB %1 · %2 · %3").arg(m_layerSampleColor.red()).arg(m_layerSampleColor.green()).arg(m_layerSampleColor.blue()) : tr("Choose Sample color, then click the canvas.")}};
}
bool EditorCanvas::copyLayerPanelDetails() {
    if (selectedLayerId().isEmpty()) return fail(tr("Select a layer first."));
    const auto info = layerPanelInfo();
    QGuiApplication::clipboard()->setText(selectedLayer()["name"].toString() + '\n' + info["dimensions"].toString() + '\n' + info["profile"].toString() + '\n' + info["bitDepth"].toString() + '\n' + info["contentType"].toString() + '\n' + info["source"].toString()); return true;
}
bool EditorCanvas::copyLayerSampleColor() {
    if (!m_layerColorValid) return fail(tr("Sample a color first."));
    QGuiApplication::clipboard()->setText(m_layerSampleColor.name().toUpper()); return true;
}
bool EditorCanvas::revealLayerSource() {
    if (!layerPanelInfo()["canReveal"].toBool()) return fail(tr("Save the document to reveal its embedded source container."));
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath()).absolutePath()));
}
void EditorCanvas::setLayerHistogramClipping(bool shadows, bool highlights) {
    if (m_histogramShadows == shadows && m_histogramHighlights == highlights) return;
    m_histogramShadows = shadows; m_histogramHighlights = highlights; m_clippingKey.clear(); m_clippingImage = QUrl{}; emit toolStateChanged();
}
