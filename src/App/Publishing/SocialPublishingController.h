#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>
#include <iiSocialPublish/Review.h>
#include <memory>

// QObject is only the product's GUI boundary. HTTP, validation and provider
// publishing protocols belong to the Qt-free iiSocialPublish SDK.
class SocialPublishingController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList providers READ providers CONSTANT)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY changed)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(QString errorString READ errorString NOTIFY changed)
public:
    explicit SocialPublishingController(QObject* parent = nullptr);
    explicit SocialPublishingController(std::shared_ptr<iiSocialPublish::Transport>, QObject* parent = nullptr);
    ~SocialPublishingController() override;

    QVariantList providers() const;
    QVariantList accounts() const;
    QVariantList jobs() const;
    int pendingCount() const;
    QString errorString() const;

    // Trusted native account/session integration supplies a user OAuth grant.
    // Credentials are deliberately not invokable/readable from QML or snapshots.
    void configureAccount(const QString& accountId, iiSocialPublish::Platform, iiSocialPublish::Account);
    void disconnectAccount(const QString& accountId);

    Q_INVOKABLE QString submit(const QString& accountId, const QVariantMap& post);
    QString submitPlan(const QString& accountId, iiSocialPublish::PublicationPlan, const QString& destinationId = {});
    Q_INVOKABLE bool cancel(const QString& jobId);
    Q_INVOKABLE void clearFinished();
signals:
    void changed();
    void jobFinished(const QString& jobId, const QVariantMap& result);
private:
    struct State;
    std::shared_ptr<State> d;
    bool startWorker();
    void retireAccount(const QString&);
    QString enqueue(const QString&, iiSocialPublish::Post, std::optional<iiSocialPublish::PublicationPlan>, const QByteArray&, const QString&);
    void complete(const QString&, iiSocialPublish::Outcome, std::vector<iiSocialPublish::Outcome> completed = {}, std::size_t next = 0);
};
