#pragma once
#include <iiAccountManager/SocietySession.h>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <memory>

// Society owns the online account. This model only consumes its local decision.
class SocietyAccount final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(bool licensed READ licensed NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(QString subject READ subject NOTIFY changed)
    Q_PROPERTY(QString code READ code NOTIFY changed)
    Q_PROPERTY(bool hasAvatar READ hasAvatar CONSTANT)
    Q_PROPERTY(QUrl avatarUrl READ avatarUrl CONSTANT)
public:
    explicit SocietyAccount(bool enforce, const QString &socketPath = {}, QObject *parent = nullptr);
    explicit SocietyAccount(iisacc::accounts::native::SocietySessionOptions options, QObject *parent = nullptr);
    ~SocietyAccount() override;
    bool signedIn() const;
    bool licensed() const;
    bool checking() const { return enforced_ && snapshot_.checking; }
    QString subject() const;
    QString code() const;
    bool hasAvatar() const { return false; }
    QUrl avatarUrl() const { return {}; }
    QString deviceId() const { return QString::fromStdString(device_.id); }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool openSociety();
    void invalidate();
signals:
    void changed();
private:
    void initialize(iisacc::accounts::native::SocietySessionOptions);
    bool enforced_ = true;
    bool launchGranted_ = false;
    QString error_;
    iisacc::accounts::native::AppDevice device_;
    ii::runtime::EventLoop loop_;
    std::unique_ptr<iisacc::accounts::native::SocietySession> session_;
    iisacc::accounts::native::SocietySessionSnapshot snapshot_;
    QTimer pump_;
};
