#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include "EditorMedia.h"
#include <QColorSpace>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <numeric>
using namespace iiSharedCanvas;
using namespace editorTools;
QObject *EditorCanvas::media() const { return m_media.get(); }
QString EditorCanvas::toolHint() const {
    if (!m_media->error().isEmpty()) return m_media->error();
    if (m_pickCloneSource) return tr("Click the source point to clone or heal from.");
    if (m_pickNeutral) return tr("Click a neutral point to set white balance.");
    if (m_tool == "elements") return tr("Drag to create a shape. Change its controls to edit the selected shape.");
    if (m_tool == "text") return tr("Click to place text. Content and typography update the selected text.");
    if (m_tool == "select") return tr("Drag a selection, click with Magic Wand, or select an object layer.");
    if (m_tool == "fill") return m_toolValues.value("selector").toString() == "Generative" ? tr("Click a connected region, then Generate fill to queue its replacement.") : tr("Click to fill connected pixels; an active selection limits the fill.");
    if (m_tool == "masking") return tr("Drag to define a mask. Apply the selection in Layers → Mask.");
    if (m_tool == "retouch") return tr("Paint the repair region. Set a source point for Clone or Heal.");
    if (m_tool == "brush") return tr("Paint on the selected bitmap layer. A new paint layer is created when needed.");
    if (m_tool == "eraser") return tr("Paint to erase pixels, or use the selected eraser operation.");
    if (m_tool == "color" || m_tool == "effects") return tr("Adjustments apply to the selected layer and respect the active selection.");
    if (m_tool == "audio-track") return tr("Import a WAV track to edit its trim, fades, channels, gain and sync.");
    return tr("Choose an operation in the tool panel. Undo restores document edits.");
}
QString EditorCanvas::generationSeed() const {
    QString seed;
    if (documentReady() && document()->stableDiffusionMetadata) for (const auto &pass : document()->stableDiffusionMetadata->samplingPasses) if (pass.seed) { seed = QString::number(*pass.seed); break; }
    return seed;
}
QVariantMap EditorCanvas::toolState() const {
    const auto count = std::count_if(m_selection.alpha.begin(), m_selection.alpha.end(), [](auto a) { return a != 0; });
    const auto guides = settingsFor("canvas");
    const auto measure = [](QString value, double length, double ppi) {
        const bool percentage = value.endsWith('%'), millimeters = value.endsWith("mm");
        value.remove(QRegularExpression("\\s*(?:ppi|mm|px|%)$"));
        return value.toDouble() * (percentage ? length / 100 : millimeters ? ppi / 25.4 : 1);
    };
    const double ppi = m_specification.value("ppi", 300).toDouble();
    const QVariantMap guide{{"mode", text(guides, 16, "Crop")},
        {"bleed", measure(text(guides, 17, "3 mm"), canvasWidth(), ppi)},
        {"safeX", measure(text(guides, 18, "5%"), canvasWidth(), ppi)},
        {"safeY", measure(text(guides, 18, "5%"), canvasHeight(), ppi)}};
    return {{"selectionActive", count > 0}, {"selectedPixels", qlonglong(count)}, {"audioTracks", documentReady() ? int(document()->audioTracks.size()) : 0},
        {"generationSeed", generationSeed()},
        {"canvasGuide", guide}, {"clippingOverlay", clippingOverlay()},
        {"eraserOverlay", toggle(settingsFor("eraser"), 13) ? m_eraserOverlay : QUrl{}}, {"frameRate", documentReady() ? double(document()->timeline.frameRate.numerator) / document()->timeline.frameRate.denominator : 24.0}, {"frameCount", frameCount()}, {"pixelLocked", m_pixelLocks.contains(selectedLayerId())}, {"positionLocked", m_positionLocks.contains(selectedLayerId())}};
}
bool EditorCanvas::insertPixels(const QImage &input, const QString &name, bool background) {
    if (!documentReady() || input.isNull() || std::uint64_t(input.width()) * input.height() > MediaLimits{}.maxPixelsPerFrame) return fail(tr("Choose a decodable image within the canvas import limit."));
    const auto assetId = unique("media.asset."), id = unique("media.layer."); auto pixels = raster(input);
    if (!commit([&](Document &d) { LayerProperties p{id, name.toStdString()};
        const double scale = std::min(double(d.extent.width) / pixels.width, double(d.extent.height) / pixels.height);
        p.transform.m11 = p.transform.m22 = scale; p.transform.translationX = (d.extent.width - pixels.width * scale) / 2; p.transform.translationY = (d.extent.height - pixels.height * scale) / 2;
        d.assets.emplace_back(RasterAsset{assetId, pixels}); auto layer = StaticBitmapLayer{p, StaticSource{assetId}};
        if (background) {
            if (auto *existing = findLayer(d, "canvas.background.layer")) { p.id = "canvas.background.layer"; *existing = StaticBitmapLayer{p, StaticSource{assetId}}; }
            else { p.id = "canvas.background.layer"; d.layers.insert(d.layers.begin(), StaticBitmapLayer{p, StaticSource{assetId}}); }
        } else d.layers.emplace_back(layer); return true; })) return false;
    invalidateToolPreview(); return selectLayer(background ? "canvas.background.layer" : QString::fromStdString(id));
}
bool EditorCanvas::importToolSource(const QUrl &source, const QString &tool, const QVariantMap &values) {
    const auto path = source.isLocalFile() ? source.toLocalFile() : source.scheme().isEmpty() ? source.toString() : QString{};
    if (path.isEmpty()) return fail(tr("Choose a local source file."));
    if (tool == "audio-track" || QFileInfo(path).suffix().compare("wav", Qt::CaseInsensitive) == 0) {
        AudioImportOptions options; options.assetId = unique("audio.asset."); auto imported = importAudioWav(path.toStdString(), options);
        if (!imported.ok()) return fail(QString::fromStdString(imported.result.message));
        m_audioSource = std::move(imported.asset); m_audioTrack = QString::fromStdString(unique("audio.track."));
        auto settings = tool == "audio-track" ? values : QVariantMap{}; settings["field-0"] = path;
        const double duration = double(m_audioSource.samples.size()) / m_audioSource.sampleRate / m_audioSource.channelCount;
        const auto range = timeRange(text(settings, 5));
        if (range.first < 0 || range.first >= duration || range.second <= range.first) settings["field-5"] = QString("00:00 — %1").arg(duration, 0, 'f', 3); m_settings["audio-track"] = settings; return updateAudio(settings);
    }
    if (tool == "elements") { m_patternImage = QImage(path); if (m_patternImage.isNull()) return fail(tr("The element fill image could not be decoded.")); emit toolStateChanged(); return m_elementBounds.contains(selectedLayerId()) ? updateElement() : true; }
    if (QStringList{"mp4", "mov", "mkv", "webm"}.contains(QFileInfo(path).suffix().toLower())) {
        VideoImportOptions options; options.frameRate = document()->timeline.frameRate;
        options.limits.maxDecodedBytes = 256ULL * 1024 * 1024; options.limits.maxFrames = 512;
        options.backend.ffmpegPath = DREAMSCAPES_FFMPEG_EXECUTABLE; options.backend.ffprobePath = DREAMSCAPES_FFPROBE_EXECUTABLE;
        const auto assetId = unique("video.asset."); const auto imported = importVideoAsset(path.toStdString(), assetId, options);
        if (!imported.ok()) return fail(QString::fromStdString(imported.result.message));
        const auto id = unique("video.layer.");
        if (!commit([&](Document &d) { d.assets.emplace_back(imported.asset); LayerProperties p{id, QFileInfo(path).fileName().toStdString()}; const auto &pixels = imported.asset.frames.front(); const double scale = std::min(double(d.extent.width) / pixels.width, double(d.extent.height) / pixels.height); p.transform.m11 = p.transform.m22 = scale; p.transform.translationX = (d.extent.width - pixels.width * scale) / 2; p.transform.translationY = (d.extent.height - pixels.height * scale) / 2; d.layers.emplace_back(VideoLayer{p, StaticSource{assetId}}); d.timeline.frameCount = std::max(d.timeline.frameCount, FrameIndex(imported.asset.frames.size())); return true; })) return false;
        return selectLayer(QString::fromStdString(id));
    }
    if (QFileInfo(path).suffix().compare("svg", Qt::CaseInsensitive) == 0) {
        VectorImportOptions options; options.assetId = unique("vector.asset."); auto imported = importSvg(path.toStdString(), options);
        if (!imported.ok()) return fail(QString::fromStdString(imported.result.message)); const auto id = unique("vector.layer.");
        if (!commit([&](Document &d) { d.assets.emplace_back(imported.asset); d.layers.emplace_back(StaticVectorLayer{{id, QFileInfo(path).fileName().toStdString()}, StaticSource{imported.asset.id}}); return true; })) return false;
        return selectLayer(QString::fromStdString(id));
    }
    auto image = QImage(path); if (image.isNull()) return fail(tr("The selected source cannot be decoded as an image. Choose an image, SVG or PCM16 WAV."));
    if (tool == "camera-photo" && values.value("selector").toString() == "Document Scan") {
        ColorAdjustment a; a.saturation = text(values, 9, "Color") == "Color" ? 0 : -1; a.contrast = number(values, 10, 68) / 100;
        auto processed = adjustRaster(raster(image), a); if (!processed.ok()) return fail(QString::fromStdString(processed.error)); image = editorTools::image(processed.pixels);
        if (text(values, 9) == "B&W") for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) image.setPixel(x, y, qGray(image.pixel(x, y)) > 128 ? 0xffffffff : 0xff000000);
    }
    if (tool == "file") return placeImage(source, text(values, 4, "Fit"));
    return insertPixels(image, QFileInfo(path).fileName(), tool == "background");
}
bool EditorCanvas::updateAudio(const QVariantMap &v) {
    if (m_audioSource.samples.empty()) return fail(tr("Import a WAV track first."));
    auto asset = m_audioSource; const auto range = timeRange(text(v, 5, "00:00 — 00:01"));
    const double duration = double(asset.samples.size()) / asset.channelCount / asset.sampleRate;
    const double start = range.first < 0 ? 0 : range.first, end = range.second < 0 ? duration : std::min(duration, range.second);
    if (start >= end || start >= duration) return fail(tr("The audio range must lie inside the imported track."));
    const auto rateText = text(v, 2, "48k"); const std::uint32_t rate = rateText == "44.1k" ? 44100 : rateText == "96k" ? 96000 : 48000;
    const auto channels = text(v, 1, "Stereo") == "Mono" ? 1U : 2U;
    std::size_t count = std::size_t(std::ceil((end - start) * rate));
    if (toggle(v, 14) && document()->timeline.frameCount > 1) count = std::size_t(std::ceil(double(frameCount()) * document()->timeline.frameRate.denominator / document()->timeline.frameRate.numerator * rate));
    if (count == 0 || count * channels > 192000000) return fail(tr("The converted audio exceeds the sample limit."));
    const double gain = std::pow(10.0, number(v, 8) / 20); const double pan = std::clamp(number(v, 9) / 100, -1.0, 1.0);
    std::vector<std::int16_t> samples(count * channels);
    for (std::size_t i = 0; i < count; ++i) {
        const double sourcePosition = (start + double(i) / count * (end - start)) * asset.sampleRate;
        const auto first = std::min<std::size_t>(std::size_t(sourcePosition), asset.samples.size() / asset.channelCount - 1), next = std::min(first + 1, asset.samples.size() / asset.channelCount - 1); const double fraction = sourcePosition - std::floor(sourcePosition);
        const double time = double(i) / rate, fadeIn = number(v, 6), fadeOut = number(v, 7), total = double(count) / rate;
        const double envelope = std::min(fadeIn > 0 ? std::min(1.0, time / fadeIn) : 1.0, fadeOut > 0 ? std::min(1.0, (total - time) / fadeOut) : 1.0);
        double duck = 1;
        if (toggle(v, 10)) {
            const double absoluteTime = time + std::max(0.0, number(v, 13)) / 1000;
            const double fps = double(document()->timeline.frameRate.numerator) / document()->timeline.frameRate.denominator;
            double activity = 0;
            for (const auto &track : document()->audioTracks) if (track.id != m_audioTrack.toStdString() && !track.muted) for (const auto &clip : track.clips) if (clip.enabled) {
                const double local = absoluteTime - clip.startFrame / fps; const auto *other = findAudioAsset(*document(), clip.assetId); if (!other || local < 0 || local > clip.durationFrames / fps) continue;
                const auto center = std::size_t(local * other->sampleRate) + clip.sourceOffsetSamples;
                for (std::size_t j = center > 128 ? center - 128 : 0; j < std::min<std::size_t>(other->samples.size() / other->channelCount, center + 129); j += 16) activity = std::max(activity, std::abs(double(other->samples[j * other->channelCount])) / 32768);
            }
            duck = std::pow(10.0, number(v, 11, -12) / 20 * std::clamp(activity / 0.1, 0.0, 1.0));
        }
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const auto sampleAt = [&](std::size_t index) { if (channels == 1 && asset.channelCount == 2) return (double(asset.samples[index * 2]) + asset.samples[index * 2 + 1]) / 2; return double(asset.samples[index * asset.channelCount + std::min<std::size_t>(channel, asset.channelCount - 1)]); };
            const double balance = channel == 0 ? 1 - std::max(0.0, pan) : 1 + std::min(0.0, pan);
            samples[i * channels + channel] = std::int16_t(std::clamp(std::lround((sampleAt(first) * (1 - fraction) + sampleAt(next) * fraction) * gain * envelope * duck * balance), -32768L, 32767L));
        }
    }
    const double negativeOffset = std::max(0.0, -number(v, 13)) / 1000;
    const auto skip = std::min(count - 1, std::size_t(negativeOffset * rate));
    if (skip > 0) { samples.erase(samples.begin(), samples.begin() + skip * channels); count -= skip; }
    asset.sampleRate = rate; asset.channelCount = std::uint16_t(channels); asset.samples = std::move(samples);
    const auto id = m_audioTrack.toStdString(); const auto name = QFileInfo(text(v, 0, "Audio")).fileName().toStdString();
    return commit([&](Document &d) {
        if (auto *existing = findAudioAsset(d, asset.id)) *existing = asset; else d.audioAssets.push_back(asset);
        const double frameRate = double(d.timeline.frameRate.numerator) / d.timeline.frameRate.denominator;
        const auto frames = FrameIndex(std::max(1.0, std::ceil(double(count) / rate * frameRate)));
        const auto offset = FrameIndex(std::max(0.0, std::round(number(v, 13) / 1000 * frameRate)));
        d.timeline.frameCount = std::max(d.timeline.frameCount, frames + offset);
        AudioTrackLayer track{id, name}; track.clips.push_back({id + ".clip", name, asset.id, offset, frames});
        if (auto *existing = findAudioTrack(d, id)) *existing = track; else d.audioTracks.push_back(track);
        if (text(v, 1) == "Split" && channels == 2) {
            for (int channel = 0; channel < 2; ++channel) {
                auto mono = asset; mono.id = asset.id + (channel == 0 ? ".left" : ".right"); mono.channelCount = 1; mono.samples.resize(count); for (std::size_t i = 0; i < count; ++i) mono.samples[i] = asset.samples[i * 2 + channel];
                if (auto *a = findAudioAsset(d, mono.id)) *a = mono; else d.audioAssets.push_back(mono);
                const auto trackId = id + (channel == 0 ? ".left" : ".right"); AudioTrackLayer split{trackId, name + (channel == 0 ? " L" : " R")}; split.clips.push_back({trackId + ".clip", split.name, mono.id, offset, frames});
                if (auto *existing = findAudioTrack(d, trackId)) *existing = split; else d.audioTracks.push_back(split);
            }
            findAudioTrack(d, id)->muted = true;
        } else for (auto &track : d.audioTracks) if (track.id == id + ".left" || track.id == id + ".right") track.muted = true;
        return true;
    });
}
bool EditorCanvas::transformCanvas(const QVariantMap &v, bool mirror) {
    if (!documentReady()) return fail(tr("Open a canvas first."));
    if (mirror && toggle(v, 13)) {
        if (text(v, 12, "Horizontal") == "Horizontal") m_previewMirrorX = !m_previewMirrorX;
        else m_previewMirrorY = !m_previewMirrorY;
        emit toolStateChanged(); return true;
    }
    const double oldWidth = canvasWidth(), oldHeight = canvasHeight(); QTransform operation; double newWidth = oldWidth, newHeight = oldHeight;
    if (mirror) { if (text(v, 12, "Horizontal") == "Horizontal") { operation.translate(oldWidth, 0); operation.scale(-1, 1); } else { operation.translate(0, oldHeight); operation.scale(1, -1); } }
    else {
        const auto preset = text(v, 8, "90° L"); const double angle = (preset == "180°" ? 180 : preset == "90° R" ? 90 : -90) + number(v, 9);
        operation.translate(oldWidth / 2, oldHeight / 2); operation.rotate(angle); operation.translate(-oldWidth / 2, -oldHeight / 2);
        const auto bounds = operation.mapRect(QRectF(0, 0, oldWidth, oldHeight)); newWidth = std::ceil(bounds.width()); newHeight = std::ceil(bounds.height()); QTransform move; move.translate(-bounds.left(), -bounds.top()); operation = operation * move;
    }
    if (!commit([&](Document &d) {
        d.extent = {int(newWidth), int(newHeight)};
        if (mirror || toggle(v, 10, true)) for (auto &layer : d.layers) {
            auto &p = layerProperties(layer); if (mirror && toggle(v, 13)) continue;
            if (mirror && toggle(v, 14) && QString::fromStdString(p.id).startsWith("text.layer.")) {
                const auto *asset = resolveAssetAt(d, layer, frame()); const auto *vector = asset ? std::get_if<VectorAsset>(asset) : nullptr;
                if (vector) { QRectF bounds; for (const auto &path : vector->paths) for (const auto &command : path.commands) std::visit([&](const auto &c) { using T = std::decay_t<decltype(c)>; if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, LineTo>) bounds |= QRectF(c.point.x, c.point.y, 0.001, 0.001); else if constexpr (std::is_same_v<T, QuadraticTo> || std::is_same_v<T, CubicTo>) bounds |= QRectF(c.end.x, c.end.y, 0.001, 0.001); }, command); const QTransform current(p.transform.m11, p.transform.m12, p.transform.m21, p.transform.m22, p.transform.translationX, p.transform.translationY); const auto center = current.map(bounds.center()); const auto reflected = operation.map(center); p.transform.translationX += reflected.x() - center.x(); p.transform.translationY += reflected.y() - center.y(); }
                continue;
            }
            QTransform current(p.transform.m11, p.transform.m12, p.transform.m21, p.transform.m22, p.transform.translationX, p.transform.translationY); const auto result = current * operation;
            p.transform = {result.m11(), result.m12(), result.m21(), result.m22(), result.dx(), result.dy()};
        }
        return true;
    })) return false;
    m_specification["pixelWidth"] = canvasWidth(); m_specification["pixelHeight"] = canvasHeight(); clearAreaSelection(); emit specificationChanged(); fitToView(); return true;
}
bool EditorCanvas::executeToolAction(const QString &tool, const QString &field, const QVariantMap &values) {
    m_settings[tool] = values;
    const auto capability = toolControlState(tool, field, values);
    if (!capability.value("enabled").toBool()) return fail(capability.value("reason").toString());
    if (!documentReady()) return fail(tr("Open a canvas first."));
    clearError();
    if (tool == "canvas" && (field == "field-11" || field == "field-15")) return transformCanvas(values, field == "field-15");
    if (tool == "generative" && field == "field-30") {
        if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("Unlock the selected layer's pixels first."));
        if (!rasterizeSelected()) return false;
        const int scale = text(values, 27, "2×") == "8×" ? 8 : text(values, 27) == "4×" ? 4 : 2;
        const auto *pixels = selectedRasterPixels(); if (std::uint64_t(pixels->width) * pixels->height * scale * scale > 67108864) return fail(tr("The upscaled layer exceeds the pixel limit."));
        const auto id = selectedLayerId(); const auto result = raster(image(*pixels).scaled(pixels->width * scale, pixels->height * scale, Qt::IgnoreAspectRatio, text(values, 28) == "Nearest" ? Qt::FastTransformation : Qt::SmoothTransformation));
        if (!replaceSelectedPixels(result)) return false;
        return commit([&](Document &d) { auto &p = layerProperties(*findLayer(d, id.toStdString())); p.transform.m11 /= scale; p.transform.m12 /= scale; p.transform.m21 /= scale; p.transform.m22 /= scale; return true; });
    }
    if (tool == "layers" && field == "field-7") return rasterizeSelected();
    if (tool == "layers" && field == "field-23") return applySelectionMask(values);
    if (tool == "layers" && field == "field-18") { const auto adjustment = text(values, 16, "Curves"); return applyPixelTool("color", adjustment == "Exposure" ? "field-1" : adjustment == "HSL" ? "field-36" : "curveAmount", adjustment == "Exposure" ? 0.35 : 10); }
    if (tool == "masking" && field == "field-13") return semanticSelection(values, false);
    if (tool == "background" && (field == "field-7" || field == "field-11")) {
        if (!semanticSelection(values, true)) return false;
        if (field == "field-11") return true;
        for (auto &a : m_selection.alpha) a = 255 - a; updateSelectionOverlay(); return applySelectionMask({{"field-25", 100}, {"field-26", number(values, 5)}});
    }
    if (tool == "select" && field == "field-16") return semanticSelection(values, false);
    if (tool == "color" && field == "field-31") { m_pickNeutral = true; emit toolStateChanged(); return true; }
    if (tool == "retouch" && field == "field-11") { m_pickCloneSource = true; emit toolStateChanged(); return true; }
    if (tool == "auto-enhance" && (field == "field-3" || field == "field-7" || field == "field-11")) return applyPixelTool(tool, field, true);
    if (tool == "audio-track" && field == "field-15") {
        if (m_audioSource.samples.empty()) return fail(tr("Import a WAV track first."));
        std::size_t peak = 0; double previousEnergy = 0, maximumFlux = 0;
        const auto sourceFrames = m_audioSource.samples.size() / m_audioSource.channelCount;
        for (std::size_t i = 0; i < sourceFrames; i += 256) { double energy = 0; for (std::size_t j = i; j < std::min(sourceFrames, i + 256); ++j) energy += std::abs(double(m_audioSource.samples[j * m_audioSource.channelCount])) / 32768; energy /= 256; const double flux = energy - previousEnergy; if (flux > maximumFlux) { maximumFlux = flux; peak = i; } previousEnergy = energy; }
        auto next = values; if (text(values, 12) == "Beat") next["field-13"] = std::clamp((double(frame()) * document()->timeline.frameRate.denominator / document()->timeline.frameRate.numerator - double(peak) / m_audioSource.sampleRate) * 1000, -1000.0, 1000.0);
        m_settings[tool] = next; return updateAudio(next);
    }
    if ((tool == "retouch" && (field == "field-3" || field == "field-18")) || (tool == "eraser" && field == "field-7")) {
        if (m_selection.alpha.empty()) return fail(tr("Mark the object with a selection first."));
        if (tool == "eraser") {
            const auto expanded = expandedObjectMask(m_selection, number(values, 5));
            if (!expanded) return fail(tr("Use a smaller or simpler object mask to expand its boundary."));
            m_selection = *expanded; updateSelectionOverlay();
        }
        if (!rasterizeSelected()) return false; auto pixels = *selectedRasterPixels(); auto mask = layerMask();
        if (tool == "eraser" && text(values, 6) == "Transparent") { for (auto &a : mask.alpha) a = 255 - a; auto result = maskRaster(pixels, mask); return result.ok() && replaceSelectedPixels(result.pixels); }
        return repairSelection();
    }
    if (tool == "eraser" && field == "field-10") return eraseVector(values);
    if (tool == "camera-photo" && (field == "field-3" || field == "field-11")) return m_media->capture(values);
    if (tool == "masking" && field == "field-7") { clearAreaSelection(); return true; }
    if (tool == "auto-enhance" && field == "field-19") return transformCanvas({{"field-8", "90° R"}, {"field-9", -90 + number(values, 18)}, {"field-10", true}}, false);

    return fail(tr("This action requires a source or operation configuration. %1 / %2").arg(tool, field));
}

QVariantMap EditorCanvas::toolPreview(const QString &tool, const QString &field, const QVariantMap &v) const {
    QVariantMap result{{"title", tool}, {"description", documentName()}}; QVariantList items;
    if (!documentReady()) return result;
    const auto selectionItem = [&] { if (!m_selection.alpha.empty()) items.append(QVariantMap{{"label", tr("Clear selection")}, {"action", "clearSelection"}}); };
    if (tool == "layers" && field == "field-0") {
        if (document()->stableDiffusionMetadata) {
            const auto &metadata = *document()->stableDiffusionMetadata;
            result["description"] = QString::fromStdString(metadata.generationParametersText);
            items.append(QVariantMap{{"label", tr("Prompt")}, {"description", QString::fromStdString(metadata.positivePrompt)}});
            items.append(QVariantMap{{"label", tr("Negative prompt")}, {"description", QString::fromStdString(metadata.negativePrompt)}});
        } else result["description"] = tr("This document has no persisted generation metadata.");
    } else if (tool == "retouch" && field == "field-15") {
        const auto rendered = renderFrame(*document(), frame());
        if (rendered.ok() && m_cloneReady) {
            auto preview = image(rendered.pixels); QPainter painter(&preview);
            painter.setPen(QPen(QColor("#8B7CFF"), std::max(1.0, canvasWidth() / 512.0)));
            const double radius = std::max(2.0, number(v, 0, 84) / 2);
            painter.drawEllipse(m_cloneSource, radius, radius);
            painter.drawLine(m_cloneSource - QPointF(radius, 0), m_cloneSource + QPointF(radius, 0));
            painter.drawLine(m_cloneSource - QPointF(0, radius), m_cloneSource + QPointF(0, radius)); painter.end();
            result["imageSource"] = dataImage(preview.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            result["description"] = tr("Source at %1, %2 · %3").arg(m_cloneSource.x()).arg(m_cloneSource.y()).arg(toggle(v, 10) ? tr("composited layers") : tr("selected layer"));
        } else result["description"] = tr("Set a clone source point in the canvas to view its sampled region.");
    } else if (tool == "asset" || tool == "camera-photo") {
        auto directory = text(v, 8); if (!QFileInfo(directory).isDir()) directory = v.value("libraryDirectory").toString(); if (directory.isEmpty()) directory = qEnvironmentVariable("DREAMSCAPES_EDITOR_ASSETS", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
        if (QFileInfo(directory + "/DirectoryStorage").isDir()) directory += "/DirectoryStorage";
        items = m_media->library(directory, v); result["description"] = tr("%1 local media files · %2").arg(items.size()).arg(directory);
    } else if (tool == "layers" && (field == "field-12" || field == "field-15")) {
        const auto *layer = findLayer(*document(), selectedLayerId().toStdString()); const auto *asset = layer ? resolveAssetAt(*document(), *layer, frame()) : nullptr; const auto *vector = asset ? std::get_if<VectorAsset>(asset) : nullptr;
        if (vector) for (std::size_t i = 0; i < vector->paths.size(); ++i) items.append(QVariantMap{{"label", tr("Path %1 · %2 commands").arg(i + 1).arg(vector->paths[i].commands.size())}, {"action", "editPath"}, {"index", int(i)}});
        result["description"] = vector ? tr("Choose a native path to move its geometry.") : tr("Select a native vector layer.");
    } else if (tool == "layers") {
        for (const auto &value : layers()) { auto item = value.toMap(); item["label"] = item["name"]; item["action"] = "selectLayer"; items.append(item); }
        if (field == "field-23" && !m_selection.alpha.empty()) { result["imageSource"] = m_selectionOverlay; items.prepend(QVariantMap{{"label", tr("Apply selection as pixel mask")}, {"action", "applyMask"}}); selectionItem(); }
    } else if (tool == "brush") {
        for (const auto &name : {"Paint", "Ink", "Texture"}) if (QString(name) == text(v, 1, "Paint") && (QString(name).contains(text(v, 0), Qt::CaseInsensitive) || text(v, 0).isEmpty())) items.append(QVariantMap{{"label", name}, {"action", "brushPreset"}, {"value", name}});
        result["description"] = tr("Native iiPaintEngine presets. Search filters this list.");
    } else if (tool == "fill" && (field == "field-0" || field == "field-5" || field == "field-8")) {
        for (const auto &color : {"#8B7CFF", "#FFFFFF", "#000000", "#FF5F57", "#28C840", "#0A84FF"}) items.append(QVariantMap{{"label", color}, {"action", "palette"}, {"value", color}});
        result["description"] = tr("Choose a fill color. Gradient uses this color and its lighter endpoint; Pattern uses alternating tiles.");
    } else if (tool == "effects" && field == "field-0") {
        for (const auto &name : {"Featured", "Film", "B&W"}) items.append(QVariantMap{{"label", name}, {"action", "filterPreset"}, {"value", name}});
    } else if (tool == "audio-track") {
        const auto *audio = m_audioSource.samples.empty() ? (document()->audioAssets.empty() ? nullptr : &document()->audioAssets.back()) : findAudioAsset(*document(), m_audioSource.id);
        if (audio && !audio->samples.empty()) {
            QImage wave(512, 160, QImage::Format_ARGB32); wave.fill(QColor("#202027")); QPainter p(&wave); p.setPen(QColor("#8B7CFF")); const auto frames = audio->samples.size() / audio->channelCount;
            for (int x = 0; x < 512; ++x) { int peak = 0; for (std::size_t i = x * frames / 512; i < std::min(frames, (x + 1) * frames / 512 + 1); ++i) peak = std::max(peak, std::abs(int(audio->samples[i * audio->channelCount]))); const int h = peak * 75 / 32768; p.drawLine(x, 80 - h, x, 80 + h); }
            p.end(); result["imageSource"] = dataImage(wave); result["description"] = tr("Edited PCM: %1 Hz · %2 channels · %3 seconds").arg(audio->sampleRate).arg(audio->channelCount).arg(double(frames) / audio->sampleRate, 0, 'f', 2);
            items.append(QVariantMap{{"label", m_media->playing() ? tr("Stop playback") : tr("Play edited track")}, {"action", m_media->playing() ? "stopAudio" : "playAudio"}});
        } else result["description"] = tr("Import a WAV track to view its actual waveform.");
    } else if (tool == "color") {
        const auto rendered = renderFrame(*document(), frame()); if (rendered.ok()) {
            std::array<int, 256> bins{}; for (auto pixel : rendered.pixels.pixels) if (qAlpha(pixel)) ++bins[qGray(pixel)]; const int maximum = std::max(1, *std::max_element(bins.begin(), bins.end()));
            QImage chart(512, 160, QImage::Format_ARGB32); chart.fill(QColor("#202027")); QPainter p(&chart); p.setPen(QColor("#8B7CFF"));
            if (field == "field-0" || field == "field-7" || field == "field-19" || field == "field-23") {
                for (int i = 0; i < 256; ++i) { if ((field == "field-19" && i < 128) || (field == "field-23" && i > 128)) continue; p.drawLine(i * 2, 159, i * 2, 159 - bins[i] * 150 / maximum); }
                result["description"] = tr("Actual frame luminance histogram · %1 nontransparent bins").arg(std::count_if(bins.begin(), bins.end(), [](auto n) { return n; }));
            } else if (field == "field-44") {
                if (toggle(v, 47)) { p.setPen(QColor("#56505F")); for (int i = 0; i < 256; ++i) p.drawLine(i * 2, 159, i * 2, 159 - bins[i] * 150 / maximum); }
                p.setPen(QColor("#8B7CFF")); QPolygonF curve; const double amount = v.value("curveAmount").toDouble() / 100;
                for (int i = 0; i < 256; ++i) { const double x = i / 255.0; const double value = std::clamp(x + amount * (text(v, 46, "Smooth") == "Smooth" ? x * (1 - x) : 0.5 - std::abs(x - 0.5)), 0.0, 1.0); curve << QPointF(i * 2, 159 - value * 150); } p.drawPolyline(curve);
                result["description"] = tr("Editable tone response · %1 channel. Move the curve slider to change pixels.").arg(text(v, 45, "RGB"));
            } else {
                for (int x = 0; x < 512; ++x) { QColor color; color.setHsv(x * 359 / 511, 220, 230); p.setPen(color); p.drawLine(x, 0, x, 159); }
                result["description"] = tr("Hue spectrum. Hue and balance controls apply to the selected layer.");
            }
            p.end(); result["imageSource"] = dataImage(chart);
        }
    } else if (tool == "select" || tool == "masking" || tool == "background") {
        for (const auto &layer : document()->layers) if (const auto *semantic = std::get_if<SemanticSegmentLayer>(&layer)) for (const auto &region : semantic->segmentation.regions) items.append(QVariantMap{{"label", QString::fromStdString(region.name)}, {"description", tr("Region %1 · confidence %2").arg(region.id).arg(region.confidence.value_or(0))}, {"action", "region"}});
        result["imageSource"] = m_selectionOverlay; result["description"] = tr("%1 selected pixels · %2 supplied semantic regions").arg(toolState().value("selectedPixels").toLongLong()).arg(items.size()); selectionItem();
    } else {
        if (!m_selectionOverlay.isEmpty() && (tool == "generative" || tool == "retouch")) result["imageSource"] = m_selectionOverlay;
        else { const auto rendered = renderFrame(*document(), frame()); if (rendered.ok()) result["imageSource"] = dataImage(image(rendered.pixels).scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)); }
        result["description"] = tr("Current document frame %1 / %2").arg(frame() + 1).arg(frameCount());
    }
    result["items"] = items; return result;
}
QVariantMap EditorCanvas::toolControlState(const QString &tool, const QString &field, const QVariantMap &v) const {
    QString reason;
    const int n = field.startsWith("field-") ? field.mid(6).toInt() : -1;
    const auto mode = v.value("selector").toString();
    if (!documentReady()) reason = tr("Open a canvas first.");
    // Separate unsupported design placeholders from supported operations that
    // currently need a document, selection, device or model resource.
    static const QMap<QString, QSet<int>> unavailable{
        {"text", {15}},
        {"camera-photo", {2, 4, 5, 6, 8, 12, 13, 14, 15}},
        {"asset", {2, 4, 6, 7, 9, 10, 12, 13, 14}},
        {"file", {5, 7, 8, 9}},
        {"background", {3, 4, 6, 14}},
        {"canvas", {4, 5, 6, 7, 19}},
        {"generative", {6, 13, 15, 16, 17, 18, 19, 20, 21, 26, 29}},
        {"layers", {2, 4, 5, 6, 14, 17, 21, 22, 24}},
        {"effects", {10, 17, 44}},
        {"retouch", {1, 19, 20, 21, 22}},
        {"fill", {14}},
        {"brush", {10, 14}},
        {"auto-enhance", {12, 13, 14, 15, 16, 17}},
        {"masking", {16, 17, 25}},
        {"eraser", {4, 9}}
    };
    const bool supported = !unavailable.value(tool).contains(n);
    if (!supported) reason = tr("This option is unavailable in the connected editing engine.");
    if (tool == "file" && n == 5) reason = tr("This document embeds media. Persistent external links and smart objects are unavailable.");
    if (tool == "file" && n == 11 && text(v, 1, "IISC") != "IISC") reason = tr("This image format exports a flattened frame. Choose IISC to preserve editable layers.");
    if (tool == "asset" && unavailable.value(tool).contains(n)) reason = tr("The local media library has no shared collection, license or version service attached.");
    if (tool == "canvas" && n >= 4 && n <= 7) reason = tr("The document stores untagged RGB. Persistent ICC profiles are unavailable.");
    if (tool == "generative" && unavailable.value(tool).contains(n)) reason = tr("Open Advanced generation to supply conditioning and enhancement model resources.");
    if (tool == "layers" && n == 1) reason = tr("The generation seed is read-only metadata.");
    if (tool == "layers" && QSet<int>{7, 8, 9, 10, 11, 12, 15, 18, 19, 20, 23, 25, 26}.contains(n) && selectedLayerId().isEmpty()) reason = tr("Select a layer first.");
    if (tool == "layers" && n == 9 && !selectedRasterPixels()) reason = tr("Alpha protection currently requires a regular bitmap layer.");
    if ((tool == "color" && n != 18) || tool == "effects" || tool == "retouch") { if (selectedLayerId().isEmpty()) reason = tr("Select a layer first."); else if (m_pixelLocks.contains(selectedLayerId())) reason = tr("The selected layer's pixels are locked."); }
    if (tool == "layers" && n == 13) {
        const auto *layer = documentReady() ? findLayer(*document(), selectedLayerId().toStdString()) : nullptr;
        const auto *asset = layer ? resolveAssetAt(*document(), *layer, frame()) : nullptr;
        const auto *vector = asset ? std::get_if<VectorAsset>(asset) : nullptr;
        if (!vector || vector->paths.size() < 2) reason = tr("Select a vector layer containing at least two paths.");
        else if (m_pixelLocks.contains(selectedLayerId())) reason = tr("Unlock the selected layer's pixels first.");
    }
    if ((tool == "masking" && n >= 10 && n <= 13) || (tool == "background" && n >= 7 && n <= 11) || (tool == "select" && n >= 13 && n <= 16)) {
        bool found = false; if (documentReady()) for (const auto &layer : document()->layers) found = found || std::holds_alternative<SemanticSegmentLayer>(layer);
        if (!found) reason = tr("Import a semantic segmentation layer to use detected subjects and regions.");
    }
    if (tool == "camera-photo" && (n == 0 || n == 1 || n == 3 || n == 11) && !m_media->cameraAvailable()) reason = tr("No camera is available. Choose Photo Library to import an image.");
    if (tool == "camera-photo" && mode == "RAW Capture" && n >= 12) reason = tr("The camera backend does not expose RAW sensor output.");
    if (tool == "brush" && n == 14) reason = tr("This canvas input bridge currently forwards pressure, without stylus tilt.");
    if (tool == "canvas" && n == 14 && toggle(v, 13)) reason = tr("Keep text readable applies when mirroring document layers. A preview mirror reflects the complete view.");
    if (tool == "audio-track" && n >= 4 && documentReady() && document()->audioAssets.empty()) reason = tr("Import a WAV track first.");
    if (tool == "layers" && n == 23 && m_selection.alpha.empty()) reason = tr("Create a selection first to apply a pixel mask.");
    if (tool == "eraser" && n == 10) { const auto *layer = documentReady() ? findLayer(*document(), selectedLayerId().toStdString()) : nullptr; if (!layer || !std::holds_alternative<StaticVectorLayer>(*layer)) reason = tr("Select a vector layer and mark the eraser region first."); }
    if (tool == "select" && n == 16 && mode != "Object") reason = tr("Choose Object selection first.");
    return {{"supported", supported}, {"enabled", reason.isEmpty()}, {"reason", reason}};
}
QVariantMap EditorCanvas::generationParameters(const QString &operation, const QVariantMap &v) {
    QVariantMap parameters{{"prompt", text(v, 0)}, {"negativePrompt", text(v, 1)}, {"outputCount", 4}, {"width", std::clamp(canvasWidth(), 64, 4096)}, {"height", std::clamp(canvasHeight(), 64, 4096)}};
    m_generationExtent = QSize(canvasWidth(), canvasHeight()); m_generationOffset = {};
    m_generationMask = m_selection; m_generationLayer = selectedLayerId();
    if (m_tool == "eraser" && operation == "Inpaint") {
        const auto expanded = expandedObjectMask(m_generationMask, number(settingsFor("eraser"), 5));
        if (!expanded) { fail(tr("Use a smaller or simpler object mask to expand its boundary.")); return {}; }
        m_generationMask = *expanded;
    }
    if (!m_generationMask.alpha.empty() && number(v, 9) > 0) {
        const auto softened = featherMask(m_generationMask, std::clamp(number(v, 9), 0.0, 256.0));
        if (!softened.ok()) { fail(QString::fromStdString(softened.error)); return {}; } m_generationMask = softened.mask;
    }
    const auto model = text(v, 2); if (!QStringList{"Auto", "Standard", "Artwork", "Photo"}.contains(model) && !model.isEmpty()) parameters["model"] = model;
    const auto vae = text(v, 25); if (!vae.isEmpty() && vae != "Auto / model default") parameters["vae"] = vae;
    if (!text(v, 23).isEmpty()) parameters["loras"] = QVariantList{QVariantMap{{"id", "editor-lora"}, {"source", text(v, 23)}, {"weight", number(v, 24, 1)}}};
    if (operation != "Text-to-image") {
        const auto rendered = renderFrame(*document(), frame()); if (!rendered.ok()) { fail(QString::fromStdString(rendered.message)); return {}; }
        const auto directory = qEnvironmentVariable("DREAMSCAPES_EDITOR_CACHE", QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/EditorInputs");
        if (!QDir().mkpath(directory)) { fail(tr("Create a writable editor input directory.")); return {}; }
        const auto file = directory + "/" + QString::fromStdString(unique("reference-")) + ".png";
        auto reference = image(rendered.pixels);
        if (operation == "Outpaint") {
            bool valid; const int expansion = text(v, 12, "512 px").split(' ').first().toInt(&valid);
            if (!valid || expansion < 1 || expansion > 1024) { fail(tr("Choose an expansion from 1 to 1024 pixels.")); return {}; }
            const auto direction = text(v, 11, "All"); const int dx = direction == "Top/Bottom" ? 0 : expansion, dy = direction == "Left/Right" ? 0 : expansion;
            const QSize extent(canvasWidth() + dx * 2, canvasHeight() + dy * 2);
            if (extent.width() > 4096 || extent.height() > 4096) { fail(tr("The expanded generation extent must be at most 4096 pixels per side.")); return {}; }
            QImage expanded(extent, QImage::Format_ARGB32); expanded.fill(Qt::transparent); QPainter p(&expanded); p.drawImage(dx, dy, reference); p.end(); reference = expanded;
            parameters["width"] = extent.width(); parameters["height"] = extent.height(); m_generationExtent = extent; m_generationOffset = {double(dx), double(dy)};
        }
        const auto supplied = text(v, 4);
        if (QFileInfo(QUrl(supplied).isLocalFile() ? QUrl(supplied).toLocalFile() : supplied).isFile()) { reference = QImage(QUrl(supplied).isLocalFile() ? QUrl(supplied).toLocalFile() : supplied); if (reference.isNull()) { fail(tr("The reference file cannot be decoded.")); return {}; } }
        if (!reference.save(file)) { fail(tr("The canvas reference image could not be saved.")); return {}; }
        parameters["referenceImages"] = QVariantList{QUrl::fromLocalFile(file).toString()}; parameters["imageStrength"] = number(v, 5, 58) / 100;
    }
    if (operation == "Inpaint" && m_selection.alpha.empty()) { fail(tr("Create an inpaint selection first.")); return {}; }
    return parameters;
}
bool EditorCanvas::placeGenerated(const QUrl &source, const QString &operation) {
    QImage input(source.toLocalFile()); if (input.isNull()) return fail(tr("The generated image could not be read."));
    if (operation == "Inpaint" || operation == "Fill") {
        if (m_generationLayer.isEmpty() || !selectLayer(m_generationLayer)) return fail(tr("The generation target layer is no longer available."));
        if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("Unlock the generation target layer's pixels first."));
        if (!m_generationMask.alpha.empty() && (m_generationMask.extent.width != canvasWidth() || m_generationMask.extent.height != canvasHeight())) return fail(tr("The canvas dimensions changed after generation was submitted."));
        m_selection = m_generationMask; updateSelectionOverlay();
        if (!rasterizeSelected()) return false;
        const auto *pixels = selectedRasterPixels(); const auto *layer = findLayer(*document(), selectedLayerId().toStdString()); const auto &t = layerProperties(*layer).transform;
        bool invertible = false; const auto inverse = QTransform(t.m11, t.m12, t.m21, t.m22, t.translationX, t.translationY).inverted(&invertible);
        if (!invertible) return fail(tr("The generation target transform cannot be inverted."));
        QImage mapped(pixels->width, pixels->height, QImage::Format_ARGB32_Premultiplied); mapped.fill(Qt::transparent);
        QPainter painter(&mapped); painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.setTransform(inverse);
        painter.drawImage(QRectF(0, 0, canvasWidth(), canvasHeight()), input); painter.end();
        auto result = blendRaster(*pixels, raster(mapped), layerMask()); return result.ok() && replaceSelectedPixels(result.pixels);
    }
    if (operation == "Outpaint") {
        const auto assetId = unique("outpaint.asset."), layerId = unique("outpaint.layer.");
        const auto pixels = raster(input.scaled(m_generationExtent, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        if (!commit([&](Document &d) { d.extent = {m_generationExtent.width(), m_generationExtent.height()}; for (auto &layer : d.layers) { auto &p = layerProperties(layer); p.transform.translationX += m_generationOffset.x(); p.transform.translationY += m_generationOffset.y(); } d.assets.emplace_back(RasterAsset{assetId, pixels}); d.layers.insert(d.layers.begin(), StaticBitmapLayer{{layerId, "Generated outpaint"}, StaticSource{assetId}}); return true; })) return false;
        m_specification["pixelWidth"] = canvasWidth(); m_specification["pixelHeight"] = canvasHeight(); emit specificationChanged();
        clearAreaSelection(); fitToView(); return selectLayer(QString::fromStdString(layerId));
    }
    return insertPixels(input, tr("Generated %1").arg(operation), operation == "Background");
}
