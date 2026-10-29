#include "EditorGraphicDraftModel.h"
#include "EditorCanvas.h"
#include <QRegularExpression>
using namespace dreamscapes;
using namespace dreamscapes::vm;
EditorGraphicDraftModel::EditorGraphicDraftModel(QObject *parent) : QObject(parent), helper_(this), image_(this) {
    helper_.setObjectName("editorGraphicHelper");
    image_.setObjectName("editorGraphicCommand");
    connect(&helper_, &EditorGraphicHelper::changed, this, [this] {
        emit operationChanged();
        QMetaObject::invokeMethod(this, &EditorGraphicDraftModel::finishHelper, Qt::QueuedConnection);
    });
    connect(&image_, &EditorGraphicCommand::changed, this, [this] {
        emit operationChanged();
        QMetaObject::invokeMethod(this, &EditorGraphicDraftModel::finishImage, Qt::QueuedConnection);
    });
}
EditorCanvas *EditorGraphicDraftModel::engine() const {
    return toolState_ ? toolState_->engine() : nullptr;
}
void EditorGraphicDraftModel::syncEngine() {
    auto *next = engine();
    if (observedEngine_ == next)
        return;
    if (observedEngine_)
        disconnect(observedEngine_, nullptr, this, nullptr);
    observedEngine_ = next;
    helper_.setCanvas(next);
    image_.setCanvas(next);
    if (next)
        connect(next, &EditorCanvas::toolStateChanged, this, &EditorGraphicDraftModel::finishSample);
}
void EditorGraphicDraftModel::syncContexts() {
    syncEngine();
    helper_.setContext(helperContext());
    image_.setContext(imageContext());
}
void EditorGraphicDraftModel::setCatalog(ViewData *value) {
    if (catalog_ != value) {
        catalog_ = value;
        initialize();
        emit bindingsChanged();
    }
}
void EditorGraphicDraftModel::setToolState(EditorToolStateModel *value) {
    if (toolState_ == value)
        return;
    if (toolState_)
        disconnect(toolState_, nullptr, this, nullptr);
    toolState_ = value;
    if (toolState_) {
        connect(toolState_, &EditorToolStateModel::toolIdChanged, this, [this] {
            if (!dispatching_)
                restoreView();
        });
        connect(toolState_, &EditorToolStateModel::valuesChanged, this, [this] {
            ++imageEpoch_;
            ++revision_;
            syncContexts();
            emit valuesChanged();
        });
        connect(toolState_, &EditorToolStateModel::engineChanged, this, &EditorGraphicDraftModel::syncContexts);
    }
    initialize();
    emit bindingsChanged();
}
void EditorGraphicDraftModel::initialize() {
    model_.reset();
    if (!catalog_ || !toolState_)
        return;
    model_ = std::make_unique<GraphicDraft>(
        catalog_->nativeCatalog(), [this] { return toolState_ ? toolState_->nativeSnapshot() : Value(); },
        [this](const ToolCommand &command) {
            if (toolState_)
                toolState_->executeNative(command);
        });
    restoreView();
    syncContexts();
}
bool EditorGraphicDraftModel::send(GraphicAction action, std::initializer_list<QVariant> args) {
    if (!model_ || dispatching_)
        return false;
    dispatching_ = true;
    const auto oldView = model_->currentViewId();
    GraphicCommand command{action, {}};
    for (const auto &arg : args)
        command.arguments.push_back(nativeValue(arg));
    const auto reply = model_->commands().send(command);
    dispatching_ = false;
    if (oldView != model_->currentViewId())
        emit viewChanged();
    ++revision_;
    syncContexts();
    emit valuesChanged();
    return reply && *reply;
}
QString EditorGraphicDraftModel::currentViewId() const {
    return model_ ? QString::fromStdString(model_->currentViewId()) : QString();
}
void EditorGraphicDraftModel::setCurrentViewId(const QString &value) {
    selectView(value);
}
QVariant EditorGraphicDraftModel::selectedView() const {
    return model_ ? viewValue(model_->selectedView()) : QVariant::fromValue(nullptr);
}
QVariantList EditorGraphicDraftModel::viewCatalog() const {
    return model_ ? viewValue(model_->viewCatalog()).toList() : QVariantList{};
}
QVariantMap EditorGraphicDraftModel::collectionIndices() const {
    return model_ ? viewValue(model_->indices()).toMap() : QVariantMap{};
}
void EditorGraphicDraftModel::setCollectionIndices(const QVariantMap &value) {
    send(GraphicAction::setIndices, {value});
}
QString EditorGraphicDraftModel::defaultView() const {
    return model_ ? QString::fromStdString(model_->defaultView()) : QString();
}
void EditorGraphicDraftModel::restoreView() {
    send(GraphicAction::restoreView, {});
}
bool EditorGraphicDraftModel::selectView(const QString &value) {
    return send(GraphicAction::selectView, {value});
}
QString EditorGraphicDraftModel::operationFor(const QVariant &row) const {
    return model_ ? QString::fromStdString(model_->operationFor(nativeValue(row))) : QString();
}
QVariantList EditorGraphicDraftModel::rowsOf(const QVariant &rows) const {
    return model_ ? viewValue(model_->rowsOf(nativeValue(rows))).toList() : QVariantList{};
}
QVariantMap EditorGraphicDraftModel::parameters(const QString &operation) const {
    return model_ ? viewValue(model_->parameters(operation.toStdString())).toMap() : QVariantMap{};
}
QString EditorGraphicDraftModel::parameterPath(const QVariant &row) const {
    return model_ ? QString::fromStdString(model_->parameterPath(nativeValue(row))) : QString();
}
QVariant EditorGraphicDraftModel::value(const QVariant &row) const {
    if (!model_)
        return {};
    const auto result = model_->value(nativeValue(row));
    return result ? viewValue(*result) : QVariant();
}
QVariant EditorGraphicDraftModel::collectionDefinition() const {
    return model_ ? viewValue(model_->collectionDefinition()) : QVariant::fromValue(nullptr);
}
QVariantList EditorGraphicDraftModel::collectionItems() const {
    return model_ ? viewValue(model_->collectionItems()).toList() : QVariantList{};
}
QVariant EditorGraphicDraftModel::collectionItem() const {
    return model_ ? viewValue(model_->collectionItem()) : QVariantMap{};
}
void EditorGraphicDraftModel::selectCollection(int index) {
    send(GraphicAction::selectCollection, {index});
}
void EditorGraphicDraftModel::addCollectionItem() {
    send(GraphicAction::addCollectionItem, {});
}
void EditorGraphicDraftModel::removeCollectionItem() {
    send(GraphicAction::removeCollectionItem, {});
}
void EditorGraphicDraftModel::edit(const QVariant &row, const QVariant &value) {
    send(GraphicAction::edit, {row, value});
}
void EditorGraphicDraftModel::resetView() {
    send(GraphicAction::resetView, {});
}
void EditorGraphicDraftModel::applyReturnedParameters(const QString &api, const QVariant &result) {
    send(GraphicAction::applyReturnedParameters, {api, result});
}
QVariant EditorGraphicDraftModel::statusFor(const QString &status) const {
    return model_ ? viewValue(model_->statusFor(status.toStdString())) : QVariant::fromValue(nullptr);
}
QVariantList EditorGraphicDraftModel::curvePoints(const QVariant &row) const {
    return model_ ? viewValue(model_->curvePoints(nativeValue(row))).toList() : QVariantList{};
}
QColor EditorGraphicDraftModel::swatchColor() const {
    if (!model_)
        return Qt::transparent;
    const auto view = model_->selectedView();
    const auto path = resolvedPath(text(get(view, "sourceParameter")), model_->indices());
    const auto result = pathValue(model_->parameters(text(get(view, "operation"))), path);
    if (!result || array(*result).size() < 3)
        return Qt::transparent;
    const auto &channels = array(*result);
    return QColor::fromRgbF(number(channels[0]), number(channels[1]), number(channels[2]),
                            channels.size() > 3 ? number(channels[3]) : 1);
}
QVariant EditorGraphicDraftModel::curveChannel() const {
    const auto name = model_ ? QString::fromStdString(text(get(model_->selectedView(), "name"))) : QString();
    const auto match = QRegularExpression("Red|Green|Blue").match(name);
    return match.hasMatch() ? QVariant(QStringList{match.captured()}) : QVariant::fromValue(nullptr);
}

QVariantMap EditorGraphicDraftModel::helperContext() const {
    return {{"tool", toolState_ ? toolState_->toolId() : QString()},
            {"view", currentViewId()},
            {"indices", collectionIndices()},
            {"draft", helperAction_ && model_
                          ? parameters(QString::fromStdString(text(get(model_->selectedView(), "operation"))))
                          : QVariantMap{}}};
}
QVariantMap EditorGraphicDraftModel::imageContext() const {
    return {{"tool", toolState_ ? toolState_->toolId() : QString()},
            {"view", currentViewId()},
            {"epoch", imageEpoch_},
            {"indices", collectionIndices()}};
}
void EditorGraphicDraftModel::activate(const QVariant &option, const QVariant &row) {
    if (!model_)
        return;
    const auto plan = model_->activations().send({nativeValue(option), nativeValue(row)});
    if (!plan)
        return;
    const auto &p = *plan;
    switch (p.route) {
    case GraphicRoute::none:
        break;
    case GraphicRoute::execute:
        execute(QString::fromStdString(p.action), QString::fromStdString(p.operation),
                p.parameters.kind() == ii::core::ValueKind::null ? QVariant() : viewValue(p.parameters));
        break;
    case GraphicRoute::reset:
        resetView();
        setResultMessage({});
        break;
    case GraphicRoute::view:
        selectView(QString::fromStdString(p.target));
        setResultMessage({});
        break;
    case GraphicRoute::source:
        sourceParameter_ = parameterPath(row);
        sourceOperation_ = operationFor(row);
        emit sourceRequested();
        break;
    case GraphicRoute::sample:
        if (!engine())
            break;
        pickerRow_ = row;
        pickerApi_ = QString::fromStdString(p.operation);
        sampleSequence_ = engine()->layerColorSample().value("sequence").toULongLong();
        samplingColor_ = engine()->beginLayerColorSample();
        setResultMessage(samplingColor_ ? tr("Click a visible color on the canvas") : engine()->error());
        if (samplingColor_)
            emit closeToolRequested();
        break;
    case GraphicRoute::color: {
        pickerRow_ = row;
        pickerApi_ = QString::fromStdString(p.operation);
        QColor color("#808080");
        const auto &channels = array(p.value);
        if (channels.size() >= 3)
            color = QColor::fromRgbF(number(channels[0]), number(channels[1]), number(channels[2]),
                                     channels.size() > 3 ? number(channels[3]) : 1);
        else if (text(p.value).starts_with('#'))
            color = QColor(QString::fromStdString(text(p.value)).section(" · ", 0, 0));
        emit colorRequested(color);
        break;
    }
    case GraphicRoute::edit:
        edit(row, viewValue(p.value));
        setResultMessage({});
        break;
    }
}
void EditorGraphicDraftModel::commitField(const QVariant &row) {
    const auto r = nativeValue(row);
    if (boolean(get(r, "sourceArgument")) && text(get(r, "api")) == "refineMask")
        execute("mask-helper", "refineMask", QVariantMap{{"amount", value(row)}});
}
void EditorGraphicDraftModel::execute(const QString &action, const QString &operation, const QVariant &supplied) {
    if (!engine() || busy() || !model_ || model_->selectedView().kind() == ii::core::ValueKind::null)
        return;
    emit sheetsCloseRequested();
    operationStatus_ = "processing";
    resultMessage_ = tr("Processing…");
    const auto api =
        operation.isEmpty() ? QString::fromStdString(text(get(model_->selectedView(), "operation"))) : operation;
    const auto detached = nativeValue(supplied);
    const auto args = detached.kind() == ii::core::ValueKind::null ? parameters(api) : viewValue(detached).toMap();
    if (action == "helper") {
        preparingHelper_ = true;
        helperAction_ = true;
        helperOrigin_ = engine();
        syncContexts();
        helper_.request(api, args);
        preparingHelper_ = false;
        finishHelper();
    } else {
        imageAction_ = true;
        imageOrigin_ = engine();
        imageActionName_ = action;
        imageApi_ = api;
        syncContexts();
        image_.request(action, toolState_->toolId(), api, args);
        finishImage();
    }
    emit operationChanged();
}
void EditorGraphicDraftModel::finishHelper() {
    if (!helperAction_ || preparingHelper_ || helper_.state().value("pending").toBool())
        return;
    const auto result = helper_.state();
    const auto current = helperOrigin_ == engine() && helper_.contextMatches(helperContext());
    helperAction_ = false;
    if (!current || !result.value("valid").toBool()) {
        resultMessage_ = current ? result.value("error").toString() : QString();
        operationStatus_ = resultMessage_.isEmpty()                ? QString()
                           : engine() && engine()->documentReady() ? "error"
                                                                   : "empty";
        emit operationChanged();
        return;
    }
    applyReturnedParameters(result.value("operation").toString(), result);
    resultMessage_ = tr("Settings updated");
    operationStatus_ = "applied";
    syncContexts();
    emit operationChanged();
}
void EditorGraphicDraftModel::finishImage() {
    if (!imageAction_ || image_.state().value("pending").toBool())
        return;
    const auto result = image_.state();
    const auto current = imageOrigin_ == engine() && image_.contextMatches(imageContext());
    imageAction_ = false;
    if (!current || !result.value("valid").toBool()) {
        resultMessage_ = current ? result.value("error").toString() : QString();
        operationStatus_ = resultMessage_.isEmpty()                ? QString()
                           : engine() && engine()->documentReady() ? "error"
                                                                   : "empty";
        emit operationChanged();
        return;
    }
    if (imageActionName_ == "preview") {
        previewImage_ = result.value("imageSource").toUrl();
        originalImage_ = result.value("originalImageSource", previewImage_).toUrl();
        applyReturnedParameters(imageApi_, result);
        emit previewRequested();
    }
    resultMessage_ = imageActionName_ == "preview" ? tr("Preview ready") : tr("Applied");
    operationStatus_ = imageActionName_ == "preview" ? "preview" : "applied";
    emit operationChanged();
    if (imageActionName_ != "preview" && imageActionName_ != "mask-helper")
        emit operationSheetRequested();
}
void EditorGraphicDraftModel::acceptSource(const QUrl &source) {
    if (toolState_)
        toolState_->setGraphicValue(sourceOperation_, sourceParameter_, source.toString());
}
void EditorGraphicDraftModel::acceptColor(const QColor &color) {
    const auto row = nativeValue(pickerRow_);
    QVariantList channels{color.redF(), color.greenF(), color.blueF()};
    if (model_ && (text(get(model_->selectedView(), "sourceType")) == "Rgba" ||
                   text(get(row, "initial")).find("100%") != std::string::npos || boolean(get(row, "sourceArgument"))))
        channels.push_back(color.alphaF());
    if (!pickerApi_.isEmpty())
        execute("helper", pickerApi_, QVariantMap{{"sample", channels}});
    else if (toolState_ && row.kind() != ii::core::ValueKind::null)
        toolState_->setGraphicValue(operationFor(pickerRow_), parameterPath(pickerRow_), channels);
}
void EditorGraphicDraftModel::finishSample() {
    if (!engine() || !samplingColor_)
        return;
    const auto sample = engine()->layerColorSample();
    if (sample.value("picking").toBool() || sample.value("pending").toBool())
        return;
    samplingColor_ = false;
    emit operationChanged();
    if (!sample.value("error").toString().isEmpty()) {
        setResultMessage(sample.value("error").toString());
        return;
    }
    if (!sample.value("valid").toBool() || sample.value("sequence").toULongLong() == sampleSequence_)
        return;
    const auto color = sample.value("color").value<QColor>();
    execute("helper", pickerApi_,
            QVariantMap{{"sample", QVariantList{color.redF(), color.greenF(), color.blueF(), color.alphaF()}}});
    emit reopenToolRequested();
}
