#include <mcp/LocalApplications.h>
#include <mcp/HttpClient.h>
#include <SocietyDrive.h>
#include <QtTest/QtTest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QSet>

namespace m = iiLocalLLM::mcp;
namespace {
struct AppProcess : QProcess {
    QByteArray output;
    QString logPath;
    ~AppProcess() override {
        if (state() != NotRunning) { terminate(); if (!waitForFinished(3000)) { kill(); waitForFinished(3000); } }
        output += readAll();
        QFile log(logPath); if (log.open(QIODevice::WriteOnly)) log.write(output);
    }
    void startApp(const QString& base, bool enabled) {
        auto env = QProcessEnvironment::systemEnvironment();
        for (const auto* name : {"DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "DYLD_FALLBACK_LIBRARY_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"}) env.remove(name);
        env.insert("SOCIETY_HELPER_DIRECTORY", base + "/helper");
        env.insert("SOCIETY_STORAGE_SETTINGS_PATH", base + "/storage.json");
        env.insert("SOCIETY_CONTAINER_PATH", base + "/container");
        env.insert("IILOCALLLM_APP_ENDPOINTS", base + "/apps");
        env.insert("IILOCALLLM_DISABLE_APP_MCP", enabled ? "0" : "1");
        env.insert("IILD_GENERATOR_EXECUTABLE", MCP_GENERATOR_FIXTURE);
        env.insert("DREAMSCAPES_TEMP_DIRECTORY", base + "/tmp");
        env.insert("QT_QPA_PLATFORM", "offscreen"); env.insert("QT_QUICK_BACKEND", "software");
        env.insert("QML_DISABLE_DISK_CACHE", "1");
        setProcessEnvironment(env); setProcessChannelMode(MergedChannels); setWorkingDirectory(base);
        logPath = base + "/app.log";
        start(MCP_APP_EXECUTABLE, {"--society-container", base + "/container"});
    }
    bool waitForRoot() {
        QElapsedTimer timer; timer.start();
        while (state() != NotRunning && timer.elapsed() < 25000) {
            waitForReadyRead(100); output += readAll();
            if (output.contains("bootstrap.entry.root-loaded")) return true;
        }
        return false;
    }
};
m::HttpOptions clientOptions(const m::LocalApplicationEndpoint& endpoint) {
    m::HttpOptions options; options.endpoint = endpoint.endpoint;
    options.bearerToken = [token = endpoint.bearerToken] { return token; };
    options.initializeTimeoutMs = 3000; options.requestTimeoutMs = 5000;
    return options;
}
QJsonObject call(m::Client& client, const QString& name, QJsonObject arguments = {}) {
    return client.request("tools/call", {{"name", name}, {"arguments", arguments}});
}
QJsonObject data(m::Client& client, const QString& name, QJsonObject arguments = {}) {
    const auto response = call(client, name, arguments);
    if (response["isError"].toBool()) throw std::runtime_error(QJsonDocument(response).toJson().constData());
    return response["structuredContent"].toObject();
}
QJsonObject findJob(m::Client& client, const QString& id) {
    for (const auto& job : data(client, "jobs")["items"].toArray())
        if (job.toObject()["id"] == id) return job.toObject();
    return {};
}
bool write(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
}
class McpTests : public QObject {
    Q_OBJECT
private slots:
    void actualAppGenerationQueueAndCancellation() {
        QTemporaryDir base(MCP_TEST_DIRECTORY "/mcp-dreamscapes-XXXXXX"); QVERIFY(base.isValid());
        base.setAutoRemove(false);
        QVERIFY(QDir().mkpath(base.filePath("container"))); QVERIFY(QDir().mkpath(base.filePath("tmp")));
        const auto drive = iiSocietyContainer::SocietyDrive::create(base.filePath("container")); QVERIFY(drive);
        QVERIFY(write(base.filePath("container/Models/first.safetensors"), "MCP process-protocol fixture model 1"));
        QVERIFY(write(base.filePath("container/Models/second.safetensors"), "MCP process-protocol fixture model 2"));
        AppProcess process; process.startApp(base.path(), true); QVERIFY(process.waitForStarted());
        QVERIFY2(process.waitForRoot(), process.output.constData());
        const auto discovered = m::discoverLocalApplications(base.filePath("apps"));
        QCOMPARE(discovered.applications.size(), 1);
        const auto endpoint = discovered.applications.first();
        QCOMPARE(endpoint.application.id, QString("com.iisacc.dreamscapes")); QCOMPARE(endpoint.processId, process.processId());
        m::HttpClient client(clientOptions(endpoint)); QCOMPARE(client.serverInfo()["name"].toString(), QString("Dreamscapes"));
        QCOMPARE(client.listTools().size(), 7);
        const auto state = data(client, "status"); QVERIFY(state["connected"].toBool()); QVERIFY(state["runtime_available"].toBool());
        QCOMPARE(state["container_path"].toString(), drive->rootPath()); QCOMPARE(state["job_count"].toInt(), 0);
        const auto firstPage = data(client, "models", {{"limit", 1}});
        QCOMPARE(firstPage["total"].toInt(), 2); QVERIFY(firstPage["has_more"].toBool());
        const auto model = data(client, "models", {{"offset", 1}, {"limit", 1}})["items"].toArray().first().toObject()["id"].toString();
        QVERIFY(!model.isEmpty()); QCOMPARE(data(client, "select_model", {{"id", model}})["selected_model"].toString(), model);
        QVERIFY(call(client, "select_model", {{"id", "missing"}})["isError"].toBool());
        QCOMPARE(data(client, "status")["selected_model"].toString(), model);
        QVERIFY(!call(client, "refresh_models")["isError"].toBool());
        for (const auto& invalid : QList<QJsonObject>{{{"prompt", "image"}, {"count", 0}},
                 {{"prompt", "image"}, {"aspect_ratio", "100:1"}}, {{"prompt", " "}}, {{"prompt", "image"}, {"command", "ignored"}}})
            QVERIFY(call(client, "generate", invalid)["isError"].toBool());
        QCOMPARE(data(client, "status")["job_count"].toInt(), 0);

        // This exercises the real app/storage/worker protocol using a deterministic PNG fixture, not native model inference.
        const auto first = data(client, "generate", {{"prompt", "fixture generation"}, {"aspect_ratio", "4:3"}});
        const auto firstId = first["first_job_id"].toString(); QVERIFY(!firstId.isEmpty());
        QCOMPARE(first["job_ids"].toArray(), QJsonArray{firstId});
        QTRY_COMPARE_WITH_TIMEOUT(findJob(client, firstId)["state"].toString(), QString("completed"), 20000);
        const auto completed = findJob(client, firstId);
        const auto imagePath = base.filePath("container/" + completed["image"].toString());
        QVERIFY(completed["image"].toString().startsWith("Generation History/"));
        const QImage image(imagePath); QVERIFY(!image.isNull());
        QCOMPARE(image.size(), QSize(completed["width"].toInt(), completed["height"].toInt()));
        QCOMPARE(completed["aspectRatio"].toString(), QString("4:3"));
        // The controller aligns dimensions to 8 pixels, so 4:3 is approximate.
        QVERIFY(qAbs(image.width() - image.height() * 4.0 / 3.0) < 8);
        QCOMPARE(data(client, "status")["latest_image"].toString(), QUrl::fromLocalFile(imagePath).toString());

        const auto second = data(client, "generate", {{"prompt", "hold"}, {"count", 2}});
        const auto ids = second["job_ids"].toArray(); QCOMPARE(ids.size(), 2);
        QCOMPARE(ids.first().toString(), second["first_job_id"].toString());
        QVERIFY(ids.first() != ids.last()); QVERIFY(!ids.contains(firstId));
        const auto active = ids.first().toString(), queued = ids.last().toString();
        QTRY_COMPARE_WITH_TIMEOUT(findJob(client, active)["state"].toString(), QString("running"), 10000);
        QCOMPARE(findJob(client, queued)["state"].toString(), QString("queued"));
        QVERIFY(data(client, "cancel", {{"id", queued}})["cancelled"].toBool());
        QVERIFY(data(client, "cancel", {{"id", active}})["cancelled"].toBool());
        QTRY_COMPARE_WITH_TIMEOUT(findJob(client, active)["state"].toString(), QString("cancelled"), 10000);
        QCOMPARE(findJob(client, queued)["state"].toString(), QString("cancelled"));
        QCOMPARE(findJob(client, firstId)["state"].toString(), QString("completed"));
        const auto page = data(client, "jobs", {{"offset", 1}, {"limit", 1}});
        QCOMPARE(page["items"].toArray().size(), 1); QCOMPARE(page["total"].toInt(), 3); QVERIFY(page["has_more"].toBool());
        QVERIFY(call(client, "cancel", {{"id", firstId}})["isError"].toBool());
        auto wrong = clientOptions(endpoint); wrong.bearerToken = [] { return QByteArray(43, 'x'); };
        QVERIFY_THROWS_EXCEPTION(iiLocalLLM::Error, m::HttpClient denied(wrong));
        client.close(); process.terminate(); QVERIFY(process.waitForFinished(5000));
        QVERIFY(m::discoverLocalApplications(base.filePath("apps")).applications.isEmpty());
    }
    void disabledAppDoesNotAdvertise() {
        QTemporaryDir base(MCP_TEST_DIRECTORY "/mcp-disabled-XXXXXX"); QVERIFY(base.isValid());
        QVERIFY(QDir().mkpath(base.filePath("container"))); QVERIFY(QDir().mkpath(base.filePath("tmp")));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(base.filePath("container")));
        AppProcess process; process.startApp(base.path(), false); QVERIFY(process.waitForStarted());
        QVERIFY2(process.waitForRoot(), process.output.constData());
        QVERIFY(m::discoverLocalApplications(base.filePath("apps")).applications.isEmpty());
        QVERIFY(!QFileInfo::exists(base.filePath("apps")));
    }
};
QTEST_GUILESS_MAIN(McpTests)
#include "tst_mcp.moc"
