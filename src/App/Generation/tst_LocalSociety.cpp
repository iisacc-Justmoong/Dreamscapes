#include "GenerationController.h"
#include "SocietyGenerationStorage.h"
#include "GenerationWorkProgress.h"
#include <iiSocietyHelper.h>
#include <iiSocietySync.h>
#include <iiSocietyGeneration/Host.h>
#include <StorageMap.h>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <filesystem>
#include <thread>
#include <stdexcept>

namespace {
class BackgroundActivity final : public GenerationBackgroundActivity {
public:
    bool permitted = true;
    bool cpuOnly = false;
    int starts = 0;
    int networkStarts = 0;
    QList<bool> completions;
    QList<QPair<QString, QString>> presentations;
    bool presentationPaused = false;
    int updates = 0;
    iiLocalDiffusion::NativeGenerationStage lastStage = iiLocalDiffusion::NativeGenerationStage::Waiting;
    std::function<void(bool)> foregroundChanged;
    std::function<void()> expiration;
    void observeForeground(std::function<void(bool)> changed) override { foregroundChanged = std::move(changed); }
    void begin(const QString &, std::function<void()> expired) override { ++starts; expiration = std::move(expired); }
    void beginNetwork(const QString &job, std::function<void()> expired) override { ++networkStarts; begin(job, std::move(expired)); }
    bool allowsBackgroundExecution() const override { return permitted && starts > completions.size(); }
    bool requiresCpuExecution() const override { return cpuOnly; }
    void update(const iiLocalDiffusion::NativeGenerationProgress &progress) override { ++updates; lastStage = progress.stage; }
    void end(bool success) override { completions.append(success); }
    void finishPresentation(const QString &job, const QString &state) override { presentations.append({job, state}); }
    void setPresentationPaused(bool paused) override { presentationPaused = paused; }
    QVariantMap status() const override { return {}; }
};

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
    void nativePreviewsPublishRealPixelsAcrossPassesAndCleanUp() {
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/native-preview-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile model(storage.filePath("Models/model.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("test model"); model.close();
        GenerationRuntime runtime;
        runtime.nativeInference = true; runtime.imageExtent = 64;
        runtime.nativeGenerate = [](const auto &request, const auto &, const auto &, const auto &progress, const auto &preview) {
            using Stage = iiLocalDiffusion::NativeGenerationStage;
            for (int sequence = 1; sequence <= 2; ++sequence) {
                const auto step = sequence == 1 ? 10 : 1, total = sequence == 1 ? 10 : 3;
                // A progress event may arrive before the pixels for the same step.
                progress({Stage::Denoising, step, total});
                preview({std::vector<std::uint8_t>(2 * 2 * 3, sequence * 40), 2, 2, sequence, step, total});
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return imageResult(request);
        };
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(storage.path())); app.setForeground(true);
        QList<QUrl> frames; QList<int> totals;
        connect(&app, &GenerationController::previewChanged, &app, [&] {
            if (app.previewImage().isEmpty() || frames.contains(app.previewImage())) return;
            frames.append(app.previewImage()); totals.append(app.previewTotalSteps());
            QCOMPARE(QImage(app.previewImage().toLocalFile()).pixelColor(0, 0).red(), frames.size() * 40);
            QVERIFY(app.latestImage().isEmpty());
        });
        QVERIFY(!app.enqueue("show the image as it develops").isEmpty());
        QTRY_VERIFY(!app.latestImage().isEmpty());
        QCOMPARE(frames.size(), 2); QCOMPARE(totals, QList<int>({10, 3}));
        QVERIFY(app.previewImage().isEmpty());
        for (const auto &frame : frames) QVERIFY(!QFileInfo::exists(frame.toLocalFile()));
        QCOMPARE(QImage(app.latestImage().toLocalFile()).size(), QSize(64, 64));
    }
    void repeatedEnginePassesKeepReportingActualWork() {
        using Stage = iiLocalDiffusion::NativeGenerationStage;
        GenerationWorkProgress work;
        work.update({Stage::Loading, 220, 220});
        QCOMPARE(work.completed(), 220);
        work.update({Stage::Loading, 220, 220});
        QCOMPARE(work.completed(), 220); // Replayed event.
        work.update({Stage::Loading, 0, 220});
        work.update({Stage::Loading, 10, 220});
        QCOMPARE(work.completed(), 230);
        work.update({Stage::Denoising, 10, 10});
        work.update({Stage::Decoding, 1, 1});
        const auto beforeRefinement = work.completed();
        work.update({Stage::Denoising, 0, 4});
        work.update({Stage::Computing, 1, 0});
        work.update({Stage::Computing, 1, 0});
        work.update({Stage::Denoising, 1, 4});
        QCOMPARE(work.completed(), beforeRefinement + 2);
        work.update({Stage::Waiting});
        work.update({Stage::Loading, -1, 220});
        QCOMPARE(work.completed(), beforeRefinement + 2);
        QVERIFY(work.total() > work.completed()); // Success belongs to image publication.
    }
    void legacyCacheMovesOnConnectionWithoutGeneration() {
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/society-cache-upgrade-XXXXXX");
        QTemporaryDir previous(DREAMSCAPES_TEST_DIRECTORY "/private-cache-upgrade-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile cached(previous.filePath("converted.gguf"));
        QVERIFY(cached.open(QIODevice::WriteOnly)); cached.write("derived model"); cached.close();
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.legacyQ8CacheDirectory = previous.path();
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(storage.path()));
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(previous.path()), 2000);
        QFile moved(storage.filePath("Models/.society-runtime/iiLocalDiffusion/q8/converted.gguf"));
        QVERIFY(moved.open(QIODevice::ReadOnly)); QCOMPARE(moved.readAll(), "derived model");
        QVERIFY(app.jobs().isEmpty());
    }
    void nativeResourcesAndConvertedModelsBelongToSociety_data() {
        QTest::addColumn<bool>("hasResources");
        QTest::newRow("embedded-vae-without-optional-resources") << false;
        QTest::newRow("society-resource-catalog") << true;
    }
    void nativeResourcesAndConvertedModelsBelongToSociety() {
        QFETCH(bool, hasResources);
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/society-owned-native-XXXXXX");
        QTemporaryDir previous(DREAMSCAPES_TEST_DIRECTORY "/old-private-cache-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        if (hasResources) {
            QVERIFY(QDir().mkpath(storage.filePath("Models/.generation-resources/iiLocalDiffusion")));
            QFile manifest(storage.filePath("Models/.generation-resources/iiLocalDiffusion/generation-defaults.json"));
            QVERIFY(manifest.open(QIODevice::WriteOnly)); manifest.write("{}");
        }
        QFile model(storage.filePath("Models/checkpoint.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("source model"); model.close();
        QVERIFY(QDir(previous.path()).mkdir("q8"));
        QFile cached(previous.filePath("q8/converted.gguf"));
        QVERIFY(cached.open(QIODevice::WriteOnly)); cached.write("derived model"); cached.close();
        GenerationRuntime runtime;
        runtime.nativeInference = true; runtime.imageExtent = 64;
        runtime.legacyQ8CacheDirectory = previous.filePath("q8");
        QString observedResources, observedCache;
        bool modifiers = true;
        runtime.nativeGenerate = [&](const auto &request, const auto &options, const auto &, const auto &, const auto &) {
            observedResources = QString::fromStdString(options.resourceDirectory.string());
            observedCache = QString::fromStdString(request.q8CacheDirectory.string());
            modifiers = options.defaultModifiers;
            if (QFileInfo::exists(runtime.legacyQ8CacheDirectory)
                || !QFileInfo::exists(QDir(observedCache).filePath("converted.gguf")))
                throw std::runtime_error("Private cache was not migrated before inference");
            return imageResult(request);
        };
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(storage.path()));
        app.setForeground(true);
        const auto id = app.enqueue("use Society storage"); QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(app.jobs().first().toMap().value("state").toString(), QString("completed"), 10000);
        QCOMPARE(observedResources, storage.filePath("Models/.generation-resources/iiLocalDiffusion"));
        QCOMPARE(observedCache, storage.filePath("Models/.society-runtime/iiLocalDiffusion/q8"));
        QCOMPARE(modifiers, hasResources); // The SDK validates the catalog contents during real inference.
        QVERIFY(QDir(previous.path()).isEmpty());
        QVERIFY(app.latestImage().toLocalFile().startsWith(storage.filePath("Generation History/")));
        QVERIFY(model.open(QIODevice::ReadOnly)); QCOMPARE(model.readAll(), "source model");
        app.refreshModels(); QCOMPARE(app.models().size(), 1); // Hidden runtime data is not another selectable model.
    }
    void cacheMigrationPreservesRedirectedAndConflictingFiles() {
        QTemporaryDir source(DREAMSCAPES_TEST_DIRECTORY "/cache-source-XXXXXX");
        QTemporaryDir destination(DREAMSCAPES_TEST_DIRECTORY "/cache-target-XXXXXX");
        QTemporaryDir outside(DREAMSCAPES_TEST_DIRECTORY "/cache-outside-XXXXXX");
        QVERIFY(QFile::link(outside.path(), source.filePath("redirect")));
        std::atomic_bool cancelled{false}; QString error;
        QVERIFY(!dreamscapes::migrateLegacyQ8Cache(source.path(), destination.path(), cancelled, &error));
        QVERIFY(QFileInfo(source.filePath("redirect")).isSymLink());
        QVERIFY(QFile::remove(source.filePath("redirect")));
        QFile original(source.filePath("model.gguf")), existing(destination.filePath("model.gguf"));
        QVERIFY(original.open(QIODevice::WriteOnly)); original.write("original"); original.close();
        QVERIFY(existing.open(QIODevice::WriteOnly)); existing.write("different"); existing.close();
        QVERIFY(!dreamscapes::migrateLegacyQ8Cache(source.path(), destination.path(), cancelled, &error));
        QVERIFY(original.open(QIODevice::ReadOnly)); QCOMPARE(original.readAll(), "original"); original.close();
        QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(), "different"); existing.close();
        QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate)); existing.write("original"); existing.close();
        cancelled = true;
        QVERIFY(!dreamscapes::migrateLegacyQ8Cache(source.path(), destination.path(), cancelled, &error));
        QVERIFY(QFileInfo::exists(original.fileName()));
        cancelled = false;
        QVERIFY(dreamscapes::migrateLegacyQ8Cache(source.path(), destination.path(), cancelled, &error));
        QVERIFY(!QFileInfo::exists(source.path()));
        QVERIFY(existing.open(QIODevice::ReadOnly)); QCOMPARE(existing.readAll(), "original");
    }
    void nativeTimeoutTracksProgress_data() {
        QTest::addColumn<int>("stage");
        QTest::addColumn<bool>("hasProgress");
        QTest::addColumn<bool>("completes");
        using Stage = iiLocalDiffusion::NativeGenerationStage;
        QTest::newRow("repeated-tensor-progress") << int(Stage::Preparing) << true << true;
        QTest::newRow("encoding-progress") << int(Stage::Encoding) << true << true;
        QTest::newRow("denoising-progress") << int(Stage::Denoising) << true << true;
        QTest::newRow("decoding-progress") << int(Stage::Decoding) << true << true;
        QTest::newRow("status-without-progress") << int(Stage::Encoding) << false << false;
        QTest::newRow("waiting-does-not-extend-timeout") << int(Stage::Waiting) << true << false;
    }
    void nativeTimeoutTracksProgress() {
        QFETCH(int, stage);
        QFETCH(bool, hasProgress);
        QFETCH(bool, completes);
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/native-progress-timeout-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile model(storage.filePath("Models/model.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("fixture"); model.close();
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.imageExtent = 64;

        runtime.nativeTimeoutMilliseconds = 200;
        runtime.nativeGenerate = [stage, hasProgress](const auto &request, const auto &, const auto &cancelled, const auto &progress, const auto &) {
            const auto started = std::chrono::steady_clock::now();
            for (int index = 0; index < 20 && !cancelled; ++index) {
                progress({static_cast<iiLocalDiffusion::NativeGenerationStage>(stage),
                    hasProgress ? 1 : 0, hasProgress ? 10 : 0});
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                // Match the SDK's independent total-duration deadline.
                if (std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(request.timeoutMilliseconds)) {
                    iiLocalDiffusion::NativeGenerationResult result;
                    result.error = "The SDK total-duration deadline expired.";
                    return result;
                }
            }
            auto result = imageResult(request);
            result.cancelled = cancelled;
            return result;
        };
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(storage.path())); app.setForeground(true);
        QElapsedTimer elapsed; elapsed.start();
        QVERIFY(!app.enqueue("keep working while progress continues").isEmpty());
        QTRY_VERIFY(app.jobs().first().toMap().value("state").toString() != "queued");
        QTRY_VERIFY(!app.busy());
        QCOMPARE(app.jobs().first().toMap().value("state").toString(), completes ? "completed" : "failed");
        if (completes) {
            QVERIFY(elapsed.elapsed() >= 2 * runtime.nativeTimeoutMilliseconds);
            QVERIFY(!app.latestImage().isEmpty());
        } else {
            QVERIFY(app.latestImage().isEmpty());
            QVERIFY(app.errorString().contains("stopped making progress"));
        }
    }
    void unsupportedBackgroundGpuPausesTheSameRequest_data() {
        QTest::addColumn<bool>("cancelPaused");
        QTest::newRow("resume") << false;
        QTest::newRow("cancel-while-paused") << true;
    }
    void unsupportedBackgroundGpuPausesTheSameRequest() {
        QFETCH(bool, cancelPaused);
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/native-paused-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile model(storage.filePath("Models/model.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("fixture"); model.close();
        auto activity = std::make_shared<BackgroundActivity>();
        activity->permitted = false;
        auto control = std::make_shared<iiLocalDiffusion::NativeExecutionControl>();
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.imageExtent = 64;

        runtime.nativeTimeoutMilliseconds = 300;
        runtime.backgroundActivity = activity;
        runtime.nativeExecutionControl = control;
        std::atomic_int calls{0};
        runtime.nativeGenerate = [&](const auto &request, const auto &, const auto &cancelled, const auto &progress, const auto &) {
            ++calls;
            progress({iiLocalDiffusion::NativeGenerationStage::Denoising, 3, 10});
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!control->isPaused() && !cancelled && std::chrono::steady_clock::now() < limit)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (!control->waitUntilRunnable(cancelled)) {
                iiLocalDiffusion::NativeGenerationResult result; result.cancelled = true; return result;
            }
            progress({iiLocalDiffusion::NativeGenerationStage::Denoising, 4, 10});
            return imageResult(request);
        };
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(storage.path())); app.setForeground(true);
        const auto id = app.enqueue("preserve latent state");
        QTRY_COMPARE(app.previewStep(), 3);
        app.setForeground(false);
        QTRY_VERIFY(control->isWaiting());
        QVERIFY(activity->presentationPaused);
        QTest::qWait(400); // Longer than the inference timeout, without using it.
        QVERIFY(app.busy());
        QCOMPARE(app.previewStep(), 3);
        QCOMPARE(app.inferenceStatus().value("state").toString(), "paused");
        QVERIFY(!app.keepsScreenAwake() && app.errorString().isEmpty());
        if (cancelPaused) QVERIFY(app.cancel(id));
        else app.setForeground(true);
        QTRY_VERIFY(!app.busy());
        QCOMPARE(app.jobs().first().toMap().value("state").toString(), cancelPaused ? "cancelled" : "completed");
        QCOMPARE(app.jobs().first().toMap().value("id").toString(), id);
        QCOMPARE(calls.load(), 1);
        QCOMPARE(activity->completions, QList<bool>({!cancelPaused}));
        QCOMPARE(activity->presentations, (QList<QPair<QString, QString>>{{id, cancelPaused ? "cancelled" : "completed"}}));
        QVERIFY(!control->isPaused());
    }
    void nativeBackgroundPermissionPreservesGeneration_data() {
        QTest::addColumn<bool>("cpuOnly");
        QTest::newRow("background-gpu") << false;
        QTest::newRow("background-cpu") << true;
    }
    void nativeBackgroundPermissionPreservesGeneration() {
        QFETCH(bool, cpuOnly);
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/native-background-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile model(storage.filePath("Models/model.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("fixture"); model.close();
        auto activity = std::make_shared<BackgroundActivity>();
        activity->cpuOnly = cpuOnly;
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.imageExtent = 64;

        runtime.backgroundActivity = activity;
        std::atomic_int phase{0};
        runtime.nativeGenerate = [&](const auto &request, const auto &, const auto &cancelled, const auto &progress, const auto &) {
            progress({iiLocalDiffusion::NativeGenerationStage::Denoising, 1, 10});
            while (phase == 0 && !cancelled) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (cancelled) { iiLocalDiffusion::NativeGenerationResult result; result.cancelled = true; return result; }
            progress({iiLocalDiffusion::NativeGenerationStage::Denoising, 2, 10});
            while (phase == 1 && !cancelled) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return imageResult(request);
        };
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(storage.path())); app.setForeground(true);
        QVERIFY(!app.enqueue("continue while hidden").isEmpty());
        QTRY_COMPARE(app.previewStep(), 1);
        QCOMPARE(activity->starts, 1);
        QVERIFY(activity->foregroundChanged);
        activity->foregroundChanged(false);
        QVERIFY(!app.keepsScreenAwake());
        QCOMPARE(app.inferenceStatus().value("state").toString(), "denoising");
        phase = 1;
        QTRY_COMPARE(app.previewStep(), 2);
        QVERIFY(app.busy());
        activity->foregroundChanged(true);
        QCOMPARE(app.inferenceStatus().value("state").toString(), "denoising");
        QVERIFY(app.keepsScreenAwake());
        activity->foregroundChanged(false);
        phase = 2;
        QTRY_VERIFY(!app.latestImage().isEmpty());
        QCOMPARE(app.jobs().first().toMap().value("state").toString(), "completed");
        QCOMPARE(activity->completions, QList<bool>({true}));
        QCOMPARE(app.jobs().first().toMap().value("generation").toMap().value("computeBackend").toString(),
                 cpuOnly ? QString("cpu") : QString("automatic"));
        QVERIFY(activity->updates >= 2);
        QVERIFY(!app.foreground() && !app.keepsScreenAwake());
    }
    void nativeBackgroundExpirationPreservesTheRequest_data() {
        QTest::addColumn<bool>("background");
        QTest::newRow("foreground") << false;
        QTest::newRow("background") << true;
    }
    void nativeBackgroundExpirationPreservesTheRequest() {
        QFETCH(bool, background);
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/native-background-expired-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile model(storage.filePath("Models/model.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("fixture"); model.close();
        auto activity = std::make_shared<BackgroundActivity>();
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.imageExtent = 64;
        runtime.nativeTimeoutMilliseconds = 300;
        runtime.backgroundActivity = activity;
        const auto control = std::make_shared<iiLocalDiffusion::NativeExecutionControl>();
        runtime.nativeExecutionControl = control;
        std::atomic_bool finish{false};
        int calls = 0;
        runtime.nativeGenerate = [&](const auto &request, const auto &, const auto &cancelled, const auto &progress, const auto &) {
            ++calls;
            progress({iiLocalDiffusion::NativeGenerationStage::Loading, 1, 220});
            while (!finish && !cancelled) {
                if (!control->waitUntilRunnable(cancelled)) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            progress({iiLocalDiffusion::NativeGenerationStage::Denoising, 2, 10});
            return imageResult(request);
        };
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(storage.path())); app.setForeground(true);
        const auto id = app.enqueue("preserve work after expiration");
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(app.inferenceStatus().value("total").toInt(), 220);
        QVERIFY(activity->expiration);
        if (background) app.setForeground(false);
        activity->permitted = false;
        activity->expiration();
        QCOMPARE(activity->completions, QList<bool>({false}));
        QVERIFY(activity->presentations.isEmpty()); // Expiration cannot finish the card.
        QVERIFY(app.busy() && app.errorString().isEmpty());
        if (background) {
            QTRY_VERIFY(control->isWaiting());
            QTest::qWait(400);
            QVERIFY(app.busy());
            QCOMPARE(app.inferenceStatus().value("state").toString(), "paused");
            app.setForeground(true);
        } else {
            QVERIFY(!control->isPaused());
            QCOMPARE(app.inferenceStatus().value("state").toString(), "loading");
        }
        finish = true;
        QTRY_VERIFY(!app.busy());
        QCOMPARE(app.jobs().first().toMap().value("state").toString(), "completed");
        QCOMPARE(app.jobs().first().toMap().value("id").toString(), id);
        QCOMPARE(calls, 1);
        QCOMPARE(activity->completions, QList<bool>({false}));
        QCOMPARE(activity->presentations, (QList<QPair<QString, QString>>{{id, "completed"}}));
        QVERIFY(!activity->presentationPaused);
        QVERIFY(activity->updates >= 2); // Updates continue after the grant ended.
    }
    void nativeProgressAndScreenActivityFollowTheWholeImage() {
        QTemporaryDir storage(DREAMSCAPES_TEST_DIRECTORY "/native-progress-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(storage.path()));
        QFile model(storage.filePath("Models/model.safetensors"));
        QVERIFY(model.open(QIODevice::WriteOnly)); model.write("fixture"); model.close();
        GenerationRuntime runtime;
        runtime.nativeInference = true;
        runtime.imageExtent = 64;

        QList<bool> screen;
        runtime.screenActivity = [&](bool active) { screen.append(active); };
        runtime.nativeGenerate = [](const auto &request, const auto &, const auto &, const auto &progress, const auto &) {
            using Stage = iiLocalDiffusion::NativeGenerationStage;
            if (request.q8CacheDirectory.string().find("Models/.society-runtime/iiLocalDiffusion/q8") == std::string::npos)
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

        std::atomic_int calls{0};
        runtime.nativeGenerate = [&](const auto &request, const auto &, const auto &cancelled, const auto &progress, const auto &) {
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

        std::atomic_int calls{0};
        runtime.nativeGenerate = [&](const auto &request, const auto &, const auto &, const auto &, const auto &) { ++calls; return imageResult(request); };
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
    void actualModelRunsOnHostWithoutClientWeights() {
        const auto source = qEnvironmentVariable("DREAMSCAPES_REMOTE_TEST_MODEL");
        if (source.isEmpty()) QSKIP("Set DREAMSCAPES_REMOTE_TEST_MODEL for actual host inference over TLS.");
        QVERIFY(QFileInfo::exists(source));
        QTemporaryDir desktop(DREAMSCAPES_TEST_DIRECTORY "/real-host-XXXXXX");
        QTemporaryDir phone(DREAMSCAPES_TEST_DIRECTORY "/real-phone-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(desktop.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(phone.path()));
        std::error_code linkError;
        std::filesystem::create_hard_link(QFile::encodeName(source).toStdString(),
            QFile::encodeName(desktop.filePath("Models/model.safetensors")).toStdString(), linkError);
        QVERIFY2(!linkError, linkError.message().c_str());
        const auto storage = iiSocietyContainer::SharedStorage::open(desktop.path());
        QVERIFY(storage && storage->models().size() == 1);
        iiSocietyGeneration::Host generationHost;
        QVERIFY(generationHost.open(desktop.path()));
        iiServerHost::LanPeer host, client;
        iiSocietyGeneration::Remote remote([&](const auto &request) { return client.request("desktop", request); });
        QJsonObject previousProgress;
        connect(&remote, &iiSocietyGeneration::Remote::progress, this, [&](const QJsonObject &progress) {
            if (progress != previousProgress) qInfo().noquote() << QJsonDocument(progress).toJson(QJsonDocument::Compact);
            previousProgress = progress;
        });
        connect(&client, &iiServerHost::LanPeer::completed, &remote,
            [&](auto id, auto result, auto) { remote.receive(id, result); });
        const auto files = iiSocietySync::filesHandler(desktop.path());
        QVERIFY(host.startHost("desktop", "Actual host", [&](const auto &peer, const auto &request) {
            return request.value("op") == "society.generation" ? generationHost.handle(peer, request) : files(peer, request);
        }, {"127.0.0.1"}, QHostAddress::LocalHost));
        QVERIFY(client.join(host.createOffer(), "phone", "Empty model client"));
        QTRY_VERIFY2(client.connected(), qPrintable(client.errorString()));
        QSignalSpy finished(&remote, &iiSocietyGeneration::Remote::finished);
        const QJsonObject job{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"prompt", "A white cockatoo on a flowering branch, botanical illustration"},
            {"width", 256}, {"height", 256}, {"steps", 2}, {"seed", 42},
            {"model", storage->models().first().reference(storage->drive().identifier())}};
        QVERIFY(remote.start(job));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 1000000);
        QVERIFY2(finished[0][3].toString().isEmpty(), qPrintable(finished[0][3].toString()));
        QCOMPARE(finished[0][0].toString(), QString("completed"));
        const auto png = finished[0][1].toByteArray();
        QCOMPARE(QImage::fromData(png).size(), QSize(256, 256));
        QVERIFY(iiSocietyContainer::SharedStorage::open(phone.path())->models().isEmpty());
        const auto evidence = QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/society-local-generation");
        QVERIFY(QDir().mkpath(evidence));
        QFile image(evidence + "/remote-real.png"); QVERIFY(image.open(QIODevice::WriteOnly));
        QCOMPARE(image.write(png), png.size());
        QFile record(evidence + "/remote-real.json"); QVERIFY(record.open(QIODevice::WriteOnly));
        record.write(QJsonDocument(finished[0][2].toJsonObject()).toJson());
    }
    void generatesOnHostBeforeBackgroundModelDownloadThenUsesLocalCache() {
        QTemporaryDir desktop(DREAMSCAPES_TEST_DIRECTORY "/society-host-XXXXXX");
        QTemporaryDir phone(DREAMSCAPES_TEST_DIRECTORY "/society-phone-XXXXXX");
        QVERIFY(iiSocietyContainer::SocietyDrive::create(desktop.path()));
        QVERIFY(iiSocietyContainer::SocietyDrive::create(phone.path()));
        QFile weights(desktop.filePath("Models/synced.safetensors"));
        const QByteArray modelBytes(700000, 'm');
        QVERIFY(weights.open(QIODevice::WriteOnly)); QCOMPARE(weights.write(modelBytes), modelBytes.size()); weights.close();
        const auto resources = QStringLiteral("Models/.generation-resources/iiLocalDiffusion/");
        const auto runtimeCache = QStringLiteral("Models/.society-runtime/iiLocalDiffusion/q8/");
        for (const auto &relative : {resources + "generation-defaults.json", resources + "vae.safetensors", runtimeCache + "model.gguf"}) {
            QVERIFY(QDir().mkpath(QFileInfo(desktop.filePath(relative)).absolutePath()));
            QFile fixture(desktop.filePath(relative));
            QVERIFY(fixture.open(QIODevice::WriteOnly)); fixture.write("fixture");
        }

        iiServerHost::LanPeer host, client;
        auto remoteGeneration = std::make_shared<iiSocietyGeneration::Remote>([&](const QJsonObject &request) {
            return client.request("desktop", request);
        });
        connect(&client, &iiServerHost::LanPeer::completed, remoteGeneration.get(),
            [remoteGeneration](auto id, auto result, auto) { remoteGeneration->receive(id, result); });
        std::atomic_int hostRuns{0};
        std::string hostModelPath;
        iiSocietyGeneration::Host generationHost([&](const auto &request, const auto &, const auto &, const auto &progress) {
            progress({iiLocalDiffusion::NativeGenerationStage::Loading, 25, 100});
            hostModelPath = request.modelPath.string(); ++hostRuns; return imageResult(request);
        });
        QVERIFY(generationHost.open(desktop.path()));
        GenerationRuntime runtime{DREAMSCAPES_FAKE_GENERATOR, "cpu", 1, 64, {}};
        runtime.remoteGeneration = remoteGeneration;
        runtime.nativeInference = true;
        runtime.nativeGenerate = [](const auto &request, const auto &, const auto &, const auto &, const auto &) {
            return imageResult(request);
        };
        auto background = std::make_shared<BackgroundActivity>();
        runtime.backgroundActivity = background;
        GenerationController app(runtime);
        QVERIFY(app.connectStorage(phone.path()));
        QVERIFY(app.models().isEmpty());
        app.setForeground(true);

        // Only the two Society roles own transports and synchronization.
        iiSocietySync::Controller hosting({}), syncing([&](const auto &peer, const auto &request) {
            return client.request(peer, request);
        });
        const auto files = iiSocietySync::filesHandler(desktop.path());
        QVERIFY(host.startHost("desktop", "Desktop Society", [&](const auto &peer, const auto &request) {
            if (request.value("op") == "society.generation") return generationHost.handle(peer, request);
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
        QVERIFY(!QFileInfo::exists(local.path("models", "synced.safetensors")));
        QVERIFY(!app.models().first().toMap().value("available").toBool());
        // Hold model replication while leaving the authenticated transport live.
        // The result must arrive even when no model byte can be downloaded.
        syncing.setPeers({}, {});
        const auto id = app.enqueue("host first generation", "1:1", 2);
        QVERIFY2(!id.isEmpty(), qPrintable(app.errorString()));
        QTRY_COMPARE(background->starts, 1);
        background->permitted = false;
        app.setForeground(false);
        QTest::qWait(300);
        background->permitted = true;
        // The OS may grant execution after UIKit has already entered background.
        app.setForeground(false);
        QTRY_COMPARE_WITH_TIMEOUT(app.completedResults().size(), 2, 30000);
        QCOMPARE(hostRuns.load(), 2);
        QCOMPARE(background->lastStage, iiLocalDiffusion::NativeGenerationStage::Loading);
        QCOMPARE(QString::fromStdString(hostModelPath), desktop.filePath("Models/synced.safetensors"));
        QVERIFY(!QFileInfo::exists(local.path("models", "synced.safetensors")));
        QCOMPARE(app.latestResult().value("execution").toString(), QString("host"));
        const auto download = app.latestResult().value("modelDownload").toMap().value("requestId").toString();
        QVERIFY(!download.isEmpty());
        iiSocietyContainer::StorageMap map(*iiSocietyContainer::SocietyDrive::open(phone.path()));
        QVERIFY(map.requestState(download).value("state") != "ready");
        QCOMPARE(background->starts, 1); QCOMPARE(background->networkStarts, 1); QVERIFY(background->completions.isEmpty());
        app.setForeground(false);
        QVERIFY(background->allowsBackgroundExecution());
        background->expiration();
        QCOMPARE(background->completions, QList<bool>{false});
        QVERIFY(map.requestState(download).value("state") != "cancelled");
        app.setForeground(true);
        syncing.setPeers({"desktop"}, {"desktop"}); syncing.synchronizeNow();
        QTRY_COMPARE_WITH_TIMEOUT(map.requestState(download).value("state").toString(), QString("ready"), 30000);
        QFile copied(local.path("models", "synced.safetensors"));
        QVERIFY(copied.open(QIODevice::ReadOnly)); QCOMPARE(copied.readAll(), modelBytes); copied.close();
        for (const auto &relative : {resources + "generation-defaults.json", resources + "vae.safetensors"}) {
            QFile resource(phone.filePath(relative));
            QVERIFY(resource.open(QIODevice::ReadOnly)); QCOMPARE(resource.readAll(), "fixture");
        }
        QVERIFY(!QFileInfo::exists(phone.filePath(runtimeCache + "model.gguf")));
        const auto result = app.latestResult();
        QCOMPARE(result.value("generation").toMap().value("backend").toString(), QString("society-host"));
        const auto image = app.latestImage().toLocalFile();
        QVERIFY(image.startsWith(phone.filePath("Generation History/")));
        const auto remote = desktop.filePath("Generation History/" + QFileInfo(image).fileName());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(remote), 15000);
        QFile original(image), published(remote); QVERIFY(original.open(QIODevice::ReadOnly)); QVERIFY(published.open(QIODevice::ReadOnly));
        QCOMPARE(published.readAll(), original.readAll());
        syncing.closeAndWait(); hosting.closeAndWait(); client.stop(); host.stop();
        // The downloaded cache is usable without any host connection.
        const auto completedBeforeOffline = app.completedResults().size();
        QVERIFY(!app.enqueue("offline cached generation").isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(app.completedResults().size(), completedBeforeOffline + 1, 10000);
        QCOMPARE(hostRuns.load(), 2);

    }
};
QTEST_GUILESS_MAIN(LocalSocietyTests)
#include "tst_LocalSociety.moc"
