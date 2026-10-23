#include "App/Generation/FailureAgent.h"
#include "App/Generation/FailureEvidence.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QDir>

class FailureAgentTests final : public QObject {
    Q_OBJECT
    QJsonObject evidence(const QString& id, const QString& directory) {
        return {{"job", QJsonObject{{"id", id}, {"mediaType", "Video"}, {"width", 576}, {"height", 1024},
                    {"video", QJsonObject{{"frames", 72}, {"fps", 24}}}, {"advancedParameters", QJsonObject{{"cfg", 3}}}}},
            {"inference", QJsonObject{{"state", "loading"}, {"backend", "video"}, {"step", 0}, {"total", 0}}},
            {"rawError", "Generation stopped making progress at loading (0/0)."}, {"timedOut", true},
            {"workerLog", "last real worker log"}, {"sessionsDirectory", directory.isEmpty() ? QString() : directory + "/.sessions"},
            {"reportPath", directory.isEmpty() ? QString() : directory + "/." + id + ".json"}};
    }
private slots:
    void initTestCase() { qunsetenv("IIMULTIMODALAGENT_MODELS_DIR"); qunsetenv("IIMULTIMODALAGENT_MODEL"); }
    void realEngineReturnsDetailedEvidenceAndPersists() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/failure-agent-XXXXXX"); QVERIFY(root.isValid());
        FailureAgent agent(DREAMSCAPES_FAILURE_AGENT, 60000);
        QSignalSpy completed(&agent, &FailureAgent::explained);
        const auto input = evidence("failure-one", root.path());
        QElapsedTimer elapsed; elapsed.start();
        const auto baseline = agent.submit(input);
        QVERIFY(elapsed.elapsed() < 500); QCOMPARE(baseline["state"], "pending");
        QVERIFY(baseline["details"].toString().contains("total unknown"));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 65000);
        const auto report = completed.first()[1].toJsonObject();
        QCOMPARE(report["jobId"], "failure-one"); QCOMPARE(report["engine"], "iiMultimodalAgent");
        QVERIFY2(report["agentError"].toString().isEmpty(), qPrintable(report["agentError"].toString()));
        QCOMPARE(report["state"], "complete"); QVERIFY(!report["agentRunId"].toString().isEmpty());
        QCOMPARE(report["reasoner"], "native-evidence");
        QVERIFY(!report["modelUnavailableReason"].toString().isEmpty());
        QCOMPARE(report["rawError"], input["rawError"]);
        QVERIFY(report["details"].toString().contains("advancedParameters"));
        QVERIFY(report["details"].toString().contains("last real worker log"));
        QFile file(root.filePath(".failure-one.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto saved = QJsonDocument::fromJson(file.readAll()).object(); QCOMPARE(saved["agentRunId"], report["agentRunId"]);
        agent.close();
    }
    void failedAgentPreservesOriginalErrorAndNextRequestRuns() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/failure-fallback-XXXXXX"); QVERIFY(root.isValid());
        FailureAgent agent(root.filePath("missing-executable"), 1000); QSignalSpy completed(&agent, &FailureAgent::explained);
        agent.submit(evidence("one", root.path())); agent.submit(evidence("two", root.path()));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 2, 10000);
        for (const auto& result : completed) {
            const auto report = result[1].toJsonObject(); QCOMPARE(report["state"], "evidence-only");
            QVERIFY(!report["agentError"].toString().isEmpty()); QVERIFY(report["details"].toString().contains("ORIGINAL ERROR"));
            QFile saved(report["reportPath"].toString()); QVERIFY(saved.open(QIODevice::ReadOnly));
            QCOMPARE(QJsonDocument::fromJson(saved.readAll()).object()["state"], "evidence-only");
        }
        QCOMPARE(completed[0][0].toString(), "one"); QCOMPARE(completed[1][0].toString(), "two"); agent.close();
        const auto hidden = QDir(root.path()).entryList({"*.iisacc-preview"}, QDir::Files | QDir::Hidden);
        QVERIFY(hidden.isEmpty());
    }
    void closeDoesNotJoinWorkerAndDropsLateResults() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/failure-close-XXXXXX"); QVERIFY(root.isValid());
        FailureAgent agent(DREAMSCAPES_FAILURE_AGENT, 1000); QSignalSpy completed(&agent, &FailureAgent::explained);
        agent.submit(evidence("closing", root.path()));
        QElapsedTimer timer; timer.start(); agent.close(); QVERIFY(timer.elapsed() < 250);
        QTest::qWait(50); QCOMPARE(completed.size(), 0);
    }
    void runningAgentDeadlineAndCloseRemainNonBlocking() {
#ifdef Q_OS_UNIX
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/failure-deadline-XXXXXX"); QVERIFY(root.isValid());
        const auto executable = root.filePath("sleep-agent.py");
        QFile script(executable); QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/usr/bin/env python3\nimport time\ntime.sleep(10)\n"); script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        FailureAgent agent(executable, 500); QSignalSpy completed(&agent, &FailureAgent::explained);
        QElapsedTimer duration; duration.start(); agent.submit(evidence("timeout", root.path()));
        QTRY_COMPARE_WITH_TIMEOUT(completed.size(), 1, 5000);
        const auto report = completed.first()[1].toJsonObject();
        QCOMPARE(report["state"], "evidence-only"); QVERIFY(report["agentError"].toString().contains("deadline"));
        QVERIFY(duration.elapsed() < 5000); agent.close();
        FailureAgent running(executable, 15000); QSignalSpy started(&running, &FailureAgent::started);
        QSignalSpy late(&running, &FailureAgent::explained);
        running.submit(evidence("close-running", root.path()));
        QTRY_COMPARE_WITH_TIMEOUT(started.size(), 1, 5000);
        duration.restart(); running.close(); QVERIFY(duration.elapsed() < 250);
        QTest::qWait(100); QCOMPARE(late.size(), 0);
#endif
    }
    void queueIsBoundedAndReportsMissingEvidenceHonestly() {
        auto remote = evidence("remote", {});
        remote["inference"] = QJsonObject{{"state", "running"}, {"phase", "decoding"}};
        remote["traceReadError"] = "Captured trace is no longer available";
        const auto projection = dreamscapes::failureEvidenceReport(remote);
        QVERIFY(projection["details"].toString().contains("Stage: decoding"));
        QVERIFY(projection["details"].toString().contains("Captured trace is no longer available"));
        FailureAgent agent("missing", 1000);
        for (int index = 0; index < 8; ++index) QCOMPARE(agent.submit(evidence(QString::number(index), {}))["state"], "pending");
        const auto report = agent.submit(evidence("overflow", {})); QCOMPARE(report["state"], "evidence-only");
        QVERIFY(!report["rawError"].toString().isEmpty());
        agent.close();
    }
};
QTEST_GUILESS_MAIN(FailureAgentTests)
#include "tst_failureagent.moc"
