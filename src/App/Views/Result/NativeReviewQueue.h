#pragma once

#include "NativeReviewSession.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dreamscapes::review {
struct QueueSnapshot final {
    vm::Value jobs{vm::Array{}};
    vm::Value inference{vm::Object{}};
    std::string previewJobId, previewSource;
};
struct QueueStrings final {
    std::string heading{"GENERATION"}, allJobs{"All jobs"}, empty{"No generation jobs"}, preview{"Live preview"};
    std::string cancel{"Cancel job"}, failure{"Failure details"};
    std::string job{"Job "}, unavailable{"Unavailable"}, modelUnavailable{"Model unavailable"}, preparing{"Preparing"};
    std::string running{"Running"}, queued{"Queued"}, completed{"Completed"}, failed{"Failed"}, canceled{"Canceled"};
    std::string interrupted{"Interrupted"}, connecting{"Connecting"}, downloading{"Downloading"};
    std::function<std::string(std::string_view)> jobTitle;
    std::function<std::string(std::size_t)> outputCount;
    std::function<std::string(std::string_view, int, int)> progressDetail;
    std::function<std::string(std::string_view, int)> count;
};

class QueueModel;
class QueueItem final {
  public:
    const std::string id, token;
    ii::core::Observable<std::string> title, stateLabel, modelLabel, detail;
    ii::core::Observable<bool> selected{false}, isFailed{false}, showProgress{false}, canCancel{false}, canInspectFailure{false};
    ii::core::Observable<int> step{0}, total{0};
    void select();
    void cancel();
    void inspectFailure();

  private:
    friend class QueueModel;
    QueueItem(std::string id, std::string token, std::weak_ptr<QueueModel> owner);
    std::weak_ptr<QueueModel> owner_;
    std::string state_;
};

// Presentation state only. Commands are delegated to host-owned services.
class QueueModel final : public std::enable_shared_from_this<QueueModel> {
  public:
    using Item = std::shared_ptr<QueueItem>;
    using Action = std::function<void(const std::string &)>;
    static std::shared_ptr<QueueModel> create(std::shared_ptr<Session> session, QueueStrings strings = {});
    ~QueueModel();
    QueueModel(const QueueModel &) = delete;
    QueueModel &operator=(const QueueModel &) = delete;

    ii::core::Observable<std::vector<Item>> items;
    ii::core::Observable<int> running{0}, queued{0}, completed{0}, failed{0}, canceled{0};
    ii::core::Observable<int> layoutRevision{0};
    ii::core::Observable<bool> empty{true}, previewVisible{false}, selectionEnabled{true};
    ii::core::Observable<std::string> heading, allJobsLabel, emptyLabel, previewLabel, cancelLabel, failureLabel;
    ii::core::Observable<std::string> runningSummary, queuedSummary, completedSummary, failedSummary, canceledSummary;
    ii::core::Observable<std::string> previewSource;
    // Like the other viewmodel state, inspect only on the owner thread.
    bool active() const noexcept { return active_; }
    bool onOwnerThread() const noexcept { return owner_ == std::this_thread::get_id(); }
    [[nodiscard]] ii::core::Result<void> setSnapshot(const QueueSnapshot &snapshot);
    [[nodiscard]] ii::core::Result<void> setPreview(std::string_view jobId, std::string source);
    void setCancelHandler(Action handler);
    void setFailureHandler(Action handler);
    void setSelectionHandler(Action handler);
    void setSelectionEnabled(bool enabled);
    void showAllJobs();
    void close();

  private:
    friend class QueueItem;
    enum class Command { Select, Cancel, Failure };
    explicit QueueModel(std::shared_ptr<Session> session, QueueStrings strings);
    void start();
    void requireOwner() const;
    void refreshSelection();
    void refreshActions();
    void invoke(std::string_view token, Command command);

    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<Session> session_;
    std::shared_ptr<ChangePort> observer_;
    std::unordered_map<std::string, Item> byId_, byToken_;
    std::string activeJobId_;
    QueueStrings strings_;
    Action cancel_, failure_, selection_;
    std::uint64_t nextToken_ = 0;
    bool active_ = true, updating_ = false, selectionAllowed_ = true;
};
} // namespace dreamscapes::review

// The generated LVUI scope borrows the host-owned presentation model.
class NativeReviewQueueBackend final {
  public:
    using Model = dreamscapes::review::QueueModel;
    explicit NativeReviewQueueBackend(std::shared_ptr<Model> source);
    NativeReviewQueueBackend();
    std::shared_ptr<Model> model;
    ii::core::Observable<std::vector<Model::Item>> &items;
    ii::core::Observable<int> &layoutRevision;
    ii::core::Observable<bool> &empty, &previewVisible, &selectionEnabled;
    ii::core::Observable<std::string> &heading, &allJobsLabel, &emptyLabel, &previewLabel, &cancelLabel, &failureLabel;
    ii::core::Observable<std::string> &runningSummary, &queuedSummary, &completedSummary, &failedSummary, &canceledSummary;
    ii::core::Observable<std::string> &previewSource;
    void showAllJobs() { model->showAllJobs(); }
};
