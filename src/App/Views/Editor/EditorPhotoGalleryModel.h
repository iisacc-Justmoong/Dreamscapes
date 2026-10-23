#pragma once
#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>

// GUI projection of the acquired catalog. Native acquisition and file commands
// remain owned by EditorCameraPhoto; data() never scans, sorts or acquires pixels.
class EditorPhotoGalleryModel final : public QAbstractListModel {
    Q_OBJECT
public:
    struct Update { int row; QVariantMap item; };
    enum { EntryRole = Qt::UserRole + 1 };
    explicit EditorPhotoGalleryModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    const QVariantList &items() const { return m_items; }
    const QList<int> &visibleRows() const { return m_rows; }
    QVariantList visibleItems() const { return m_visible; }
    int selectedCount() const { return m_selected; }
    void setItems(QVariantList items);
    bool setFilter(const QString &query, const QString &sort);
    // Current catalog state only: identity and sort/filter keys are retained.
    // Catalog replacement uses setItems; search/order changes use setFilter.
    void updateItems(const QList<Update> &updates);
private:
    void project();
    QVariantList m_items, m_visible;
    QList<int> m_rows, m_position;
    QString m_query, m_sort = "Newest";
    int m_selected = 0;
};
