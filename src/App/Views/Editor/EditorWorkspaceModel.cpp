#include "EditorWorkspaceModel.h"
#include "../../Generation/GenerationController.h"
#include "../../ViewModels/Native/GenerationPresentation.h"
#include "../../ViewModels/ViewData.h"
using namespace dreamscapes;
using namespace dreamscapes::vm;
void EditorWorkspaceModel::setProject(EditorProject *project) {
    if (project_ == project)
        return;
    if (project_)
        disconnect(project_, nullptr, this, nullptr);
    project_ = project;
    if (project_) {
        connect(project_, &EditorProject::navigationChanged, this, &EditorWorkspaceModel::syncGenerationMetadata);
        connect(project_, &EditorProject::projectChanged, this, &EditorWorkspaceModel::syncGenerationMetadata);
    }
    syncGenerationMetadata();
    emit bindingsChanged();
}
QObject *EditorWorkspaceModel::generation() const {
    return generation_;
}
void EditorWorkspaceModel::setGeneration(QObject *value) {
    auto *next = qobject_cast<GenerationController *>(value);
    if (generation_ == next)
        return;
    if (generation_)
        disconnect(generation_, nullptr, this, nullptr);
    generation_ = next;
    if (generation_) {
        connect(generation_, &GenerationController::submissionQueued, this, [this](const QStringList &ids) {
            if (submitting_) {
                ids_ = ids;
                emit generationChanged();
            }
        });
        connect(generation_, &GenerationController::resultsChanged, this, &EditorWorkspaceModel::generationChanged);
    }
    emit bindingsChanged();
}
void EditorWorkspaceModel::setGenerationResult(const QVariantMap &value) {
    if (result_ != value) {
        result_ = value;
        emit metadataChanged();
    }
}
void EditorWorkspaceModel::setGenerationResults(const QVariantList &values) {
    if (results_ != values) {
        results_ = values;
        syncGenerationMetadata();
    }
}
void EditorWorkspaceModel::syncGenerationMetadata() {
    QVariantMap mapped;
    if (project_) {
        const auto boards = project_->artboards();
        for (int i = 0; i < boards.size(); ++i)
            mapped[boards[i].toMap().value("id").toString()] = results_.size() == 1  ? results_.front()
                                                               : i < results_.size() ? results_[i]
                                                                                     : QVariant(QVariantMap{});
    }
    byArtboard_ = mapped;
    emit metadataChanged();
}
QVariantMap EditorWorkspaceModel::activeGenerationResult() const {
    if (project_) {
        const auto boards = project_->artboards();
        const auto index = project_->currentIndex();
        if (index >= 0 && index < boards.size()) {
            const auto id = boards[index].toMap().value("id").toString();
            if (!id.isEmpty())
                return byArtboard_.value(id).toMap();
        }
        if (index >= 0 && index < results_.size())
            return results_[index].toMap();
    }
    return result_;
}
QVariantList EditorWorkspaceModel::completedResults() const {
    QVariantList values;
    if (generation_)
        for (const auto &entry : generation_->completedResults())
            if (ids_.contains(entry.toMap().value("id").toString()))
                values.push_back(entry);
    return values;
}
EditorInvocation EditorWorkspaceModel::invocation(const QString &tool, const QString &field,
                                                  const QVariantMap &values) const {
    EditorInvocation input{tool.toStdString(), field.toStdString(), nativeValue(values)};
    if (toolState_ && toolState_->catalog()) {
        auto &catalog = toolState_->catalog()->nativeCatalog();
        const auto definition = catalog.queries(Domain::tools).send({Read::tool, {Value(input.tool)}});
        if (definition)
            input.definition = *definition;
        const auto state = toolState_->nativeSnapshot();
        input.generativeDraft = get(get(state, "settingsByTool"), "generative");
        if (input.generativeDraft.kind() == ii::core::ValueKind::null) {
            const auto tool = catalog.queries(Domain::tools).send({Read::tool, {Value("generative")}});
            if (tool) {
                const auto defaults = catalog.queries(Domain::tools).send({Read::defaults, {*tool}});
                if (defaults)
                    input.generativeDraft = *defaults;
            }
        }
    }
    return input;
}
void EditorWorkspaceModel::requestSource(const QString &tool, const QVariantMap &values) {
    if (!canvas())
        return;
    const auto directory = canvas()->localDirectory(values.value(tool == "file" ? "field-0" : "field-8").toString());
    emit sourceRequested(
        tool, values, directory,
        tool == "audio-track"
            ? QStringList{tr("WAV audio (*.wav)")}
            : QStringList{
                  tr("Images, canvases and vectors (*.iisc *.png *.jpg *.jpeg *.webp *.bmp *.tif *.tiff *.svg)")});
}
void EditorWorkspaceModel::showPreview(const QString &tool, const QString &field, const QVariantMap &values) {
    if (!canvas())
        return;
    previewTool_ = tool;
    previewField_ = field;
    selectedPath_ = -1;
    previewValues_ = values;
    if (generation_)
        previewValues_["libraryDirectory"] = generation_->containerPath();
    preview_ = canvas()->toolPreview(tool, field, previewValues_);
    emit previewChanged();
    emit previewRequested();
}
void EditorWorkspaceModel::submitGeneration(const QString &tool, const QString &field, const QVariantMap &values) {
    if (!toolState_ || !canvas())
        return;
    if (!generation_) {
        toolState_->setNotice(tr("The generation runtime is not attached."));
        return;
    }
    const auto plan = router_.generation().send(invocation(tool, field, values));
    if (!plan)
        return;
    const auto parameters =
        canvas()->generationParameters(QString::fromStdString(plan->operation), viewValue(plan->values).toMap());
    if (parameters.isEmpty()) {
        toolState_->setNotice(canvas()->error());
        return;
    }
    operation_ = QString::fromStdString(plan->operation);
    target_ = canvas();
    submitting_ = true;
    emit generationChanged();
    const auto id = generation_->enqueueAdvanced(parameters);
    submitting_ = false;
    emit generationChanged();
    toolState_->setNotice(id.isEmpty() ? generation_->errorString()
                                       : tr("Generation queued. Open results to place a variation."));
}
bool EditorWorkspaceModel::submitAdvanced(const QVariantMap &parameters) {
    if (!canvas() || !generation_ || !toolState_)
        return false;
    target_ = canvas();
    operation_ = "Text-to-image";
    submitting_ = true;
    emit generationChanged();
    const auto id = generation_->enqueueAdvanced(parameters);
    submitting_ = false;
    emit generationChanged();
    toolState_->setNotice(id.isEmpty() ? generation_->errorString()
                                       : tr("Generation queued. Open results to place a variation."));
    return !id.isEmpty();
}
void EditorWorkspaceModel::handleToolAction(const QString &tool, const QString &field, const QVariantMap &values) {
    if (!toolState_ || !canvas())
        return;
    toolState_->setNotice({});
    const auto input = invocation(tool, field, values);
    const auto result = router_.routes().send(input);
    if (!result)
        return;
    const auto &plan = *result;
    const auto targetTool = QString::fromStdString(plan.tool), targetField = QString::fromStdString(plan.field);
    const auto settings = viewValue(plan.values).toMap();
    switch (plan.route) {
    case EditorRoute::source:
        requestSource(targetTool, settings);
        break;
    case EditorRoute::preview:
        showPreview(targetTool, targetField, settings);
        break;
    case EditorRoute::generate:
        submitGeneration(targetTool, targetField, settings);
        break;
    case EditorRoute::paintColor:
        emit paintColorRequested();
        break;
    case EditorRoute::openDocument:
        emit openDocumentRequested(settings.value("field-2").toBool(),
                                   canvas()->localDirectory(settings.value("field-0").toString()));
        break;
    case EditorRoute::saveDocumentAs:
        emit saveAsRequested(settings.value("field-10").toString(),
                             canvas()->localDirectory(settings.value("field-0").toString()));
        break;
    case EditorRoute::exportImage:
        if (!canvas()->prepareImageExport())
            toolState_->setNotice(canvas()->error());
        else
            emit exportRequested(settings.value("field-1").toString(), settings.value("field-10").toString(),
                                 canvas()->localDirectory(settings.value("field-0").toString()));
        break;
    case EditorRoute::capabilities: {
        QVariantList items;
        for (const auto &value : array(get(input.definition, "fields"))) {
            const auto fieldId = QString::fromStdString(text(get(value, "id")));
            const auto capability = canvas()->toolControlState(tool, fieldId, settings);
            const auto reason = capability.value("reason", tr("Connected")).toString();
            if (!reason.isEmpty() && reason != tr("Connected"))
                items.push_back(QVariantMap{
                    {"label", viewValue(get(value, "label"))}, {"description", reason}, {"enabled", false}});
        }
        previewTool_ = "capabilities";
        preview_ = {{"title", tr("Tool availability")},
                    {"description", tr("Required document, device and runtime capabilities")},
                    {"items", items}};
        emit previewChanged();
        emit previewRequested();
        break;
    }
    case EditorRoute::execute:
        toolState_->setNotice(canvas()->executeToolAction(tool, field, settings) ? canvas()->toolHint()
                                                                                 : canvas()->error());
        break;
    }
}
void EditorWorkspaceModel::importSource(const QUrl &source, const QString &tool, const QVariantMap &values) {
    if (!toolState_ || !canvas())
        return;
    if (tool == "reference") {
        toolState_->setValue("field-4", source.toString());
        return;
    }
    if (!canvas()->importToolSource(source, tool, values)) {
        toolState_->setNotice(canvas()->error());
        return;
    }
    if (tool == "audio-track") {
        auto all = toolState_->settingsByTool();
        all["audio-track"] = canvas()->toolValues("audio-track");
        toolState_->setSettingsByTool(all);
    }
}
void EditorWorkspaceModel::sourceRejected(const QString &tool) {
    if (toolState_ && tool == "elements") {
        toolState_->syncElementSettings();
        toolState_->setNotice({});
    }
}
void EditorWorkspaceModel::exportImage(const QUrl &destination, const QString &format) {
    if (canvas() && toolState_)
        toolState_->setNotice(canvas()->exportImage(destination, format) ? tr("Exporting frame…") : canvas()->error());
}
void EditorWorkspaceModel::activatePreview(const QVariant &input) {
    if (!canvas() || !toolState_)
        return;
    const auto item = viewValue(nativeValue(input)).toMap();
    const auto action = item.value("action").toString();
    if (action == "selectLayer")
        canvas()->selectLayer(item.value("id").toString());
    else if (action == "brushPreset") {
        toolState_->setValue("field-1", item.value("value"));
        toolState_->setValue("field-5", item.value("value") == "Paint" ? 60 : 100);
    } else if (action == "filterPreset")
        toolState_->setValue("field-1", item.value("value"));
    else if (action == "palette")
        toolState_->setValue(item.value("field", "field-1").toString(), item.value("value"));
    else if (action == "editPath")
        setSelectedPath(item.value("index").toInt());
    else if (action == "job" && generation_)
        generation_->cancel(item.value("id").toString());
    else if (action == "region") {
        const QVariantMap region{
            {"selector", "Region"}, {"field-14", item.value("label")}, {"field-15", item.value("label")}};
        canvas()->configureTool("masking", region);
        canvas()->executeToolAction("masking", "field-13", region);
        canvas()->configureTool(toolState_->toolId(), toolState_->values());
    } else if (action == "importAsset")
        canvas()->importToolSource(item.value("source").toUrl(), "asset", toolState_->values());
}
void EditorWorkspaceModel::showGenerationResults() {
    QVariantList items;
    QHash<QString, QVariantMap> jobs;
    if (generation_)
        for (const auto &value : generation_->jobs())
            jobs[value.toMap().value("id").toString()] = value.toMap();
    for (const auto &id : ids_) {
        const auto job = jobs.value(id);
        const auto state = job.value("state").toString();
        const bool pending = generationPending(state.toLower().toStdString());
        const auto error = job.value("error").toString();
        items.push_back(QVariantMap{
            {"id", id},
            {"label", pending ? tr("Cancel %1").arg(id) : id},
            {"description", job.isEmpty() ? QString() : state + (error.isEmpty() ? QString() : ": " + error)},
            {"action", "job"},
            {"enabled", pending}});
    }
    preview_ = {{"title", tr("Generation results")},
                {"description", toolState_ ? toolState_->notice() : QString()},
                {"items", items}};
    previewTool_ = "generation-results";
    emit previewChanged();
    emit previewRequested();
}
bool EditorWorkspaceModel::placeVariation(const QVariant &input) {
    if (!target_)
        return false;
    const auto entry = viewValue(nativeValue(input)).toMap();
    auto source = entry.value("documentSource").toUrl();
    if (source.isEmpty())
        source = entry.value("imageSource").toUrl();
    if (!target_->placeGenerated(source, operation_))
        return false;
    emit previewCloseRequested();
    return true;
}

bool EditorWorkspaceModel::createCanvas(const QVariantMap &specification) {
    if (!project_)
        return false;
    if (importer_)
        importer_->cancel();
    project_->cancelDocumentOpen();
    return project_->createCanvas(specification);
}
bool EditorWorkspaceModel::openDocument(const QUrl &source, bool asCopy) {
    if (!project_)
        return false;
    if (importer_)
        importer_->cancel();
    return project_->openDocumentSource(source, asCopy);
}
bool EditorWorkspaceModel::openImages(const QVariantList &sources, const QVariantList &entries) {
    if (!project_ || !importer_)
        return false;
    project_->cancelDocumentOpen();
    return importer_->start(sources, entries, false);
}
