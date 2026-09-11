#include "GenerationController.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLockFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace iiSocietyContainer;
namespace {
bool write(const QString &path, const QByteArray &value)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(value) == value.size();
}
QJsonObject read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
GenerationRuntime fakeRuntime()
{
    return {QStringLiteral(DREAMSCAPES_FAKE_GENERATOR), "cpu", 1, 64, {}, QStringLiteral(DREAMSCAPES_TEST_DIRECTORY)};
}
QJsonObject recordedJob(const GenerationController &controller, const QString &id)
{
    for (const auto &value : controller.jobs()) {
        const auto job = value.toMap();
        if (job.value("id") == id) return QJsonObject::fromVariantMap(job);
    }
    return {};
}
QString state(const GenerationController &controller, const QString &id)
{
    for (const auto &value : controller.jobs()) {
        const auto job = value.toMap();
        if (job.value("id") == id) return job.value("state").toString();
    }
    return {};
}
bool prepare(const QTemporaryDir &root)
{
    return root.isValid() && SocietyDrive::create(root.path()).has_value()
        && write(root.filePath("Models/first model.safetensor"), "test weight A")
        && write(root.filePath("Models/second.SAFETENSORS"), "test weight B");
}
}

class GenerationTests : public QObject
{
    Q_OBJECT
private slots:
    void defaultResolutionReachesBothGenerationBackends_data()
    {
        QTest::addColumn<QString>("ratio");
        QTest::addColumn<QSize>("expected");
        QTest::addColumn<bool>("native");
        const QList<QPair<QString, QSize>> sizes{{"1:1", {1024, 1024}}, {"4:3", {1368, 1024}},
            {"3:4", {1024, 1368}}, {"16:9", {1824, 1024}}, {"9:16", {1024, 1824}}};
        for (bool native : {false, true})
            for (const auto &[ratio, size] : sizes)
                QTest::newRow(qPrintable((native ? "native-" : "worker-") + ratio)) << ratio << size << native;
    }

    void defaultResolutionReachesBothGenerationBackends()
    {
        QFETCH(QString, ratio);
        QFETCH(QSize, expected);
        QFETCH(bool, native);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/default-resolution-XXXXXX");
        QVERIFY(prepare(root));
        GenerationRuntime runtime;
        runtime.executable = QStringLiteral(DREAMSCAPES_FAKE_GENERATOR);
        runtime.temporaryDirectory = QStringLiteral(DREAMSCAPES_TEST_DIRECTORY);
        runtime.nativeInference = native;
        iiLocalDiffusion::NativeGenerationRequest nativeRequest;
        runtime.nativeGenerate = [&nativeRequest](const auto &request, const auto &, const auto &) {
            nativeRequest = request;
            return iiLocalDiffusion::NativeGenerationResult{
                std::vector<std::uint8_t>(request.width * request.height * 3, 127), request.width, request.height};
        };
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        const auto id = controller.enqueue("default resolution", ratio);
        QVERIFY(!id.isEmpty());
        const auto submitted = recordedJob(controller, id);
        QCOMPARE(QSize(submitted.value("width").toInt(), submitted.value("height").toInt()), expected);
        QCOMPARE(qMin(submitted.value("width").toInt(), submitted.value("height").toInt()), 1024);
        QCOMPARE(submitted.value("steps").toInt(), 10);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("completed"), 10000);
        QCOMPARE(QImage(controller.latestImage().toLocalFile()).size(), expected);
        if (native) {
            QCOMPARE(QSize(nativeRequest.width, nativeRequest.height), expected);
            QCOMPARE(nativeRequest.steps, 10);
        } else {
            const auto received = recordedJob(controller, id).value("generation").toObject();
            QCOMPARE(QSize(received.value("width").toInt(), received.value("height").toInt()), expected);
            QCOMPARE(received.value("steps").toInt(), 10);
        }
    }

    void completedResultsExposeEveryImageAndExcludeUnpublishedFiles()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/gallery-results-XXXXXX");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(controller.property("completedResults").isValid());
        QVERIFY(controller.property("completedResults").toList().isEmpty());
        const auto first = controller.enqueue("multiple", "4:3");
        const auto second = controller.enqueue("another image");
        const auto cancelled = controller.enqueue("cancel before execution");
        QVERIFY(controller.cancel(cancelled));
        const auto failed = controller.enqueue("fail");
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, failed), QString("failed"), 10000);
        QCOMPARE(state(controller, first), "completed");
        QCOMPARE(state(controller, second), "completed");
        const auto results = controller.property("completedResults").toList();
        QCOMPARE(results.size(), 3);
        QSet<QUrl> sources;
        for (int index = 0; index < results.size(); ++index) {
            const auto result = results[index].toMap();
            QCOMPARE(result.value("id").toString(), index < 2 ? first : second);
            QCOMPARE(result.value("prompt").toString(), index < 2 ? "multiple" : "another image");
            const auto source = result.value("imageSource").toUrl();
            QVERIFY(!QImage(source.toLocalFile()).isNull());
            QCOMPARE(source.toLocalFile(), root.filePath(result.value("image").toString()));
            sources.insert(source);
        }
        QCOMPARE(sources.size(), 3);
        QCOMPARE(results.last().toMap(), controller.latestResult());
        // A deleted or redirected published file cannot become a gallery/project input.
        const auto firstPath = results.first().toMap().value("imageSource").toUrl().toLocalFile();
        QVERIFY(QFile::remove(firstPath));
        QCOMPARE(controller.property("completedResults").toList().size(), 2);
        QVERIFY(QFile::link(results.last().toMap().value("imageSource").toUrl().toLocalFile(), firstPath));
        QCOMPARE(controller.property("completedResults").toList().size(), 2);
        QTemporaryDir other(DREAMSCAPES_TEST_DIRECTORY "/gallery-other-storage-XXXXXX");
        QVERIFY(prepare(other));
        QVERIFY(controller.connectStorage(other.path()));
        QVERIFY(controller.property("completedResults").toList().isEmpty());
    }

    void batchSubmissionKeepsOneModelSnapshotAndRejectsInvalidCounts()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/batch-queue-XXXXXX");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.executable.clear();
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        const auto model = controller.selectedModel();
        QSignalSpy changes(&controller, &GenerationController::jobsChanged);
        const auto first = controller.enqueue("  a forest  ", "16:9", 1000);
        QVERIFY(!first.isEmpty());
        QCOMPARE(controller.jobs().size(), 1000);
        QCOMPARE(changes.size(), 1);
        controller.setSelectedModel(controller.models().last().toMap().value("id").toString());
        QSet<QString> ids;
        QSet<QString> creationTimes;
        for (const auto &entry : controller.jobs()) {
            const auto job = entry.toMap();
            ids.insert(job.value("id").toString());
            creationTimes.insert(job.value("createdAt").toString());
            QCOMPARE(job.value("state").toString(), QString("queued"));
            QCOMPARE(job.value("prompt").toString(), QString("a forest"));
            QCOMPARE(job.value("aspectRatio").toString(), QString("16:9"));
            QCOMPARE(job.value("model").toMap().value("path").toString(), model);
        }
        QCOMPARE(ids.size(), 1000);
        QCOMPARE(creationTimes.size(), 1000);
        QVERIFY(ids.contains(first));
        for (int invalid : {-1, 0, 1001})
            QVERIFY(controller.enqueue("invalid count", "1:1", invalid).isEmpty());
        QVERIFY(controller.enqueue(" ", "1:1", 3).isEmpty());
        QVERIFY(controller.enqueue("invalid ratio", "bad", 3).isEmpty());
        QCOMPARE(controller.jobs().size(), 1000);
        QVERIFY(controller.cancel(first));
        QCOMPARE(state(controller, first), QString("cancelled"));
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
    }

    void batchGenerationPublishesTheSelectedNumberOfImagesSerially()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/batch-generation-XXXXXX");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        connect(&controller, &GenerationController::jobsChanged, this, [&] {
            int running = 0;
            for (const auto &entry : controller.jobs())
                running += entry.toMap().value("state") == "running";
            QVERIFY(running <= 1);
        });
        QVERIFY(!controller.enqueue("three images", "4:3", 3).isEmpty());
        QCOMPARE(controller.jobs().size(), 3);
        const auto allCompleted = [&] {
            for (const auto &entry : controller.jobs())
                if (entry.toMap().value("state") != "completed") return false;
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(allCompleted(), 10000);
        QCOMPARE(QDir(root.filePath("Generation History")).entryList(QDir::Files).size(), 3);
        QSet<qint64> workerIds;
        for (const auto &entry : controller.jobs()) {
            const auto job = QJsonObject::fromVariantMap(entry.toMap());
            workerIds.insert(job.value("worker").toObject().value("pid").toInteger());
        }
        QCOMPARE(workerIds.size(), 1);
        QVERIFY(!workerIds.contains(0));
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
    }

    void foregroundPreparationFailureCanRecoverWithAnotherModel()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/foreground-failure-XXXXXX");
        QVERIFY(prepare(root));
        QVERIFY(write(root.filePath("Models/first model.safetensor"), "prepare-fail"));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_COMPARE(controller.inferenceStatus().value("state").toString(), QString("error"));
        QVERIFY(!controller.inferenceStatus().value("ready").toBool());
        QVERIFY(controller.jobs().isEmpty());
        controller.setSelectedModel(controller.models().last().toMap().value("id").toString());
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
    }

    void foregroundControlRequiresTheSdkCapability()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/foreground-legacy-XXXXXX");
        QVERIFY(prepare(root));
        const auto executable = root.filePath("legacy.py");
        QVERIFY(write(executable,
            "#!/usr/bin/env python3\nimport sys\n"
            "print('IILD_READY {\"schema\":\"iild-worker-v1\"}', flush=True)\n"
            "for line in sys.stdin:\n print('Unexpected request to a legacy worker', flush=True)\n"));
        QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        auto runtime = fakeRuntime();
        runtime.executable = executable;
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_COMPARE(controller.inferenceStatus().value("state").toString(), QString("error"));
        QVERIFY(controller.inferenceStatus().value("error").toString().contains("iiLocalDiffusion"));
        QVERIFY(controller.jobs().isEmpty());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
    }

    void foregroundPreparesWithoutAQueueAndReusesTheWorker()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/foreground-XXXXXX");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        QVERIFY(controller.jobs().isEmpty());
        QVERIFY(!controller.busy());
        QVERIFY(controller.latestImage().isEmpty());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
        const auto before = controller.inferenceStatus();
        const auto id = controller.enqueue("use the prepared model");
        QTRY_COMPARE(state(controller, id), QString("completed"));
        const auto worker = recordedJob(controller, id).value("worker").toObject();
        QCOMPARE(worker.value("pid").toVariant(), before.value("pid"));
        QCOMPARE(worker.value("cache").toObject().value("pipeline_loads").toInt(-1), 0);
        QCOMPARE(worker.value("cache").toObject().value("device_placements").toInt(-1), 0);
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        controller.setForeground(false);
        QTRY_COMPARE(controller.inferenceStatus().value("foreground").toBool(), false);
        controller.setForeground(true);
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        QCOMPARE(controller.inferenceStatus().value("pid"), before.value("pid"));
        QVERIFY(QFile::remove(root.filePath("Models/first model.safetensor")));
        QVERIFY(QFile::remove(root.filePath("Models/second.SAFETENSORS")));
        controller.refreshModels();
        QTRY_COMPARE(controller.inferenceStatus().value("state").toString(), QString("waiting-model"));
        QVERIFY(!controller.inferenceStatus().value("ready").toBool());
        QCOMPARE(controller.jobs().size(), 1);
    }

    void foregroundModelChangesAndQueuedRequestsRemainSeparate()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/foreground-model-XXXXXX");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        controller.setForeground(true);
        QTRY_VERIFY(controller.inferenceStatus().contains("requestId"));
        const auto first = controller.enqueue("queued during preparation");
        controller.setSelectedModel(controller.models().last().toMap().value("id").toString());
        const auto second = controller.enqueue("selected second model");
        QTRY_COMPARE(state(controller, first), QString("completed"));
        QTRY_COMPARE(state(controller, second), QString("completed"));
        QTRY_VERIFY(controller.inferenceStatus().value("ready").toBool());
        QVERIFY(controller.inferenceStatus().value("model").toString().endsWith("second.SAFETENSORS"));
        QCOMPARE(controller.jobs().size(), 2);
        QCOMPARE(QDir(root.filePath("Generation History")).entryList(QDir::Files).size(), 2);
    }

    void queuesBelongOnlyToTheCurrentAppSession()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/memory-queue-XXXXXX");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.executable.clear();
        {
            GenerationController first(runtime);
            QVERIFY(first.connectStorage(root.path()));
            QVERIFY(!first.enqueue("discard when this app closes").isEmpty());
            QCOMPARE(first.jobs().size(), 1);
            GenerationController otherApp(runtime);
            QVERIFY(otherApp.connectStorage(root.path()));
            QVERIFY(otherApp.jobs().isEmpty());
            QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
        }
        GenerationController reopened(runtime);
        QVERIFY(reopened.connectStorage(root.path()));
        QVERIFY(reopened.jobs().isEmpty());
        QVERIFY(reopened.latestImage().isEmpty());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
    }

    void livePreviewsAreOrderedAndNeverBecomeFinalArtifacts_data()
    {
        QTest::addColumn<QString>("prompt");
        QTest::addColumn<QString>("expectedState");
        QTest::newRow("completed") << "live" << "completed";
        QTest::newRow("malformed events") << "live-invalid" << "completed";
        QTest::newRow("failed after previews") << "live-fail" << "failed";
        QTest::newRow("previews without final image") << "live-empty" << "failed";
    }

    void livePreviewsAreOrderedAndNeverBecomeFinalArtifacts()
    {
        QFETCH(QString, prompt);
        QFETCH(QString, expectedState);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/live-generation-XXXXXX");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.steps = 3;
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        QList<int> steps;
        QList<QUrl> images;
        connect(&controller, &GenerationController::previewChanged, this, [&] {
            if (controller.previewImage().isEmpty()) return;
            QVERIFY(controller.busy());
            QVERIFY(controller.latestImage().isEmpty());
            QCOMPARE(controller.previewTotalSteps(), 3);
            steps.append(controller.previewStep());
            images.append(controller.previewImage());
            const QImage image(controller.previewImage().toLocalFile());
            QCOMPARE(image.pixelColor(0, 0).red(), controller.previewStep() * 60);
        });
        const auto id = controller.enqueue(prompt);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), expectedState, 10000);
        QCOMPARE(steps, QList<int>({1, 2, 3}));
        QCOMPARE(QSet<QUrl>(images.cbegin(), images.cend()).size(), 3);
        QVERIFY(controller.previewImage().isEmpty());
        QCOMPARE(controller.previewStep(), 0);
        QCOMPARE(controller.previewTotalSteps(), 0);
        for (const auto &image : images) QVERIFY(!QFileInfo::exists(image.toLocalFile()));
        QCOMPARE(controller.latestImage().isEmpty(), expectedState != "completed");
        QCOMPARE(recordedJob(controller, id)
                     .value("previewSteps").toInt(), 3);
    }

    void cancellationClearsLivePreviewBeforeNextJob()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/cancel-preview-XXXXXX");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto first = controller.enqueue("live-hold");
        QTRY_COMPARE(controller.previewStep(), 1);
        const auto preview = controller.previewImage();
        QVERIFY(controller.cancel(first));
        QTRY_COMPARE(state(controller, first), QString("cancelled"));
        QVERIFY(controller.previewImage().isEmpty());
        QVERIFY(!QFileInfo::exists(preview.toLocalFile()));
        const auto next = controller.enqueue("next");
        QTRY_COMPARE(state(controller, next), QString("completed"));
        QCOMPARE(controller.previewStep(), 0);
    }

    void sharedModelsAreCapturedAtSubmissionAndOutputsStayPrivate()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/shared-generation-XXXXXX");
        QVERIFY(prepare(root));
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("settings.json").toUtf8());
        qunsetenv("SOCIETY_CONTAINER_PATH");
        QVERIFY(SharedStorage::setDefaultContainer(root.path()));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage());
        QCOMPARE(controller.models().size(), 2);
        QCOMPARE(controller.containerPath(), root.path());
        controller.setSelectedModel("first model.safetensor");
        const auto literal = QStringLiteral("slow moon\n$(this is literal prompt text)");
        const auto first = controller.enqueue(literal, "4:3");
        controller.setSelectedModel("second.SAFETENSORS");
        const auto second = controller.enqueue("checkpoint");
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, first), QString("completed"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, second), QString("completed"), 10000);
        const auto firstOutput = recordedJob(controller, first).value("generation").toObject();
        QCOMPARE(firstOutput.value("model_path").toString(), root.filePath("Models/first model.safetensor"));
        QCOMPARE(firstOutput.value("prompt").toString(), literal);
        QCOMPARE(firstOutput.value("width").toInt(), 88);
        QCOMPARE(firstOutput.value("height").toInt(), 64);
        for (const auto &key : {"work_dir", "cache_dir", "output_dir", "preview_dir"}) {
            const auto path = firstOutput.value(key).toString();
            QVERIFY(!path.isEmpty());
            QVERIFY(!path.startsWith(root.path() + '/'));
            QVERIFY(!QFileInfo::exists(path));
        }
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
        QCOMPARE(firstOutput.value("backend").toString(), QString("local"));
        QVERIFY(!firstOutput.contains("startup_timeout"));
        const auto secondOutput = recordedJob(controller, second).value("generation").toObject();
        QCOMPARE(firstOutput.value("worker_pid"), secondOutput.value("worker_pid"));
        QCOMPARE(firstOutput.value("request_count").toInt(), 1);
        QCOMPARE(secondOutput.value("request_count").toInt(), 2);
        QVERIFY(firstOutput.value("python_cache_prefix").isNull());
        QCOMPARE(firstOutput.value("dont_write_bytecode").toString(), QString("1"));
        QCOMPARE(secondOutput.value("model_path").toString(), root.filePath("Models/second.SAFETENSORS"));
        const auto request = recordedJob(controller, first);
        QCOMPARE(request.value("model").toObject().value("containerId").toString(), SharedStorage::open()->drive().identifier());
        QVERIFY(QDir(root.filePath("Files")).isEmpty());
        QVERIFY(QDir(root.filePath("Asset Library")).isEmpty());
        const auto history = QDir(root.filePath("Generation History")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        QCOMPARE(history.size(), 2);
        QVERIFY(history.contains(first + "-0001.png"));
        QVERIFY(history.contains(second + "-0001.png"));
        QVERIFY(QFileInfo::exists(root.filePath("Models/first model.safetensor")));
        QCOMPARE(controller.latestImage().toLocalFile(), root.filePath("Generation History/" + second + "-0001.png"));
        const auto latest = controller.latestResult();
        QCOMPARE(latest.value("imageSource").toUrl(), controller.latestImage());
        QCOMPARE(latest.value("id").toString(), second);
        QCOMPARE(latest.value("prompt").toString(), "checkpoint");
        QCOMPARE(latest.value("aspectRatio").toString(), "1:1");
        QCOMPARE(latest.value("model").toMap().value("path").toString(), "second.SAFETENSORS");
        QVERIFY(request.value("finishedAt").toString() <= recordedJob(controller, second).value("startedAt").toString());
    }

    void changedModelsAreRejectedInTheCurrentMemoryQueue()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/memory-model-XXXXXX");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto good = controller.enqueue("keep original model");
        const auto cancelled = controller.enqueue("cancel before execution");
        QVERIFY(controller.cancel(cancelled));
        controller.setSelectedModel("second.SAFETENSORS");
        const auto changed = controller.enqueue("must use original model");
        QVERIFY(write(root.filePath("Models/second.SAFETENSORS"), "replacement model"));
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, good), QString("completed"), 10000);
        QTRY_COMPARE(state(controller, changed), QString("failed"));
        QCOMPARE(state(controller, cancelled), QString("cancelled"));
        QVERIFY(QDir(root.filePath("Asset Library")).isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath("Generation History/" + changed + "-0001.png")));
    }

    void failuresCannotBecomeSuccessfulImages_data()
    {
        QTest::addColumn<QString>("prompt");
        QTest::newRow("nonzero exit") << "fail";
        QTest::newRow("zero exit without image") << "empty";
        QTest::newRow("image dimensions do not match request") << "wrong-size";
        QTest::newRow("invalid second image leaves no partial history") << "mixed-invalid";
    }
    void failuresCannotBecomeSuccessfulImages()
    {
        QFETCH(QString, prompt);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/failed-generation-XXXXXX");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto id = controller.enqueue(prompt);
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), QString("failed"), 10000);
        QVERIFY(controller.latestImage().isEmpty());
        QVERIFY(controller.latestResult().isEmpty());
        QVERIFY(!controller.errorString().isEmpty());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
        QVERIFY(QDir(root.filePath("Asset Library")).isEmpty());
        QVERIFY(!controller.enqueue(" ").size());
        QVERIFY(controller.enqueue("valid", "unsupported").isEmpty());
    }

    void cancellationReleasesTheWorkerForTheNextJob()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/cancel-generation-XXXXXX");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto first = controller.enqueue("hold");
        QTRY_VERIFY(controller.busy());
        QTest::qWait(100);
        GenerationController secondWindow(fakeRuntime());
        QVERIFY(secondWindow.connectStorage(root.path()));
        QTest::qWait(100);
        QVERIFY(!secondWindow.busy());
        QVERIFY(secondWindow.jobs().isEmpty());
        const auto independent = secondWindow.enqueue("independent app session");
        QTRY_COMPARE(state(secondWindow, independent), QString("completed"));
        QCOMPARE(state(controller, first), QString("running"));
        QVERIFY(controller.cancel(first));
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, first), QString("cancelled"), 5000);
        const auto next = controller.enqueue("next");
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, next), QString("completed"), 10000);
    }

    void workerFailureAndCrashDoNotBlockTheNextRequest_data()
    {
        QTest::addColumn<QString>("failure");
        QTest::newRow("request fails") << "fail";
        QTest::newRow("worker crashes") << "crash";
        QTest::newRow("fragmented unicode error") << "long-error";
    }

    void workerFailureAndCrashDoNotBlockTheNextRequest()
    {
        QFETCH(QString, failure);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/worker-recovery-XXXXXX");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto first = controller.enqueue("first");
        const auto failed = controller.enqueue(failure);
        const auto next = controller.enqueue("next");
        QTRY_COMPARE(state(controller, first), QString("completed"));
        QTRY_COMPARE(state(controller, failed), QString("failed"));
        QTRY_COMPARE(state(controller, next), QString("completed"));
        const auto before = recordedJob(controller, first).value("generation").toObject();
        const auto after = recordedJob(controller, next).value("generation").toObject();
        QCOMPARE(before.value("worker_pid") == after.value("worker_pid"), failure != "crash");
        QCOMPARE(after.value("request_count").toInt(), failure != "crash" ? 3 : 1);
        if (failure == "long-error")
            QCOMPARE(recordedJob(controller, failed).value("error").toString(), QString("오류").repeated(2000));
        QCOMPARE(QDir(root.filePath("Generation History")).entryList(QDir::Files).size(), 2);
    }

    void redirectedOutputCannotEscapeSociety()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/redirect-generation-XXXXXX");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.executable.clear();
        QString id;
        {
            GenerationController queued(runtime);
            QVERIFY(queued.connectStorage(root.path()));
            id = queued.enqueue("outside must remain empty");
        }
        QVERIFY(QDir().rmdir(root.filePath("Generation History")));
        QVERIFY(QFile::link(root.filePath("Files"), root.filePath("Generation History")));
        GenerationController controller(fakeRuntime());
        QVERIFY(!controller.connectStorage(root.path()));
        QVERIFY(QDir(root.filePath("Files")).isEmpty());
    }

    void allImagesShareOneFlatHistoryAndAssetsAreUntouched()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/flat-history-XXXXXX");
        QVERIFY(prepare(root));
        const auto asset = root.filePath("Asset Library/existing.png");
        QVERIFY(write(asset, "an existing asset must remain unchanged"));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
        const auto first = controller.enqueue("multiple");
        const auto second = controller.enqueue("multiple");
        QTRY_COMPARE_WITH_TIMEOUT(state(controller, second), QString("completed"), 10000);
        QCOMPARE(state(controller, first), QString("completed"));
        const QDir history(root.filePath("Generation History"));
        QCOMPARE(history.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden).size(), 0);
        QCOMPARE(history.entryList(QDir::Files | QDir::Hidden).size(), 4);
        for (const auto &id : {first, second}) {
            for (int index = 1; index <= 2; ++index) {
                const QImage image(history.filePath(id + QString("-%1.png").arg(index, 4, 10, QLatin1Char('0'))));
                QCOMPARE(image.size(), QSize(64, 64));
            }
            const auto request = recordedJob(controller, id);
            QCOMPARE(request.value("images").toArray().size(), 2);
        }
        QCOMPARE(QDir(root.filePath("Asset Library")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), QStringList{"existing.png"});
        QFile preserved(asset);
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), QByteArray("an existing asset must remain unchanged"));
        GenerationController reopened(fakeRuntime());
        QVERIFY(reopened.connectStorage(root.path()));
        QVERIFY(reopened.latestImage().isEmpty());
        QVERIFY(reopened.jobs().isEmpty());
        QCOMPARE(history.entryList(QDir::Files | QDir::Hidden).size(), 4);
    }

    void historyNameCollisionPreservesExistingImage()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/history-collision-XXXXXX");
        QVERIFY(prepare(root));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        const auto id = controller.enqueue("collision");
        const auto existing = root.filePath("Generation History/" + id + "-0001.png");
        QVERIFY(write(existing, "preserve existing bytes"));
        QTRY_COMPARE(state(controller, id), QString("failed"));
        QFile preserved(existing);
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), QByteArray("preserve existing bytes"));
        QVERIFY(controller.latestImage().isEmpty());
        QVERIFY(QDir(root.filePath("Asset Library")).isEmpty());
    }

    void legacyQueuesAreDiscardedWhileCompletedImagesArePreserved()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/legacy-memory-XXXXXX");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.executable.clear();
        GenerationController offline(runtime);
        QVERIFY(offline.connectStorage(root.path()));
        const auto completed = offline.enqueue("old completed image");
        const auto queued = offline.enqueue("do not resume this old queue");
        auto job = recordedJob(offline, completed);
        const auto oldOutput = "Asset Library/Dreamscapes/" + completed;
        QVERIFY(QDir().mkpath(root.filePath(oldOutput)));
        QImage generated(64, 64, QImage::Format_RGB32);
        generated.fill(Qt::blue);
        QVERIFY(generated.save(root.filePath(oldOutput + "/image-0001.png")));
        QVERIFY(write(root.filePath(oldOutput + "/generation.json"), "{\"backend\":\"diffusers\"}"));
        job["state"] = "completed";
        job["output"] = oldOutput;
        job["image"] = oldOutput + "/image-0001.png";
        const auto legacy = root.filePath("Generation History/Dreamscapes");
        QVERIFY(QDir().mkpath(legacy + '/' + completed));
        QVERIFY(QDir().mkpath(legacy + '/' + queued));
        QVERIFY(write(legacy + '/' + completed + "/request.json", QJsonDocument(job).toJson()));
        QVERIFY(write(legacy + '/' + queued + "/request.json", QJsonDocument(recordedJob(offline, queued)).toJson()));
        QVERIFY(write(root.filePath("Asset Library/preserved.asset"), "keep this asset"));
        GenerationController migrated(fakeRuntime());
        QVERIFY2(migrated.connectStorage(root.path()), qPrintable(migrated.errorString()));
        QVERIFY(migrated.jobs().isEmpty());
        QVERIFY(migrated.latestImage().isEmpty());
        QTest::qWait(100);
        QVERIFY(!migrated.busy());
        const QDir history(root.filePath("Generation History"));
        QCOMPARE(history.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).size(), 1);
        QCOMPARE(QImage(root.filePath("Generation History/" + completed + "-0001.png")), generated);
        QCOMPARE(QDir(root.filePath("Asset Library")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), QStringList{"preserved.asset"});
        QVERIFY(!QFileInfo::exists(legacy));
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
    }

    void obsoleteWorkingFilesAreRemovedWithoutFollowingModelLinks()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/obsolete-working-XXXXXX");
        QVERIFY(prepare(root));
        const auto legacy = root.filePath(".dreamscapes/generation");
        QVERIFY(QDir().mkpath(legacy + "/Runtime Cache"));
        QVERIFY(write(legacy + "/request.json", "{\"state\":\"queued\"}"));
        QVERIFY(write(legacy + "/Runtime Cache/temporary.bin", "disposable"));
        QVERIFY(QFile::link(root.filePath("Models"), legacy + "/Runtime Cache/model-link"));
        QImage completed(16, 16, QImage::Format_RGB32);
        completed.fill(Qt::red);
        QVERIFY(completed.save(root.filePath("Generation History/existing.png")));
        GenerationController controller(fakeRuntime());
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(controller.jobs().isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
        QVERIFY(QFileInfo::exists(root.filePath("Models/first model.safetensor")));
        QVERIFY(QFileInfo::exists(root.filePath("Models/second.SAFETENSORS")));
        QCOMPARE(QImage(root.filePath("Generation History/existing.png")), completed);
    }

    void redirectedLegacyStorageIsPreserved()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/redirected-legacy-XXXXXX");
        QTemporaryDir outside(DREAMSCAPES_TEST_DIRECTORY "/preserved-legacy-XXXXXX");
        QVERIFY(prepare(root));
        QVERIFY(outside.isValid());
        QVERIFY(QDir().mkpath(outside.filePath("generation")));
        QVERIFY(write(outside.filePath("generation/preserved.txt"), "preserve foreign data"));
        QVERIFY(QFile::link(outside.path(), root.filePath(".dreamscapes")));
        GenerationController controller(fakeRuntime());
        QVERIFY(!controller.connectStorage(root.path()));
        QVERIFY(QFileInfo(root.filePath(".dreamscapes")).isSymLink());
        QVERIFY(QFileInfo::exists(outside.filePath("generation/preserved.txt")));
    }

    void obsoleteFilesInUseArePreserved()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/obsolete-busy-XXXXXX");
        QVERIFY(prepare(root));
        const auto legacy = root.filePath(".dreamscapes/generation");
        QVERIFY(QDir().mkpath(legacy));
        QVERIFY(write(legacy + "/request.json", "still in use"));
        QLockFile previous(legacy + "/.worker.lock");
        QVERIFY(previous.tryLock());
        GenerationController controller(fakeRuntime());
        QVERIFY(!controller.connectStorage(root.path()));
        QVERIFY(QFileInfo::exists(legacy + "/request.json"));
        previous.unlock();
        QVERIFY(controller.connectStorage(root.path()));
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
    }

    void temporaryFilesAreRemovedAtEveryJobExit_data()
    {
        QTest::addColumn<QString>("prompt");
        QTest::addColumn<QString>("expected");
        QTest::newRow("completed") << "live" << "completed";
        QTest::newRow("failed") << "live-fail" << "failed";
        QTest::newRow("cancelled") << "live-hold" << "cancelled";
        QTest::newRow("app closes") << "live-hold" << "closed";
    }

    void temporaryFilesAreRemovedAtEveryJobExit()
    {
        QFETCH(QString, prompt);
        QFETCH(QString, expected);
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/session-society-XXXXXX");
        QTemporaryDir temporary(DREAMSCAPES_TEST_DIRECTORY "/session-temporary-XXXXXX");
        QVERIFY(prepare(root));
        QVERIFY(temporary.isValid());
        const auto original = QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        auto runtime = fakeRuntime();
        runtime.temporaryDirectory = temporary.path();
        runtime.steps = 3;
        {
            GenerationController controller(runtime);
            QVERIFY(controller.connectStorage(root.path()));
            const auto id = controller.enqueue(prompt);
            QTRY_VERIFY(!controller.previewImage().isEmpty());
            auto jobDirectory = QFileInfo(controller.previewImage().toLocalFile()).dir();
            QVERIFY(jobDirectory.cdUp());
            QVERIFY(controller.previewImage().toLocalFile().startsWith(temporary.path() + '/'));
            QCOMPARE(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden), original);
            QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
            if (expected == "cancelled") QVERIFY(controller.cancel(id));
            if (expected != "closed") {
                QTRY_COMPARE_WITH_TIMEOUT(state(controller, id), expected, 10000);
                QVERIFY(!QFileInfo::exists(jobDirectory.path()));
                for (const auto &name : QDir(temporary.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
                    QVERIFY(name.startsWith("dreamscapes-inference-"));
            }
        }
        QVERIFY(QDir(temporary.path()).isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath(".dreamscapes")));
        QVERIFY(QDir(root.filePath("Asset Library")).isEmpty());
        QCOMPARE(QDir(root.filePath("Generation History")).entryList(QDir::Files | QDir::Hidden).size(), expected == "completed" ? 1 : 0);
        GenerationController reopened(runtime);
        QVERIFY(reopened.connectStorage(root.path()));
        QVERIFY(reopened.jobs().isEmpty());
    }

    void temporaryFilesCannotBePlacedInSociety()
    {
        QTemporaryDir root(DREAMSCAPES_TEST_DIRECTORY "/invalid-temporary-XXXXXX");
        QVERIFY(prepare(root));
        auto runtime = fakeRuntime();
        runtime.temporaryDirectory = root.filePath("Files");
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root.path()));
        const auto id = controller.enqueue("must not create Society work files");
        QTRY_COMPARE(state(controller, id), QString("failed"));
        QVERIFY(QDir(root.filePath("Files")).isEmpty());
        QVERIFY(QDir(root.filePath("Generation History")).isEmpty());
    }

    void realForegroundPreparation()
    {
        const auto root = qEnvironmentVariable("DREAMSCAPES_REAL_SMOKE_CONTAINER");
        if (root.isEmpty()) QSKIP("Opt-in foreground preparation requires a local Society model fixture.");
        GenerationRuntime runtime{qEnvironmentVariable("IILD_GENERATOR_EXECUTABLE"), "auto", 1, 64, {}, QStringLiteral(DREAMSCAPES_TEST_DIRECTORY)};
        GenerationController controller(runtime);
        QVERIFY(controller.connectStorage(root));
        const auto before = QDir(root + "/Generation History").entryList(QDir::Files);
        controller.setForeground(true);
        QTRY_VERIFY_WITH_TIMEOUT(controller.inferenceStatus().value("ready").toBool()
                                || controller.inferenceStatus().value("state") == "error", 180000);
        QVERIFY2(controller.inferenceStatus().value("ready").toBool(),
                 qPrintable(controller.inferenceStatus().value("error").toString()));
        QVERIFY(controller.inferenceStatus().value("gpu_resident").toBool());
        QCOMPARE(QDir(root + "/Generation History").entryList(QDir::Files), before);
        QVERIFY(controller.jobs().isEmpty());
        const auto pid = controller.inferenceStatus().value("pid");
        const auto id = controller.enqueue("Use the model already waiting on the GPU");
        QTRY_VERIFY_WITH_TIMEOUT(state(controller, id) == "completed" || state(controller, id) == "failed", 180000);
        QCOMPARE(state(controller, id), QString("completed"));
        const auto worker = recordedJob(controller, id).value("worker").toObject();
        QCOMPARE(worker.value("pid").toVariant(), pid);
        for (const auto &key : {"model_hashes", "configuration_reads", "pipeline_loads", "device_placements"})
            QCOMPARE(worker.value("cache").toObject().value(key).toInt(-1), 0);
        qInfo().noquote() << "Foreground GPU residency:" << QJsonDocument(worker).toJson(QJsonDocument::Compact);
    }

    void realSocietyInference()
    {
        const auto root = qEnvironmentVariable("DREAMSCAPES_REAL_SMOKE_CONTAINER");
        if (root.isEmpty()) QSKIP("Opt-in real inference requires a prepared Society model fixture.");
        GenerationRuntime runtime{qEnvironmentVariable("IILD_GENERATOR_EXECUTABLE"), "cpu", 1, 64, {}, QStringLiteral(DREAMSCAPES_TEST_DIRECTORY)};
        QVERIFY(!runtime.executable.isEmpty());
        GenerationController controller(runtime);
        QVERIFY2(controller.connectStorage(root), qPrintable(controller.errorString()));
        QVERIFY(!controller.models().isEmpty());
        const auto id = controller.enqueue("Society shared storage neural inference verification");
        QVERIFY2(!id.isEmpty(), qPrintable(controller.errorString()));
        QTRY_VERIFY_WITH_TIMEOUT(state(controller, id) == "completed" || state(controller, id) == "failed", 180000);
        QCOMPARE(state(controller, id), QString("completed"));
        QVERIFY(!controller.latestImage().isEmpty());
        const auto second = controller.enqueue("A second fresh prompt using the same Society model");
        QTRY_VERIFY_WITH_TIMEOUT(state(controller, second) == "completed" || state(controller, second) == "failed", 180000);
        QCOMPARE(state(controller, second), QString("completed"));
        const auto firstWorker = recordedJob(controller, id).value("worker").toObject();
        const auto secondWorker = recordedJob(controller, second).value("worker").toObject();
        QCOMPARE(firstWorker.value("pid"), secondWorker.value("pid"));
        QCOMPARE(firstWorker.value("cache").toObject().value("pipeline_loads").toInt(-1), 1);
        QCOMPARE(firstWorker.value("cache").toObject().value("device_placements").toInt(-1), 1);
        QCOMPARE(secondWorker.value("cache").toObject().value("model_hashes").toInt(-1), 0);
        QCOMPARE(secondWorker.value("cache").toObject().value("pipeline_hits").toInt(-1), 1);
        QCOMPARE(secondWorker.value("cache").toObject().value("pipeline_loads").toInt(-1), 0);
        QCOMPARE(secondWorker.value("cache").toObject().value("configuration_reads").toInt(-1), 0);
        QCOMPARE(secondWorker.value("cache").toObject().value("device_placements").toInt(-1), 0);
        QCOMPARE(secondWorker.value("cache").toObject().value("device_placement_hits").toInt(-1), 1);
        qInfo().noquote() << "Society inference output:" << controller.latestImage().toLocalFile();
    }
};
QTEST_GUILESS_MAIN(GenerationTests)
#include "tst_Generation.moc"
