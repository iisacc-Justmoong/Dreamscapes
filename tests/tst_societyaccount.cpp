#include "../src/App/Generation/SocietyAccount.h"
#include "fixtures/LicenseTokenFixture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>

class SocietyUrlRecorder final : public QObject {
    Q_OBJECT
public:
    QUrl seen;
public slots:
    void open(const QUrl &url) { seen = url; }
};

class SocietyAccountTests final : public QObject {
    Q_OBJECT
private slots:
    void opensSocietyThroughThePlatformHandler() {
        SocietyUrlRecorder recorder;
        QDesktopServices::setUrlHandler(QStringLiteral("society"), &recorder, "open");
        const auto reset = qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("society")); });
        SocietyAccount account(false);
        QVERIFY(account.openSociety());
        QCOMPARE(recorder.seen, QUrl(QStringLiteral("society://")));
    }
    void nativeDeviceMatchesSocietyDevice() {
        const auto machine = QSysInfo::machineUniqueId(); QVERIFY(!machine.isEmpty());
        const auto expected = QCryptographicHash::hash(QByteArray("iisacc-device-v1:") + machine, QCryptographicHash::Sha256).toHex();
        QCOMPARE(QByteArray::fromStdString(iisacc::accounts::native::detected_device("com.iisacc.dreamscapes", "1", "").id), expected);
    }
    void unavailableSocietyNeverGrantsAccess() {
        QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_RESOURCES "/unavailable-XXXXXX"));
        QVERIFY(directory.isValid());
        using namespace iisacc::accounts::native;
        SocietySessionOptions options{.product_id="dreamscapes",
            .device={std::string(64,'a'),"pc","Fixture","macos","1","com.iisacc.dreamscapes","1"},
            .socket_path=directory.filePath("missing.sock").toStdString(), .timeout=std::chrono::seconds(1)};
        SocietyAccount account(options);
        QVERIFY(!account.checking());
        QVERIFY(account.code().isEmpty());
        QVERIFY(!account.licensed());
        QTRY_COMPARE_WITH_TIMEOUT(account.code(), QStringLiteral("society_unavailable"), 5000);
        QVERIFY(!account.signedIn()); QVERIFY(!account.licensed());
    }
    void signedLocalDecisionAndFailure() {
        QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_RESOURCES "/account-XXXXXX"));
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("broker.sock");
        QLocalServer server; server.setSocketOptions(QLocalServer::UserAccessOption); QVERIFY(server.listen(path));
        bool owned = false, tampered = false; int requests = 0;
        connect(&server, &QLocalServer::newConnection, this, [&] {
            while (auto *socket = server.nextPendingConnection()) {
                connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QLocalSocket::readyRead, socket, [&, socket] {
                    auto data = socket->property("data").toByteArray() + socket->readAll(); socket->setProperty("data", data);
                    if (data.size() < 4) return;
                    const auto *p = reinterpret_cast<const unsigned char *>(data.constData());
                    const quint32 n = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
                    if (data.size() < 4 + int(n)) return;
                    auto input = QJsonDocument::fromJson(data.mid(4, n)).object(); ++requests;
                    QCOMPARE(input.value("productId").toString(), QStringLiteral("dreamscapes"));
                    QCOMPARE(input.value("appId").toString(), QStringLiteral("com.iisacc.dreamscapes"));
                    QVERIFY(!input.contains("accessToken")); QVERIFY(!input.contains("subject"));
                    const auto now = QDateTime::currentSecsSinceEpoch();
                    QJsonObject payload{{"v",1},{"iss","https://iisacc.com"},{"aud","society"},{"sub","fixture-owner"},
                        {"deviceId",QString(64,'a')},{"issuedAt",now},{"expiresAt",now+2},
                        {"licenses",QJsonArray{QJsonObject{{"productId","dreamscapes"},{"licensed",owned},{"expiresAt",QJsonValue::Null}}}}};
                    auto token = dreamscapes_license_fixture::sign(QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString());
                    if (tampered) token[token.find('.')+5] ^= 1;
                    auto body = QJsonDocument(QJsonObject{{"requestId",input.value("requestId")},{"allowed",owned},
                        {"licenseToken",QString::fromStdString(token)}}).toJson(QJsonDocument::Compact);
                    const auto size = quint32(body.size()); QByteArray header;
                    header.append(char(size>>24)); header.append(char(size>>16)); header.append(char(size>>8)); header.append(char(size));
                    socket->write(header+body); socket->disconnectFromServer();
                });
            }
        });
        using namespace iisacc::accounts::native;
        SocietySessionOptions options{.product_id="dreamscapes",
            .device={std::string(64,'a'),"pc","Fixture","macos","1","com.iisacc.dreamscapes","1"},
            .socket_path=path.toStdString(), .timeout=std::chrono::seconds(2),
            .keys={{"dreamscapes-fixture",dreamscapes_license_fixture::publicKey()}}};
        SocietyAccount account(options);
        QTRY_COMPARE_WITH_TIMEOUT(account.code(), QStringLiteral("license_not_owned"), 10000);
        QVERIFY(account.signedIn()); QVERIFY(!account.licensed());
        owned = true; tampered = true; account.refresh();
        QTRY_COMPARE_WITH_TIMEOUT(account.code(), QStringLiteral("invalid_society_response"), 10000);
        QVERIFY(!account.signedIn()); QVERIFY(!account.licensed());
        tampered = false; account.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(account.licensed(), 10000);
        QCOMPARE(account.subject(), QStringLiteral("fixture-owner"));
        server.close();
        QTest::qWait(2500);
        account.refresh();
        QVERIFY(account.licensed());QCOMPARE(account.code(),QStringLiteral("licensed"));
        QVERIFY(requests >= 3);
    }
};
QTEST_GUILESS_MAIN(SocietyAccountTests)
#include "tst_societyaccount.moc"
