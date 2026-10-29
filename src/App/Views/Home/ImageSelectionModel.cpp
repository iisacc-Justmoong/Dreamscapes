#include "ImageSelectionModel.h"
#include "../../ViewModels/ViewData.h"
#include <QHash>
using namespace dreamscapes;
using namespace dreamscapes::vm;
void ImageSelectionModel::setBackend(QObject *value) {
    auto *next = qobject_cast<GenerationReviewModel *>(value);
    if (backend_ == next)
        return;
    if (backend_)
        disconnect(backend_, nullptr, this, nullptr);
    backend_ = next;
    if (backend_)
        connect(backend_, &GenerationReviewModel::stateChanged, this, &ImageSelectionModel::refresh);
    refresh();
    emit bindingsChanged();
}
void ImageSelectionModel::setEntries(const QVariantList &values) {
    if (entries_ == values)
        return;
    entries_ = values;
    send({ImageSelectionAction::setEntries, nativeValue(values)});
    emit bindingsChanged();
}
void ImageSelectionModel::refresh() {
    adoptPresentation(model_.presentation());
    if (backend_ && !backend_->selectionMode())
        toggle_ = false;
    emit changed();
}
void ImageSelectionModel::adoptPresentation(const Value &projection) {
    state_ = viewValue(projection).toMap();
    QHash<QString, QVariant> originals;
    for (const auto &entry : entries_) {
        const auto key = QString::fromStdString(text(imageKey(nativeValue(entry))));
        if (!originals.contains(key))
            originals.insert(key, entry);
    }
    for (const auto *name : {"images", "selectedImages"}) {
        QVariantList rows;
        for (const auto &entry : state_.value(name).toList()) {
            const auto key = QString::fromStdString(text(imageKey(nativeValue(entry))));
            rows.push_back(originals.value(key, entry));
        }
        state_[name] = rows;
    }
}
void ImageSelectionModel::send(const ImageSelectionCommand &command) {
    const auto reply = model_.commands().send(command);
    if (reply)
        adoptPresentation(*reply);
    emit changed();
}
bool ImageSelectionModel::selectionMode() const {
    return backend_ ? backend_->selectionMode() : state_.value("selectionMode").toBool();
}
void ImageSelectionModel::setSelectionMode(bool enabled) {
    if (backend_)
        backend_->setSelectionMode(enabled);
    else
        send({ImageSelectionAction::setMode, Value(enabled)});
}
QStringList ImageSelectionModel::selectedKeys() const {
    if (!backend_)
        return state_.value("selectedKeys").toStringList();
    QStringList keys;
    for (const auto &entry : backend_->selectedEntries())
        keys.push_back(backend_->keyForEntry(entry.toMap()));
    return keys;
}
void ImageSelectionModel::setSelectedKeys(const QStringList &keys) {
    if (!backend_)
        send({ImageSelectionAction::setKeys, nativeValue(keys)});
}
QString ImageSelectionModel::anchorKey() const {
    return state_.value("anchorKey").toString();
}
void ImageSelectionModel::setAnchorKey(const QString &key) {
    if (!backend_)
        send({ImageSelectionAction::setAnchor, Value(key.toStdString())});
}
QVariantList ImageSelectionModel::selectedImages() const {
    return backend_ ? backend_->selectedImages() : state_.value("selectedImages").toList();
}
bool ImageSelectionModel::isSelected(const QVariant &entry) const {
    return backend_ ? backend_->isSelected(viewValue(nativeValue(entry)).toMap())
                    : model_.isSelected(nativeValue(entry));
}
void ImageSelectionModel::toggle(const QVariant &entry) {
    if (backend_)
        backend_->toggle(viewValue(nativeValue(entry)).toMap());
    else
        send({ImageSelectionAction::toggle, nativeValue(entry)});
}
void ImageSelectionModel::selectAt(int index, int modifiers, int previous) {
    if (backend_) {
        backend_->selectAt(index, modifiers);
        return;
    }
    const bool additive = modifiers & (Qt::ControlModifier | Qt::MetaModifier), range = modifiers & Qt::ShiftModifier;
    if ((additive || range) && !selectionMode())
        toggle_ = false;
    ImageSelectionCommand command{ImageSelectionAction::selectAt, {}, index, previous, additive, range, toggle_};
    send(command);
}
void ImageSelectionModel::selectAll() {
    toggle_ = false;
    if (backend_)
        backend_->selectAll();
    else
        send({ImageSelectionAction::selectAll});
}
void ImageSelectionModel::reset() {
    toggle_ = false;
    if (backend_)
        backend_->clearSelection();
    else
        send({ImageSelectionAction::reset});
    emit changed();
}
void ImageSelectionModel::toggleMode() {
    if (selectionMode())
        reset();
    else {
        if (!backend_)
            reset();
        setSelectionMode(true);
        setToggleOnClick(true);
    }
}
