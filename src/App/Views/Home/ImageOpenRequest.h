#pragma once
#include <Container.h>
#include <iiSocietyHelper/FileDownload.h>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <vector>

// One accepted editor-open intent. Selection is metadata; only this explicit
// command may hydrate originals. The SDK remains the file/transfer owner.
class ImageOpenRequest : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(iiSocietyContainer::Container *container READ container WRITE setContainer NOTIFY bindingsChanged)
    Q_PROPERTY(iiSocietyHelper::FileDownload *download READ download WRITE setDownload NOTIFY bindingsChanged)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(int completed READ completed NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY changed)
public:
    explicit ImageOpenRequest(QObject *parent = nullptr);
    ~ImageOpenRequest() override;
    iiSocietyContainer::Container *container() const { return m_container; }
    iiSocietyHelper::FileDownload *download() const { return m_download; }
    void setContainer(iiSocietyContainer::Container *container);
    void setDownload(iiSocietyHelper::FileDownload *download);
    bool active() const { return m_active; }
    int completed() const { return m_completed; }
    int total() const { return m_total; }
    QString error() const { return m_error; }
    bool canRetry() const { return !m_active&&!m_retryEntries.isEmpty()&&sameStorage(); }
    Q_INVOKABLE bool start(const QVariantList &entries);
    Q_INVOKABLE bool retry();
    Q_INVOKABLE void cancel();
signals:
    void bindingsChanged();
    void changed();
    void ready(QVariantList entries, QVariantList sources);
    void failed(QString message);
    void cancelled();
    void rejected(QString message);
private:
    struct Entry {
        QVariantMap selected, prepared;
        QString key, version;
        QUrl source;
    };
    static QUrl originalSource(const QVariantMap &entry);
    bool sameStorage() const;
    bool ownsDownload() const;
    void stop(bool failure, const QString &message = {});
    bool reject(const QString &message);
    void schedule();
    void advance();
    void prepare(Entry &entry, const QVariantMap &alias);
    void downloadChanged();
    void downloaded(const QVariantMap &file);
    QPointer<iiSocietyContainer::Container> m_container;
    QPointer<iiSocietyHelper::FileDownload> m_download;
    std::vector<Entry> m_entries;
    QVariantList m_retryEntries;
    QString m_id, m_root, m_pending, m_error;
    quint64 m_epoch = 0;
    int m_completed = 0, m_total = 0;
    bool m_active = false;
};
