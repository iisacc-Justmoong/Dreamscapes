#include "EditorGraphicCommand.h"
#include "EditorCanvas.h"
#include "EditorGraphicAdjust.h"
#include <QtConcurrentRun>
#include <exception>

EditorGraphicCommand::EditorGraphicCommand(QObject *parent) : EditorGraphicCommand(graphicImage::calculate, parent) {}
EditorGraphicCommand::EditorGraphicCommand(Operation operation, QObject *parent) : QObject(parent), m_operation(std::move(operation)) {
    connect(&m_completion, &QFutureWatcher<Result>::finished, this, &EditorGraphicCommand::complete); project("idle");
}
EditorGraphicCommand::~EditorGraphicCommand() { retire(); disconnect(&m_completion, nullptr, this, nullptr); }
EditorCanvas *EditorGraphicCommand::canvas() const { return m_canvas; }
void EditorGraphicCommand::retire() {
    m_wanted = 0;
    if (m_running) m_running->stop.request_stop();
    if (m_pending) m_pending->stop.request_stop();
    m_pending.reset();
}
void EditorGraphicCommand::project(const QString &status, const QString &error, const Result *result) {
    m_state = {{"pending", status == "pending"}, {"valid", status == "ready"}, {"status", status},
        {"operation", m_requestedOperation}, {"action", m_action}, {"error", error}, {"sequence", m_sequence}};
    if (result) {
        for (auto it = result->preview.cbegin(); it != result->preview.cend(); ++it) m_state[it.key()] = it.value();
        m_state["parameters"] = result->parameters; m_state["mask"] = result->mask;
    }
}
void EditorGraphicCommand::invalidate(bool notify) {
    const bool changedState = m_state.value("status").toString() != "idle";
    if (!m_wanted && !changedState && !notify) return;
    retire(); project("idle"); if (changedState || notify) emit changed();
}
const EditorGraphicCommand::Job *EditorGraphicCommand::current() const {
    if (m_pending && m_pending->token == m_wanted) return &*m_pending;
    return m_running && m_running->token == m_wanted ? &*m_running : nullptr;
}
void EditorGraphicCommand::setCanvas(EditorCanvas *canvas) {
    if (m_canvas == canvas) return;
    if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas;
    if (canvas) {
        const auto changedTarget = [this] {
            if (m_adopting) {
                if (m_canvas && m_canvas->m_graphicDocumentSequence == m_adoptingDocumentSequence) return;
            }
            invalidate();
        };
        connect(canvas, &EditorCanvas::documentChanged, this, changedTarget);
        connect(canvas, &EditorCanvas::revisionChanged, this, changedTarget);
        connect(canvas, &EditorCanvas::frameChanged, this, [this] { invalidate(); });
        connect(canvas, &EditorCanvas::selectionChanged, this, [this] { invalidate(); });
        connect(canvas, &EditorCanvas::activeArtboardChanged, this, [this] { invalidate(); });
        connect(canvas, &EditorCanvas::liveStrokeActiveChanged, this, [this] { if (m_canvas && m_canvas->liveStrokeActive()) invalidate(); });
        connect(canvas, &EditorCanvas::toolStateChanged, this, [this] {
            if (m_adopting) return;
            if (const auto *job = current(); job && (!m_canvas || !m_canvas->graphicImageContextMatches(*job->input))) invalidate();
        });
        connect(canvas, &QObject::destroyed, this, [this] { m_canvas = nullptr; invalidate(true); });
    }
    invalidate(true);
}
void EditorGraphicCommand::setContext(const QVariantMap &context) {
    if (context == m_context) return;
    m_context = context; invalidate(true);
}
bool EditorGraphicCommand::cancel() {
    if (!m_wanted) return false;
    invalidate(); return true;
}
qulonglong EditorGraphicCommand::request(const QString &action, const QString &tool, const QString &operation, const QVariantMap &parameters) {
    const auto reject = [this](const QString &error) -> qulonglong {
        retire(); project("error", error); emit changed(); return 0;
    };
    if (m_adopting) return reject(tr("The previous image command is being applied."));
    try {
        m_action = action; m_requestedOperation = EditorGraphicAdjust::canonicalOperation(operation);
        if (!m_canvas || !m_operation) return reject(tr("Choose an open canvas for image processing."));
        if (action != "preview" && action != "apply" && action != "mask-helper") return reject(tr("Choose a valid image command."));
        if (action == "mask-helper" && !graphicImage::maskOperation(m_requestedOperation))
            return reject(tr("Choose a mask operation for this command."));
        QString error; auto input = m_canvas->graphicImageInput(tool, operation, parameters, action == "preview", error);
        if (!input) return reject(error);
        if (!m_canvas || !m_canvas->graphicImageContextMatches(*input)) return reject(tr("The image target changed while preparing the command."));
        auto token = ++m_serial; if (!token) token = ++m_serial;
        Job job{token, action, std::make_shared<const Input>(std::move(*input)), {}};
        retire(); m_wanted = token; m_action = action; m_requestedOperation = job.input->operation;
        m_pending.emplace(std::move(job)); project("pending"); launch(); emit changed(); return token;
    } catch (const std::exception &error) { return reject(QString::fromUtf8(error.what())); }
    catch (...) { return reject(tr("The image command could not be started.")); }
}
void EditorGraphicCommand::launch() {
    if (m_running || !m_pending) return;
    auto future = QtConcurrent::run([operation = m_operation, input = m_pending->input, stop = m_pending->stop.get_token()] {
        try {
            auto result = operation(*input, stop);
            if (result.ok() && result.mask != graphicImage::maskOperation(input->operation))
                result.error = "The image result does not match the requested operation.";
            if (result.ok() && result.mask && !input->preview) {
                result.error = graphicImage::validateMaskOutput(*input, result, stop);
                if (stop.stop_requested()) result.cancelled = true;
            }
            return result;
        }
        catch (const std::exception &error) { Result result; result.error = QString::fromUtf8(error.what()); return result; }
        catch (...) { Result result; result.error = "The image command failed."; return result; }
    });
    m_running = std::move(m_pending); m_pending.reset(); m_completion.setFuture(std::move(future));
}
void EditorGraphicCommand::complete() {
    if (!m_running) return;
    auto job = std::move(*m_running); m_running.reset(); auto result = m_completion.future().takeResult();
    bool notify = false;
    bool currentResult = job.token == m_wanted;
    if (currentResult && (!m_canvas || !m_canvas->graphicImageContextMatches(*job.input))) {
        retire(); project("idle"); currentResult = false; notify = true;
    }
    if (currentResult) {
        if (result.ok() && !job.input->preview) {
            Result publication; publication.preview = result.preview; publication.parameters = result.parameters;
            publication.mask = result.mask; publication.pixels = result.pixels;
            m_adopting = true; m_adoptingDocumentSequence = job.input->documentSequence;
            const QPointer<EditorGraphicCommand> owner(this); QString error;
            const bool applied = m_canvas->applyGraphicImageResult(*job.input, std::move(result), error);
            if (!owner) return;
            m_adopting = false;
            result = std::move(publication);
            if (!applied) result.error = error;
            currentResult = job.token == m_wanted;
        }
        if (currentResult) {
            notify = true;
            m_wanted = 0;
            if (result.ok()) { ++m_sequence; project("ready", {}, &result); }
            else project(result.cancelled ? "idle" : "error", result.error);
        }
    }
    try { launch(); }
    catch (const std::exception &error) { retire(); project("error", QString::fromUtf8(error.what())); emit changed(); return; }
    catch (...) { retire(); project("error", tr("The next image command could not be started.")); emit changed(); return; }
    if (notify) emit changed();
}
