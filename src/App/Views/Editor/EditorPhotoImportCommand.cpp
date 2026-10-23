#include "EditorPhotoImportCommand.h"
#include "EditorCanvas.h"
#include "EditorCameraPhoto.h"
#include "EditorToolUtils.h"
#include <QtConcurrentRun>
#include <exception>
using namespace iiSharedCanvas;

EditorPhotoImportCommand::EditorPhotoImportCommand(EditorCanvas *canvas, EditorCameraPhoto *source, QObject *parent)
    : EditorPhotoImportCommand(canvas, source, dreamscapes::preparePhotoImport, parent) {}
EditorPhotoImportCommand::EditorPhotoImportCommand(EditorCanvas *canvas, EditorCameraPhoto *source,
    dreamscapes::PhotoImportOperation operation, QObject *parent)
    : QObject(parent), m_canvas(canvas), m_sources(source), m_operation(std::move(operation)) {
    connect(&m_completion, &QFutureWatcher<dreamscapes::PhotoImportOutput>::finished, this, &EditorPhotoImportCommand::complete);
    connect(source, &EditorCameraPhoto::importStarted, this, &EditorPhotoImportCommand::capture);
    connect(source, &EditorCameraPhoto::originalsReady, this, &EditorPhotoImportCommand::prepare);
    connect(source, &EditorCameraPhoto::importCancelled, this, [this](qulonglong token) { if (m_target && m_target->token == token) retire({}, false); });
    const auto changed = [this] {
        if (!m_target) return;
        if (m_adopting && m_canvas && m_canvas->m_graphicDocumentSequence == m_target->documentSequence) return;
        retire(tr("The destination document changed while preparing photos. Import them again."));
    };
    connect(canvas, &EditorCanvas::revisionChanged, this, changed);
    connect(canvas, &EditorCanvas::documentChanged, this, changed);
    connect(canvas, &EditorCanvas::fileStateChanged, this, [this] {
        if (!m_target || (m_adopting && m_canvas && m_canvas->m_graphicDocumentSequence == m_target->documentSequence)) return;
        if (!matches(*m_target)) retire(tr("The destination file changed while preparing photos."));
    });
    connect(canvas, &EditorCanvas::liveStrokeActiveChanged, this, [this] { if (m_target && m_canvas && m_canvas->liveStrokeActive()) retire(tr("Finish the current stroke before importing photos.")); });
    connect(canvas, &QObject::destroyed, this, [this] { m_canvas = nullptr; retire(tr("The destination canvas is unavailable.")); });
    connect(source, &QObject::destroyed, this, [this] { m_sources = nullptr; retire({}, false); });
}
EditorPhotoImportCommand::~EditorPhotoImportCommand() { retire({}, false); }
bool EditorPhotoImportCommand::matches(const Target &target, bool afterApply) const {
    return m_canvas && m_canvas->documentReady() && !m_canvas->liveStrokeActive()
        && m_canvas->m_graphicDocumentSequence == target.documentSequence
        && m_canvas->revision() == target.revision + (afterApply ? 1 : 0) && m_canvas->filePath() == target.path
        && (target.artboard.empty() || findArtboard(*m_canvas->document(), target.artboard));
}
void EditorPhotoImportCommand::capture(qulonglong token) {
    if (!m_sources || !m_sources->claimImport(token)) return;
    if (m_adopting || !m_canvas || !m_canvas->documentReady() || m_canvas->liveStrokeActive() || !m_operation) {
        m_sources->finishImport(token, tr("Choose an open, idle canvas for photo import.")); return;
    }
    Target target; target.token = token; target.documentSequence = m_canvas->m_graphicDocumentSequence;
    target.revision = m_canvas->revision(); target.path = m_canvas->filePath();
    target.selectedLayer = m_canvas->selectedLayerId(); target.artboard = m_canvas->editingArtboardId(); target.extent = m_canvas->editingExtent();
    target.frameRate = m_canvas->document()->timeline.frameRate;
    target.storageRoot = std::filesystem::path(m_canvas->settingsFor("camera-photo").value("libraryDirectory").toString().toStdU16String());
    m_target = std::move(target);
}
void EditorPhotoImportCommand::prepare(qulonglong token, const dreamscapes::PhotoOriginals &sources) {
    if (!m_target || m_target->token != token) return;
    if (!matches(*m_target)) { retire(tr("The destination document changed while preparing photos.")); return; }
    Job job; job.target = *m_target; job.input.originals = sources; job.input.storageRoot = m_target->storageRoot;
    job.input.frameRate = m_target->frameRate;
    job.input.cancelled = std::make_shared<std::atomic_bool>(false); m_pending = std::move(job);
    try { launch(); } catch (const std::exception &error) { retire(QString::fromUtf8(error.what())); }
}
void EditorPhotoImportCommand::retire(const QString &error, bool notify) {
    const auto token = m_target ? m_target->token : 0; m_target.reset();
    if (m_running) m_running->input.cancelled->store(true);
    if (m_pending) m_pending->input.cancelled->store(true);
    m_pending.reset();
    if (token && m_sources) {
        if (!notify) m_sources->abandonImport(token);
        else if (error.isEmpty()) m_sources->cancelImport();
        else m_sources->finishImport(token, error);
    }
}
void EditorPhotoImportCommand::launch() {
    if (m_running || !m_pending) return;
    auto future = QtConcurrent::run([operation = m_operation, input = m_pending->input] {
        try { return operation(input); }
        catch (const std::exception &error) { dreamscapes::PhotoImportOutput out; out.error = error.what(); return out; }
        catch (...) { dreamscapes::PhotoImportOutput out; out.error = "The photos could not be prepared."; return out; }
    });
    m_running = std::move(m_pending); m_pending.reset(); m_completion.setFuture(std::move(future));
}
void EditorPhotoImportCommand::complete() {
    if (!m_running) return;
    auto job = std::move(*m_running); m_running.reset(); auto output = m_completion.future().takeResult();
    if (!m_target || m_target->token != job.target.token) {
        try { launch(); } catch (const std::exception &error) { retire(QString::fromUtf8(error.what())); }
        return;
    }
    if (!matches(job.target)) { retire(tr("The destination document changed while preparing photos.")); return; }
    if (!output.ok()) {
        retire(output.cancelled ? QString{} : output.error.empty() ? tr("Photo preparation returned no usable layers.") : QString::fromStdString(output.error)); return;
    }
    // All pixels are ready before one document transaction and its synchronous save.
    std::vector<std::string> selected;
    m_adopting = true; const QPointer<EditorPhotoImportCommand> owner(this);
    const bool saved = m_canvas->commit([&](Document &document) {
        for (auto &photo : output.photos) {
            const auto asset = editorTools::unique("media.asset."), layer = editorTools::unique("media.layer.");
            const auto *video = std::get_if<VideoAsset>(&photo.media);
            if (video && video->frames.empty()) return false;
            const auto &pixels = video ? video->frames.front() : std::get<RasterLayer>(photo.media);
            if (pixels.width <= 0 || pixels.height <= 0) return false;
            LayerProperties properties{layer, photo.name};
            if (!job.target.artboard.empty()) properties.artboardId = job.target.artboard;
            const double scale = std::min(double(job.target.extent.width) / pixels.width, double(job.target.extent.height) / pixels.height);
            properties.transform.m11 = properties.transform.m22 = scale;
            properties.transform.translationX = (job.target.extent.width - pixels.width * scale) / 2;
            properties.transform.translationY = (job.target.extent.height - pixels.height * scale) / 2;
            if (video) {
                auto prepared = std::move(std::get<VideoAsset>(photo.media)); prepared.id = asset;
                document.timeline.frameCount = std::max(document.timeline.frameCount, FrameIndex(prepared.frames.size()));
                document.assets.emplace_back(std::move(prepared)); document.layers.emplace_back(VideoLayer{std::move(properties), StaticSource{asset}});
            } else {
                document.assets.emplace_back(RasterAsset{asset, std::move(std::get<RasterLayer>(photo.media))});
                document.layers.emplace_back(StaticBitmapLayer{std::move(properties), StaticSource{asset}});
            }
            selected.push_back(layer);
        }
        return !selected.empty();
    });
    if (!owner) return;
    m_adopting = false;
    if (!m_target || m_target->token != job.target.token) return;
    if (!saved) { retire(m_canvas ? m_canvas->error() : tr("The destination canvas is unavailable.")); return; }
    if (!matches(job.target, true)) { retire(tr("The destination changed during photo placement.")); return; }
    m_target.reset();
    if (m_canvas->editingArtboardId() == job.target.artboard && m_canvas->selectedLayerId() == job.target.selectedLayer && !selected.empty()) {
        m_canvas->selectLayer(QString::fromStdString(selected.back())); if (!owner) return;
    }
    if (m_sources) m_sources->finishImport(job.target.token, {});
}
