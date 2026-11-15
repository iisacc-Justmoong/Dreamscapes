#include "App/Views/Result/NativeReviewQueueBridge.h"
#include "App/ViewModels/GenerationProjection.h"
#include <QCoreApplication>
#include <QUrl>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
QVariantMap job(QString id, QString state) {
    return {{"id", std::move(id)}, {"state", std::move(state)}, {"mediaType", "Image"}, {"modelName", "Model"}};
}
void exercise() {
    {
        dreamscapes::GenerationProjection source;
        int titleFormats = 0;
        dreamscapes::review::QueueStrings strings;
        strings.jobTitle = [&titleFormats](std::string_view id) { ++titleFormats; return std::string(id); };
        auto model = dreamscapes::review::QueueModel::create(std::make_shared<dreamscapes::review::Session>(), std::move(strings));
        dreamscapes::review::NativeReviewQueueBridge bridge(&source, model);
        const auto first = QUrl::fromLocalFile("/tmp/first preview.png");
        source.applySourceInputs({{"jobs", QVariantList{job("first", "running")}},
                                 {"previewJobId", "first"}, {"previewImage", first}});
        check(model->previewSource.get() == first.toString(QUrl::FullyEncoded).toStdString(),
              "an atomic generation snapshot did not deliver its live preview");
        titleFormats = 0;
        source.applySourceInputs({{"previewImage", QUrl::fromLocalFile("/tmp/first revised.png")}});
        check(titleFormats == 0, "a preview-only update reformatted every queue row");
        std::vector<std::string> observed;
        auto subscription = model->previewSource.observe([&](const auto &value, std::uint64_t) { observed.push_back(value); });
        const auto second = QUrl::fromLocalFile("/tmp/second preview.png");
        source.applySourceInputs({{"jobs", QVariantList{job("second", "running")}},
                                 {"previewJobId", "second"}, {"previewImage", second}});
        check(observed == std::vector<std::string>{second.toString(QUrl::FullyEncoded).toStdString()} &&
              model->items.get().size() == 1 && model->items.get()[0]->id == "second",
              "switching jobs exposed an intermediate preview from a different generation");
    }
    dreamscapes::GenerationProjection projection;
    projection.setJobs({job("one", "running"), job("two", "queued")});
    projection.setInferenceStatus({{"state", "loading"}, {"step", 1}, {"total", 5}});
    auto session = std::make_shared<dreamscapes::review::Session>();
    auto queue = dreamscapes::review::QueueModel::create(session);
    QString canceled, inspected;
    QStringList selected;
    {
        dreamscapes::review::NativeReviewQueueBridge bridge(&projection, queue,
            {.cancel = [&](const auto &id) { canceled = id; }, .failure = [&](const auto &id) { inspected = id; },
             .selection = [&](const auto &id) { selected.append(id); }});
        auto row = queue->items.get().at(0);
        check(queue->running.get() == 1 && row->showProgress.get() && row->step.get() == 1,
              "initial generation data did not reach the queue");
        const auto preview = QUrl::fromLocalFile("/tmp/live preview.png");
        bridge.setPreview("one", preview);
        check(queue->previewSource.get() == preview.toString(QUrl::FullyEncoded).toStdString(),
              "the active preview URL was not retained");
        bridge.setPreview("other", preview);
        check(queue->previewSource.get().empty(), "another job's preview was displayed");
        row->cancel();
        check(canceled == "one", "cancel did not retain the actual job ID");
        row->select(); queue->showAllJobs();
        check(selected == QStringList({"one", ""}), "queue selection did not reach the host with its job ID");
        projection.setJobs({job("one", "failed"), job("two", "queued")});
        check(queue->items.get()[0] == row && !row->canCancel.get() && row->canInspectFailure.get(),
              "a state update replaced the row or kept the wrong actions");
        row->inspectFailure();
        check(inspected == "one", "failure details did not retain the actual job ID");
        projection.setJobs({job("one", "failed"), job("one", "queued")});
        check(!bridge.lastError().isEmpty() && queue->items.get()[0] == row && queue->items.get().size() == 2,
              "an invalid snapshot damaged the previous queue or lost its error");
        inspected.clear(); row->inspectFailure();
        check(inspected.isEmpty() && !row->canInspectFailure.get(),
              "a rejected snapshot left a stale job command available");
        row->select();
        check(selected == QStringList({"one", ""}) && session->jobFilter().empty() && !queue->selectionEnabled.get(),
              "a rejected snapshot accepted a stale queue activation");
        projection.setJobs({job("two", "queued")});
        check(bridge.lastError().isEmpty() && queue->items.get().size() == 1 && queue->items.get()[0]->canCancel.get() &&
              queue->selectionEnabled.get(),
              "a valid update did not recover the bridge");
    }
    check(!queue->items.get()[0]->canCancel.get(), "a detached bridge left cancellation enabled");
    queue->items.get()[0]->select();
    check(selected == QStringList({"one", ""}) && session->jobFilter().empty() && !queue->selectionEnabled.get(),
          "a detached bridge accepted a queue activation");
    projection.setJobs({job("three", "queued")});
    check(queue->items.get()[0]->id == "two", "a detached bridge still updated the queue");
    auto source = std::make_unique<dreamscapes::GenerationProjection>();
    source->setJobs({job("four", "queued")});
    auto model = dreamscapes::review::QueueModel::create(std::make_shared<dreamscapes::review::Session>());
    dreamscapes::review::NativeReviewQueueBridge bridge(source.get(), model,
        {.cancel = [&](const auto &id) { canceled = id; }});
    check(model->items.get()[0]->canCancel.get(), "a configured command was not enabled");
    source.reset();
    check(!model->items.get()[0]->canCancel.get(), "a destroyed source left a stale command enabled");
    bridge.detach();
    dreamscapes::GenerationProjection invalid;
    invalid.setJobs({job("duplicate", "queued"), job("duplicate", "running")});
    auto initial = dreamscapes::review::QueueModel::create(std::make_shared<dreamscapes::review::Session>());
    dreamscapes::review::NativeReviewQueueBridge rejected(&invalid, initial);
    check(!rejected.lastError().isEmpty() && !initial->selectionEnabled.get(),
          "an initially invalid snapshot left queue selection enabled");
    invalid.setJobs({job("valid", "queued")});
    check(rejected.lastError().isEmpty() && initial->selectionEnabled.get(),
          "an initially invalid source did not restore selection after recovery");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try { exercise(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Native queue bridge follows the generation projection and retires its commands\n";
}
