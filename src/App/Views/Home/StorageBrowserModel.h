#pragma once
#include "../../ViewModels/Native/StorageProjection.h"
#include <Container.h>
#include <QPointer>
class StorageBrowserModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(iiSocietyContainer::Container *container READ container WRITE setContainer NOTIFY containerChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString section READ section WRITE setSection NOTIFY sectionChanged)
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(QString selectedKey READ selectedKey WRITE setSelectedKey NOTIFY selectedKeyChanged)
    Q_PROPERTY(QVariantList sectionFiles READ sectionFiles NOTIFY sectionFilesChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QVariantMap selectedEntry READ selectedEntry NOTIFY selectedKeyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString notice READ notice WRITE setNotice NOTIFY noticeChanged)
  public:
    explicit StorageBrowserModel(QObject *parent = nullptr) : QObject(parent) {}
    ~StorageBrowserModel() override;
    iiSocietyContainer::Container *container() const {
        return container_;
    }
    void setContainer(iiSocietyContainer::Container *);
    bool active() const {
        return active_;
    }
    void setActive(bool);
    QString section() const {
        return QString::fromStdString(request_.section);
    }
    void setSection(const QString &);
    QString directory() const {
        return QString::fromStdString(request_.directory);
    }
    void setDirectory(const QString &);
    QString query() const {
        return QString::fromStdString(request_.query);
    }
    void setQuery(const QString &);
    QString sortOrder() const {
        return QString::fromStdString(request_.sort);
    }
    void setSortOrder(const QString &);
    QString selectedKey() const {
        return QString::fromStdString(request_.selectedKey);
    }
    void setSelectedKey(const QString &);
    QVariantList sectionFiles() const {
        return files_;
    }
    QVariantList entries() const {
        return entries_;
    }
    QVariantMap selectedEntry() const;
    bool busy() const {
        return inFlight_;
    }
    QString notice() const {
        return notice_;
    }
    void setNotice(const QString &value) {
        if (notice_ != value) {
            notice_ = value;
            emit noticeChanged();
        }
    }
    Q_INVOKABLE void updateSnapshot();
    Q_INVOKABLE void resetNavigation();
    Q_INVOKABLE void up();
    Q_INVOKABLE void activate(const QVariantMap &);
    Q_INVOKABLE QString downloadStatus(const QVariantMap &) const;
  signals:
    void containerChanged();
    void activeChanged();
    void sectionChanged();
    void directoryChanged();
    void queryChanged();
    void sortOrderChanged();
    void selectedKeyChanged();
    void sectionFilesChanged();
    void entriesChanged();
    void busyChanged();
    void noticeChanged();
    void navigationResetRequested();
    void positionResetRequested();
    void fileRequested(QVariantMap file);

  private:
    void schedule();
    void dispatch();
    void receive(quint64 epoch, ii::runtime::QueryReply<dreamscapes::vm::StoragePresentation>);
    QPointer<iiSocietyContainer::Container> container_;
    QString identity_, notice_;
    std::optional<quint64> mirrorRevision_;
    bool active_ = false, inFlight_ = false, dirty_ = false;
    quint64 epoch_ = 0;
    dreamscapes::vm::StorageSnapshot request_;
    dreamscapes::vm::StorageProjector projector_;
    QVariantList files_, entries_;
    QVariantMap originalAliases_;
};
