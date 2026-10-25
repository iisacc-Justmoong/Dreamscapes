#include "EditorGraphicDraftModel.h"
#include <QRegularExpression>
using namespace dreamscapes;
using namespace dreamscapes::vm;
void EditorGraphicDraftModel::setCatalog(ViewData *value) { if (catalog_ != value) { catalog_ = value; initialize(); emit bindingsChanged(); } }
void EditorGraphicDraftModel::setToolState(EditorToolStateModel *value) {
    if (toolState_ == value) return; if (toolState_) disconnect(toolState_, nullptr, this, nullptr); toolState_ = value;
    if (toolState_) {
        connect(toolState_, &EditorToolStateModel::toolIdChanged, this, [this] { if (!dispatching_) restoreView(); });
        connect(toolState_, &EditorToolStateModel::valuesChanged, this, [this] { ++revision_; emit valuesChanged(); });
    }
    initialize(); emit bindingsChanged();
}
void EditorGraphicDraftModel::initialize() {
    model_.reset(); if (!catalog_ || !toolState_) return;
    model_ = std::make_unique<GraphicDraft>(catalog_->nativeCatalog(), [this] { return toolState_ ? toolState_->nativeSnapshot() : Value(); }, [this](const ToolCommand &command) { if (toolState_) toolState_->executeNative(command); });
    restoreView();
}
bool EditorGraphicDraftModel::send(GraphicAction action, std::initializer_list<QVariant> args) {
    if (!model_ || dispatching_) return false; dispatching_ = true;
    const auto oldView = model_->currentViewId();
    GraphicCommand command{action, {}}; for (const auto &arg : args) command.arguments.push_back(nativeValue(arg));
    const auto reply = model_->commands().send(command); dispatching_ = false;
    if (oldView != model_->currentViewId()) emit viewChanged();
    ++revision_; emit valuesChanged(); return reply && *reply;
}
QString EditorGraphicDraftModel::currentViewId() const { return model_ ? QString::fromStdString(model_->currentViewId()) : QString(); }
void EditorGraphicDraftModel::setCurrentViewId(const QString &value) { selectView(value); }
QVariant EditorGraphicDraftModel::selectedView() const { return model_ ? viewValue(model_->selectedView()) : QVariant::fromValue(nullptr); }
QVariantList EditorGraphicDraftModel::viewCatalog() const { return model_ ? viewValue(model_->viewCatalog()).toList() : QVariantList{}; }
QVariantMap EditorGraphicDraftModel::collectionIndices() const { return model_ ? viewValue(model_->indices()).toMap() : QVariantMap{}; }
void EditorGraphicDraftModel::setCollectionIndices(const QVariantMap &value) { send(GraphicAction::setIndices, {value}); }
QString EditorGraphicDraftModel::defaultView() const { return model_ ? QString::fromStdString(model_->defaultView()) : QString(); }
void EditorGraphicDraftModel::restoreView() { send(GraphicAction::restoreView, {}); }
bool EditorGraphicDraftModel::selectView(const QString &value) { return send(GraphicAction::selectView, {value}); }
QString EditorGraphicDraftModel::operationFor(const QVariant &row) const { return model_ ? QString::fromStdString(model_->operationFor(nativeValue(row))) : QString(); }
QVariantList EditorGraphicDraftModel::rowsOf(const QVariant &rows) const { return model_ ? viewValue(model_->rowsOf(nativeValue(rows))).toList() : QVariantList{}; }
QVariantMap EditorGraphicDraftModel::parameters(const QString &operation) const { return model_ ? viewValue(model_->parameters(operation.toStdString())).toMap() : QVariantMap{}; }
QString EditorGraphicDraftModel::parameterPath(const QVariant &row) const { return model_ ? QString::fromStdString(model_->parameterPath(nativeValue(row))) : QString(); }
QVariant EditorGraphicDraftModel::value(const QVariant &row) const { if (!model_) return {}; const auto result = model_->value(nativeValue(row)); return result ? viewValue(*result) : QVariant(); }
QVariant EditorGraphicDraftModel::collectionDefinition() const { return model_ ? viewValue(model_->collectionDefinition()) : QVariant::fromValue(nullptr); }
QVariantList EditorGraphicDraftModel::collectionItems() const { return model_ ? viewValue(model_->collectionItems()).toList() : QVariantList{}; }
QVariant EditorGraphicDraftModel::collectionItem() const { return model_ ? viewValue(model_->collectionItem()) : QVariantMap{}; }
void EditorGraphicDraftModel::selectCollection(int index) { send(GraphicAction::selectCollection, {index}); }
void EditorGraphicDraftModel::addCollectionItem() { send(GraphicAction::addCollectionItem, {}); }
void EditorGraphicDraftModel::removeCollectionItem() { send(GraphicAction::removeCollectionItem, {}); }
void EditorGraphicDraftModel::edit(const QVariant &row, const QVariant &value) { send(GraphicAction::edit, {row, value}); }
void EditorGraphicDraftModel::resetView() { send(GraphicAction::resetView, {}); }
void EditorGraphicDraftModel::applyReturnedParameters(const QString &api, const QVariant &result) { send(GraphicAction::applyReturnedParameters, {api, result}); }
QVariant EditorGraphicDraftModel::statusFor(const QString &status) const { return model_ ? viewValue(model_->statusFor(status.toStdString())) : QVariant::fromValue(nullptr); }
QVariantList EditorGraphicDraftModel::curvePoints(const QVariant &row) const { return model_ ? viewValue(model_->curvePoints(nativeValue(row))).toList() : QVariantList{}; }
QColor EditorGraphicDraftModel::swatchColor() const {
    if (!model_) return Qt::transparent; const auto view = model_->selectedView(); const auto path = resolvedPath(text(get(view, "sourceParameter")), model_->indices());
    const auto result = pathValue(model_->parameters(text(get(view, "operation"))), path); if (!result || array(*result).size() < 3) return Qt::transparent;
    const auto &channels = array(*result); return QColor::fromRgbF(number(channels[0]), number(channels[1]), number(channels[2]), channels.size() > 3 ? number(channels[3]) : 1);
}
QVariant EditorGraphicDraftModel::curveChannel() const { const auto name = model_ ? QString::fromStdString(text(get(model_->selectedView(), "name"))) : QString(); const auto match = QRegularExpression("Red|Green|Blue").match(name); return match.hasMatch() ? QVariant(QStringList{match.captured()}) : QVariant::fromValue(nullptr); }
