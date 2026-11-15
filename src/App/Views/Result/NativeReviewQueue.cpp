#include "NativeReviewQueue.h"
#include "../../ViewModels/Native/GenerationPresentation.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace dreamscapes::review {
namespace {
bool pending(std::string_view state) { return vm::generationPending(state); }
std::string singleLine(std::string value) {
    std::string result;
    result.reserve(value.size());
    bool previousBreak = false;
    for (std::size_t at = 0; at < value.size();) {
        const bool unicodeBreak = at + 2 < value.size() && value.compare(at, 2, "\xe2\x80", 2) == 0 &&
                                  (value[at + 2] == '\xa8' || value[at + 2] == '\xa9');
        if (value[at] == '\r' || value[at] == '\n' || unicodeBreak) {
            if (!previousBreak) result += ' ';
            previousBreak = true; at += unicodeBreak ? 3 : 1;
        } else {
            previousBreak = false; result += value[at++];
        }
    }
    return result;
}
int integer(const vm::Value &value, int fallback) {
    const auto number = vm::number(value, fallback);
    if (!std::isfinite(number) || number < (std::numeric_limits<int>::min)() ||
        number > (std::numeric_limits<int>::max)()) return fallback;
    return static_cast<int>(number);
}
std::string stateName(std::string_view state, const QueueStrings &s) {
    if (state == "running") return s.running;
    if (state == "queued") return s.queued;
    if (state == "completed") return s.completed;
    if (state == "failed") return s.failed;
    if (state == "cancelled") return s.canceled;
    if (state == "interrupted") return s.interrupted;
    if (state == "connecting-host") return s.connecting;
    if (state == "downloading") return s.downloading;
    return state.empty() ? s.unavailable : std::string(state);
}
struct ProjectedRow {
    std::string id, state, title, stateLabel, modelLabel, detail;
    bool progress = false;
    int step = 0, total = 0;
};
template <class Observable, class T>
void publish(Observable &observable, T value, std::exception_ptr &failure) {
    try { observable.set(std::move(value)); }
    catch (...) { if (!failure) failure = std::current_exception(); }
}
int nextRevision(int current) { return current == (std::numeric_limits<int>::max)() ? 0 : current + 1; }
}

QueueItem::QueueItem(std::string idValue, std::string tokenValue, std::weak_ptr<QueueModel> owner)
    : id(std::move(idValue)), token(std::move(tokenValue)), owner_(std::move(owner)) {}
void QueueItem::select() { if (auto owner = owner_.lock()) owner->invoke(token, QueueModel::Command::Select); }
void QueueItem::cancel() { if (auto owner = owner_.lock()) owner->invoke(token, QueueModel::Command::Cancel); }
void QueueItem::inspectFailure() { if (auto owner = owner_.lock()) owner->invoke(token, QueueModel::Command::Failure); }

std::shared_ptr<QueueModel> QueueModel::create(std::shared_ptr<Session> session, QueueStrings strings) {
    auto result = std::shared_ptr<QueueModel>(new QueueModel(std::move(session), std::move(strings)));
    result->start();
    return result;
}
QueueModel::QueueModel(std::shared_ptr<Session> session, QueueStrings strings)
    : session_(std::move(session)), strings_(std::move(strings)) {
    if (!session_) throw std::invalid_argument("A review queue requires a session");
    if (!strings_.jobTitle) strings_.jobTitle = [prefix = strings_.job](std::string_view id) {
        return prefix + std::string(id);
    };
    if (!strings_.outputCount) strings_.outputCount = [](std::size_t count) { return std::to_string(count) + " outputs"; };
    if (!strings_.progressDetail) strings_.progressDetail = [](std::string_view phase, int step, int total) {
        return std::string(phase) + " · " + std::to_string(step) + " / " + std::to_string(total);
    };
    if (!strings_.count) strings_.count = [](std::string_view label, int value) {
        return std::string(label) + "  " + std::to_string(value);
    };
    heading.set(strings_.heading); allJobsLabel.set(strings_.allJobs); emptyLabel.set(strings_.empty);
    previewLabel.set(strings_.preview); cancelLabel.set(strings_.cancel); failureLabel.set(strings_.failure);
    runningSummary.set(strings_.count(strings_.running, 0)); queuedSummary.set(strings_.count(strings_.queued, 0));
    completedSummary.set(strings_.count(strings_.completed, 0)); failedSummary.set(strings_.count(strings_.failed, 0));
    canceledSummary.set(strings_.count(strings_.canceled, 0));
}
QueueModel::~QueueModel() {
    if (active_ && owner_ == std::this_thread::get_id()) {
        try { close(); } catch (...) { /* Destructors must not propagate observer exceptions. */ }
    }
}
void QueueModel::requireOwner() const {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Review queue requires its owner thread");
    if (!active_) throw std::logic_error("Review queue is closed");
    if (updating_) throw std::logic_error("Review queue update is already in progress");
}
void QueueModel::start() {
    requireOwner();
    const std::weak_ptr<QueueModel> weak = shared_from_this();
    observer_ = std::make_shared<ChangePort>([weak](const Change &) {
        if (auto owner = weak.lock(); owner && owner->active_) owner->refreshSelection();
    });
    if (const auto result = session_->observe(observer_); !result) throw std::runtime_error(result.error().message);
}

ii::core::Result<void> QueueModel::setSnapshot(const QueueSnapshot &snapshot) {
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor, "Review queue requires its owner thread");
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed, "Review queue is closed");
    if (updating_) return ii::core::fail(ii::core::ErrorCode::busy, "Review queue update is already in progress");
    const auto keepAlive = shared_from_this();
    if (snapshot.jobs.kind() != ii::core::ValueKind::array)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Review queue jobs must be an array");
    const auto &jobs = vm::array(snapshot.jobs);
    if (jobs.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return ii::core::fail(ii::core::ErrorCode::out_of_range, "Review queue contains too many jobs");
    std::unordered_set<std::string> ids;
    std::string activeId;
    bool foundActive = false;
    int runningCount = 0, queuedCount = 0, completedCount = 0, failedCount = 0, canceledCount = 0;
    for (const auto &job : jobs) {
        auto id = vm::text(vm::get(job, "id"));
        if (!id.empty() && !ids.insert(id).second)
            return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Review queue job IDs must be unique");
        const auto state = vm::text(vm::get(job, "state"));
        if (state == "running" || state == "connecting-host" || state == "downloading") {
            ++runningCount;
            if (!foundActive) { foundActive = true; activeId = std::move(id); }
        } else if (state == "queued") ++queuedCount;
        else if (state == "completed") ++completedCount;
        else if (state == "failed") ++failedCount;
        else if (state == "cancelled" || state == "interrupted") ++canceledCount;
    }
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{updating_};
    updating_ = true;
    std::string nextRunning, nextQueued, nextCompleted, nextFailed, nextCanceled;
    try {
        nextRunning = strings_.count(strings_.running, runningCount); nextQueued = strings_.count(strings_.queued, queuedCount);
        nextCompleted = strings_.count(strings_.completed, completedCount); nextFailed = strings_.count(strings_.failed, failedCount);
        nextCanceled = strings_.count(strings_.canceled, canceledCount);
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Review queue count formatting failed", std::current_exception()); }
    std::vector<ProjectedRow> projection;
    projection.reserve(jobs.size());
    for (const auto &job : jobs) {
        ProjectedRow row;
        row.id = vm::text(vm::get(job, "id")); row.state = vm::text(vm::get(job, "state"));
        try { row.title = strings_.jobTitle(std::string_view(row.id).substr(0, 8)); }
        catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Review queue title formatting failed", std::current_exception()); }
        row.stateLabel = stateName(row.state, strings_);
        auto media = vm::text(vm::get(job, "mediaType"));
        auto model = vm::text(vm::get(job, "modelName"));
        row.modelLabel = (media.empty() ? strings_.unavailable : media) + " · " +
                         (model.empty() ? strings_.modelUnavailable : model);
        const bool active = !row.id.empty() && row.id == activeId;
        row.step = integer(vm::get(snapshot.inference, "step"), -1);
        row.total = integer(vm::get(snapshot.inference, "total"), 0);
        row.progress = active && row.total > 0 && row.step >= 0;
        if (active) {
            auto phase = vm::text(vm::get(snapshot.inference, "state"));
            if (phase.empty()) phase = strings_.preparing;
            if (row.progress) {
                try { row.detail = strings_.progressDetail(phase, row.step, row.total); }
                catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Review queue progress formatting failed", std::current_exception()); }
            } else row.detail = phase;
        } else if (row.state == "completed") {
            try { row.detail = strings_.outputCount(vm::array(vm::get(job, media == "Video" ? "videos" : "images")).size()); }
            catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Review queue output formatting failed", std::current_exception()); }
        } else {
            row.detail = vm::text(vm::get(job, "error"));
            if (row.detail.empty()) row.detail = row.stateLabel;
        }
        row.detail = singleLine(std::move(row.detail));
        projection.push_back(std::move(row));
    }
    std::vector<Item> next;
    std::unordered_map<std::string, Item> nextById, nextByToken;
    next.reserve(projection.size());
    for (const auto &row : projection) {
        Item item;
        if (!row.id.empty()) if (const auto it = byId_.find(row.id); it != byId_.end()) item = it->second;
        if (!item) {
            if (nextToken_ == (std::numeric_limits<std::uint64_t>::max)())
                return ii::core::fail(ii::core::ErrorCode::out_of_range, "Review queue token space exhausted");
            item = Item(new QueueItem(row.id, "job-" + std::to_string(++nextToken_), weak_from_this()));
        }
        next.push_back(item);
        if (!row.id.empty()) nextById.emplace(row.id, item);
        nextByToken.emplace(item->token, item);
    }
    bool layoutChanged = next.size() != items.get().size();
    for (std::size_t i = 0; i < next.size(); ++i) {
        const auto &item = next[i]; const auto &row = projection[i];
        if (i >= items.get().size() || items.get()[i] != item || item->showProgress.get() != row.progress ||
            item->canCancel.get() != (!row.id.empty() && pending(row.state) && bool(cancel_)) ||
            item->canInspectFailure.get() != (!row.id.empty() && row.state == "failed" && bool(failure_))) layoutChanged = true;
    }
    // Retire removed tokens before publishing any row changes.
    byId_ = std::move(nextById); byToken_ = std::move(nextByToken);
    activeJobId_ = activeId;
    for (std::size_t i = 0; i < next.size(); ++i) next[i]->state_ = projection[i].state;
    std::exception_ptr notificationFailure;
    for (std::size_t i = 0; i < next.size(); ++i) {
        auto &item = next[i]; const auto &row = projection[i];
        publish(item->title, row.title, notificationFailure); publish(item->stateLabel, row.stateLabel, notificationFailure);
        publish(item->modelLabel, row.modelLabel, notificationFailure); publish(item->detail, row.detail, notificationFailure);
        publish(item->step, row.step, notificationFailure); publish(item->total, row.total, notificationFailure);
        publish(item->showProgress, row.progress, notificationFailure);
        publish(item->isFailed, row.state == "failed", notificationFailure);
        publish(item->selected, !row.id.empty() && session_->jobFilter() == row.id, notificationFailure);
        publish(item->canCancel, !row.id.empty() && pending(row.state) && bool(cancel_), notificationFailure);
        publish(item->canInspectFailure, !row.id.empty() && row.state == "failed" && bool(failure_), notificationFailure);
    }
    publish(running, runningCount, notificationFailure); publish(queued, queuedCount, notificationFailure);
    publish(completed, completedCount, notificationFailure); publish(failed, failedCount, notificationFailure);
    publish(canceled, canceledCount, notificationFailure);
    publish(runningSummary, nextRunning, notificationFailure); publish(queuedSummary, nextQueued, notificationFailure);
    publish(completedSummary, nextCompleted, notificationFailure); publish(failedSummary, nextFailed, notificationFailure);
    publish(canceledSummary, nextCanceled, notificationFailure);
    const auto preview = !activeId.empty() && activeId == snapshot.previewJobId ? snapshot.previewSource : std::string{};
    publish(previewSource, preview, notificationFailure); publish(previewVisible, !preview.empty(), notificationFailure);
    publish(empty, next.empty(), notificationFailure);
    if (layoutChanged) publish(layoutRevision, nextRevision(layoutRevision.get()), notificationFailure);
    publish(items, std::move(next), notificationFailure);
    if (notificationFailure)
        return ii::core::fail(ii::core::ErrorCode::failed, "A review queue observer failed", notificationFailure);
    return {};
}

ii::core::Result<void> QueueModel::setPreview(std::string_view jobId, std::string source) {
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor, "Review queue requires its owner thread");
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed, "Review queue is closed");
    if (updating_) return ii::core::fail(ii::core::ErrorCode::busy, "Review queue update is already in progress");
    const auto keepAlive = shared_from_this();
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{updating_};
    updating_ = true;
    if (activeJobId_.empty() || jobId != activeJobId_) source.clear();
    const bool visible = !source.empty();
    std::exception_ptr notificationFailure;
    publish(previewSource, std::move(source), notificationFailure);
    publish(previewVisible, visible, notificationFailure);
    if (notificationFailure)
        return ii::core::fail(ii::core::ErrorCode::failed, "A review queue observer failed", notificationFailure);
    return {};
}

void QueueModel::refreshSelection() {
    if (owner_ != std::this_thread::get_id() || !active_) return;
    std::exception_ptr failure;
    for (const auto &item : items.get()) publish(item->selected, !item->id.empty() && item->id == session_->jobFilter(), failure);
    if (failure) std::rethrow_exception(failure);
}
void QueueModel::refreshActions() {
    std::exception_ptr failure;
    bool layoutChanged = false;
    for (const auto &item : items.get()) {
        const bool canCancel = !item->id.empty() && pending(item->state_) && bool(cancel_);
        const bool canInspect = !item->id.empty() && item->state_ == "failed" && bool(failure_);
        layoutChanged |= item->canCancel.get() != canCancel || item->canInspectFailure.get() != canInspect;
        publish(item->canCancel, canCancel, failure); publish(item->canInspectFailure, canInspect, failure);
    }
    if (layoutChanged) publish(layoutRevision, nextRevision(layoutRevision.get()), failure);
    if (failure) std::rethrow_exception(failure);
}
void QueueModel::setCancelHandler(Action handler) {
    requireOwner(); const auto keepAlive = shared_from_this(); cancel_ = std::move(handler); refreshActions();
}
void QueueModel::setFailureHandler(Action handler) {
    requireOwner(); const auto keepAlive = shared_from_this(); failure_ = std::move(handler); refreshActions();
}
void QueueModel::setSelectionHandler(Action handler) {
    requireOwner(); selection_ = std::move(handler);
}
void QueueModel::setSelectionEnabled(bool enabled) {
    requireOwner();
    const auto keepAlive = shared_from_this();
    selectionAllowed_ = enabled;
    selectionEnabled.set(enabled);
}
void QueueModel::showAllJobs() {
    requireOwner();
    if (!selectionAllowed_) return;
    const auto keepAlive = shared_from_this();
    const auto session = session_;
    session->setJobFilter({});
    if (active_) if (auto action = selection_) action({});
}
void QueueModel::invoke(std::string_view token, Command command) {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Review queue requires its owner thread");
    if (!active_ || updating_) return;
    const auto found = byToken_.find(std::string(token));
    if (found == byToken_.end() || found->second->id.empty()) return;
    const auto item = found->second;
    if (command == Command::Select) {
        if (!selectionAllowed_) return;
        const auto session = session_;
        session->setJobFilter(item->id);
        const auto current = byToken_.find(std::string(token));
        if (active_ && current != byToken_.end() && current->second == item)
            if (auto action = selection_) action(item->id);
        return;
    }
    const auto action = command == Command::Cancel ? cancel_ : failure_;
    if (action && ((command == Command::Cancel && pending(item->state_)) ||
                   (command == Command::Failure && item->state_ == "failed"))) action(item->id);
}
void QueueModel::close() {
    requireOwner();
    const auto keepAlive = weak_from_this().lock();
    active_ = false; observer_.reset(); cancel_ = {}; failure_ = {}; selection_ = {}; selectionAllowed_ = false;
    byId_.clear(); byToken_.clear(); activeJobId_.clear();
    std::exception_ptr failure;
    publish(selectionEnabled, false, failure);
    for (const auto &item : items.get()) {
        publish(item->canCancel, false, failure); publish(item->canInspectFailure, false, failure);
    }
    session_.reset();
    if (failure) std::rethrow_exception(failure);
}
} // namespace dreamscapes::review

namespace {
std::shared_ptr<dreamscapes::review::QueueModel> requireQueueModel(
    std::shared_ptr<dreamscapes::review::QueueModel> model) {
    if (!model) throw std::invalid_argument("A native review queue requires a model");
    return model;
}
}
NativeReviewQueueBackend::NativeReviewQueueBackend()
    : NativeReviewQueueBackend(dreamscapes::review::QueueModel::create(std::make_shared<dreamscapes::review::Session>())) {}
NativeReviewQueueBackend::NativeReviewQueueBackend(std::shared_ptr<Model> source)
    : model(requireQueueModel(std::move(source))), items(model->items), layoutRevision(model->layoutRevision),
      empty(model->empty), previewVisible(model->previewVisible), selectionEnabled(model->selectionEnabled), heading(model->heading),
      allJobsLabel(model->allJobsLabel), emptyLabel(model->emptyLabel), previewLabel(model->previewLabel),
      cancelLabel(model->cancelLabel), failureLabel(model->failureLabel), runningSummary(model->runningSummary),
      queuedSummary(model->queuedSummary), completedSummary(model->completedSummary), failedSummary(model->failedSummary),
      canceledSummary(model->canceledSummary), previewSource(model->previewSource) {}
