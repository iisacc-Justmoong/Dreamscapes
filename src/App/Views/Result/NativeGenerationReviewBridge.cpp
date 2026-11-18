#include "NativeGenerationReviewBridge.h"
#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
namespace {
std::string stateName(generation::QueueState state) {
    using generation::QueueState;
    switch (state) {
    case QueueState::Queued:return "queued";
    case QueueState::Running:return "running";
    case QueueState::Completed:return "completed";
    case QueueState::Failed:return "failed";
    case QueueState::Canceled:return "cancelled";
    }
    return {};
}
}
GenerationReviewBridge::GenerationReviewBridge(std::shared_ptr<generation::Queue> queue,std::shared_ptr<Session> session,
    std::shared_ptr<QueueModel> display,std::shared_ptr<home::HomeQueueModel> home)
    :queue_(std::move(queue)),session_(std::move(session)),display_(std::move(display)),home_(std::move(home)) {
    if (!queue_ || !session_ || !display_ || !home_) throw std::invalid_argument("Generation review requires all session models");
}
std::shared_ptr<GenerationReviewBridge> GenerationReviewBridge::create(std::shared_ptr<generation::Queue> queue,
    std::shared_ptr<Session> session,std::shared_ptr<QueueModel> display,std::shared_ptr<home::HomeQueueModel> home) {
    auto value=std::shared_ptr<GenerationReviewBridge>(new GenerationReviewBridge(std::move(queue),std::move(session),std::move(display),std::move(home)));
    value->start();return value;
}
GenerationReviewBridge::~GenerationReviewBridge() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
void GenerationReviewBridge::start() {
    const std::weak_ptr<GenerationReviewBridge> weak=shared_from_this();
    display_->setCancelHandler([weak](const std::string &id) {if (auto self=weak.lock();self && self->active_) (void)self->queue_->cancel(id);});
    jobs_=queue_->jobs.observe([weak](const auto &,std::uint64_t) {if (auto self=weak.lock()) self->rebind();});
    outputs_=queue_->outputs.observe([weak](const auto &,std::uint64_t) {if (auto self=weak.lock()) {self->projectOutputs();self->refresh();}});
    rebind();projectOutputs();
}
void GenerationReviewBridge::rebind() {
    if (!active_ || owner_!=std::this_thread::get_id()) return;
    subscriptions_.clear();
    const auto values=queue_->jobs.get();subscriptions_.reserve(values.size());
    const std::weak_ptr<GenerationReviewBridge> weak=shared_from_this();
    const auto changed=[weak](const auto &,std::uint64_t) {if (auto self=weak.lock()) self->refresh();};
    for (const auto &job:values) {
        subscriptions_.emplace_back();auto &sub=subscriptions_.back();
        sub.state=job->state.observe(changed);sub.progress=job->progress.observe(changed);sub.error=job->error.observe(changed);
    }
    refresh();
}
void GenerationReviewBridge::projectOutputs() {
    if (!active_ || owner_!=std::this_thread::get_id()) return;
    const auto jobs=queue_->jobs.get();const auto outputs=queue_->outputs.get();
    std::map<std::string,std::string,std::less<>> media;
    for (const auto &job:jobs) media.emplace(job->id,job->request.media);
    std::vector<vm::Value> entries;entries.reserve(outputs.size());
    for (const auto &output:outputs) {
        auto value=vm::object(output.entry);
        // The existing review session uses id as the generation-job identity
        // while the result source or Society key identifies an individual file.
        value["id"]=vm::Value(output.jobId);value["jobId"]=vm::Value(output.jobId);
        if (!vm::has(output.entry,"mediaType")) {
            const auto found=media.find(output.jobId);
            if (found!=media.end()) value["mediaType"]=vm::Value(found->second);
        }
        entries.emplace_back(std::move(value));
    }
    session_->replaceEntries(entries);
}
void GenerationReviewBridge::refresh() {
    if (!active_ || owner_!=std::this_thread::get_id()) return;
    if (refreshing_) {pending_=true;return;}
    refreshing_=true;
    try {
        do {
            pending_=false;
            const auto jobs=queue_->jobs.get();const auto outputs=queue_->outputs.get();
            std::map<std::string,vm::Array,std::less<>> outputPaths;
            for (const auto &item:outputs) outputPaths[item.jobId].push_back(vm::get(item.entry,"path"));
            vm::Array rows;rows.reserve(jobs.size());
            vm::Value inference(vm::Object{});std::string activeModel;
            for (const auto &job:jobs) {
                const auto state=job->state.get();
                const auto label=job->request.modelLabel.empty()?job->request.model:job->request.modelLabel;
                vm::Object row{{"id",vm::Value(job->id)},{"state",vm::Value(stateName(state))},
                    {"mediaType",vm::Value(job->request.media)},{"modelName",vm::Value(label)},
                    {"error",vm::Value(job->error.get())}};
                row[job->request.media=="Video"?"videos":"images"]=vm::Value(outputPaths[job->id]);
                rows.emplace_back(std::move(row));
                if (state==generation::QueueState::Running && activeModel.empty()) {
                    activeModel=label;const auto progress=job->progress.get();
                    inference=vm::Value(vm::Object{{"state",vm::Value(progress.stage)},
                        {"step",vm::Value(std::min<std::int64_t>(progress.completed,std::numeric_limits<int>::max()))},
                        {"total",vm::Value(std::min<std::int64_t>(progress.total,std::numeric_limits<int>::max()))}});
                }
            }
            const auto values=vm::Value(std::move(rows));
            (void)display_->setSnapshot({values,std::move(inference),{}, {}});
            if (!active_) break;
            const bool pending=std::ranges::any_of(jobs,[](const auto &job) {
                return job->state.get()==generation::QueueState::Running || job->state.get()==generation::QueueState::Queued;
            });
            (void)home_->setSnapshot({values,pending?std::string{}:"Ready to generate",activeModel,"Automatic"});
        } while (active_ && pending_);
        refreshing_=false;
    } catch (...) {refreshing_=false;throw;}
    if (clearHandler_ && display_->active()) {clearHandler_=false;display_->setCancelHandler({});}
}
void GenerationReviewBridge::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Generation review requires its owner thread");
    if (!active_) return;
    active_=false;jobs_={};outputs_={};subscriptions_.clear();
    if (refreshing_) clearHandler_=true;
    else if (display_->active()) display_->setCancelHandler({});
}
}
