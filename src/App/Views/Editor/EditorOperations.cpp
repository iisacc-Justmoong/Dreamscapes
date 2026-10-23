#include "EditorCanvas.h"
#include "EditorMedia.h"
#include "EditorAudioPlayback.h"
#include "EditorFrameExporter.h"
#include "EditorToolUtils.h"
#include <QPointer>
#include <QPainter>
#include <QPainterPathStroker>
#include <QScopedValueRollback>
using namespace iiSharedCanvas;
using namespace editorTools;
namespace {
QPainterPath painterPath(const VectorPath &path);
constexpr std::uint64_t historyBudget = 128ULL * 1024 * 1024;
std::uint64_t patternBytes(const QMap<QString, QImage> &patterns) {
    std::uint64_t result = 0;
    for (const auto &image : patterns) result += image.sizeInBytes();
    return result;
}
std::uint64_t documentBufferBytes(const Document &document) {
    std::uint64_t result = 0;
    const auto vectorBytes = [](const auto &values) { return std::uint64_t(values.size()) * sizeof(values[0]); };
    for (const auto &asset : document.assets) std::visit([&](const auto &a) {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::is_same_v<T, RasterAsset>) result += vectorBytes(a.pixels.pixels);
        else if constexpr (std::is_same_v<T, ChunkedRasterAsset>)
            for (const auto &chunk : a.chunks) result += vectorBytes(chunk.pixels.pixels);
        else if constexpr (std::is_same_v<T, VideoAsset>)
            for (const auto &frame : a.frames) result += vectorBytes(frame.pixels);
        else if constexpr (std::is_same_v<T, DepthAsset>) result += vectorBytes(a.values);
        else if constexpr (std::is_same_v<T, LineArtAsset>) result += vectorBytes(a.coverage);
        else if constexpr (std::is_same_v<T, CannyAsset> || std::is_same_v<T, ScribbleAsset>) result += vectorBytes(a.mask);
        else if constexpr (std::is_same_v<T, NormalMapAsset>) result += vectorBytes(a.samples);
        else if constexpr (std::is_same_v<T, ShuffleAsset> || std::is_same_v<T, TileAsset> || std::is_same_v<T, ReferenceAsset>) result += vectorBytes(a.colors);
        else if constexpr (std::is_same_v<T, IpAdapterAsset>) {
            result += vectorBytes(a.conditional.values);
            if (a.unconditional) result += vectorBytes(a.unconditional->values);
        }
    }, asset);
    for (const auto &audio : document.audioAssets) result += vectorBytes(audio.samples);
    return result;
}
}
bool EditorCanvas::playAudio() {
    if (!documentReady() || document()->audioAssets.empty()) return fail(tr("Import a WAV track first."));
    const auto *audio = m_audioSource.samples.empty() ? &document()->audioAssets.back() : findAudioAsset(*document(), m_audioSource.id);
    if(!audio)return fail(tr("Choose an existing audio asset."));
    const auto owner=QPointer<EditorCanvas>(this);
    const auto id=m_media->playAsset(editingFrameSnapshot().document,audio->id);
    if(!owner)return id!=0;
    return id!=0||fail(m_media->audioPlayback()->lastRejection());
}
bool EditorCanvas::playTimelineAudio() {
    if (!documentReady()) return fail(tr("Open a canvas first."));
    const auto owner=QPointer<EditorCanvas>(this);
    const auto id=m_media->playTimeline(editingFrameSnapshot().document,frame());
    if(!owner)return id!=0;
    return id!=0||fail(m_media->audioPlayback()->lastRejection());
}
bool EditorCanvas::exportImage(const QUrl &source, const QString &format) {
    const auto owner=QPointer<EditorCanvas>(this);
    if(!m_frameExporter->prepared()&&!prepareImageExport())return false;
    if(!owner)return false;
    const bool accepted=m_frameExporter->start(source,format);
    if(!owner)return accepted;
    if(!accepted)return fail(m_frameExporter->lastRejection());
    clearError();return true;
}
bool EditorCanvas::prepareImageExport() {
    if(!documentReady())return fail(tr("Open a canvas before exporting a frame."));
    if(m_frameExporter->busy())return fail(tr("An image export is already running."));
    const auto owner=QPointer<EditorCanvas>(this);
    const bool prepared=m_frameExporter->prepare(editingFrameSnapshot(),m_specification.value("ppi",300).toDouble());
    if(!owner)return prepared;
    return prepared||fail(m_frameExporter->lastRejection());
}
void EditorCanvas::cancelImageExport(){m_frameExporter->cancelPrepared();}
QObject *EditorCanvas::frameExporter()const{return m_frameExporter.get();}
bool EditorCanvas::combineVectorPaths(const QString &operation) {
    if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("Unlock the selected layer's pixels first."));
    const auto *layer = documentReady() ? findLayer(*document(), selectedLayerId().toStdString()) : nullptr;
    const auto *asset = layer ? resolveAssetAt(*document(), *layer, frame()) : nullptr;
    const auto *vector = asset ? std::get_if<VectorAsset>(asset) : nullptr;
    if (!vector || vector->paths.size() < 2) return fail(tr("Select at least two native paths in one vector layer."));
    if (operation != "Union" && operation != "Subtract" && operation != "Intersect") return fail(tr("Choose Union, Subtract or Intersect."));
    auto updated = *vector;
    const auto footprint = [](const VectorPath &path) {
        auto result = path.fill ? painterPath(path) : QPainterPath{};
        if (path.stroke) { QPainterPathStroker stroke; stroke.setWidth(path.stroke->width); stroke.setCapStyle(Qt::RoundCap); result = result.united(stroke.createStroke(painterPath(path))); }
        return result;
    };
    auto combined = footprint(vector->paths.front());
    for (std::size_t i = 1; i < vector->paths.size(); ++i) {
        const auto next = footprint(vector->paths[i]);
        combined = operation == "Union" ? combined.united(next) : operation == "Subtract" ? combined.subtracted(next) : combined.intersected(next);
    }
    const auto &first = vector->paths.front();
    updated.paths.clear();
    const auto path = nativePath(combined, first.fill ? first.fill->argb : first.stroke ? first.stroke->paint.argb : 0xff000000);
    if (!path.commands.empty()) updated.paths.push_back(path);
    invalidateToolPreview();
    return commit([&](Document &d) { auto *current = findAsset(d, updated.id); if (!current) return false; *current = updated; return true; });
}

QVariantMap EditorCanvas::clippingState() const {
    const bool showHighlights = m_histogramHighlights || toggle(settingsFor("color"), 18);
    const bool showShadows = m_histogramShadows || toggle(settingsFor("color"), 18);
    const auto v = settingsFor("color");
    const int white = qRound(number(v, 17, 100) / 100 * 255), black = qRound(number(v, 21) / 100 * 255);
    const bool supported = documentReady() && !infiniteCanvas();
    return {{"supported", supported}, {"enabled", supported && (showHighlights || showShadows)},
            {"highlights", showHighlights}, {"shadows", showShadows}, {"white", white}, {"black", black}};
}
std::optional<RasterMask> EditorCanvas::expandedObjectMask(const RasterMask &source, double radius) const {
    if (radius <= 0 || source.alpha.empty()) return source;
    if (radius > 256 || source.extent.width < 1 || source.extent.height < 1 || std::uint64_t(source.extent.width) * source.extent.height != source.alpha.size()) return std::nullopt;
    QPainterPath area; int runs = 0;
    for (int y = 0; y < source.extent.height; ++y) {
        int x = 0;
        while (x < source.extent.width) {
            while (x < source.extent.width && source.alpha[y * source.extent.width + x] < 128) ++x;
            const int begin = x;
            while (x < source.extent.width && source.alpha[y * source.extent.width + x] >= 128) ++x;
            if (x > begin) { if (++runs > 100000) return std::nullopt; area.addRect(begin, y, x - begin, 1); }
        }
    }
    QPainterPathStroker edge; edge.setWidth(radius * 2); edge.setJoinStyle(Qt::RoundJoin); edge.setCapStyle(Qt::RoundCap);
    area = area.united(edge.createStroke(area.simplified()));
    QImage coverage(source.extent.width, source.extent.height, QImage::Format_ARGB32); coverage.fill(Qt::transparent);
    QPainter painter(&coverage); painter.setRenderHint(QPainter::Antialiasing); painter.fillPath(area, Qt::white); painter.end();
    auto result = source;
    for (int y = 0; y < coverage.height(); ++y) for (int x = 0; x < coverage.width(); ++x) result.alpha[y * coverage.width() + x] = qAlpha(coverage.pixel(x, y));
    return result;
}
bool EditorCanvas::repairSelection() {
    if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("Unlock the selected layer's pixels first."));
    if (!rasterizeSelected()) return false;
    if (m_selection.alpha.empty()) return fail(tr("Mark the repair region first."));
    const auto base = *selectedRasterPixels(); auto repaired = base; auto mask = layerMask();
    std::vector<unsigned char> known(mask.alpha.size()); std::vector<std::size_t> frontier;
    for (std::size_t i = 0; i < known.size(); ++i) known[i] = mask.alpha[i] == 0;
    if (std::ranges::none_of(known, [](auto k) { return k; })) return fail(tr("The repair selection must leave source pixels outside the selected region."));
    const auto neighbors = [&](std::size_t i) { std::array<std::size_t, 4> result{i, i, i, i}; const int x = i % base.width, y = i / base.width; if (x > 0) result[0] = i - 1; if (x + 1 < base.width) result[1] = i + 1; if (y > 0) result[2] = i - base.width; if (y + 1 < base.height) result[3] = i + base.width; return result; };
    for (std::size_t i = 0; i < known.size(); ++i) if (!known[i]) for (auto j : neighbors(i)) if (j != i && known[j]) { frontier.push_back(i); break; }
    while (!frontier.empty()) {
        std::vector<std::size_t> next;
        for (auto i : frontier) {
            double r = 0, g = 0, b = 0, a = 0, count = 0;
            for (auto j : neighbors(i)) if (j != i && known[j] == 1) { auto p = repaired.pixels[j]; r += qRed(p); g += qGreen(p); b += qBlue(p); a += qAlpha(p); ++count; }
            if (count > 0) { repaired.pixels[i] = qRgba(int(r / count), int(g / count), int(b / count), int(a / count)); known[i] = 2; }
        }
        for (auto i : frontier) if (known[i] == 2) { known[i] = 1; for (auto j : neighbors(i)) if (j != i && !known[j]) { known[j] = 3; next.push_back(j); } }
        for (auto i : next) known[i] = 0; frontier = std::move(next);
    }
    // Relax the propagated colors against their neighbors while retaining the exterior boundary.
    for (int pass = 0; pass < 12; ++pass) { auto previous = repaired;
        for (std::size_t i = 0; i < mask.alpha.size(); ++i) if (mask.alpha[i]) { int r = 0, g = 0, b = 0; for (auto j : neighbors(i)) { auto p = previous.pixels[j]; r += qRed(p); g += qGreen(p); b += qBlue(p); } repaired.pixels[i] = qRgba(r / 4, g / 4, b / 4, qAlpha(base.pixels[i])); }
    }
    const auto result = blendRaster(base, repaired, mask); invalidateToolPreview(); return result.ok() && replaceSelectedPixels(result.pixels);
}
namespace {
QPainterPath painterPath(const VectorPath &path) {
    QPainterPath result;
    for (const auto &command : path.commands) std::visit([&](const auto &c) { using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, MoveTo>) result.moveTo(c.point.x, c.point.y);
        else if constexpr (std::is_same_v<T, LineTo>) result.lineTo(c.point.x, c.point.y);
        else if constexpr (std::is_same_v<T, QuadraticTo>) result.quadTo(c.control.x, c.control.y, c.end.x, c.end.y);
        else if constexpr (std::is_same_v<T, CubicTo>) result.cubicTo(c.control1.x, c.control1.y, c.control2.x, c.control2.y, c.end.x, c.end.y);
        else result.closeSubpath();
    }, command);
    return result;
}
}
bool EditorCanvas::eraseVector(const QVariantMap &v) {
    if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("Unlock the selected layer's pixels first."));
    if (m_selection.alpha.empty()) return fail(tr("Draw the vector eraser region first."));
    const auto id = selectedLayerId(); const auto *layer = findLayer(*document(), id.toStdString());
    if (!layer || !std::holds_alternative<StaticVectorLayer>(*layer)) return fail(tr("Select a native vector layer."));
    const auto *asset = resolveAssetAt(*document(), *layer, frame()); const auto *vector = asset ? std::get_if<VectorAsset>(asset) : nullptr;
    if (!vector) return fail(tr("Select vector geometry at the current frame."));
    QPainterPath area; int runs = 0;
    for (int y = 0; y < m_selection.extent.height; ++y) { int x = 0; while (x < m_selection.extent.width) { while (x < m_selection.extent.width && m_selection.alpha[y * m_selection.extent.width + x] < 128) ++x; const int start = x; while (x < m_selection.extent.width && m_selection.alpha[y * m_selection.extent.width + x] >= 128) ++x; if (x > start) { if (++runs > 100000) return fail(tr("Use a smaller vector eraser region.")); area.addRect(start, y, x - start, 1); } } }
    const auto &t = layerProperties(*layer).transform; bool invertible = false; const auto inverse = QTransform(t.m11, t.m12, t.m21, t.m22, t.translationX, t.translationY).inverted(&invertible);
    if (!invertible) return fail(tr("The selected vector transform cannot be inverted.")); area = inverse.map(area).simplified();
    auto updated = *vector; updated.paths.clear(); const auto mode = text(v, 8, "Trim");
    for (const auto &path : vector->paths) {
        auto p = painterPath(path); auto outside = p.subtracted(area);
        if (path.fill) { auto part = nativePath(outside, path.fill->argb); if (!part.commands.empty()) updated.paths.push_back(part); if (mode.compare("Split path", Qt::CaseInsensitive) == 0) { auto inside = nativePath(p.intersected(area), path.fill->argb); if (!inside.commands.empty()) updated.paths.push_back(inside); } }
        if (path.stroke) {
            QPainterPathStroker stroker; stroker.setWidth(path.stroke->width);
            // Native StrokeStyle uses distance-to-segment coverage with round ends.
            stroker.setCapStyle(Qt::RoundCap); stroker.setJoinStyle(Qt::RoundJoin);
            const auto footprint = stroker.createStroke(p);
            if (!footprint.intersects(area)) updated.paths.push_back(path);
            else if (mode.compare("Erase stroke", Qt::CaseInsensitive) != 0) {
                auto remaining = nativePath(footprint.subtracted(area), path.stroke->paint.argb);
                if (!remaining.commands.empty()) updated.paths.push_back(remaining);
                if (mode.compare("Split path", Qt::CaseInsensitive) == 0) {
                    auto cut = nativePath(footprint.intersected(area), path.stroke->paint.argb);
                    if (!cut.commands.empty()) updated.paths.push_back(cut);
                }
            }
        }
    }
    return commit([&](Document &d) { auto *current = findAsset(d, vector->id); if (!current) return false; *current = updated; return true; });
}

void EditorCanvas::resetHistory() {
    m_history.clear(); m_historyBytes = 0; m_historyIndex = -1; m_historySuspended = false;
    recordHistory();
}
void EditorCanvas::updateElementHistoryMetadata() {
    // SDK commit notifications record the document before the shape creator can
    // attach its session metadata. Complete that same history entry afterward.
    if (m_historySuspended || m_historyIndex < 0 || m_historyIndex >= int(m_history.size())) return;
    auto &entry = m_history[m_historyIndex];
    m_historyBytes -= entry.retainedBytes;
    entry.retainedBytes -= patternBytes(entry.elementPatterns);
    entry.elementBounds = m_elementBounds;
    entry.elementSettings = m_elementSettings;
    entry.elementPatterns = m_elementPatterns;
    entry.retainedBytes += patternBytes(entry.elementPatterns);
    m_historyBytes += entry.retainedBytes;
    while (m_history.size() > 2 && m_historyIndex > 0 && m_historyBytes > historyBudget) {
        m_historyBytes -= m_history.front().retainedBytes;
        m_history.pop_front(); --m_historyIndex;
    }
}
void EditorCanvas::recordHistory() {
    if (m_historySuspended || liveStrokeActive() || !documentReady()) return;
    while (m_historyIndex + 1 < int(m_history.size())) {
        m_historyBytes -= m_history.back().retainedBytes;
        m_history.pop_back();
    }
    const auto retainedBytes = documentBufferBytes(*document()) + patternBytes(m_elementPatterns);
    // Evict before copying the next document, with the existing two-state minimum.
    // Cached costs avoid rescanning every retained asset for each eviction.
    while (m_history.size() > 1 && (m_history.size() >= 32 || m_historyBytes + retainedBytes > historyBudget)) {
        m_historyBytes -= m_history.front().retainedBytes;
        m_history.pop_front();
    }
    m_history.push_back({*document(), m_settings, m_elementBounds, m_elementSettings, m_elementPatterns, retainedBytes});
    m_historyBytes += retainedBytes;
    m_historyIndex = int(m_history.size()) - 1; emit undoRedoChanged();
}
bool EditorCanvas::restoreHistory(int index) {
    cancelElementTransform();
    if (index < 0 || index >= int(m_history.size()) || liveStrokeActive()
        || (m_gestureActive && m_historySuspended)) return false;
    cancelInteractionGesture();
    const auto selection = selectedLayerId(); m_historySuspended = true;
    const auto snapshot = m_history[index];
    bool success = true;
    {
        QScopedValueRollback<bool> guard(m_changingElementSelection, true);
        success = synchronizeDocument(snapshot.document);
        if (success) m_canvas = snapshot.document;
        if (success) {
            m_elementBounds = snapshot.elementBounds; m_elementSettings = snapshot.elementSettings;
            m_elementPatterns = snapshot.elementPatterns;
            m_patternImage = {};
            m_settings = snapshot.settings; m_toolValues = m_settings.value(m_tool);
            refresh();
            if (!selection.isEmpty() && findLayer(*document(), selection.toStdString())) selectLayer(selection);
            else if (!document()->layers.empty()) selectLayer(QString::fromStdString(layerProperties(document()->layers.back()).id));
            else clearSelection();
            m_historyIndex = index;
        }
    }
    if (success) synchronizeElementSelection(true);
    m_historySuspended = false; invalidateToolPreview(); emit undoRedoChanged(); emit documentInfoChanged();
    if (success) { QVariantMap settings; for (auto it = m_settings.cbegin(); it != m_settings.cend(); ++it) settings[it.key()] = it.value(); emit historyRestored(settings); }
    return success;
}
bool EditorCanvas::undo() { return canUndo() && restoreHistory(m_historyIndex - 1); }
bool EditorCanvas::redo() { return canRedo() && restoreHistory(m_historyIndex + 1); }

bool EditorCanvas::moveVectorPath(int index, qreal x, qreal y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return fail(tr("Enter finite path offsets."));
    if (m_pixelLocks.contains(selectedLayerId()) || m_positionLocks.contains(selectedLayerId())) return fail(tr("Unlock the selected layer before moving its path."));
    const auto id = selectedLayerId();
    return commit([&](Document &d) {
        auto *layer = findLayer(d, id.toStdString()); if (!layer) return false;
        const auto sourceValue = layerSource(*layer); const auto *source = std::get_if<StaticSource>(&sourceValue); if (!source) return false;
        auto *asset = findVectorAsset(d, source->assetId); if (!asset || index < 0 || index >= int(asset->paths.size())) return false;
        for (auto &command : asset->paths[index].commands) std::visit([&](auto &c) { using T = std::decay_t<decltype(c)>; const auto move = [&](Point &p) { p.x += x; p.y += y; };
            if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, LineTo>) move(c.point);
            else if constexpr (std::is_same_v<T, QuadraticTo>) { move(c.control); move(c.end); }
            else if constexpr (std::is_same_v<T, CubicTo>) { move(c.control1); move(c.control2); move(c.end); }
        }, command); return true;
    });
}
