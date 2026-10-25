#include "EditorToolStateModel.h"
#include "EditorCanvas.h"
using namespace dreamscapes;
using namespace dreamscapes::vm;
EditorToolStateModel::EditorToolStateModel(QObject *parent) : QObject(parent) {}
EditorCanvas *EditorToolStateModel::engine() const { return engine_; }
void EditorToolStateModel::setCatalog(ViewData *catalog) {
    if (catalog_ == catalog) return; catalog_ = catalog; model_ = catalog ? std::make_unique<EditorToolState>(catalog->nativeCatalog()) : nullptr;
    refresh(); if (model_) { send(ToolAction::write, {QStringLiteral("hasEngine"), bool(engine_)}); syncElementSettings(); } emit catalogChanged();
}
void EditorToolStateModel::setEngine(EditorCanvas *engine) {
    if (engine_ == engine) return; if (engine_) disconnect(engine_, nullptr, this, nullptr); engine_ = engine;
    if (engine_) {
        connect(engine_, &EditorCanvas::elementSelectionChanged, this, &EditorToolStateModel::scheduleSync);
        connect(engine_, &EditorCanvas::layerStateChanged, this, &EditorToolStateModel::scheduleSync);
        connect(engine_, &EditorCanvas::documentInfoChanged, this, &EditorToolStateModel::scheduleSync);
        connect(engine_, &EditorCanvas::editingGeometryChanged, this, &EditorToolStateModel::scheduleSync);
        connect(engine_, &QObject::destroyed, this, [this] { engine_ = nullptr; send(ToolAction::write, {QStringLiteral("hasEngine"), false}); });
    }
    send(ToolAction::write, {QStringLiteral("hasEngine"), bool(engine_)}); syncElementSettings(); configure(); emit engineChanged();
}
void EditorToolStateModel::scheduleSync() {
    if (syncPending_) return; syncPending_ = true;
    QMetaObject::invokeMethod(this, [this] { syncPending_ = false; syncElementSettings(); }, Qt::QueuedConnection);
}
void EditorToolStateModel::configure() {
    if (!engine_ || !model_ || applying_) return;
    applying_ = true; engine_->configureTool(toolId(), values()); applying_ = false;
}
void EditorToolStateModel::send(ToolAction action, std::initializer_list<QVariant> args) {
    if (!model_) return; ToolCommand command{action, {}}; for (const auto &arg : args) command.arguments.push_back(nativeValue(arg));
    executeNative(command);
}
void EditorToolStateModel::executeNative(const ToolCommand &command) {
    if (!model_) return;
    const auto previousTool = toolId(); const auto previousValues = values();
    const auto result = model_->commands().send(command); refresh(); if (!result) return;
    for (const auto &edit : result->edits) {
        const auto tool = QString::fromStdString(edit.tool), key = QString::fromStdString(edit.field); const auto value = viewValue(edit.value);
        const bool imageFill = tool == "elements" && key == "field-2" && value.toString() == "Image"; auto requested = values(); if (imageFill) requested["field-2"] = QStringLiteral("Image");
        if (engine_) { applying_ = true; if (tool == "elements") engine_->configureTool(tool, values()); const auto accepted = engine_->applyToolField(tool, key, value); applying_ = false; setNotice(accepted ? QString() : engine_->error()); }
        emit valueEdited(tool, key, value); if (imageFill) emit sourceRequested(tool, requested);
    }
    if (result->reset) { if (engine_ && !engine_->resetTool(toolId(), values())) setNotice(engine_->error()); emit resetRequested(toolId(), values()); }
    if (command.action != ToolAction::acceptedSelection && (previousTool != toolId() || previousValues != values())) configure();
}
void EditorToolStateModel::refresh() {
    if (!model_) return; const auto old = state_; state_ = viewValue(model_->presentation()).toMap();
    if (old.value("definition") != state_.value("definition")) emit definitionChanged();
    if (old.value("values") != state_.value("values")) emit valuesChanged();
    if (old.value("toolId") != state_.value("toolId")) emit toolIdChanged();
    if (old.value("settingsByTool") != state_.value("settingsByTool")) emit settingsByToolChanged();
    if (old.value("graphicViewsByTool") != state_.value("graphicViewsByTool")) emit graphicViewsByToolChanged();
    if (old.value("compactViewsByTool") != state_.value("compactViewsByTool")) emit compactViewsByToolChanged();
    if (old.value("fileViewId") != state_.value("fileViewId")) emit fileViewIdChanged();
    if (old.value("elementsViewId") != state_.value("elementsViewId")) emit elementsViewIdChanged();
    if (old.value("elementsMultipleSelection") != state_.value("elementsMultipleSelection")) emit elementsMultipleSelectionChanged();
    if (old.value("elementsSelectionScope") != state_.value("elementsSelectionScope")) emit elementsSelectionScopeChanged();
    if (old.value("notice") != state_.value("notice")) emit noticeChanged();
    if (old.value("generationModels") != state_.value("generationModels")) emit generationModelsChanged();
}
void EditorToolStateModel::setToolId(QString value) { send(ToolAction::write, {QStringLiteral("toolId"), value}); }
void EditorToolStateModel::setSettingsByTool(QVariantMap value) { send(ToolAction::write, {QStringLiteral("settingsByTool"), value}); }
void EditorToolStateModel::setGraphicViewsByTool(QVariantMap value) { send(ToolAction::write, {QStringLiteral("graphicViewsByTool"), value}); }
void EditorToolStateModel::setCompactViewsByTool(QVariantMap value) { send(ToolAction::write, {QStringLiteral("compactViewsByTool"), value}); }
void EditorToolStateModel::setFileViewId(QString value) { send(ToolAction::write, {QStringLiteral("fileViewId"), value}); }
void EditorToolStateModel::setElementsViewId(QString value) { send(ToolAction::write, {QStringLiteral("elementsViewId"), value}); }
void EditorToolStateModel::setElementsMultipleSelection(bool value) { send(ToolAction::write, {QStringLiteral("elementsMultipleSelection"), value}); }
void EditorToolStateModel::setElementsSelectionScope(QString value) { send(ToolAction::write, {QStringLiteral("elementsSelectionScope"), value}); }
void EditorToolStateModel::setNotice(QString value) { send(ToolAction::write, {QStringLiteral("notice"), value}); }
void EditorToolStateModel::setGenerationModels(QVariantList value) { send(ToolAction::write, {QStringLiteral("generationModels"), value}); }
void EditorToolStateModel::syncElementSettings(bool force) {
    QVariantMap accepted; if (engine_) accepted = {{"selectedElementSettings", engine_->selectedElementSettings()}, {"selectedLayerId", engine_->selectedLayerId()}, {"elementSelectionCount", engine_->elementSelectionCount()}, {"selectedLayer", engine_->selectedLayer()}, {"generationSeed", engine_->generationSeed()}, {"editingWidth", engine_->editingWidth()}, {"editingHeight", engine_->editingHeight()}};
    send(ToolAction::acceptedSelection, {accepted, force});
}
void EditorToolStateModel::setValue(const QString &field, const QVariant &value) { send(ToolAction::setValue, {field, value}); }
void EditorToolStateModel::setGraphicValue(const QString &operation, const QString &parameter, const QVariant &value) { send(ToolAction::setGraphicValue, {operation, parameter, value}); }
void EditorToolStateModel::setGraphicValues(const QString &operation, const QVariant &patch) { send(ToolAction::setGraphicValues, {operation, patch}); }
void EditorToolStateModel::replaceGraphicCollection(const QString &operation, const QString &path, const QVariant &items) { send(ToolAction::replaceGraphicCollection, {operation, path, items}); }
void EditorToolStateModel::resetGraphicView(const QString &operation, const QVariant &defaults) { send(ToolAction::resetGraphicView, {operation, defaults}); }
void EditorToolStateModel::resetTool() { send(ToolAction::resetTool, {}); }
