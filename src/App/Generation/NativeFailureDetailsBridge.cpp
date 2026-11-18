#include "NativeFailureDetailsBridge.h"
#include "NativeFailureDiagnostics.h"
#include <stdexcept>

namespace dreamscapes::failure {
NativeFailureDetailsBridge::NativeFailureDetailsBridge(std::shared_ptr<FailureDetailsModel> model):model_(model) {
    if (!model) throw std::invalid_argument("Failure details require a model");
}
ii::core::Result<void> NativeFailureDetailsBridge::openReport(const generation::QueueJob &job) {
    if (owner_!=std::this_thread::get_id())
        return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Failure details require their owner thread");
    auto model=model_.lock();
    if (!model) return ii::core::fail(ii::core::ErrorCode::closed,"Failure details view is unavailable");
    const auto state=job.state.get();
    if (state!=generation::QueueState::Failed && !(state==generation::QueueState::Canceled && !job.error.get().empty()))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"The job has no failure or recovery report");
    auto report=generation::nativeFailureReport(job);
    auto result=model->setReport(report);
    if (!result) {lastError_=result.error().message;model->setReportFailure(lastError_);return result;}
    lastError_.clear();
    (void)model->title.set(state==generation::QueueState::Canceled?"Generation details":"Failure explanation");
    model->show();return {};
}
}
