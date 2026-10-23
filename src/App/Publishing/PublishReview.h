#pragma once
#include "PublishDraft.h"
#include "SocialPublishingController.h"
#include <iiSocialPublish/Review.h>
#include <QFutureWatcher>

// GUI boundary only. Destination grouping and execution live in iiSocialPublish.
class PublishReview : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(PublishDraft *draft READ draft WRITE setDraft NOTIFY changed)
    Q_PROPERTY(QString platform READ platform WRITE setPlatform NOTIFY changed)
    Q_PROPERTY(QVariantMap view READ view NOTIFY changed)
    Q_PROPERTY(bool exporting READ exporting NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit PublishReview(QObject *parent=nullptr);
    ~PublishReview() override;
    PublishDraft *draft() const { return m_draft; }
    void setDraft(PublishDraft *);
    QString platform() const { return m_platform; }
    void setPlatform(const QString &);
    QVariantMap view() const { return m_view; }
    bool exporting() const { return m_exporting; }
    QString error() const { return m_error; }
    Q_INVOKABLE bool edit(const QString &key,const QVariant &value);
    Q_INVOKABLE bool reset(const QString &key = {});
    Q_INVOKABLE bool exportFrames(const QUrl &folder);
    Q_INVOKABLE QString submit(SocialPublishingController *,const QString &accountId);
signals:
    void changed();
private:
    void rebuild();
    QPointer<PublishDraft> m_draft, m_exportDraft;
    QPointer<EditorCanvas> m_exportCanvas;
    QString m_platform="x", m_exportPlatform, m_error;
    qulonglong m_exportRevision=0;
    QVariantMap m_view;
    iiSocialPublish::PublicationPlan m_plan;
    std::string m_intent;
    QFutureWatcher<QVariantMap> m_export;
    bool m_exporting=false;
};
