#include "EditorBackgroundModel.h"
#include "EditorCanvas.h"
#include <QScopedValueRollback>
using namespace dreamscapes;
using namespace dreamscapes::vm;
EditorBackgroundModel::EditorBackgroundModel(QObject *parent) : QObject(parent), color_(this) {
    connect(&color_, &ColorManagerModel::colorEdited, this, [this] {
        if (syncing_ || !owner_)
            return;
        QScopedValueRollback guard(syncing_, true);
        edit("backgroundColor", '#' + color_.hex().toUpper());
        edit("backgroundAlpha", color_.channelValue(10));
        edit("backgroundFill", "Solid");
    });
}
void EditorBackgroundModel::setToolState(EditorToolStateModel *owner) {
    if (owner_ == owner)
        return;
    if (owner_)
        disconnect(owner_, nullptr, this, nullptr);
    owner_ = owner;
    if (owner_) {
        connect(owner_, &EditorToolStateModel::valuesChanged, this, &EditorBackgroundModel::syncColor);
        connect(owner_, &EditorToolStateModel::valuesChanged, this, &EditorBackgroundModel::paletteChanged);
        connect(owner_, &EditorToolStateModel::catalogChanged, this, &EditorBackgroundModel::paletteChanged);
    }
    QPointer<EditorBackgroundModel> self(this);
    syncColor();
    if (!self) return;
    emit toolStateChanged();
    if (self) emit paletteChanged();
}
QVariantMap EditorBackgroundModel::values() const {
    return owner_ ? owner_->values() : QVariantMap{};
}
QVariant EditorBackgroundModel::value(const QString &field, const QVariant &fallback) const {
    const auto current = values();
    if (current.contains(field))
        return current.value(field);
    if (fallback.isValid())
        return fallback;
    if (owner_ && owner_->catalog()) {
        const auto &color = get(owner_->catalog()->nativeCatalog().resource("background-defaults"), "color");
        if (has(color, field.toStdString()))
            return viewValue(get(color, field.toStdString()));
    }
    return fallback;
}
EditorCanvas *EditorBackgroundModel::engine() const {
    return owner_ ? owner_->engine() : nullptr;
}
void EditorBackgroundModel::edit(const QString &field, const QVariant &value) {
    if (owner_)
        owner_->setValue(field, value);
}
void EditorBackgroundModel::report(bool success) {
    if (owner_)
        owner_->setNotice(success || !engine() ? QString() : engine()->error());
}
void EditorBackgroundModel::defaults(const char *group) {
    if (!owner_ || !owner_->catalog())
        return;
    const auto &defaults = get(owner_->catalog()->nativeCatalog().resource("background-defaults"), group);
    for (const auto &[key, value] : object(defaults))
        edit(QString::fromStdString(key), viewValue(value));
}
void EditorBackgroundModel::syncColor() {
    if (syncing_ || !owner_)
        return;
    QScopedValueRollback guard(syncing_, true);
    color_.setMode(value("backgroundPickerMode", 0).toInt());
    const auto color = value("backgroundColor", "#8066FF").toString();
    if ('#' + color_.hex().toUpper() != color.toUpper())
        color_.beginColor(QColor(color));
    const auto alpha = value("backgroundAlpha", 100).toDouble();
    if (std::abs(color_.channelValue(10) - alpha) > .01)
        color_.editChannel(10, alpha);
}
void EditorBackgroundModel::changeStop(int index, const QString &field, const QVariant &value) {
    auto stops = this->value("backgroundGradientStops").toList();
    if (index < 0 || index >= stops.size())
        return;
    auto stop = stops[index].toMap();
    stop[field] = value;
    stops[index] = stop;
    edit("backgroundGradientStops", stops);
    edit("backgroundFill", "Gradient");
}
void EditorBackgroundModel::applyFill() {
    if (!engine())
        return;
    report(engine()->applyBackgroundFill(values()));
    const auto color = '#' + color_.hex();
    auto recent =
        value("backgroundRecent", QStringList{"#8066FF", "#FFAA5A", "#60D3B5", "#101116", "#FFFFFF"}).toStringList();
    recent.removeAll(color);
    recent.prepend(color);
    while (recent.size() > 10)
        recent.removeLast();
    edit("backgroundRecent", recent);
}
void EditorBackgroundModel::resetColor() {
    {
        QScopedValueRollback guard(syncing_, true);
        defaults("color");
    }
    syncColor();
    if (owner_)
        owner_->setNotice({});
}
void EditorBackgroundModel::addSwatch() {
    const auto color = '#' + color_.hex().toUpper();
    auto saved = value("backgroundSwatches", QStringList{}).toStringList();
    if (!saved.contains(color)) {
        saved.append(color);
        edit("backgroundSwatches", saved);
    }
    edit("backgroundPalette", "Saved swatches");
}
void EditorBackgroundModel::addStop() {
    auto stops = value("backgroundGradientStops").toList();
    if (stops.size() >= 32)
        return;
    stops.push_back(QVariantMap{{"position", .5}, {"color", '#' + color_.hex()}});
    std::stable_sort(stops.begin(), stops.end(), [](const auto &a, const auto &b) {
        return a.toMap().value("position").toDouble() < b.toMap().value("position").toDouble();
    });
    edit("backgroundGradientStops", stops);
    edit("backgroundFill", "Gradient");
}
void EditorBackgroundModel::removeStop(int index) {
    auto stops = value("backgroundGradientStops").toList();
    if (stops.size() <= 2)
        return;
    stops.removeAt(std::clamp(index, 0, int(stops.size()) - 1));
    edit("backgroundGradientStops", stops);
}
void EditorBackgroundModel::reverseStops() {
    auto stops = value("backgroundGradientStops").toList();
    std::reverse(stops.begin(), stops.end());
    for (auto &v : stops) {
        auto stop = v.toMap();
        stop["position"] = 1 - stop.value("position").toDouble();
        v = stop;
    }
    edit("backgroundGradientStops", stops);
}
void EditorBackgroundModel::selectGradient(const QVariant &preset) {
    const auto p = viewValue(nativeValue(preset)).toMap();
    const auto stops = palette_.queries().send({PaletteAction::stops, {}, {}, nativeValue(preset)});
    if (!stops) return;
    edit("backgroundGradientStops", viewValue(*stops));
    edit("backgroundGradientName", p.value("name"));
    edit("backgroundFill", "Gradient");
}
QVariantList EditorBackgroundModel::gradientPresets() const {
    const auto defaults = owner_ && owner_->catalog()
        ? get(owner_->catalog()->nativeCatalog().resource("background-defaults"), "presets") : Value(Array{});
    const auto result = palette_.queries().send({PaletteAction::presets, defaults,
                                                nativeValue(value("backgroundSavedPresets", QVariantList{})), {}});
    return result ? viewValue(*result).toList() : QVariantList{};
}
void EditorBackgroundModel::saveGradient() {
    const auto result = palette_.queries().send({PaletteAction::save, {},
        nativeValue(value("backgroundSavedPresets", QVariantList{})),
        nativeValue(QVariantMap{{"name", value("backgroundGradientName", "Gradient")},
                                {"stops", value("backgroundGradientStops")}})});
    if (result) edit("backgroundSavedPresets", viewValue(*result));
}
void EditorBackgroundModel::runRemoval() {
    if (!engine())
        return;
    const auto result = engine()->previewBackgroundRemoval(values());
    edit("backgroundRemovalPreview", result);
    if (owner_)
        owner_->setNotice(result.value("ready").toBool() ? QString() : result.value("error").toString());
}
void EditorBackgroundModel::refineRemoval(const QString &field, const QVariant &value) {
    const bool ready = this->value("backgroundRemovalPreview").toMap().value("ready").toBool();
    edit(field, value);
    if (ready)
        runRemoval();
}
void EditorBackgroundModel::resetEdges() {
    defaults("edges");
    if (value("backgroundRemovalPreview").toMap().value("ready").toBool())
        runRemoval();
}
void EditorBackgroundModel::resetRemoval() {
    edit("backgroundRemovalPreview", QVariantMap{});
    resetEdges();
    defaults("removal");
    if (owner_)
        owner_->setNotice({});
}
void EditorBackgroundModel::applyRemoval(bool asMask) {
    if (!engine() || !value("backgroundRemovalPreview").toMap().value("ready").toBool())
        return;
    if (engine()->applyBackgroundRemoval(values())) {
        edit("backgroundRemovalPreview", QVariantMap{});
        if (asMask)
            edit("backgroundView", "Mask");
    } else
        report(false);
}
void EditorBackgroundModel::chooseReplacement(const QUrl &source, const QString &kind) {
    edit(kind == "Image" ? "replaceImage" : "replaceFile", source.toString());
    edit("replaceSource", kind);
    auto recent = value("replaceRecentFiles").toStringList();
    recent.removeAll(source.toString());
    recent.prepend(source.toString());
    while (recent.size() > 10)
        recent.removeLast();
    edit("replaceRecentFiles", recent);
}
void EditorBackgroundModel::generateReplacement() {
    if (!engine() || !engine()->documentReady()) {
        if (owner_)
            owner_->setNotice(tr("Open a canvas first."));
        return;
    }
    auto next = values();
    next["field-12"] = value("replacePrompt", "Soft studio background, warm daylight");
    next["field-13"] = "Generate";
    emit actionRequested("background", "field-15", next);
}
void EditorBackgroundModel::resetReplacement() {
    defaults("replace");
    if (owner_)
        owner_->setNotice({});
}
void EditorBackgroundModel::applyReplacement() {
    const auto source = value("replaceSource", "Generative").toString();
    if (source == "Generative") {
        generateReplacement();
        return;
    }
    if (engine())
        report(engine()->applyBackgroundReplacement(value(source == "Image" ? "replaceImage" : "replaceFile").toUrl(),
                                                    values()));
}
void EditorBackgroundModel::maskAction(const QString &name) {
    if (!engine())
        return;
    const auto ok = engine()->backgroundMaskAction(name);
    if (owner_)
        owner_->setNotice(ok ? (name == "clear" ? tr("Selection cleared.") : QString()) : engine()->error());
}
void EditorBackgroundModel::maskProperty(const QString &field, const QVariant &value) {
    if (engine())
        report(engine()->setBackgroundMaskProperty(field, value));
}
void EditorBackgroundModel::maskCollectionAction(const QString &action) {
    if (engine())
        report(action == "new"         ? engine()->createBackgroundMask()
               : action == "duplicate" ? engine()->duplicateBackgroundMask()
                                       : engine()->deleteBackgroundMask());
}
void EditorBackgroundModel::resetMask() {
    defaults("mask");
    if (engine() && !engine()->maskPanelInfo().value("selected").toMap().value("id").toString().isEmpty())
        maskAction("reset");
}
