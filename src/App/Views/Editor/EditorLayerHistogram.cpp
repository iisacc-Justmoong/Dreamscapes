#include "EditorLayerHistogram.h"
#include "EditorCanvas.h"
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <QtConcurrentRun>
#include <QByteArray>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <exception>

namespace {
using namespace iiSharedCanvas;
using Histogram = EditorLayerHistogram;

bool timeDependent(const Document &document, const std::string &artboard, bool composite, const std::string &selection) {
    for (const auto &layer : document.layers) {
        const auto &properties = layerProperties(layer);
        if (composite) {
            if (!properties.visible || (!artboard.empty() && properties.artboardId != artboard)) continue;
        } else if (properties.id != selection) continue;
        if (layerTiming(layer) == LayerTiming::Dynamic || contentKind(layer) == ContentKind::Video
            || !properties.motion.empty() || (properties.frameRange
                && (properties.frameRange->firstFrame != 0 || properties.frameRange->lastFrame != document.timeline.frameCount - 1))) return true;
    }
    return false;
}

Histogram::Result analyze(const Document &input, FrameIndex frame,
    const std::string &artboard, bool composite, const std::string &selection,
    const Histogram::Visibility *visibility, const std::shared_ptr<std::atomic_bool> &cancelled) {
    Histogram::Result result;
    const auto stopped = [&] { return cancelled && cancelled->load(); };
    if (stopped()) { result.cancelled = true; return result; }
    const Document *source = &input;
    bool restore = false;
    if (visibility) {
        if (visibility->artboards.size() != input.artboards.size() || visibility->layers.size() != input.layers.size()) {
            result.error = "The histogram snapshot is incomplete."; return result;
        }
        for (std::size_t i = 0; i < input.artboards.size(); ++i) restore |= input.artboards[i].visible != visibility->artboards[i];
        for (std::size_t i = 0; i < input.layers.size(); ++i) restore |= layerProperties(input.layers[i]).visible != visibility->layers[i];
    }
    std::optional<Document> sampled;
    if (restore || !composite) {
        // Raster payload copies required for a visibility override happen on
        // this worker, never as an extra GUI snapshot copy.
        sampled.emplace(input);
        if (restore) {
            for (std::size_t i = 0; i < sampled->artboards.size(); ++i) sampled->artboards[i].visible = visibility->artboards[i];
            for (std::size_t i = 0; i < sampled->layers.size(); ++i) layerProperties(sampled->layers[i]).visible = visibility->layers[i];
        }
        if (!composite) {
            for (auto &layer : sampled->layers) layerProperties(layer).visible = layerProperties(layer).id == selection;
            for (auto &board : sampled->artboards) { board.backgroundArgb = 0; board.visible = true; }
        }
        source = &*sampled;
    }
    if (stopped()) { result.cancelled = true; return result; }
    const auto rendered = artboard.empty() ? renderFrame(*source, frame) : renderArtboard(*source, frame, artboard);
    if (!rendered.ok()) { result.error = rendered.message; return result; }
    std::size_t index = 0;
    for (const auto pixel : rendered.pixels.pixels) {
        if ((index++ & 4095) == 0 && stopped()) { result.cancelled = true; return result; }
        if (!(pixel >> 24)) continue;
        const auto red = (pixel >> 16) & 255, green = (pixel >> 8) & 255, blue = pixel & 255;
        const std::array<std::uint32_t, 4> values{red, green, blue, (11 * red + 16 * green + 5 * blue) / 32};
        ++result.pixelCount;
        for (std::size_t i = 0; i < values.size(); ++i) ++result.channels[i].bins[values[i]];
    }
    // Work directly on packed native ARGB pixels. Expanding the full image to
    // floating RGBA would multiply its memory and alter the existing gray rule.
    if (result.pixelCount) for (auto &channel : result.channels) {
        double sum = 0, squares = 0;
        for (std::size_t i = 0; i < channel.bins.size(); ++i) {
            sum += double(channel.bins[i]) * i;
            squares += double(channel.bins[i]) * i * i;
        }
        channel.mean = sum / result.pixelCount;
        channel.standardDeviation = std::sqrt(std::max(0.0, squares / result.pixelCount - channel.mean * channel.mean));
    }
    return result;
}
}

EditorLayerHistogram::Result EditorLayerHistogram::calculate(const Request &request) {
    if (!request.snapshot.document) { Result result; result.error = "The histogram snapshot is unavailable."; return result; }
    return ::analyze(*request.snapshot.document, request.snapshot.frame, request.snapshot.artboardId,
        request.composite, request.selectedLayer, &request.visibility, request.cancelled);
}
EditorLayerHistogram::Result EditorLayerHistogram::analyze(const iiSharedCanvas::Document &document,
    iiSharedCanvas::FrameIndex frame, const std::string &artboard, bool composite, const std::string &selection) {
    return ::analyze(document, frame, artboard, composite, selection, nullptr, {});
}
QVariantMap EditorLayerHistogram::describe(const Result &result, const QString &channel, bool available, bool pending) {
    QVariantMap data{{"pixelCount", qulonglong(result.pixelCount)}, {"available", available},
        {"pending", pending}, {"error", QString::fromStdString(result.error)}};
    for (std::size_t i = 0; i < 3; ++i) {
        QVariantList bins; bins.reserve(256);
        for (const auto count : result.channels[i].bins) bins.append(qulonglong(count));
        data[QStringList{"red", "green", "blue"}[int(i)]] = bins;
    }
    const auto index = channel == "Red" ? 0 : channel == "Green" ? 1 : channel == "Blue" ? 2 : 3;
    data["mean"] = result.channels[index].mean;
    data["standardDeviation"] = result.channels[index].standardDeviation;
    return data;
}

EditorLayerHistogram::EditorLayerHistogram(QObject *parent) : EditorLayerHistogram(calculate, parent) {}
EditorLayerHistogram::EditorLayerHistogram(Operation operation, QObject *parent)
    : QObject(parent), m_operation(std::move(operation)) {
    m_delay.setSingleShot(true); m_delay.setInterval(60);
    connect(&m_delay, &QTimer::timeout, this, &EditorLayerHistogram::prepare);
    connect(&m_completion, &QFutureWatcher<Result>::finished, this, &EditorLayerHistogram::complete);
    project();
}
EditorLayerHistogram::~EditorLayerHistogram() {
    retire(false);
    disconnect(&m_completion, nullptr, this, nullptr);
}
EditorCanvas *EditorLayerHistogram::canvas() const { return m_canvas; }
bool EditorLayerHistogram::available() const {
    return m_canvas && m_canvas->documentReady() && !m_canvas->infiniteCanvas()
        && (m_composite || !m_canvas->selectedLayerId().isEmpty());
}
void EditorLayerHistogram::setCanvas(EditorCanvas *canvas) {
    if (canvas == m_canvas) return;
    if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas;
    m_observedSelection = canvas ? canvas->selectedLayerId() : QString{};
    if (canvas) {
        connect(canvas, &EditorCanvas::revisionChanged, this, [this] { invalidate(true); });
        connect(canvas, &EditorCanvas::documentChanged, this, [this] { invalidate(true); });
        connect(canvas, &EditorCanvas::frameChanged, this, [this] { if (m_active && m_timeDependent) invalidate(); });
        connect(canvas, &EditorCanvas::activeArtboardChanged, this, [this] { invalidate(); });
        connect(canvas, &EditorCanvas::layerStateChanged, this, [this] {
            const auto selected = m_canvas->selectedLayerId();
            if (selected == m_observedSelection) return;
            m_observedSelection = selected;
            if (!m_composite) invalidate();
        });
        connect(canvas, &EditorCanvas::liveStrokeActiveChanged, this, [this] { invalidate(true); });
        connect(canvas, &QObject::destroyed, this, [this] { m_canvas = nullptr; invalidate(true, true); });
    }
    invalidate(true, true);
}
void EditorLayerHistogram::setActive(bool active) {
    if (m_active == active) return;
    m_active = active; invalidate(false, true);
}
void EditorLayerHistogram::setComposite(bool composite) {
    if (m_composite == composite) return;
    m_composite = composite; invalidate(false, true);
}
void EditorLayerHistogram::setChannel(const QString &channel) {
    const auto next = QStringList{"RGB", "Red", "Green", "Blue"}.contains(channel) ? channel : QStringLiteral("RGB");
    if (m_channel == next) return;
    m_channel = next; project(); emit changed();
}
void EditorLayerHistogram::retire(bool clearCache) {
    ++m_epoch; m_delay.stop(); m_wanted.clear(); m_waiting = false; m_timeDependent = true;
    if (m_running) m_running->request.cancelled->store(true);
    if (m_pending) m_pending->request.cancelled->store(true);
    m_pending.reset(); m_result.reset();
    if (clearCache) m_cache.clear();
}
void EditorLayerHistogram::invalidate(bool clearCache, bool configurationChanged) {
    retire(clearCache);
    m_waiting = m_active && available();
    if (m_waiting && !m_canvas->liveStrokeActive()) m_delay.start();
    const bool dataChanged = project();
    if (dataChanged || configurationChanged) emit changed();
}
bool EditorLayerHistogram::project() {
    // Pointer/frame bursts supersede work, but do not repeatedly allocate or
    // republish the same pending map to every chart/statistic binding.
    const bool eligible = available();
    if (!m_result && !m_histogram.isEmpty() && m_histogram.value("pending").toBool() == m_waiting
        && m_histogram.value("available").toBool() == eligible && !m_histogram.value("pixelCount").toULongLong()
        && m_histogram.value("error").toString().isEmpty()) return false;
    auto next = describe(m_result ? *m_result : Result{}, m_channel, eligible, m_waiting);
    if (next == m_histogram) return false;
    m_histogram = std::move(next); return true;
}
void EditorLayerHistogram::refresh() {
    if (!m_active || !available()) return;
    m_cache.clear(); invalidate();
}
void EditorLayerHistogram::prepare() {
    if (!m_active || !available() || m_canvas->liveStrokeActive()) return;
    try {
        const auto selection = m_composite ? std::string{} : m_canvas->selectedLayerId().toStdString();
        const auto artboard = m_canvas->editingArtboardId();
        m_timeDependent = timeDependent(*m_canvas->document(), artboard, m_composite, selection);
        const auto hex = [](const std::string &text) { return QString::fromLatin1(QByteArray(text.data(), qsizetype(text.size())).toHex()); };
        const auto key = QStringList{QString::number(m_canvas->revision()), QString::number(m_timeDependent ? m_canvas->frame() : 0),
            hex(artboard), m_composite ? "composite" : "layer", hex(selection)}.join(':');
        m_wanted = key;
        if (const auto *cached = m_cache.object(key)) {
            m_result = *cached; m_waiting = false; project(); emit changed(); return;
        }
        Request request; request.snapshot = m_canvas->editingFrameSnapshot(); request.selectedLayer = selection; request.composite = m_composite;
        request.cancelled = std::make_shared<std::atomic_bool>(false);
        const auto *document = m_canvas->document();
        request.visibility.artboards.reserve(document->artboards.size()); request.visibility.layers.reserve(document->layers.size());
        for (const auto &board : document->artboards) request.visibility.artboards.push_back(board.visible);
        for (const auto &layer : document->layers) request.visibility.layers.push_back(iiSharedCanvas::layerProperties(layer).visible);
        m_pending = Job{key, std::move(request), m_epoch}; launch();
    } catch (const std::exception &error) {
        m_result.emplace(); m_result->error = error.what(); m_waiting = false; project(); emit changed();
    }
}
void EditorLayerHistogram::launch() {
    if (m_running || !m_pending) return;
    m_running = std::move(m_pending); m_pending.reset();
    m_completion.setFuture(QtConcurrent::run([operation = m_operation, request = m_running->request] {
        try { return operation(request); }
        catch (const std::exception &error) { Result result; result.error = error.what(); return result; }
        catch (...) { Result result; result.error = "The histogram could not be calculated."; return result; }
    }));
}
void EditorLayerHistogram::complete() {
    if (!m_running) return;
    auto job = std::move(*m_running); m_running.reset();
    auto result = m_completion.future().takeResult();
    const bool current = m_active && available() && job.epoch == m_epoch && job.key == m_wanted;
    if (current) {
        m_cache.insert(job.key, new Result(result)); m_result = std::move(result); m_waiting = false; project();
    }
    launch();
    // All state is published before direct observers can close/rebind this view.
    if (current) emit changed();
}
