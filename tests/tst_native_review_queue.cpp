#include "App/Views/Result/NativeReviewQueue.h"
#include <future>
#include <iostream>
#include <stdexcept>
#include <utility>

using dreamscapes::review::QueueModel;
using dreamscapes::review::QueueSnapshot;
using dreamscapes::review::Session;
using dreamscapes::vm::Array;
using dreamscapes::vm::Object;
using dreamscapes::vm::Value;

namespace {
void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
Value job(std::string id, std::string state, std::string media = "Image") {
    return Value(Object{{"id", Value(std::move(id))}, {"state", Value(std::move(state))},
                        {"mediaType", Value(std::move(media))}, {"modelName", Value("Test model")}});
}
void projectionAndCommands() {
    auto session = std::make_shared<Session>();
    auto queue = QueueModel::create(session);
    require(queue->items.get().empty() && queue->empty.get() && queue->running.get() == 0 &&
            queue->heading.get() == "GENERATION", "A new queue was not empty");
    QueueSnapshot snapshot{.jobs = Value(Array{job("run", "running"), job("wait", "queued"),
                                              job("done", "completed"), job("fail", "failed"),
                                              job("stop", "cancelled")}),
                           .inference = Value(Object{{"state", Value("denoising")},
                                                     {"step", Value(3)}, {"total", Value(10)}}),
                           .previewJobId = "run", .previewSource = "file:///live.png"};
    require(bool(queue->setSnapshot(snapshot)), "A valid queue snapshot was rejected");
    require(!queue->empty.get() && queue->runningSummary.get() == "Running  1" &&
            queue->running.get() == 1 && queue->queued.get() == 1 && queue->completed.get() == 1 &&
            queue->failed.get() == 1 && queue->canceled.get() == 1, "Queue counts do not match the jobs");
    auto rows = queue->items.get();
    require(rows.size() == 5 && rows[0]->id == "run" && rows[1]->id == "wait" &&
            rows[0]->showProgress.get() && rows[0]->step.get() == 3 && rows[0]->total.get() == 10 &&
            !rows[1]->showProgress.get() && queue->previewSource.get() == "file:///live.png",
            "Progress or preview was assigned to the wrong job");
    const auto rowRevision = queue->items.revision();
    require(bool(queue->setPreview("run", "file:///next.png")) && queue->previewSource.get() == "file:///next.png" &&
            queue->items.revision() == rowRevision && queue->items.get()[0] == rows[0],
            "Preview-only updates changed queue rows");
    require(bool(queue->setPreview("another", "file:///wrong.png")) && queue->previewSource.get().empty(),
            "A preview-only update admitted a different job");
    require(!std::async(std::launch::async, [&] { return queue->setPreview("run", "file:///wrong-thread.png"); }).get(),
            "Another thread changed the preview");
    require(!rows[0]->canCancel.get() && !rows[3]->canInspectFailure.get(),
            "An unavailable host command was enabled");
    std::string canceled, inspected;
    std::vector<std::string> requested;
    queue->setCancelHandler([&](const std::string &id) { canceled = id; });
    queue->setFailureHandler([&](const std::string &id) { inspected = id; });
    queue->setSelectionHandler([&](const std::string &id) { requested.push_back(id); });
    require(rows[0]->canCancel.get() && rows[1]->canCancel.get() && !rows[2]->canCancel.get() &&
            rows[3]->canInspectFailure.get(), "Command availability does not follow actual job state");
    rows[0]->cancel(); rows[3]->inspectFailure();
    require(canceled == "run" && inspected == "fail", "A command did not deliver its original job ID");
    session->replaceEntries({Value(Object{{"id", Value("run")}, {"documentSource", Value("file:///run.iisc")}}),
                             Value(Object{{"id", Value("wait")}, {"documentSource", Value("file:///wait.iisc")}})});
    session->selectAt(0);
    rows[1]->select();
    require(session->jobFilter() == "wait" && rows[1]->selected.get() && session->selectedCount() == 1 &&
            session->hiddenSelectedCount() == 1, "Queue filtering lost the review selection");
    rows[1]->select();
    require(requested == std::vector<std::string>({"wait", "wait"}),
            "The host did not receive each queue activation, including the current job");
    queue->showAllJobs();
    require(session->jobFilter().empty() && !rows[1]->selected.get() && session->selectedCount() == 1 &&
            session->visibleCount() == 2, "All jobs did not clear only the job filter");
    queue->showAllJobs();
    require(requested == std::vector<std::string>({"wait", "wait", "", ""}),
            "All jobs did not notify the host when its filter was already clear");
    auto original = rows[0];
    const auto revision = queue->items.revision();
    const auto layoutRevision = queue->layoutRevision.get();
    snapshot.inference = Value(Object{{"state", Value("denoising")}, {"step", Value(4)}, {"total", Value(10)}});
    require(bool(queue->setSnapshot(snapshot)), "A progress update was rejected");
    require(queue->items.revision() == revision && queue->items.get()[0] == original && original->step.get() == 4 &&
            queue->layoutRevision.get() == layoutRevision,
            "Progress reset row identity or did not update its value");
    snapshot.previewJobId = "another";
    require(bool(queue->setSnapshot(snapshot)) && queue->previewSource.get().empty(),
            "A preview from another job was displayed as the active job");
    auto observer = rows[0]->stateLabel.observe([](const auto &, std::uint64_t) { throw std::runtime_error("observer"); });
    auto changed = snapshot;
    changed.jobs = Value(Array{job("run", "failed"), job("wait", "completed"), job("done", "completed"),
                               job("fail", "failed"), job("stop", "cancelled")});
    const auto observed = queue->setSnapshot(changed);
    require(!observed && !rows[1]->canCancel.get() && queue->completed.get() == 2 && queue->failed.get() == 2,
            "An observer failure left the queue only partly updated");
    require(queue->layoutRevision.get() != layoutRevision, "A row-height change did not invalidate offscreen measurements");
    canceled.clear(); rows[1]->cancel();
    require(canceled.empty(), "An observer failure permitted a stale cancellation");
    observer.reset();
    auto duplicate = snapshot;
    duplicate.jobs = Value(Array{job("run", "running"), job("run", "failed")});
    require(!queue->setSnapshot(duplicate) && queue->items.get()[0] == original,
            "Duplicate job IDs corrupted the current projection");
    snapshot.jobs = Value(Array{job("wait", "queued")});
    require(bool(queue->setSnapshot(snapshot)), "Removing a job failed");
    canceled.clear(); original->cancel();
    require(canceled.empty(), "A removed row dispatched a stale cancellation");
    snapshot.jobs = Value(Array{job("run", "running")});
    require(bool(queue->setSnapshot(snapshot)), "Reintroducing a job failed");
    original->cancel();
    require(canceled.empty() && queue->items.get()[0]->token != original->token,
            "A retired row targeted a later job with the same ID");
    const auto count = requested.size(); original->select();
    require(requested.size() == count, "A retired row activated a later job with the same ID");
    auto wrongThread = std::async(std::launch::async, [&] { return queue->setSnapshot(snapshot); }).get();
    require(!wrongThread, "A different thread mutated the queue");
    queue->close();
    queue->items.get()[0]->cancel();
    require(canceled.empty(), "A closed queue dispatched an action");
}
void anonymousAndTranslatedJobs() {
    dreamscapes::review::QueueStrings strings;
    strings.outputCount = [](std::size_t n) { return std::to_string(n) + " clips"; };
    strings.jobTitle = [](std::string_view id) { return "[" + std::string(id) + "]"; };
    auto queue = QueueModel::create(std::make_shared<Session>(), std::move(strings));
    auto video = job("finished", "completed", "Video");
    auto videoFields = dreamscapes::vm::object(video);
    videoFields["videos"] = Value(Array{Value("a"), Value("b")});
    auto failed = dreamscapes::vm::object(job("broken", "failed"));
    failed["error"] = Value("first\r\n\xe2\x80\xa8second");
    QueueSnapshot snapshot{.jobs = Value(Array{job("", "running"), job("known", "running"),
                                               Value(std::move(videoFields)), Value(std::move(failed))}),
                           .inference = Value(Object{{"step", Value(1)}, {"total", Value(4)}}),
                           .previewJobId = "known", .previewSource = "file:///other.png"};
    require(bool(queue->setSnapshot(snapshot)), "Anonymous jobs were not preserved");
    auto rows = queue->items.get();
    require(rows.size() == 4 && queue->running.get() == 2 && !rows[0]->showProgress.get() &&
            !rows[1]->showProgress.get() && queue->previewSource.get().empty(),
            "An unidentifiable active job was replaced by a different job");
    require(rows[2]->title.get() == "[finished]" && rows[2]->detail.get() == "2 clips" && rows[3]->detail.get() == "first second",
            "Output count localization or single-line diagnostics changed");
    bool canceled = false;
    queue->setCancelHandler([&](const auto &) { canceled = true; });
    rows[0]->cancel();
    require(!canceled && !rows[0]->canCancel.get(), "An anonymous row dispatched a command");
}
void formattingFailurePreservesJobs() {
    dreamscapes::review::QueueStrings strings;
    strings.outputCount = [](std::size_t) -> std::string { throw std::runtime_error("formatter"); };
    auto queue = QueueModel::create(std::make_shared<Session>(), std::move(strings));
    QueueSnapshot snapshot{.jobs = Value(Array{job("one", "queued")})};
    require(bool(queue->setSnapshot(snapshot)), "The initial job was rejected");
    const auto previous = queue->items.get()[0];
    snapshot.jobs = Value(Array{job("one", "completed")});
    const auto result = queue->setSnapshot(snapshot);
    require(!result && queue->items.get()[0] == previous && queue->queued.get() == 1 &&
            queue->completed.get() == 0, "A failed formatter changed the previous queue");
}
}
int main() {
    try { projectionAndCommands(); anonymousAndTranslatedJobs(); formattingFailurePreservesJobs(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Native review queue projects real jobs and rejects stale actions\n";
}
