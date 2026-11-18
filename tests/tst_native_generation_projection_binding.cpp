#include "App/Generation/GenerationController.h"
#include "App/ViewModels/GenerationProjection.h"
#include "App/ViewModels/GenerationProjectionBinding.h"
#include "App/ViewModels/WorkspaceSession.h"
#include "App/Views/Home/NativeHomeQueueBridge.h"
#include "App/Views/Home/NativeHomeQueueSession.h"
#include "App/Views/Result/GenerationReviewModel.h"
#include "App/Views/Result/NativeReviewQueueBridge.h"
#include <iiSocietyContainer>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void exercise() {
    using namespace dreamscapes;
    QTemporaryDir root(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/projection-binding-XXXXXX.societycontainer"));
    check(root.isValid(), "temporary Society fixture unavailable");
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("settings.ini").toUtf8());
    qunsetenv("SOCIETY_CONTAINER_PATH");
    check(iiSocietyContainer::SocietyDrive::create(root.path()).has_value(), "fixture creation failed");
    QFile weight(root.filePath("Models/example.safetensors"));
    check(weight.open(QIODevice::WriteOnly) && weight.write("test weight") == 11, "fixture weight unavailable");
    weight.close();
    GenerationRuntime runtime;
    runtime.nativeInference = true;
    runtime.imageExtent = 64;
    runtime.steps = 1;
    runtime.modelPreferencesFile = root.filePath("models.conf");
    runtime.failureReportsDirectory = root.path();
    runtime.inspectImageModel = [](const auto &) { return iiLocalDiffusion::ImageModelInspection{}; };
    auto first = std::make_unique<GenerationController>(runtime);
    first->setForeground(false);
    WorkspaceSession session;
    session.setGeneration(first.get());
    auto nativeHome = std::make_unique<home::NativeHomeQueueSession>(&session, home::HomeQueueStrings{},
        [&] { session.openGenerationReview(); });
    auto nativeHomeModel = nativeHome->model();
    nativeHome->setRuntimeNames("Selected model", "Automatic");
    check(nativeHomeModel->model.get() == "Selected model" && nativeHomeModel->canOpenQueue.get(),
          "native Home session did not receive names and navigation");
    const QVariantMap current{{"id", "manual"}, {"imageSource", QUrl("file:///a%20b.png")},
                              {"opaque", QVariantMap{{"url", QUrl("file:///original.iisc")}}}};
    session.setCurrentResult(current);
    session.setResultJobIds({"existing"});
    GenerationProjection projection;
    projection.setSelectedResultId("selection-owned-by-view");
    int inputChanges = 0;
    QObject::connect(&projection, &GenerationProjection::inputsChanged, &projection, [&] { ++inputChanges; });
    GenerationProjectionBinding binding(&session, &projection);
    check(inputChanges == 1, "initial state was not applied as one snapshot");
    check(projection.currentResult() == current && projection.requestIds() == QStringList{"existing"},
          "session values or opaque metadata lost");
    check(!projection.connected() && projection.selectedResultId() == "selection-owned-by-view",
          "source state or view-owned selection was overwritten");
    check(projection.metaObject()->indexOfProperty("previewImage") >= 0 &&
          projection.metaObject()->indexOfProperty("previewJobId") >= 0,
          "the native projection does not expose preview identity and address");
    projection.applySourceInputs({{"previewImage", QUrl("file:///stale.png")}, {"previewJobId", "stale"}});
    first->previewChanged();
    check(projection.previewImage().isEmpty() && projection.previewJobId().isEmpty(),
          "the controller's preview notification did not replace stale preview identity and address");
    auto queue = std::make_shared<home::HomeQueueModel>();
    home::NativeHomeQueueBridge home(&projection, queue, {});
    GenerationReviewModel reviewModel;
    auto reviewQueue = review::QueueModel::create(reviewModel.nativeSession());
    QString selectedJob, canceledJob;
    review::NativeReviewQueueBridge reviewBridge(&projection, reviewQueue,
        {.cancel = [&](const QString &id) {
             canceledJob = id;
             if (auto *controller = qobject_cast<GenerationController *>(session.generation()))
                 (void)controller->cancel(id);
         },
         .selection = [&](const QString &id) { selectedJob = id; }});
    check(first->connectStorage(root.path()), "failed to connect isolated storage");
    check(projection.connected(), "storage notification did not update the native projection");
    const auto job = first->enqueue("fixture", "1:1", 2, 7);
    check(!job.isEmpty(), "fake queue submission failed");
    const auto jobs = first->jobs();
    check(jobs.size() == 2 && projection.jobs() == jobs, "real controller jobs did not reach the projection");
    check(queue->summary.get() == "0 running · 2 waiting · 0 failed", "jobs did not reach the native Home model");
    check(nativeHomeModel->summary.get() == "0 running · 2 waiting · 0 failed",
          "controller jobs did not reach a QML-free Home session");
    nativeHomeModel->openQueue();
    check(session.resultVisible(), "native Home navigation did not open the product review session");
    nativeHome.reset();
    check(!nativeHomeModel->canOpenQueue.get(), "retired Home session left navigation enabled");
    check(reviewQueue->items.get().size() == 2, "real controller jobs did not reach the native review queue");
    const auto selected = reviewQueue->items.get().front();
    selected->select();
    check(selectedJob == QString::fromStdString(selected->id) && reviewModel.jobFilter() == selectedJob,
          "native queue activation did not reach the product review session and host");
    reviewQueue->showAllJobs();
    check(selectedJob.isEmpty() && reviewModel.jobFilter().isEmpty(), "All jobs did not reach the product review session");
    session.setResultJobIds({job});
    check(projection.requestIds() == QStringList{job} && projection.submissionJobs().size() == 1 &&
          projection.generationPending(), "session request identifiers did not select the real queued job");
    const auto canceled = reviewQueue->items.get().back();
    canceled->cancel();
    check(canceledJob == QString::fromStdString(canceled->id) && !canceled->canCancel.get() &&
          reviewQueue->canceled.get() == 1, "native cancellation did not reach the real stopped generation queue");
    auto second = std::make_unique<GenerationController>(runtime);
    second->setForeground(false);
    projection.applySourceInputs({{"previewImage", QUrl("file:///old-controller.png")}, {"previewJobId", "old"}});
    const auto before = inputChanges;
    session.setGeneration(second.get());
    check(inputChanges == before + 1 && projection.jobs().isEmpty() && !projection.connected() &&
          projection.previewImage().isEmpty() && projection.previewJobId().isEmpty(),
          "controller replacement exposed a partial or stale snapshot");
    first->jobsChanged();
    check(inputChanges == before + 1 && projection.jobs().isEmpty(), "retired controller remained connected");
    first.reset();
    check(projection.requestIds() == QStringList{job}, "retired controller destroyed session state");
    second.reset();
    check(projection.jobs().isEmpty() && !projection.connected() && projection.currentResult() == current &&
          projection.requestIds() == QStringList{job}, "controller destruction damaged retained session state");
    bool rejected = false;
    std::thread other([&] {
        try { GenerationProjectionBinding invalid(&session, &projection); }
        catch (const std::logic_error &) { rejected = true; }
    });
    other.join();
    check(rejected, "foreign owner thread was accepted");
    auto retiringSession = std::make_unique<WorkspaceSession>();
    auto retiringProjection = std::make_unique<GenerationProjection>();
    retiringSession->setCurrentResult(current);
    GenerationProjectionBinding retiring(retiringSession.get(), retiringProjection.get());
    retiringSession.reset();
    check(retiringProjection->currentResult().isEmpty(), "session destruction left stale source values");
    retiringProjection.reset();
    auto dyingWorkspace = std::make_unique<WorkspaceSession>();
    int staleNavigation = 0;
    auto survivingHome = std::make_unique<home::NativeHomeQueueSession>(dyingWorkspace.get(), home::HomeQueueStrings{},
        [&] { ++staleNavigation; });
    auto survivingModel = survivingHome->model();
    check(survivingModel->canOpenQueue.get(), "Home session did not expose navigation");
    dyingWorkspace.reset();
    check(!survivingModel->canOpenQueue.get(), "destroyed workspace left Home navigation enabled");
    survivingModel->openQueue();
    check(staleNavigation == 0, "destroyed workspace received a stale Home command");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try { exercise(); }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
