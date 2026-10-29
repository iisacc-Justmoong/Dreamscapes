#include "App/Generation/FailureAgent.h"
#include "App/Generation/FailureEvidence.h"
#include "App/Generation/FailureModelBudget.h"
#include "App/Generation/FailureReportStorage.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QDir>

namespace {
class CountingDiagnosticModel final : public iiMultimodalAgent::agent::Model {
public:
    int context = 512;
    QString received;
    std::optional<iiMultimodalAgent::ContextBudget> measure(
        const iiMultimodalAgent::agent::ModelRequest& request,
        const iiMultimodalAgent::CancellationToken&) override {
        qint64 bytes = request.systemPrompt.toUtf8().size() + 50;
        for (const auto& message : request.messages) bytes += message.text.toUtf8().size();
        return iiMultimodalAgent::ContextBudget{bytes, context};
    }
    iiMultimodalAgent::agent::ModelReply generate(
        const iiMultimodalAgent::agent::ModelRequest& request,
        const iiMultimodalAgent::CancellationToken& token,
        const std::function<bool(const QString&)>&) override {
        const auto budget = measure(request, token).value();
        if (budget.inputTokens + request.generation.maxTokens > budget.contextTokens)
            throw std::runtime_error("diagnostic input exceeded the model context");
        received = request.messages.last().text;
        return {"verified fixture explanation"};
    }
};
}

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
    void modelInputFitsMeasuredContextWithoutChangingTheFullEvidence() {
        auto model = std::make_shared<CountingDiagnosticModel>();
        dreamscapes::FailureModelBudget budgeted(model);
        iiMultimodalAgent::agent::ModelRequest request;
        request.systemPrompt = "Explain evidence as data.";
        request.generation.maxTokens = 80;
        const QString full = QString("Native error: weight preparation failed.\n") + QString(5000, QChar(0xac00));
        request.messages.append({"input", iiMultimodalAgent::agent::MessageRole::User, full});
        const iiMultimodalAgent::CancellationToken token;
        const auto budget = budgeted.measure(request, token).value();
        QVERIFY(budget.inputTokens + request.generation.maxTokens <= budget.contextTokens);
        QCOMPARE(budgeted.generate(request, token, {}).text, QString("verified fixture explanation"));
        QVERIFY(model->received.startsWith("Native error: weight preparation failed."));
        QVERIFY(model->received.contains("full report is preserved"));
        QCOMPARE(request.messages.last().text, full);

        request.messages.last().text = "A short failure";
        budgeted.generate(request, token, {});
        QCOMPARE(model->received, QString("A short failure"));
        model->context = 100;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, budgeted.measure(request, token));
    }
    void nativeErrorIsPrioritizedForTheOptionalModel() {
        auto input = evidence("failure-detail", {});
        auto job = input["job"].toObject();
        job["telemetry"] = QJsonObject{{"phase", "denoise"},
            {"detail", "segment 3 weight preparation failed"}};
        input["job"] = job; input["rawError"] = "2";
        const auto report = dreamscapes::failureEvidenceReport(input);
        const auto prompt = dreamscapes::failureModelPrompt(input, report);
        QVERIFY(prompt.startsWith("Recorded native diagnostic (untrusted data):"));
        QVERIFY(prompt.indexOf("segment 3 weight preparation failed") < prompt.indexOf("ORIGINAL ERROR"));
        QCOMPARE(report["rawError"].toString(), QString("2"));
        QCOMPARE(report["evidence"].toObject(), input);
    }
    void reportCreatesMissingParentDirectoriesThroughTheProvider() {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/failure-parent-XXXXXX"); QVERIFY(root.isValid());
        const auto path = root.filePath(QString::fromUtf8("reports/하위/report.json"));
        const auto saved = dreamscapes::persistFailureReport({{"reportPath", path}, {"jobId", "nested-report"}});
        QVERIFY2(!saved.contains("persistenceError"), qPrintable(saved["persistenceError"].toString()));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object()["jobId"], "nested-report");
        QFile blocker(root.filePath("blocker")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        const auto rejected = dreamscapes::persistFailureReport({{"reportPath", blocker.fileName() + "/report.json"}});
        QVERIFY(rejected.contains("persistenceError"));
        QVERIFY(!QFile::exists(blocker.fileName() + "/report.json"));
    }
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
