#pragma once
#include "ReviewSelection.h"
#include <QAbstractListModel>
#include <QVariantList>
#include <QHash>

class GenerationReviewModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries WRITE setEntries NOTIFY entriesChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY projectionChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY stateChanged)
    Q_PROPERTY(int shortlistedCount READ shortlistedCount NOTIFY stateChanged)
    Q_PROPERTY(int rejectedCount READ rejectedCount NOTIFY stateChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY stateChanged)
    Q_PROPERTY(int hiddenSelectedCount READ hiddenSelectedCount NOTIFY stateChanged)
    Q_PROPERTY(QVariantList selectedEntries READ selectedEntries NOTIFY stateChanged)
    Q_PROPERTY(QVariantList selectedImages READ selectedImages NOTIFY stateChanged)
    Q_PROPERTY(QVariantList actionEntries READ actionEntries NOTIFY stateChanged)
    Q_PROPERTY(QVariantList inspectorRows READ inspectorRows NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap focusedEntry READ focusedEntry NOTIFY stateChanged)
    Q_PROPERTY(QString focusedKey READ focusedKey NOTIFY stateChanged)
    Q_PROPERTY(int focusedIndex READ focusedIndex NOTIFY stateChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString jobFilter READ jobFilter WRITE setJobFilter NOTIFY filterChanged)
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY sortChanged)
    Q_PROPERTY(bool selectionMode READ selectionMode WRITE setSelectionMode NOTIFY stateChanged)
public:
    enum Roles { KeyRole = Qt::UserRole + 1, EntryRole, ReviewStateRole, SelectedRole, FocusedRole,
        OrdinalRole, ThumbnailRole, MediaTypeRole, TitleRole };
    explicit GenerationReviewModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int,QByteArray> roleNames() const override;
    QVariantList entries() const;
    void setEntries(const QVariantList& entries);
    int totalCount() const { return int(m_selection.order().size()); }
    int shortlistedCount() const;
    int rejectedCount() const;
    int selectedCount() const { return int(m_selection.selectedKeys().size()); }
    int hiddenSelectedCount() const;
    QVariantList selectedEntries() const;
    QVariantList selectedImages() const;
    QVariantList actionEntries() const;
    QVariantList inspectorRows() const;
    QVariantMap focusedEntry() const;
    QString focusedKey() const;
    int focusedIndex() const;
    QString filter() const { return m_filter; }
    QString jobFilter() const { return m_jobFilter; }
    void setJobFilter(const QString& job);
    QString sort() const { return m_sort; }
    void setFilter(const QString& filter);
    void setSort(const QString& sort);
    bool selectionMode() const { return m_selectionMode; }
    void setSelectionMode(bool enabled);
    Q_INVOKABLE QString keyForEntry(const QVariantMap& entry) const;
    Q_INVOKABLE QVariantMap entryAt(int index) const;
    Q_INVOKABLE QString keyAt(int index) const { return index >= 0 && index < m_visible.size() ? m_visible[index] : QString{}; }
    Q_INVOKABLE int indexForKey(const QString& key) const;
    Q_INVOKABLE bool isSelected(const QVariantMap& entry) const;
    Q_INVOKABLE void selectAt(int index, int modifiers = 0);
    Q_INVOKABLE void focusAt(int index);
    Q_INVOKABLE void focusSource(const QString& source);
    Q_INVOKABLE void toggle(const QVariantMap& entry);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void toggleReview(const QString& state);
    Q_INVOKABLE void setReview(const QString& state);
    Q_INVOKABLE void setEntryReview(const QString& key, const QString& state);
    Q_INVOKABLE QString reviewState(const QString& key) const;
signals:
    void entriesChanged();
    void stateChanged();
    void projectionAboutToChange();
    void projectionChanged();
    void filterChanged();
    void sortChanged();
private:
    using State = dreamscapes::review::State;
    void publishState(bool metadataChanged = false);
    QStringList projection() const;
    std::vector<std::string> visibleKeys() const;
    std::vector<std::string> actionKeys() const;
    static QString stateName(State state);
    static State parseState(const QString& state);
    dreamscapes::review::Selection m_selection;
    QHash<QString,QVariantMap> m_entries;
    QHash<QString,int> m_ordinals;
    QStringList m_visible;
    QString m_filter = QStringLiteral("all"), m_sort = QStringLiteral("arrival");
    QString m_jobFilter;
    bool m_selectionMode = false;
};
