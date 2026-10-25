#include "WorkspaceSession.h"
#include "ViewData.h"
#include "Native/GenerationPresentation.h"
#include "../Generation/GenerationController.h"
#include "../Views/Home/ImageOpenRequest.h"
#include "../Views/Editor/EditorProject.h"
#include "../Views/Editor/EditorImageImportRequest.h"
#include <iiSocietyHelper/FileDownload.h>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QScopedValueRollback>

namespace dreamscapes {
WorkspaceSession::WorkspaceSession(QObject *parent) : QObject(parent), state_(viewValue(model_.presentation()).toMap()) {}
QObject *WorkspaceSession::generation() const { return generation_; }
void WorkspaceSession::setGeneration(QObject *value) { auto *next = qobject_cast<GenerationController *>(value); if (generation_ != next) { generation_ = next; emit generationChanged(); } }
void WorkspaceSession::setEditor(QObject *value) { if (editor_ != value) { editor_ = value; emit editorChanged(); } }
QObject *WorkspaceSession::imageOpen() const { return imageOpen_; }
void WorkspaceSession::setImageOpen(QObject *value) { auto *next = qobject_cast<ImageOpenRequest *>(value); if (imageOpen_ != next) { imageOpen_ = next; emit imageOpenChanged(); } }
void WorkspaceSession::change(const QVariantMap &patch) {
    const auto request = nativeValue(patch); const auto result = model_.changes().send({vm::object(request)}); if (!result) return;
    const auto old = state_; state_ = viewValue(*result).toMap();
    if (old.value("resultVisible") != state_.value("resultVisible")) emit resultVisibleChanged();
    if (old.value("editorVisible") != state_.value("editorVisible")) emit editorVisibleChanged();
    if (old.value("publishVisible") != state_.value("publishVisible")) emit publishVisibleChanged();
    if (old.value("storageBrowserSection") != state_.value("storageBrowserSection")) emit storageBrowserSectionChanged();
    if (old.value("pendingFileKey") != state_.value("pendingFileKey") || old.value("pendingFileSection") != state_.value("pendingFileSection") || old.value("pendingFileContainerId") != state_.value("pendingFileContainerId") || old.value("pendingCanvasOpen") != state_.value("pendingCanvasOpen")) emit pendingChanged();
    if (old.value("canvasImageSource") != state_.value("canvasImageSource") || old.value("canvasSpecification") != state_.value("canvasSpecification") || old.value("canvasGenerationResult") != state_.value("canvasGenerationResult") || old.value("canvasGenerationResults") != state_.value("canvasGenerationResults")) emit canvasChanged();
    if (old.value("currentResult") != state_.value("currentResult")) emit currentResultChanged();
    if (old.value("resultJobIds") != state_.value("resultJobIds")) emit resultJobIdsChanged();
    if (old.value("generationRequestError") != state_.value("generationRequestError")) emit generationRequestErrorChanged();
}
#define SESSION_BOOL(name, Name) bool WorkspaceSession::name() const { return state_.value(#name).toBool(); } void WorkspaceSession::set##Name(bool value) { change({{#name, value}}); }
SESSION_BOOL(resultVisible, ResultVisible) SESSION_BOOL(editorVisible, EditorVisible) SESSION_BOOL(publishVisible, PublishVisible)
#undef SESSION_BOOL
QString WorkspaceSession::storageBrowserSection() const { return state_.value("storageBrowserSection").toString(); }
void WorkspaceSession::setStorageBrowserSection(QString value) { change({{"storageBrowserSection", value}}); }
QString WorkspaceSession::pendingFileKey() const { return state_.value("pendingFileKey").toString(); }
QString WorkspaceSession::pendingFileSection() const { return state_.value("pendingFileSection").toString(); }
QString WorkspaceSession::pendingFileContainerId() const { return state_.value("pendingFileContainerId").toString(); }
QVariant WorkspaceSession::pendingCanvasOpen() const { return state_.value("pendingCanvasOpen"); }
void WorkspaceSession::setPendingCanvasOpen(const QVariant &value) { change({{"pendingCanvasOpen", value}}); }
QUrl WorkspaceSession::canvasImageSource() const { return QUrl(state_.value("canvasImageSource").toString()); }
QVariantMap WorkspaceSession::canvasSpecification() const { return state_.value("canvasSpecification").toMap(); }
void WorkspaceSession::setCanvasSpecification(const QVariantMap &value) { change({{"canvasSpecification", value}}); }
QVariantMap WorkspaceSession::canvasGenerationResult() const { return state_.value("canvasGenerationResult").toMap(); }
QVariantList WorkspaceSession::canvasGenerationResults() const { return state_.value("canvasGenerationResults").toList(); }
QVariantMap WorkspaceSession::currentResult() const { return state_.value("currentResult").toMap(); }
void WorkspaceSession::setCurrentResult(const QVariantMap &value) { change({{"currentResult", value}}); }
QStringList WorkspaceSession::resultJobIds() const { return state_.value("resultJobIds").toStringList(); }
void WorkspaceSession::setResultJobIds(const QStringList &value) { change({{"resultJobIds", value}}); }
QString WorkspaceSession::generationRequestError() const { return state_.value("generationRequestError").toString(); }
void WorkspaceSession::setGenerationRequestError(QString value) { change({{"generationRequestError", value}}); }
EditorProject *WorkspaceSession::project() const { return editor_ ? qobject_cast<EditorProject *>(editor_->property("project").value<QObject *>()) : nullptr; }
EditorImageImportRequest *WorkspaceSession::importer() const { return editor_ ? qobject_cast<EditorImageImportRequest *>(editor_->property("imageImportRequest").value<QObject *>()) : nullptr; }
QObject *WorkspaceSession::ensureEditor() { emit editorRequested(); return editor_; }
bool WorkspaceSession::storageVisible() const { return !storageBrowserSection().isEmpty() && !resultVisible() && !editorVisible(); }
bool WorkspaceSession::openCanvas(const QUrl &source, const QVariantMap &result) {
    cancelFileOpen(); ensureEditor(); emit inputsDismissRequested(); auto *document = project(); if (!document) return false;
    const QUrl original = result.value("documentSource").toUrl(); const QUrl target = original.isEmpty() ? source : original;
    if (!target.isEmpty()) {
        const QVariantMap intent{{"source", target.toString()}, {"result", result}}; setPendingCanvasOpen(intent);
        if (!document->openDocumentSource(target)) { if (pendingCanvasOpen().toMap() == intent) setPendingCanvasOpen({}); setGenerationRequestError(document->error()); return false; }
        setGenerationRequestError({}); return true;
    }
    if (importer()) importer()->cancel(); document->cancelDocumentOpen();
    if (!document->createCanvas({{"width", 1024}, {"height", 1024}, {"unit", "px"}, {"ppi", 300}, {"background", "White"}, {"name", tr("Untitled Canvas")}})) { setGenerationRequestError(document->error()); return false; }
    return finishCanvasOpen({}, result);
}
bool WorkspaceSession::finishCanvasOpen(const QUrl &source, const QVariantMap &result) {
    change({{"publishVisible", false}, {"canvasImageSource", source}, {"canvasGenerationResult", result}, {"canvasGenerationResults", !source.isEmpty() && !result.isEmpty() ? QVariantList{result} : QVariantList{}}, {"canvasSpecification", QVariantMap{}}, {"generationRequestError", QString()}, {"editorVisible", true}});
    emit canvasFocusRequested(); return true;
}
bool WorkspaceSession::createCanvas(const QVariantMap &specification) {
    ensureEditor(); if (!project()) return false;
    if (importer()) importer()->cancel(); project()->cancelDocumentOpen();
    if (!project()->createCanvas(specification)) return false;
    change({{"canvasSpecification", specification}, {"canvasImageSource", QUrl()}, {"canvasGenerationResult", QVariantMap{}}, {"canvasGenerationResults", QVariantList{}}, {"generationRequestError", QString()}, {"editorVisible", true}});
    emit canvasFocusRequested(); return true;
}
void WorkspaceSession::documentOpened(const QUrl &source) {
    const auto intent = pendingCanvasOpen().toMap(); const auto result = intent.value("source").toString() == source.toString() ? intent.value("result").toMap() : QVariantMap{};
    setPendingCanvasOpen({}); finishCanvasOpen(source, result);
}
void WorkspaceSession::documentOpenFailed(const QString &message) { change({{"pendingCanvasOpen", QVariant()}, {"generationRequestError", message}}); }
bool WorkspaceSession::openImages(const QVariantList &entries) {
    if (entries.isEmpty()) return false;
    for (const auto &entry : entries) if (!vm::isImage(nativeValue(entry))) { setGenerationRequestError(tr("Select images to open in the editor.")); return false; }
    cancelFileOpen();
    for (const auto &value : entries) { const auto entry = value.toMap(); const auto section = entry.value("section").toString(); if (entry.value("available").isValid() && !entry.value("available").toBool() && !section.isEmpty()) { if (!storageVisible() || storageBrowserSection() != section) showStorage(section); break; } }
    if (!imageOpen_ || !imageOpen_->start(entries)) return false;
    change({{"pendingFileKey", QString()}, {"pendingFileSection", QString()}, {"pendingFileContainerId", QString()}, {"generationRequestError", QString()}}); return true;
}
bool WorkspaceSession::openPreparedImages(const QVariantList &entries, const QVariantList &sources) {
    ensureEditor(); if (!project() || !importer()) return false; project()->cancelDocumentOpen();
    if (!importer()->start(sources, entries, false)) return false; setGenerationRequestError({}); return true;
}
bool WorkspaceSession::finishPreparedImages(const QVariantList &entries, const QVariantList &sources) {
    if (sources.isEmpty()) return false; emit inputsDismissRequested();
    change({{"canvasImageSource", sources.front()}, {"canvasGenerationResult", entries.isEmpty() ? QVariantMap{} : entries.front().toMap()}, {"canvasGenerationResults", entries}, {"canvasSpecification", QVariantMap{}}, {"generationRequestError", QString()}, {"editorVisible", true}});
    emit canvasFocusRequested(); return true;
}
void WorkspaceSession::openHomeFileDialog() { cancelFileOpen(); emit inputsDismissRequested(); emit openFileDialogRequested(); }
void WorkspaceSession::closeCanvas() { cancelFileOpen(); setEditorVisible(false); }
void WorkspaceSession::openNewCanvas() { cancelFileOpen(); emit inputsDismissRequested(); emit newCanvasRequested(); }
void WorkspaceSession::presentLatestResult(const QVariantList &results, const QString &prompt) {
    if (resultJobIds().isEmpty()) return;
    const auto result = results.isEmpty() ? QVariantMap{} : results.back().toMap(); const auto source = result.value("imageSource").toUrl().toString();
    if (source == currentResult().value("imageSource").toUrl().toString()) return;
    setCurrentResult(result);
    if (resultVisible() && !source.isEmpty() && prompt.trimmed().isEmpty()) emit promptCompletionRequested(result.value("prompt").toString(), result.value("aspectRatio", "1:1").toString());
}
void WorkspaceSession::dismissResult() {
    change({{"resultVisible", false}, {"resultJobIds", QStringList{}}, {"currentResult", QVariantMap{}}, {"generationRequestError", QString()}}); emit reviewResetRequested();
}
void WorkspaceSession::cancelSubmission() {
    if (generation_) {
        QStringList queued, active; for (const auto &value : generation_->jobs()) { const auto job = value.toMap(); if (!resultJobIds().contains(job.value("id").toString()) || !vm::generationPending(job.value("state").toString().toStdString())) continue; (job.value("state").toString() == "queued" ? queued : active).push_back(job.value("id").toString()); }
        for (const auto &id : queued + active) generation_->cancel(id);
    }
    emit inputsDismissRequested(); dismissResult(); emit homeRequested();
}
void WorkspaceSession::showSubmission(const QStringList &ids) {
    cancelFileOpen(); change({{"storageBrowserSection", QString()}, {"editorVisible", false}, {"generationRequestError", QString()}, {"currentResult", QVariantMap{}}, {"resultJobIds", ids}, {"resultVisible", true}});
    emit reviewResetRequested(); emit inputsDismissRequested(); emit reviewFocusRequested();
}
void WorkspaceSession::openGenerationReview() {
    cancelFileOpen(); change({{"storageBrowserSection", QString()}, {"editorVisible", false}, {"resultVisible", true}}); emit inputsDismissRequested(); emit reviewFocusRequested();
}
bool WorkspaceSession::openHomeFile(const QVariantMap &file) {
    if (imageOpen_) imageOpen_->cancel();
    if (file.isEmpty()) { setGenerationRequestError(tr("Choose a file to open.")); return false; }
    if (file.contains("exists") && !file.value("exists").toBool()) { setGenerationRequestError(tr("This file is no longer available.")); return false; }
    if (file.contains("available") && !file.value("available").toBool()) {
        const auto section = file.value("section", "files").toString(), key = file.value("key").toString(), container = file.value("containerId").toString();
        if (key.isEmpty() || container.isEmpty()) { setGenerationRequestError(tr("The file's drive information is missing. Refresh files and try again.")); return false; }
        if (!storageVisible() || storageBrowserSection() != section) showStorage(section);
        change({{"pendingFileKey", key}, {"pendingFileSection", section}, {"pendingFileContainerId", container}, {"generationRequestError", QString()}});
        auto *download = generation_ ? qobject_cast<iiSocietyHelper::FileDownload *>(generation_->fileDownload()) : nullptr;
        if (download && download->start(key, container)) return true;
        change({{"pendingFileKey", QString()}, {"pendingFileSection", QString()}, {"pendingFileContainerId", QString()}, {"generationRequestError", tr("The file download could not be started. Refresh files and try again.")}}); return false;
    }
    cancelFileOpen();
    if (file.value("mediaType") == "Video") { setEditorVisible(false); dismissResult(); setCurrentResult(file); setResultVisible(true); emit manualResultRequested(file); return true; }
    const auto name = file.value("name", file.value("path")).toString();
    static const QRegularExpression canvas("\\.iiscp?$", QRegularExpression::CaseInsensitiveOption);
    if (canvas.match(name).hasMatch() || file.value("isImage").toBool() || (!file.contains("isImage") && vm::isImage(nativeValue(file)))) {
        QUrl source = file.value("source").toUrl();
        if (source.isEmpty()) { const auto path = file.value("path").toString(); source = path.isEmpty() ? QUrl(QString::fromStdString(vm::text(vm::imageSource(nativeValue(file))))) : QUrl::fromLocalFile(path); }
        return openCanvas(source, file);
    }
    if (!file.value("source").toUrl().isEmpty() && QDesktopServices::openUrl(file.value("source").toUrl())) return true;
    setGenerationRequestError(tr("This file could not be opened on this device.")); return false;
}
void WorkspaceSession::cancelFileOpen() {
    if (cancelling_) return; QScopedValueRollback guard(cancelling_, true);
    const auto key = pendingFileKey(), container = pendingFileContainerId();
    if (imageOpen_) imageOpen_->cancel(); if (importer()) importer()->cancel(); if (project()) project()->cancelDocumentOpen();
    auto *download = generation_ ? qobject_cast<iiSocietyHelper::FileDownload *>(generation_->fileDownload()) : nullptr;
    if (download && download->active() && !key.isEmpty() && !container.isEmpty()) { const auto state = download->state(); if (state.value("key").toString() == key && state.value("containerId").toString() == container) download->cancel(); }
    change({{"pendingFileKey", QString()}, {"pendingFileSection", QString()}, {"pendingFileContainerId", QString()}, {"pendingCanvasOpen", QVariant()}});
}
void WorkspaceSession::downloaded(const QVariantMap &file) {
    if (!generation_ || !storageVisible() || pendingFileKey() != file.value("key").toString() || pendingFileContainerId() != file.value("containerId").toString() || generation_->container()->containerId() != file.value("containerId").toString() || pendingFileSection() != storageBrowserSection()) return;
    const auto files = vm::sectionFiles(nativeValue(generation_->container()->aliases()), pendingFileSection().toStdString()); QVariantMap fresh;
    for (const auto &entry : vm::array(files)) if (vm::text(vm::get(entry, "key")) == file.value("key").toString().toStdString()) { fresh = viewValue(entry).toMap(); break; }
    change({{"pendingFileKey", QString()}, {"pendingFileSection", QString()}, {"pendingFileContainerId", QString()}}); if (fresh.value("available").toBool()) openHomeFile(fresh);
}
void WorkspaceSession::showStorage(const QString &section) {
    if (section != "files" && section != "published" && section != "generation-history") return;
    cancelFileOpen(); emit inputsDismissRequested(); change({{"editorVisible", false}, {"resultVisible", false}, {"generationRequestError", QString()}, {"storageBrowserSection", section}});
    emit storageRequested(section);
}
bool WorkspaceSession::reportUnavailableAction(const QString &action) {
    setGenerationRequestError(action == "audio" ? tr("Audio generation is not available yet. Your prompt is preserved.") : action == "board" ? tr("Board creation is not available yet. Your prompt is preserved.") : tr("This action is not available on this device.")); return false;
}
} // namespace dreamscapes
