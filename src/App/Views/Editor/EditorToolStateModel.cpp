#include "EditorToolStateModel.h"
#include "EditorCameraPhoto.h"
#include "EditorCanvas.h"
using namespace dreamscapes;
using namespace dreamscapes::vm;
EditorToolStateModel::EditorToolStateModel(QObject *parent) : QObject(parent) {}
EditorCanvas *EditorToolStateModel::engine() const {
    return engine_;
}
void EditorToolStateModel::setCatalog(ViewData *catalog) {
    if (catalog_ == catalog)
        return;
    catalog_ = catalog;
    model_ = catalog ? std::make_unique<EditorToolState>(catalog->nativeCatalog()) : nullptr;
    if (model_) {
        for (const auto &command : pendingCommands_)
            (void)model_->commands().send(command);
        pendingCommands_.clear();
    }
    refresh();
    if (model_) {
        send(ToolAction::write, {QStringLiteral("hasEngine"), bool(engine_)});
        syncElementSettings();
    }
    emit catalogChanged();
}
void EditorToolStateModel::setEngine(EditorCanvas *engine) {
    if (engine_ == engine)
        return;
    if (engine_)
        disconnect(engine_, nullptr, this, nullptr);
    engine_ = engine;
    if (engine_) {
        connect(engine_, &EditorCanvas::elementSelectionChanged, this, &EditorToolStateModel::scheduleSync);
        connect(engine_, &EditorCanvas::layerStateChanged, this, &EditorToolStateModel::scheduleSync);
        connect(engine_, &EditorCanvas::documentInfoChanged, this, &EditorToolStateModel::scheduleSync);
        connect(engine_, &EditorCanvas::editingGeometryChanged, this, &EditorToolStateModel::scheduleSync);
        connect(engine_, &EditorCanvas::toolStateChanged, this, [this] {
            if (!applying_)
                scheduleSync();
        });
        connect(engine_, &QObject::destroyed, this, [this] {
            engine_ = nullptr;
            send(ToolAction::write, {QStringLiteral("hasEngine"), false});
        });
    }
    send(ToolAction::write, {QStringLiteral("hasEngine"), bool(engine_)});
    syncElementSettings();
    configure();
    emit engineChanged();
}
void EditorToolStateModel::scheduleSync() {
    if (syncPending_)
        return;
    syncPending_ = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            syncPending_ = false;
            syncElementSettings();
        },
        Qt::QueuedConnection);
}
void EditorToolStateModel::configure() {
    if (!engine_ || !model_ || applying_)
        return;
    applying_ = true;
    engine_->configureTool(toolId(), values());
    applying_ = false;
}
void EditorToolStateModel::send(ToolAction action, std::initializer_list<QVariant> args) {
    ToolCommand command{action, {}};
    for (const auto &arg : args)
        command.arguments.push_back(nativeValue(arg));
    if (!model_) {
        if (action == ToolAction::write)
            pendingCommands_.push_back(std::move(command));
        return;
    }
    executeNative(command);
}
void EditorToolStateModel::executeNative(const ToolCommand &command) {
    if (!model_)
        return;
    const auto previousTool = toolId();
    const auto previousValues = values();
    const auto result = model_->commands().send(command);
    refresh();
    if (!result)
        return;
    bool elementsConfigured = false;
    for (const auto &edit : result->edits) {
        const auto tool = QString::fromStdString(edit.tool), key = QString::fromStdString(edit.field);
        const auto value = viewValue(edit.value);
        const bool imageFill = tool == "elements" && key == "field-2" && value.toString() == "Image";
        auto requested = values();
        if (imageFill)
            requested["field-2"] = QStringLiteral("Image");
        if (engine_) {
            applying_ = true;
            if (tool == "elements" && !elementsConfigured) {
                engine_->configureTool(tool, values());
                elementsConfigured = true;
            }
            const auto accepted = engine_->applyToolField(tool, key, value);
            applying_ = false;
            setNotice(accepted ? QString() : engine_->error());
        }
        emit valueEdited(tool, key, value);
        if (imageFill)
            emit sourceRequested(tool, requested);
    }
    if (result->reset) {
        if (engine_ && !engine_->resetTool(toolId(), values()))
            setNotice(engine_->error());
        emit resetRequested(toolId(), values());
    }
    if (engine_ && (!result->edits.empty() || result->reset))
        syncElementSettings();
    if (command.action != ToolAction::acceptedSelection && (previousTool != toolId() || previousValues != values()))
        configure();
}
void EditorToolStateModel::refresh() {
    if (!model_)
        return;
    const auto old = state_;
    state_ = viewValue(model_->presentation()).toMap();
    ++revision_;
    emit revisionChanged();
    if (old.value("collapsedLayerGroups") != state_.value("collapsedLayerGroups")) emit layerGroupsChanged();
    if (old.value("elementValues") != state_.value("elementValues"))
        emit elementValuesChanged();
    if (old.value("textValues") != state_.value("textValues"))
        emit textValuesChanged();
    if (old.value("photoValues") != state_.value("photoValues"))
        emit photoValuesChanged();
    if (old.value("activeElementCandidate") != state_.value("activeElementCandidate"))
        emit activeElementCandidateChanged();
    if (old.value("scopedElementCandidates") != state_.value("scopedElementCandidates"))
        emit scopedElementCandidatesChanged();
    if (old.value("elementFields") != state_.value("elementFields"))
        emit elementFieldsChanged();
    if (old.value("compactViewCatalog") != state_.value("compactViewCatalog"))
        emit compactViewCatalogChanged();
    if (old.value("compactSelectedView") != state_.value("compactSelectedView"))
        emit compactSelectedViewChanged();
    if (old.value("compactCurrentViewId") != state_.value("compactCurrentViewId"))
        emit compactCurrentViewIdChanged();
    if (old.value("compactPresentedFields") != state_.value("compactPresentedFields"))
        emit compactPresentedFieldsChanged();
    if (old.value("definition") != state_.value("definition"))
        emit definitionChanged();
    if (old.value("values") != state_.value("values"))
        emit valuesChanged();
    if (old.value("toolId") != state_.value("toolId"))
        emit toolIdChanged();
    if (old.value("settingsByTool") != state_.value("settingsByTool"))
        emit settingsByToolChanged();
    if (old.value("graphicViewsByTool") != state_.value("graphicViewsByTool"))
        emit graphicViewsByToolChanged();
    if (old.value("compactViewsByTool") != state_.value("compactViewsByTool"))
        emit compactViewsByToolChanged();
    if (old.value("fileViewId") != state_.value("fileViewId"))
        emit fileViewIdChanged();
    if (old.value("elementsViewId") != state_.value("elementsViewId"))
        emit elementsViewIdChanged();
    if (old.value("elementsMultipleSelection") != state_.value("elementsMultipleSelection"))
        emit elementsMultipleSelectionChanged();
    if (old.value("elementsSelectionScope") != state_.value("elementsSelectionScope"))
        emit elementsSelectionScopeChanged();
    if (old.value("notice") != state_.value("notice"))
        emit noticeChanged();
    if (old.value("generationModels") != state_.value("generationModels"))
        emit generationModelsChanged();
}
void EditorToolStateModel::setToolId(QString value) {
    send(ToolAction::write, {QStringLiteral("toolId"), value});
}
void EditorToolStateModel::setSettingsByTool(QVariantMap value) {
    send(ToolAction::write, {QStringLiteral("settingsByTool"), value});
}
void EditorToolStateModel::setGraphicViewsByTool(QVariantMap value) {
    send(ToolAction::write, {QStringLiteral("graphicViewsByTool"), value});
}
void EditorToolStateModel::setCompactViewsByTool(QVariantMap value) {
    send(ToolAction::write, {QStringLiteral("compactViewsByTool"), value});
}
void EditorToolStateModel::setFileViewId(QString value) {
    send(ToolAction::write, {QStringLiteral("fileViewId"), value});
}
void EditorToolStateModel::setElementsViewId(QString value) {
    send(ToolAction::write, {QStringLiteral("elementsViewId"), value});
}
void EditorToolStateModel::setElementsMultipleSelection(bool value) {
    send(ToolAction::write, {QStringLiteral("elementsMultipleSelection"), value});
}
void EditorToolStateModel::setElementsSelectionScope(QString value) {
    send(ToolAction::write, {QStringLiteral("elementsSelectionScope"), value});
}
void EditorToolStateModel::setNotice(QString value) {
    send(ToolAction::write, {QStringLiteral("notice"), value});
}
void EditorToolStateModel::setGenerationModels(QVariantList value) {
    send(ToolAction::write, {QStringLiteral("generationModels"), value});
}
void EditorToolStateModel::syncElementSettings(bool force) {
    QVariantMap accepted;
    if (engine_)
        accepted = {{"selectedElementSettings", engine_->selectedElementSettings()},
                    {"selectedLayerId", engine_->selectedLayerId()},
                    {"elementSelectionCount", engine_->elementSelectionCount()},
                    {"elementCandidates", engine_->elementCandidates()},
                    {"selectedLayer", engine_->selectedLayer()},
                    {"generationSeed", engine_->generationSeed()},
                    {"editingWidth", engine_->editingWidth()},
                    {"editingHeight", engine_->editingHeight()}};
    send(ToolAction::acceptedSelection, {accepted, force});
}
void EditorToolStateModel::setValue(const QString &field, const QVariant &value) {
    send(ToolAction::setValue, {field, value});
}
void EditorToolStateModel::setGraphicValue(const QString &operation, const QString &parameter, const QVariant &value) {
    send(ToolAction::setGraphicValue, {operation, parameter, value});
}
void EditorToolStateModel::setGraphicValues(const QString &operation, const QVariant &patch) {
    send(ToolAction::setGraphicValues, {operation, patch});
}
void EditorToolStateModel::replaceGraphicCollection(const QString &operation, const QString &path,
                                                    const QVariant &items) {
    send(ToolAction::replaceGraphicCollection, {operation, path, items});
}
void EditorToolStateModel::resetGraphicView(const QString &operation, const QVariant &defaults) {
    send(ToolAction::resetGraphicView, {operation, defaults});
}
void EditorToolStateModel::resetTool() {
    send(ToolAction::resetTool, {});
}
void EditorToolStateModel::restoreSettings(const QVariantMap &settings) {
    send(ToolAction::restoreSettings, {settings});
    syncElementSettings(true);
}

bool EditorToolStateModel::elementFieldEnabled(const QString &field) const {
    return model_ && model_->elementFieldEnabled(field.toStdString());
}
QString EditorToolStateModel::formattedElement(const QString &field, int index) const {
    return model_ ? QString::fromStdString(model_->formattedElement(field.toStdString(), index)) : QString();
}
void EditorToolStateModel::selectElementView(const QString &id) {
    send(ToolAction::selectElementView, {id});
}
void EditorToolStateModel::editElement(const QString &field, const QVariant &value) {
    send(ToolAction::editElement, {field, value});
}
void EditorToolStateModel::chooseElementShape(const QVariant &entry) {
    const auto active = activeElementCandidate().toMap();
    if (engine_ && !active.isEmpty() && !active.value("editable").toBool()) {
        engine_->clearElementSelection();
        syncElementSettings();
    }
    const auto shape = nativeValue(entry);
    send(ToolAction::selectElementView, {viewValue(get(shape, "id"))});
    send(ToolAction::editElement, {QStringLiteral("selector"), viewValue(get(shape, "name"))});
}
void EditorToolStateModel::resetElementView() {
    send(ToolAction::resetElementView, {});
}
void EditorToolStateModel::setElementSelected(const QString &id, bool selected) {
    if (engine_)
        setNotice(engine_->setElementSelected(id, selected, elementsMultipleSelection()) ? QString()
                                                                                         : engine_->error());
}
void EditorToolStateModel::elementSelectionAction(const QString &action) {
    if (!engine_)
        return;
    bool success = true;
    if (action == "clear")
        success = engine_->clearElementSelection();
    else if (action == "inspect") {
        const auto candidates = engine_->elementCandidates();
        if (candidates.isEmpty())
            return;
        success = engine_->selectElement(candidates.front().toMap().value("id").toString());
        selectElementView("inspect");
    } else if (elementsSelectionScope() == "All shapes")
        success = action == "all" ? engine_->selectAllElements() : engine_->invertElementSelection();
    else
        for (const auto &value : scopedElementCandidates()) {
            const auto candidate = value.toMap();
            success = engine_->setElementSelected(candidate.value("id").toString(),
                                                  action == "all" || !candidate.value("selected").toBool(), true);
            if (!success)
                break;
        }
    setNotice(success ? QString() : engine_->error());
}
void EditorToolStateModel::commitElementNumber(const QString &field, int index, const QString &text) {
    send(ToolAction::commitElementNumber, {field, index, text});
}
void EditorToolStateModel::selectCompactView(const QString &id) {
    send(ToolAction::selectCompactView, {id});
}
void EditorToolStateModel::resetCompactView() {
    send(ToolAction::resetCompactView, {});
}
QVariantMap EditorToolStateModel::capability(const QVariant &field) const {
    const auto data = nativeValue(field);
    if (boolean(get(data, "unsupported")))
        return {{"supported", false},
                {"enabled", false},
                {"reason", tr("This option is unavailable in the connected editing engine.")}};
    return engine_ ? engine_->toolControlState(toolId(), QString::fromStdString(text(get(data, "id"))), values())
                   : QVariantMap{{"supported", true}, {"enabled", true}, {"reason", QString()}};
}
void EditorToolStateModel::editText(const QString &field, const QVariant &value) {
    send(ToolAction::editText, {field, value});
}
void EditorToolStateModel::patchText(const QVariant &values) {
    send(ToolAction::patchText, {values});
}
void EditorToolStateModel::resetTextView(int index) {
    send(ToolAction::resetTextView, {index});
}
void EditorToolStateModel::applyTextRole(const QVariant &role) {
    send(ToolAction::textRole, {role});
}
bool EditorToolStateModel::textOptionAvailable(const QString &field, const QString &option) const {
    return model_ && model_->textOptionAvailable(field.toStdString(), option.toStdString());
}
void EditorToolStateModel::textLinePreset(const QString &preset) {
    send(ToolAction::textLinePreset, {preset});
}

void EditorToolStateModel::importPhotoSources(const QString &containerPath) {
    if (!engine_)
        return;
    auto *photo = qobject_cast<EditorCameraPhoto *>(engine_->photoSources());
    if (!photo)
        return;
    if (!containerPath.isEmpty())
        setValue("libraryDirectory", containerPath);
    photo->importFor(photoValues().value("selector").toString());
}

void EditorToolStateModel::editLayerAdjustment(const QString &field, const QVariant &value) {
    if (!engine_)
        return;
    if (!engine_->applyToolField("color", field, value)) {
        setNotice(engine_->error());
        return;
    }
    send(ToolAction::editLayerAdjustment, {field, value});
}
bool EditorToolStateModel::openLayerAdjustment(const QVariantMap &entry) {
    if (!engine_ || !catalog_ || entry.isEmpty())
        return false;
    const auto key = entry.value("key").toString();
    if (key == "auto") {
        send(ToolAction::write, {QStringLiteral("lastLayerAdjustmentTool"), QStringLiteral("auto-enhance")});
        setNotice(
            engine_->executeToolAction("auto-enhance", "field-3",
                                       catalog_->tools()->defaults(catalog_->tools()->tool("auto-enhance")).toMap())
                ? QString()
                : engine_->error());
        return false;
    }
    if (key == "mono") {
        editLayerAdjustment("field-36", -100);
        return false;
    }
    send(ToolAction::prepareLayerAdjustment, {entry, engine_->toolValues("color")});
    emit layerAdjustmentRequested(entry.value("label").toString());
    return true;
}
void EditorToolStateModel::layerAction(const QString &key, bool shadow, bool highlight) {
    if (!engine_)
        return;
    bool success = true;
    if (key == "sample") {
        success = engine_->beginLayerColorSample();
        if (success)
            emit closeToolRequested();
    } else if (key == "copyColor")
        success = engine_->copyLayerSampleColor();
    else if (key == "copyDetails")
        success = engine_->copyLayerPanelDetails();
    else if (key == "reveal")
        success = engine_->revealLayerSource();
    else if (key == "refresh")
        emit layerHistogramRequested();
    else if (key == "shadow" || key == "highlight")
        engine_->setLayerHistogramClipping(shadow, highlight);
    setNotice(success ? QString() : engine_->error());
}
void EditorToolStateModel::expandLayerGroup(const QString &id, bool expanded) {
    ToolCommand command{ToolAction::expandLayerGroup, {Value(id.toStdString()), Value(expanded)}};
    if (!model_) { pendingCommands_.push_back(std::move(command)); return; }
    const auto result = model_->commands().send(command);
    if (!result) return;
    const auto groups = viewValue(model_->collapsedLayerGroups()).toMap();
    if (groups == collapsedLayerGroups()) return;
    state_["collapsedLayerGroups"] = groups;
    emit layerGroupsChanged(); // No broad tool projection, configure, or document mutation.
}
bool EditorToolStateModel::editLayerOpacity(const QString &text) {
    const auto input = numberInputs_.queries().send({NumberInputKind::percentage,text.toStdString()});
    if (!input || !input->accepted || !engine_ || engine_->selectedLayerId().isEmpty()) return false;
    if (input->value == std::round(engine_->selectedLayer().value("opacity").toDouble() * 100.)) return true;
    const bool accepted = engine_->setLayerOpacity(engine_->selectedLayerId(),input->value / 100.);
    setNotice(accepted ? QString() : engine_->error());
    return accepted;
}
QVariantList EditorToolStateModel::layerHierarchy(const QUrl &assetRoot, const QVariantMap &collapsed) const {
    QVariantList rows;
    if (!engine_)
        return rows;
    for (const auto &value : engine_->layerHierarchy()) {
        auto row = value.toMap();
        row["count"] = 0;
        row["iconSource"] = assetRoot.resolved(
            QUrl((row.value("locked").toBool() ? QStringLiteral("Lock") : row.value("kind").toString()) + ".svg"));
        if (row.value("group").toBool())
            row["expanded"] = !collapsed.value(row.value("layerId").toString()).toBool();
        rows.push_back(row);
    }
    return rows;
}
QVariantMap EditorToolStateModel::layerAdjustment(const QString &key, const QVariantList &entries) const {
    for (const auto &entry : entries)
        if (entry.toMap().value("key").toString() == key)
            return entry.toMap();
    return {};
}
QVariantMap EditorToolStateModel::layerAdjustmentField(const QString &id) const {
    if (id == "curveAmount")
        return {{"id", id},     {"type", "Slider"}, {"label", tr("Curve amount")},
                {"initial", 0}, {"minimum", -100},  {"maximum", 100},
                {"step", 1},    {"unit", ""},       {"decimals", 0}};
    if (catalog_)
        for (const auto &field : catalog_->tools()->tool("color").toMap().value("fields").toList())
            if (field.toMap().value("id").toString() == id)
                return field.toMap();
    return {};
}

void EditorToolStateModel::requestElementImageFill() {
    auto requested = elementValues();
    requested["field-2"] = QStringLiteral("Image");
    emit sourceRequested("elements", requested);
}
