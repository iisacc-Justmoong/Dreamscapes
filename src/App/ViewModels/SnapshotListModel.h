#pragma once
#include "Native/ProjectionRows.h"
#include <QAbstractListModel>
#include <QVariant>
namespace dreamscapes {
class SnapshotListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries WRITE setEntries NOTIFY entriesChanged)
    Q_PROPERTY(QVariantMap fieldMapping READ fieldMapping WRITE setFieldMapping NOTIFY fieldMappingChanged)
    Q_PROPERTY(QString keyRole READ keyRole WRITE setKeyRole NOTIFY fieldMappingChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
  public:
    explicit SnapshotListModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : rows_.size();
    }
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override {
        return roles_;
    }
    QVariantList entries() const {
        return entries_;
    }
    void setEntries(const QVariantList &);
    QVariantMap fieldMapping() const {
        return fields_;
    }
    void setFieldMapping(const QVariantMap &);
    QString keyRole() const {
        return key_;
    }
    void setKeyRole(QString);
    Q_INVOKABLE QVariantMap get(int index) const;
  signals:
    void entriesChanged();
    void countChanged();
    void fieldMappingChanged();
    void rejected(QString reason);

  private:
    void synchronize();
    QVariantList entries_, rows_;
    QVariantMap fields_;
    QString key_ = QStringLiteral("id");
    QHash<int, QByteArray> roles_;
    vm::RowProjector projector_;
};
} // namespace dreamscapes
