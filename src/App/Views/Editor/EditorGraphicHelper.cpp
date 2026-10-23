#include "EditorGraphicHelper.h"
#include "EditorCanvas.h"
#include "EditorGraphicAdjust.h"
#include <iiGraphicAdjust/iiGraphicAdjust.h>
#include <QtConcurrentRun>
#include <algorithm>
#include <exception>

EditorGraphicHelper::Result EditorGraphicHelper::calculate(const Request &request, std::stop_token stop) {
    Result result;
    const auto stopped = [&] { result.cancelled = stop.stop_requested(); return result.cancelled; };
    if (stopped()) return result;
    if (!graphicHelper::needsImage(request.calculation)) return graphicHelper::calculate(request.calculation, nullptr, stop);
    auto source = request.previewPixels;
    if (!source) {
        auto captured = editorRaster::read(request.snapshot, request.selectedLayer, stop);
        if (!captured) { if (stopped()) return result; result.error = captured.error(); return result; }
        source = std::move(*captured);
    }
    if (stopped()) return result;
    const auto imported = iiGraphicAdjust::fromArgb32(source->width, source->height, source->pixels);
    if (!imported) { result.error = imported.error().message; return result; }
    if (stopped()) return result;
    const auto view = imported->view();
    return graphicHelper::calculate(request.calculation, &view, stop);
}

EditorGraphicHelper::EditorGraphicHelper(QObject *parent) : EditorGraphicHelper(calculate, parent) {}
EditorGraphicHelper::EditorGraphicHelper(Operation operation, QObject *parent) : QObject(parent), m_operation(std::move(operation)) {
    connect(&m_completion, &QFutureWatcher<Result>::finished, this, &EditorGraphicHelper::complete);
    project("idle");
}
EditorGraphicHelper::~EditorGraphicHelper() { retire(); disconnect(&m_completion, nullptr, this, nullptr); }
EditorCanvas *EditorGraphicHelper::canvas() const { return m_canvas; }
void EditorGraphicHelper::retire() {
    m_wanted = 0;
    if (m_running) m_running->stop.request_stop();
    if (m_pending) m_pending->stop.request_stop();
    m_pending.reset();
}
bool EditorGraphicHelper::project(const QString &status, const QString &error, QVariantMap parameters) {
    QVariantMap next{{"pending", status == "pending"}, {"valid", status == "ready"}, {"status", status},
        {"operation", status == "idle" ? QString{} : m_requestedOperation}, {"parameters", std::move(parameters)},
        {"error", error}, {"sequence", m_sequence}};
    if (next == m_state) return false;
    m_state = std::move(next); return true;
}
void EditorGraphicHelper::invalidate(bool forceNotify) {
    retire(); const bool dataChanged = project("idle");
    if (dataChanged || forceNotify) emit changed();
}
void EditorGraphicHelper::setCanvas(EditorCanvas *canvas) {
    if (canvas == m_canvas) return;
    if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas; m_observedTool = canvas ? canvas->m_tool : QString{};
    if (canvas) {
        connect(canvas, &EditorCanvas::documentChanged, this, [this] { invalidate(); });
        connect(canvas, &EditorCanvas::revisionChanged, this, [this] { invalidate(); });
        connect(canvas, &EditorCanvas::frameChanged, this, [this] { invalidate(); });
        connect(canvas, &EditorCanvas::selectionChanged, this, [this] { invalidate(); });
        connect(canvas, &EditorCanvas::activeArtboardChanged, this, [this] { invalidate(); });
        connect(canvas, &EditorCanvas::liveStrokeActiveChanged, this, [this] { if (m_canvas->liveStrokeActive()) invalidate(); });
        connect(canvas, &EditorCanvas::toolStateChanged, this, [this] {
            const auto tool = m_canvas->m_tool;
            const bool switched = tool != m_observedTool; m_observedTool = tool;
            if (switched || (m_wanted && m_canvas->m_pixelLocks.contains(m_canvas->selectedLayerId()))) invalidate();
        });
        connect(canvas, &QObject::destroyed, this, [this] { m_canvas = nullptr; m_observedTool.clear(); invalidate(true); });
    }
    invalidate(true);
}
void EditorGraphicHelper::setContext(const QVariantMap &context) {
    if (context == m_context) return;
    m_context = context; invalidate(true);
}
bool EditorGraphicHelper::cancel() {
    if (!m_wanted) return false;
    invalidate(); return true;
}
qulonglong EditorGraphicHelper::request(const QString &operation, const QVariantMap &parameters) {
    const auto reject = [this](const QString &error) -> qulonglong {
        retire(); project("error", error); emit changed(); return 0;
    };
    try {
        m_requestedOperation = EditorGraphicAdjust::canonicalOperation(operation);
        const auto prepared = EditorGraphicAdjust::prepareHelper(operation, parameters);
        if (!prepared.ok()) { m_requestedOperation = operation; return reject(prepared.error); }
        if (!m_canvas) return reject(tr("Choose an open canvas for this settings calculation."));
        const auto capability = m_canvas->graphicOperationState(operation);
        if (!capability.value("enabled").toBool()) return reject(capability.value("reason").toString());
        if (m_canvas->liveStrokeActive()) return reject(tr("Finish the current stroke before calculating settings."));
        Request input; input.calculation = *prepared.request;
        if (graphicHelper::needsImage(input.calculation)) {
            const auto canonical = EditorGraphicAdjust::canonicalOperation(operation);
            if (m_canvas->toolPreviewMatches(m_context.value("tool").toString()) && m_canvas->m_previewGraphicOperation == canonical)
                input.previewPixels = m_canvas->m_previewPixels;
            else { input.snapshot = m_canvas->editingFrameSnapshot(); input.selectedLayer = m_canvas->selectedLayerId().toStdString(); }
        }
        auto token = ++m_serial; if (!token) token = ++m_serial;
        Job job{token, std::make_shared<const Request>(std::move(input)), {}};
        retire(); m_requestedOperation = EditorGraphicAdjust::canonicalOperation(operation);
        m_wanted = token; m_pending.emplace(std::move(job)); const bool changedState = project("pending");
        launch();
        if (changedState) emit changed(); return token;
    } catch (const std::exception &error) { return reject(QString::fromUtf8(error.what())); }
    catch (...) { return reject(tr("The settings calculation could not be started.")); }
}
void EditorGraphicHelper::launch() {
    if (m_running || !m_pending) return;
    auto future = QtConcurrent::run([operation = m_operation, input = m_pending->input, stop = m_pending->stop.get_token()] {
        try { return operation(*input, stop); }
        catch (const std::exception &error) { Result result; result.error = error.what(); return result; }
        catch (...) { Result result; result.error = "The settings calculation failed."; return result; }
    });
    m_running = std::move(m_pending); m_pending.reset(); m_completion.setFuture(std::move(future));
}
void EditorGraphicHelper::complete() {
    if (!m_running) return;
    const auto token = m_running->token; auto result = m_completion.future().takeResult(); m_running.reset();
    const bool current = token == m_wanted;
    if (current) {
        m_wanted = 0;
        if (result.ok()) { ++m_sequence; project("ready", {}, EditorGraphicAdjust::helperParameters(result)); }
        else project(result.cancelled ? "idle" : "error", QString::fromStdString(result.error));
    }
    try { launch(); }
    catch (const std::exception &error) { retire(); project("error", QString::fromUtf8(error.what())); emit changed(); return; }
    catch (...) { retire(); project("error", tr("The next settings calculation could not be started.")); emit changed(); return; }
    if (current) emit changed(); // No member access after direct observers run.
}
