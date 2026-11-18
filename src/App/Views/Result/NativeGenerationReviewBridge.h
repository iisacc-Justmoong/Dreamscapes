#pragma once
#include "App/Generation/NativeGenerationQueue.h"
#include "NativeReviewQueue.h"
#include "App/Views/Home/NativeHomeQueue.h"
#include <memory>
#include <thread>
#include <vector>

namespace dreamscapes::review {
// Projects one live generation session into the existing LVUI models. It
// never owns the engine or modifies a result file.
class GenerationReviewBridge final : public std::enable_shared_from_this<GenerationReviewBridge> {
  public:
    static std::shared_ptr<GenerationReviewBridge> create(std::shared_ptr<generation::Queue>,
        std::shared_ptr<Session>,std::shared_ptr<QueueModel>,std::shared_ptr<home::HomeQueueModel>);
    ~GenerationReviewBridge();
    void close();
  private:
    struct JobSubscriptions final {
        ii::core::ObservableSubscription state,progress,error;
    };
    GenerationReviewBridge(std::shared_ptr<generation::Queue>,std::shared_ptr<Session>,
        std::shared_ptr<QueueModel>,std::shared_ptr<home::HomeQueueModel>);
    void start();
    void rebind();
    void projectOutputs();
    void refresh();
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<generation::Queue> queue_;
    std::shared_ptr<Session> session_;
    std::shared_ptr<QueueModel> display_;
    std::shared_ptr<home::HomeQueueModel> home_;
    ii::core::ObservableSubscription jobs_,outputs_;
    std::vector<JobSubscriptions> subscriptions_;
    bool active_=true,refreshing_=false,pending_=false,clearHandler_=false;
};
}
