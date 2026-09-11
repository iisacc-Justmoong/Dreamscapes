#include "GenerationController.h"
#include <iiSocietyHelper.h>
#include <iiSocietySync.h>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <filesystem>
#include <thread>
#include <stdexcept>

namespace {
iiLocalDiffusion::NativeGenerationResult imageResult(const iiLocalDiffusion::NativeGenerationRequest &request)
{
    return {std::vector<std::uint8_t>(request.width * request.height * 3, 127), request.width, request.height};
}
void waitForCancellation(const std::atomic_bool &cancelled)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!cancelled && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
}

class LocalSocietyTests : public QObject {
    Q_OBJECT
private slots:
    void nativeProgressAndScreenActivityFollowTheWholeImage() {
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/native-progress-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile model(storage.filePath("Models/model.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("fixture"); model.close();
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.imageExtent = 64;
        runtime.temporaryDirectory = DREAMSCAPES_TEST_DIRECTORY;
        runtime.nativeQ8CacheDirectory = QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/q8-cache");
        QList<bool> screen;
        runtime.screenActivity = [&](bool active) { screen.append(active); };
        runtime.nativeGenerate = [](const auto &request, const auto &, const auto &progress) {
            using Stage = iiLocalDiffusion::NativeGenerationStage;
            if (request.q8CacheDirectory.string() != DREAMSCAPES_TEST_DIRECTORY "/q8-cache")
                throw std::runtime_error("Missing Q8 cache request");
            progress({Stage::Preparing, 1, 220});
            progress({Stage::Loading, 220, 220});
            progress({Stage::Encoding});
            progress({Stage::Denoising, 1, 10});
            progress({Stage::Loading, 10, 10}); // Even equal totals are not denoising.
            progress({Stage::Denoising, 10, 10});
            progress({Stage::Decoding, 4, 4});
            auto result = imageResult(request);
            result.modelCacheHit = true;
            result.memoryBudgetBytes = 4352ull * 1024 * 1024;
            result.threads = 6;
            result.modelLoadMilliseconds = 0.5;
            result.generationMilliseconds = 100;
            result.q8CacheUsed = true;
            result.diskCacheHit = true;
            result.modelBytes = 4180204992;
            result.preparationMilliseconds = 0.2;
            return result;
        };
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(storage.path())); app.setForeground(true);
        QList<int> steps;
        QStringList phases;
        connect(&app, &GenerationController::previewChanged, &app, [&] {
            if (app.previewStep()) {
                QCOMPARE(app.previewTotalSteps(), 10);
                steps.append(app.previewStep());
            }
        });
        connect(&app, &GenerationController::inferenceStatusChanged, &app, [&] {
            phases.append(app.inferenceStatus().value("state").toString());
        });
        QVERIFY(!app.enqueue("progress contract").isEmpty());
        QTRY_VERIFY(!app.latestImage().isEmpty());
        QCOMPARE(steps, QList<int>({1, 10}));
        QVERIFY(phases.contains("loading") && phases.contains("encoding") && phases.contains("decoding"));
        QCOMPARE(screen, QList<bool>({true, false}));
        QVERIFY(!app.busy() && !app.keepsScreenAwake());
        const auto generation = app.jobs().front().toMap().value("generation").toMap();
        const auto performance = generation.value("performance").toMap();
        QCOMPARE(performance.value("modelCacheHit").toBool(), true);
        QCOMPARE(performance.value("threads").toInt(), 6);
        QCOMPARE(performance.value("memoryBudgetBytes").toDouble(), double(4352ull * 1024 * 1024));
        QCOMPARE(performance.value("modelLoadMilliseconds").toDouble(), 0.5);
        QCOMPARE(performance.value("generationMilliseconds").toDouble(), 100.0);
        QCOMPARE(performance.value("q8CacheUsed").toBool(), true);
        QCOMPARE(performance.value("diskCacheHit").toBool(), true);
        QCOMPARE(performance.value("modelBytes").toDouble(), 4180204992.0);
        QCOMPARE(performance.value("preparationMilliseconds").toDouble(), 0.2);
        QVERIFY(phases.contains("preparing-model"));
    }
    void nativeTermination_data() {
        QTest::addColumn<QString>("reason");
        QTest::addColumn<bool>("preparing");
        for (const auto *reason : {"cancel", "background", "timeout", "exception", "unknown-exception"}) {
            QTest::newRow(reason) << QString(reason) << false;
            QTest::newRow(qPrintable(QString(reason) + "-q8-preparation")) << QString(reason) << true;
        }
    }
    void nativeTermination() {
        QFETCH(QString, reason);
        QFETCH(bool, preparing);
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/native-termination-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile model(storage.filePath("Models/model.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("fixture"); model.close();
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.imageExtent = 64;
        runtime.nativeTimeoutMilliseconds = reason == "timeout" ? 30 : 3000;
        runtime.temporaryDirectory = DREAMSCAPES_TEST_DIRECTORY;
        std::atomic_int calls{0};
        runtime.nativeGenerate = [&](const auto &request, const auto &cancelled, const auto &progress) {
            if (++calls > 1) return imageResult(request);
            if (reason == "exception") throw std::runtime_error("fixture engine exception");
            if (reason == "unknown-exception") throw 42;
            progress({preparing ? iiLocalDiffusion::NativeGenerationStage::Preparing
                                : iiLocalDiffusion::NativeGenerationStage::Loading, 7, 220});
            waitForCancellation(cancelled);
            iiLocalDiffusion::NativeGenerationResult result;
            result.cancelled = cancelled;
            return result;
        };
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(storage.path())); app.setForeground(true);
        const auto id = app.enqueue("terminate during loading");
        QVERIFY(!id.isEmpty());
        if (reason == "cancel" || reason == "background") {
            QTRY_COMPARE(app.inferenceStatus().value("total").toInt(), 220);
            if (reason == "cancel") QVERIFY(app.cancel(id));
            else app.setForeground(false);
        }
        const auto expected = reason == "cancel" ? "cancelled" : reason == "background" ? "interrupted" : "failed";
        QTRY_COMPARE_WITH_TIMEOUT(app.jobs().first().toMap().value("state").toString(), expected, 2000);
        QVERIFY(!app.busy() && !app.keepsScreenAwake());
        QVERIFY(app.latestImage().isEmpty());
        if (reason != "cancel") QVERIFY(!app.errorString().isEmpty());
        app.setForeground(true);
        QVERIFY(!app.enqueue("retry after termination").isEmpty());
        QTRY_VERIFY(!app.latestImage().isEmpty());
        QVERIFY(app.errorString().isEmpty());
        QCOMPARE(calls.load(), 2);
    }
    void nativeCancellationAtRunningTransitionNeverStartsTheEngine() {
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/native-transition-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile model(storage.filePath("Models/model.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("fixture"); model.close();
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.temporaryDirectory = DREAMSCAPES_TEST_DIRECTORY;
        std::atomic_int calls{0};
        runtime.nativeGenerate = [&](const auto &request, const auto &, const auto &) { ++calls; return imageResult(request); };
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(storage.path())); app.setForeground(true);
        connect(&app, &GenerationController::jobsChanged, &app, [&] {
            if (!app.jobs().isEmpty() && app.jobs().first().toMap().value("state") == "running")
                app.cancel(app.jobs().first().toMap().value("id").toString());
        });
        QVERIFY(!app.enqueue("cancel before engine call").isEmpty());
        QTRY_COMPARE(app.jobs().first().toMap().value("state").toString(), "cancelled");
        QCOMPARE(calls.load(), 0);
        QVERIFY(!app.busy() && !app.keepsScreenAwake());
    }
    void dreamscapesHasNoHostConnectionApi() {
        GenerationController app;
        QCOMPARE(app.metaObject()->indexOfMethod("connectRemote(QString)"), -1);
        QCOMPARE(app.metaObject()->indexOfProperty("remoteConnected"), -1);
    }
    void initialMirrorStaysUnavailableUntilSocietyPublishesIt() {
        QTemporaryDir phone(DREAMSCAPES_TEST_DIRECTORY "/society-pending-XXXXXX");
        const auto local = iiSocietyContainer::SocietyDrive::create(phone.path());
        QVERIFY(local);
        const auto hostId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(iiSocietyContainer::SocietyDrive::adoptReplicaIdentity(phone.path(), local->identifier(), hostId));
        QFile model(phone.filePath("Models/pending.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("model"); model.close();
        GenerationRuntime runtime;
        GenerationController app(runtime);
        QVERIFY(!app.connectStorage(phone.path()));
        app.setForeground(true);
        QVERIFY(app.models().isEmpty());
        QVERIFY(app.enqueue("not ready").isEmpty());
        QVERIFY(iiSocietyContainer::SocietyDrive::completeReplica(phone.path(), hostId));
        QTRY_VERIFY_WITH_TIMEOUT(app.connected(), 6000);
        QCOMPARE(app.models().size(), 1);
        QVERIFY(app.errorString().isEmpty());
        QVERIFY(QFile::remove(model.fileName()));
        QTRY_VERIFY_WITH_TIMEOUT(app.models().isEmpty(), 6000);
        QVERIFY(app.enqueue("removed model").isEmpty());
    }
    void invalidNativeModelFailsWithoutPublishingAnImage() {
        if (!iiLocalDiffusion::nativeDiffusionAvailable()) QSKIP("Native backend disabled in the SDK build");
        QTemporaryDir phone(DREAMSCAPES_TEST_DIRECTORY "/society-native-invalid-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(phone.path()));
        QFile model(phone.filePath("Models/invalid.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("invalid checkpoint"); model.close();
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.temporaryDirectory = DREAMSCAPES_TEST_DIRECTORY;
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(phone.path()));
        app.setForeground(true);
        const auto id = app.enqueue("a forest");
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(app.jobs().first().toMap().value("state").toString(), "failed", 10000);
        QVERIFY(!app.errorString().isEmpty());
        QVERIFY(app.latestImage().isEmpty());
        QVERIFY(QDir(phone.filePath("Generation History")).entryList(QDir::Files).isEmpty());
    }
    void realNativeModelGeneratesWhenExplicitlyRequested() {
        const auto source = qEnvironmentVariable("DREAMSCAPES_NATIVE_TEST_MODEL");
        if (source.isEmpty()) QSKIP("Set DREAMSCAPES_NATIVE_TEST_MODEL to verify actual native inference");
        QVERIFY(iiLocalDiffusion::nativeDiffusionAvailable());
        QTemporaryDir local(DREAMSCAPES_TEST_DIRECTORY "/society-native-real-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(local.path()));
        // Read the supplied weight bytes without a second 7 GB copy or mutation.
        std::error_code error;
        std::filesystem::create_hard_link(QFile::encodeName(source).toStdString(),
            QFile::encodeName(local.filePath("Models/model.safetensors")).toStdString(), error);
        QVERIFY2(!error, error.message().c_str());
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.steps = qEnvironmentVariableIntValue("DREAMSCAPES_NATIVE_TEST_STEPS");
        if (!runtime.steps) runtime.steps = 4;
        runtime.imageExtent = qEnvironmentVariableIntValue("DREAMSCAPES_NATIVE_TEST_EXTENT");
        if (!runtime.imageExtent) runtime.imageExtent = 256;
        runtime.temporaryDirectory = DREAMSCAPES_TEST_DIRECTORY;
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(local.path()));
        connect(&app, &GenerationController::previewChanged, &app, [&] {
            QVERIFY(app.previewStep() >= 0 && app.previewStep() <= runtime.steps);
            QVERIFY(app.previewTotalSteps() == 0 || app.previewTotalSteps() == runtime.steps);
        });
        app.setForeground(true);
        const auto id = app.enqueue("A white cockatoo on a flowering branch, botanical illustration");
        QVERIFY(!id.isEmpty());
        QTRY_VERIFY2_WITH_TIMEOUT(!app.latestImage().isEmpty() || !app.errorString().isEmpty(),
                                 qPrintable(app.errorString()), 300000);
        QVERIFY2(!app.latestImage().isEmpty(), qPrintable(app.errorString()));
        const auto image = app.latestImage().toLocalFile();
        QCOMPARE(QImage(image).size(), QSize(runtime.imageExtent, runtime.imageExtent));
        const auto evidence = QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/society-local-generation");
        QVERIFY(QDir().mkpath(evidence));
        QFile::remove(evidence + "/native-real.png");
        QVERIFY(QFile::copy(image, evidence + "/native-real.png"));
        QFile record(evidence + "/native-real.json");
        QVERIFY(record.open(QIODevice::WriteOnly));
        record.write(QJsonDocument(QJsonObject::fromVariantMap(app.latestResult())).toJson());
    }
    void societySyncsTheModelAndDreamscapesGeneratesOfflineFromItsLocalContainer() {
        QTemporaryDir desktop(DREAMSCAPES_TEST_DIRECTORY "/society-host-XXXXXX");
        QTemporaryDir phone(DREAMSCAPES_TEST_DIRECTORY "/society-phone-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(desktop.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(phone.path()));
        QFile weights(desktop.filePath("Models/synced.safetensors"));
        const QByteArray modelBytes(700000, 'm');
        QVERIFY(weights.open(QIODevice::WriteOnly)); QCOMPARE(weights.write(modelBytes), modelBytes.size()); weights.close();

        GenerationRuntime runtime{DREAMSCAPES_FAKE_GENERATOR, "cpu", 1, 64, {}, DREAMSCAPES_TEST_DIRECTORY};
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(phone.path()));
        QVERIFY(app.models().isEmpty());
        app.setForeground(true);

        // Only the two Society roles own transports and synchronization.
        iiServerHost::LanPeer host, client;
        iiSocietySync::Controller hosting({}), syncing([&](const auto &peer, const auto &request) {
            return client.request(peer, request);
        });
        const auto files = iiSocietySync::filesHandler(desktop.path());
        QVERIFY(host.startHost("desktop", "Desktop Society", [&](const auto &peer, const auto &request) {
            return request.value("op") == "society.sync" ? hosting.handle(peer, request) : files(peer, request);
        }, {"127.0.0.1"}, QHostAddress::LocalHost));
        QVERIFY(client.join(host.createOffer(), "phone", "iPhone Society"));
        QTRY_VERIFY2(client.connected(), qPrintable(client.errorString()));
        const QString scope(64, 'a');
        hosting.open(desktop.path(), scope); syncing.open(phone.path(), scope);
        QTRY_VERIFY(hosting.available() && syncing.available());
        hosting.setPeers({"phone"}, {});
        connect(&client, &iiServerHost::LanPeer::completed, &syncing, [&](auto id, auto result, auto) {
            syncing.receive(id, result);
        });
        QSignalSpy synchronized(&syncing, &iiSocietySync::Controller::synchronized);
        syncing.setPeers({"desktop"}, {"desktop"});
        QTRY_VERIFY2_WITH_TIMEOUT(!synchronized.isEmpty(), qPrintable(syncing.errorString()), 30000);
        QTRY_COMPARE_WITH_TIMEOUT(app.models().size(), 1, 6000);
        QVERIFY(app.connected());
        QCOMPARE(app.containerPath(), phone.path());
        iiSocietyHelper::FileSystem local;
        QVERIFY(local.open(phone.path()));
        QCOMPARE(local.containerId(), iiSocietyContainer::SocietyDrive::open(desktop.path())->identifier());
        QCOMPARE(local.path("models", "synced.safetensors"), phone.filePath("Models/synced.safetensors"));
        QFile copied(local.path("models", "synced.safetensors"));
        QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(), modelBytes); copied.close();

        syncing.close(); hosting.close(); client.stop(); host.stop();
        const auto id = app.enqueue("offline local generation");
        QVERIFY2(!id.isEmpty(), qPrintable(app.errorString()));
        QTRY_VERIFY2_WITH_TIMEOUT(!app.latestImage().isEmpty(), qPrintable(app.errorString()), 10000);
        const auto result = app.latestResult();
        QCOMPARE(result.value("generation").toMap().value("model_path").toString(),
                 phone.filePath("Models/synced.safetensors"));
        QVERIFY(app.latestImage().toLocalFile().startsWith(phone.filePath("Generation History/")));
        QVERIFY(QDir(desktop.filePath("Generation History")).entryList(QDir::Files).isEmpty());
        QVERIFY(!client.connected());
        QVERIFY(!client.hosting());
    }
};
QTEST_GUILESS_MAIN(LocalSocietyTests)
#include "tst_LocalSociety.moc"
