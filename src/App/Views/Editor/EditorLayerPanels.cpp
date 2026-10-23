#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include "EditorColorSampler.h"
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <array>
#include <cmath>
#include <map>
#include <limits>
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
        if (activeArtboard() && board->id != m_activeArtboard) continue;
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
    int width = editingWidth(), height = editingHeight();
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
    info["canReveal"] = !filePath().isEmpty() && fileAlias().value("available").toBool();
    const auto index = std::size_t(layer - document()->layers.data());
    if (index > 0) {
        const auto &below = document()->layers[index - 1];
        const auto &p = layerProperties(below);
        const auto fullRange = [&](const LayerProperties &entry) {
            return !entry.frameRange || (entry.frameRange->firstFrame == 0
                && entry.frameRange->lastFrame == document()->timeline.frameCount - 1);
        };
        info["canMerge"] = unlocked && properties.visible && p.visible && !dynamicLayer(*layer) && !dynamicLayer(below)
            && properties.motion.empty() && p.motion.empty() && fullRange(properties) && fullRange(p)
            && !infiniteCanvas() && properties.artboardId == p.artboardId
            && !m_pixelLocks.contains(QString::fromStdString(p.id)) && !m_positionLocks.contains(QString::fromStdString(p.id));
    }
    return info;
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
    if (!documentReady()) return fail(tr("Open or create a canvas first."));
    const auto key = id.toStdString();
    if (const auto *group = findArtboard(*document(), key); group && group->visible == visible) return true;
    if (liveStrokeActive()) return fail(tr("Finish the current stroke before editing the document."));
    const auto result = editDocument([&](DocumentEditor &editor) { return editor.setArtboardVisible(key, visible); });
    if (result.ok()) clearError();
    return result.ok();
}
bool EditorCanvas::mergeSelectedDown() {
    if (!layerPanelInfo()["canMerge"].toBool()) return fail(tr("Choose two visible, unlocked static layers in the same group."));
    const auto selected = selectedLayerId().toStdString(); QString target;
    if (!commit([&](Document &d) {
        const auto index = std::size_t(findLayer(d, selected) - d.layers.data());
        auto sampled = d; for (std::size_t n = 0; n < sampled.layers.size(); ++n) layerProperties(sampled.layers[n]).visible = n == index || n + 1 == index;
        for (auto &board : sampled.artboards) { board.backgroundArgb = 0; board.visible = true; }
        const auto rendered = renderEditingFrame(sampled); if (!rendered.ok()) return false;
        auto properties = layerProperties(d.layers[index - 1]); target = QString::fromStdString(properties.id);
        properties.transform = {}; properties.opacity = 1; properties.blendMode = RasterBlendMode::SourceOver; properties.motion.clear(); properties.frameRange.reset();
        if (!activeArtboard() && properties.artboardId) for (const auto &board : d.artboards) if (board.id == *properties.artboardId) { properties.transform.translationX = -board.region.origin.x; properties.transform.translationY = -board.region.origin.y; }
        const auto asset = unique("merge.asset."); d.assets.emplace_back(RasterAsset{asset, rendered.pixels});
        d.layers[index - 1] = StaticBitmapLayer{properties, StaticSource{asset}}; d.layers.erase(d.layers.begin() + index); return true;
    })) return false;
    return selectLayer(target);
}

bool EditorCanvas::beginLayerColorSample() {
    if (!documentReady() || infiniteCanvas() || liveStrokeActive()) return fail(tr("Open a finite canvas and finish the current stroke to sample a color."));
    retireColorRead(); m_colorReadError.clear(); m_layerColorPicking = true; emit toolStateChanged(); return true;
}
void EditorCanvas::cancelLayerColorSample() {
    if (!m_layerColorPicking && m_colorRead != ColorRead::Layer) return;
    retireColorRead(); emit toolStateChanged();
}
bool EditorCanvas::retireColorRead() {
    const bool changed = m_colorRead != ColorRead::None || m_layerColorPicking || m_pickNeutral;
    m_colorSampler->cancel(); m_colorReadToken = 0; m_colorRead = ColorRead::None;
    m_layerColorPicking = m_pickNeutral = false; m_neutralReadSettings.clear();
    return changed;
}
bool EditorCanvas::sampleLayerColor(const QPointF &position) {
    return requestColorRead(position, ColorRead::Layer);
}
bool EditorCanvas::requestColorRead(const QPointF &position, ColorRead purpose) {
    const auto reject = [this](const QString &message) { m_colorReadError = message; emit toolStateChanged(); return false; };
    if (!documentReady() || infiniteCanvas() || liveStrokeActive() || !std::isfinite(position.x()) || !std::isfinite(position.y())
        || position.x() < 0 || position.y() < 0 || position.x() >= editingWidth() || position.y() >= editingHeight())
        return reject(tr("Choose a point inside a finite canvas after finishing the current stroke."));
    const auto world = QPointF(std::floor(position.x()), std::floor(position.y())) + editingOrigin();
    const auto fits = [](qreal value) { return std::isfinite(value) && value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max(); };
    if (!fits(world.x()) || !fits(world.y())) return reject(tr("The color sample is outside the canvas coordinate range."));
    try {
        EditorColorSampler::Request input;
        input.snapshot = editingFrameSnapshot(); input.worldPosition = {std::int32_t(world.x()), std::int32_t(world.y())};
        input.visibility.artboards.reserve(document()->artboards.size()); input.visibility.layers.reserve(document()->layers.size());
        for (const auto &board : document()->artboards) input.visibility.artboards.push_back(board.visible);
        for (const auto &layer : document()->layers) input.visibility.layers.push_back(layerProperties(layer).visible);
        m_colorReadFrame = input.snapshot.frame; m_colorReadArtboard = input.snapshot.artboardId;
        m_colorReadToken = m_colorSampler->submit(std::move(input));
    } catch (const std::exception &error) {
        retireColorRead(); return reject(QString::fromUtf8(error.what()));
    }
    m_colorRead = purpose; m_colorReadRevision = revision(); m_colorReadLayer = selectedLayerId();
    m_colorReadPosition = position; m_colorReadError.clear(); m_layerColorPicking = m_pickNeutral = false;
    m_neutralReadSettings = purpose == ColorRead::Neutral ? settingsFor("color") : QVariantMap{};
    emit toolStateChanged(); return true;
}
void EditorCanvas::finishColorRead(qulonglong token, quint32 argb, const QString &error, bool cancelled) {
    if (!token || token != m_colorReadToken) return;
    const auto purpose = m_colorRead;
    const bool eligible = documentReady() && revision() == m_colorReadRevision && !liveStrokeActive()
        && (purpose != ColorRead::Neutral || (frame() == m_colorReadFrame && editingArtboardId() == m_colorReadArtboard && selectedLayerId() == m_colorReadLayer));
    m_colorRead = ColorRead::None; m_colorReadToken = 0;
    m_colorReadError = error;
    if (!eligible || cancelled || !error.isEmpty()) { m_neutralReadSettings.clear(); emit toolStateChanged(); return; }
    if (purpose == ColorRead::Layer) {
        m_layerSampleColor = QColor::fromRgba(argb); m_layerColorValid = true; ++m_layerSampleSequence;
        m_layerSampleFrame = m_colorReadFrame; m_layerSampleArtboard = m_colorReadArtboard; m_layerSamplePosition = m_colorReadPosition;
        emit toolStateChanged(); return;
    }
    if (purpose == ColorRead::Neutral) {
        m_settings["color"] = std::move(m_neutralReadSettings);
        const QVariantMap updates{{"field-24", "Relative"}, {"field-25", (qBlue(argb) - qRed(argb)) / 255.0 * 6000},
            {"field-28", (qGreen(argb) - (qRed(argb) + qBlue(argb)) / 2.0) / 255 * 100}};
        if (m_tool == "color") { m_toolValues = m_settings["color"]; for (auto it = updates.cbegin(); it != updates.cend(); ++it) m_toolValues[it.key()] = it.value(); }
        // Temperature and tint describe one accepted white-balance command.
        // One processing pass and durable edit replace two sequential writes.
        const QPointer<EditorCanvas> guard(this);
        const bool applied = applyPixelFields("color", updates, "field-25");
        if (guard && !applied) emit toolStateChanged();
    }
}
QVariantMap EditorCanvas::layerColorSample() const {
    return {{"valid", m_layerColorValid}, {"picking", m_layerColorPicking}, {"pending", m_colorRead == ColorRead::Layer}, {"error", m_colorReadError},
        {"sequence", m_layerSampleSequence}, {"frame", m_layerSampleFrame}, {"artboard", QString::fromStdString(m_layerSampleArtboard)}, {"position", m_layerSamplePosition},
        {"hex", m_layerColorValid ? m_layerSampleColor.name().toUpper() : QStringLiteral("No sample")},
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
    m_histogramShadows = shadows; m_histogramHighlights = highlights; emit toolStateChanged();
}
