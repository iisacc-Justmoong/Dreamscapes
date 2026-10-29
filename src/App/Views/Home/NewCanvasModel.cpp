#include "NewCanvasModel.h"
#include "../../ViewModels/ViewData.h"
using namespace dreamscapes;
using namespace dreamscapes::vm;
NewCanvasModel::NewCanvasModel(QObject *parent)
    : QObject(parent), catalogue_(this),
      model_(
          [this](int category, std::string_view query) {
              return nativeValue(catalogue_.sections(category, QString::fromUtf8(query.data(), query.size())));
          },
          [this](double w, double h, std::string_view unit, double ppi, std::string_view background) {
              return nativeValue(catalogue_.specification(w, h, QString::fromUtf8(unit.data(), unit.size()), ppi,
                                                          QString::fromUtf8(background.data(), background.size())));
          },
          [this](double value, std::string_view from, std::string_view to, double ppi) {
              return catalogue_.convert(value, QString::fromUtf8(from.data(), from.size()),
                                        QString::fromUtf8(to.data(), to.size()), ppi);
          }) {
    refresh(model_.presentation());
}
void NewCanvasModel::send(CanvasDraftAction action, const QString &key, const QVariant &value) {
    const auto reply = model_.commands().send({action, key.toStdString(), nativeValue(value)});
    if (!reply)
        return;
    refresh(reply->presentation);
    if (reply->created)
        emit canvasRequested(viewValue(*reply->created).toMap());
}
void NewCanvasModel::refresh(const Value &value) {
    const auto old = state_;
    state_ = viewValue(value).toMap();
    if (old.value("categoryIndex") != state_.value("categoryIndex"))
        emit categoryIndexChanged();
    if (old.value("query") != state_.value("query"))
        emit queryChanged();
    if (old.value("custom") != state_.value("custom"))
        emit customChanged();
    if (old.value("selectedPreset") != state_.value("selectedPreset"))
        emit selectedPresetChanged();
    if (old.value("draftWidth") != state_.value("draftWidth"))
        emit draftWidthChanged();
    if (old.value("draftHeight") != state_.value("draftHeight"))
        emit draftHeightChanged();
    if (old.value("unit") != state_.value("unit"))
        emit unitChanged();
    if (old.value("resolution") != state_.value("resolution"))
        emit resolutionChanged();
    if (old.value("canvasBackground") != state_.value("canvasBackground"))
        emit canvasBackgroundChanged();
    if (old.value("creationError") != state_.value("creationError"))
        emit creationErrorChanged();
    if (old.value("sections") != state_.value("sections"))
        emit sectionsChanged();
    if (old.value("resultCount") != state_.value("resultCount"))
        emit resultCountChanged();
    if (old.value("specification") != state_.value("specification"))
        emit specificationChanged();
    if (old.value("selectionName") != state_.value("selectionName"))
        emit selectionNameChanged();
    if (old.value("dimensionText") != state_.value("dimensionText"))
        emit dimensionTextChanged();
    if (old.value("aspectRatio") != state_.value("aspectRatio"))
        emit aspectRatioChanged();
}
void NewCanvasModel::setCategoryIndex(int value) {
    send(CanvasDraftAction::write, "categoryIndex", value);
}
void NewCanvasModel::setQuery(QString value) {
    send(CanvasDraftAction::write, "query", value);
}
void NewCanvasModel::setCustom(bool value) {
    send(CanvasDraftAction::write, "custom", value);
}
void NewCanvasModel::setSelectedPreset(QVariantMap value) {
    send(CanvasDraftAction::write, "selectedPreset", value);
}
void NewCanvasModel::setDraftWidth(QString value) {
    send(CanvasDraftAction::write, "draftWidth", value);
}
void NewCanvasModel::setDraftHeight(QString value) {
    send(CanvasDraftAction::write, "draftHeight", value);
}
void NewCanvasModel::setUnit(QString value) {
    send(CanvasDraftAction::write, "unit", value);
}
void NewCanvasModel::setResolution(QString value) {
    send(CanvasDraftAction::write, "resolution", value);
}
void NewCanvasModel::setCanvasBackground(QString value) {
    send(CanvasDraftAction::write, "canvasBackground", value);
}
void NewCanvasModel::setCreationError(QString value) {
    send(CanvasDraftAction::write, "creationError", value);
}
void NewCanvasModel::choose(const QVariant &preset) {
    send(CanvasDraftAction::choose, {}, preset);
}
void NewCanvasModel::chooseCategory(int index) {
    send(CanvasDraftAction::chooseCategory, {}, index);
}
void NewCanvasModel::search(const QString &query) {
    send(CanvasDraftAction::search, {}, query);
}
void NewCanvasModel::customSize() {
    send(CanvasDraftAction::customSize, {}, QVariant());
}
void NewCanvasModel::begin() {
    send(CanvasDraftAction::begin, {}, QVariant());
}
void NewCanvasModel::changeUnit(const QString &unit) {
    send(CanvasDraftAction::changeUnit, {}, unit);
}
void NewCanvasModel::create() {
    send(CanvasDraftAction::create, {}, QVariant());
}
