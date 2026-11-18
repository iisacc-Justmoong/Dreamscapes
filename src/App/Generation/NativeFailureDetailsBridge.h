#pragma once

#include "App/Generation/NativeGenerationQueue.h"
#include "App/Views/Failure/NativeFailureDetails.h"
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::failure {
// Projects a native queue job into the native view model on the owner thread.
class NativeFailureDetailsBridge final {
  public:
    explicit NativeFailureDetailsBridge(std::shared_ptr<FailureDetailsModel>);
    [[nodiscard]] ii::core::Result<void> openReport(const generation::QueueJob &);
    [[nodiscard]] const std::string &lastError() const noexcept {return lastError_;}
  private:
    std::weak_ptr<FailureDetailsModel> model_;
    const std::thread::id owner_=std::this_thread::get_id();
    std::string lastError_;
};
}
