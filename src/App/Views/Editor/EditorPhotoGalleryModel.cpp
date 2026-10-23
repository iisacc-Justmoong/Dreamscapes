#include "EditorPhotoGalleryModel.h"
#include <QDateTime>
#include <QPointer>
#include <algorithm>

int EditorPhotoGalleryModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
}
QVariant EditorPhotoGalleryModel::data(const QModelIndex &index, int role) const {
    if (role != EntryRole || !index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 || index.row() >= m_rows.size()) return {};
    return m_items[m_rows[index.row()]];
}
QHash<int, QByteArray> EditorPhotoGalleryModel::roleNames() const { return {{EntryRole, "entry"}}; }
void EditorPhotoGalleryModel::project() {
    m_rows.clear(); m_visible.clear(); m_position.fill(-1, m_items.size()); m_selected = 0;
    for (int row = 0; row < m_items.size(); ++row) {
        const auto item = m_items[row].toMap(); m_selected += item.value("selected").toBool();
        if (m_query.isEmpty() || item.value("label").toString().contains(m_query, Qt::CaseInsensitive)) m_rows.append(row);
    }
    std::stable_sort(m_rows.begin(), m_rows.end(), [this](int left, int right) {
        const auto a = m_items[left].toMap(), b = m_items[right].toMap();
        if (m_sort == "Name") return a.value("label").toString().localeAwareCompare(b.value("label").toString()) < 0;
        if (m_sort == "Size") return a.value("size").toLongLong() > b.value("size").toLongLong();
        return m_sort == "Oldest" ? a.value("created").toDateTime() < b.value("created").toDateTime() : a.value("created").toDateTime() > b.value("created").toDateTime();
    });
    m_visible.reserve(m_rows.size());
    for (int row = 0; row < m_rows.size(); ++row) { m_position[m_rows[row]] = row; m_visible.append(m_items[m_rows[row]]); }
}
void EditorPhotoGalleryModel::setItems(QVariantList items) {
    if (items == m_items) return;
    const QPointer<EditorPhotoGalleryModel> owner(this);
    beginResetModel(); if (!owner) return;
    m_items = std::move(items); project(); endResetModel();
}
bool EditorPhotoGalleryModel::setFilter(const QString &query, const QString &sort) {
    if (query == m_query && sort == m_sort) return false;
    const QPointer<EditorPhotoGalleryModel> owner(this);
    beginResetModel(); if (!owner) return true;
    m_query = query; m_sort = sort; project(); endResetModel(); return true;
}
void EditorPhotoGalleryModel::updateItems(const QList<Update> &updates) {
    int first = int(m_rows.size()), last = -1;
    for (const auto &update : updates) {
        if (update.row < 0 || update.row >= m_items.size() || m_items[update.row] == update.item) continue;
        const auto prior = m_items[update.row].toMap();
        m_selected += int(update.item.value("selected").toBool()) - int(prior.value("selected").toBool());
        m_items[update.row] = update.item;
        const int row = m_position[update.row];
        if (row >= 0) { m_visible[row] = update.item; first = std::min(first, row); last = std::max(last, row); }
    }
    // Commit the whole batch before a view observer can change the filter or
    // destroy the owner. No member access follows this notification.
    if (last >= 0) emit dataChanged(index(first, 0), index(last, 0), {EntryRole});
}
