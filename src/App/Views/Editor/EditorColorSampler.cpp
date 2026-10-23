#include "EditorColorSampler.h"
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <QtConcurrentRun>
#include <exception>

EditorColorSampler::Result EditorColorSampler::sample(const Request &request, std::stop_token stop) {
    Result result;
    const auto stopped = [&] { result.cancelled = stop.stop_requested(); return result.cancelled; };
    if (stopped()) return result;
    if (!request.snapshot.document) { result.error = "The color sample snapshot is unavailable."; return result; }
    const auto &input = *request.snapshot.document;
    if (request.visibility.artboards.size() != input.artboards.size() || request.visibility.layers.size() != input.layers.size()) {
        result.error = "The color sample snapshot is incomplete."; return result;
    }
    bool restore = false;
    for (std::size_t i = 0; i < input.artboards.size(); ++i) restore |= input.artboards[i].visible != request.visibility.artboards[i];
    for (std::size_t i = 0; i < input.layers.size(); ++i) restore |= iiSharedCanvas::layerProperties(input.layers[i]).visible != request.visibility.layers[i];
    const auto *source = &input;
    std::optional<iiSharedCanvas::Document> authored;
    if (restore) {
        // Focus presentation can hide sibling boards. Restore authored flags on
        // this worker without another raster-payload copy on the input thread.
        authored.emplace(input);
        for (std::size_t i = 0; i < authored->artboards.size(); ++i) authored->artboards[i].visible = request.visibility.artboards[i];
        for (std::size_t i = 0; i < authored->layers.size(); ++i) iiSharedCanvas::layerProperties(authored->layers[i]).visible = request.visibility.layers[i];
        source = &*authored;
    }
    if (stopped()) return result;
    const iiSharedCanvas::CanvasRegion region{request.worldPosition, {1, 1}};
    const auto rendered = request.snapshot.artboardId.empty()
        ? iiSharedCanvas::renderFrameRegion(*source, request.snapshot.frame, region, {1, 1})
        : iiSharedCanvas::renderArtboardRegion(*source, request.snapshot.frame, request.snapshot.artboardId, region, {1, 1});
    if (stopped()) return result;
    if (!rendered.ok()) { result.error = rendered.message; return result; }
    if (rendered.pixels.pixels.size() != 1) { result.error = "The color sample has no pixel."; return result; }
    result.argb = rendered.pixels.pixels.front();
    return result;
}

EditorColorSampler::EditorColorSampler(QObject *parent) : EditorColorSampler(sample, parent) {}
EditorColorSampler::EditorColorSampler(Operation operation, QObject *parent)
    : QObject(parent), m_operation(std::move(operation)) {
    connect(&m_completion, &QFutureWatcher<Result>::finished, this, &EditorColorSampler::complete);
}
EditorColorSampler::~EditorColorSampler() {
    cancel();
    disconnect(&m_completion, nullptr, this, nullptr);
}
bool EditorColorSampler::cancel() {
    const bool wasActive = active(); m_wanted = 0;
    if (m_running) m_running->stop.request_stop();
    if (m_pending) m_pending->stop.request_stop();
    m_pending.reset();
    return wasActive;
}
qulonglong EditorColorSampler::submit(Request request) {
    auto input = std::make_shared<const Request>(std::move(request));
    auto token = ++m_serial; if (!token) token = ++m_serial;
    cancel();
    m_wanted = token;
    m_pending.emplace(Job{token, std::move(input), {}});
    try { launch(); }
    catch (...) { m_pending.reset(); m_wanted = 0; throw; }
    return token;
}
void EditorColorSampler::launch() {
    if (m_running || !m_pending) return;
    auto future = QtConcurrent::run([operation = m_operation, input = m_pending->input, stop = m_pending->stop.get_token()] {
        try { return operation(*input, stop); }
        catch (const std::exception &error) { Result result; result.error = error.what(); return result; }
        catch (...) { Result result; result.error = "The color could not be sampled."; return result; }
    });
    m_running = std::move(m_pending); m_pending.reset();
    m_completion.setFuture(std::move(future));
}
void EditorColorSampler::complete() {
    if (!m_running) return;
    const auto token = m_running->token;
    auto result = m_completion.future().takeResult();
    m_running.reset();
    const bool current = token == m_wanted;
    if (current) m_wanted = 0;
    try { launch(); }
    catch (const std::exception &error) {
        const auto failed = m_wanted; m_pending.reset(); m_wanted = 0;
        emit finished(failed, 0, QString::fromUtf8(error.what()), false); return;
    }
    catch (...) {
        const auto failed = m_wanted; m_pending.reset(); m_wanted = 0;
        emit finished(failed, 0, tr("The color read could not be started."), false); return;
    }
    // Observers may close the owner or submit another point. Publish only after
    // all state transitions, with no access to members after the signal.
    if (current) emit finished(token, result.argb, QString::fromStdString(result.error), result.cancelled);
}
