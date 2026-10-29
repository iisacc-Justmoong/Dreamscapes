#include "SnapshotListModel.h"
#include "ViewData.h"
namespace dreamscapes {
QVariant SnapshotListModel::data(const QModelIndex &index, int role) const {
    return index.isValid() && roles_.contains(role) ? get(index.row()).value(QString::fromUtf8(roles_.value(role)))
                                                    : QVariant();
}
QVariantMap SnapshotListModel::get(int index) const {
    return index >= 0 && index < rows_.size() ? rows_[index].toMap() : QVariantMap{};
}
void SnapshotListModel::setEntries(const QVariantList &values) {
    if (entries_ == values)
        return;
    entries_ = values;
    synchronize();
    emit entriesChanged();
}
void SnapshotListModel::setFieldMapping(const QVariantMap &fields) {
    if (fields_ == fields)
        return;
    beginResetModel();
    fields_ = fields;
    rows_.clear();
    roles_.clear();
    int role = Qt::UserRole + 1;
    for (auto it = fields_.begin(); it != fields_.end(); ++it)
        roles_[role++] = it.key().toUtf8();
    endResetModel();
    synchronize();
    emit fieldMappingChanged();
    emit countChanged();
}
void SnapshotListModel::setKeyRole(QString key) {
    if (key_ != key) {
        key_ = std::move(key);
        synchronize();
        emit fieldMappingChanged();
    }
}
void SnapshotListModel::synchronize() {
    if (fields_.isEmpty())
        return;
    QVariantList next;
    for (const auto &entry : entries_) {
        const auto source = entry.toMap();
        QVariantMap row;
        for (auto it = fields_.begin(); it != fields_.end(); ++it)
            row[it.key()] = source.value(it.value().toString());
        next.push_back(row);
    }
    const auto plan = projector_.queries().send({nativeValue(rows_), nativeValue(next), key_.toStdString()});
    if (!plan || !plan->valid) {
        emit rejected(tr("Snapshot rows require unique stable identifiers."));
        return;
    }
    const auto count = rows_.size();
    for (const auto &step : plan->changes) {
        const int from = step.from, to = step.to;
        switch (step.kind) {
        case vm::RowChangeKind::insert:
            beginInsertRows({}, to, to);
            rows_.insert(to, viewValue(step.row));
            endInsertRows();
            break;
        case vm::RowChangeKind::move:
            if (beginMoveRows({}, from, from, {}, to > from ? to + 1 : to)) {
                const auto row = rows_.takeAt(from);
                rows_.insert(to, row);
                endMoveRows();
            }
            break;
        case vm::RowChangeKind::update:
            rows_[from] = viewValue(step.row);
            emit dataChanged(index(from), index(from));
            break;
        case vm::RowChangeKind::remove:
            beginRemoveRows({}, from, from + step.count - 1);
            rows_.remove(from, step.count);
            endRemoveRows();
            break;
        }
    }
    if (count != rows_.size())
        emit countChanged();
}
} // namespace dreamscapes
