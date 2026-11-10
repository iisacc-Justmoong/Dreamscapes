#include "NativeFailureDetailsBridge.h"
#include "GenerationController.h"
#include "App/ViewModels/ViewData.h"
#include <QThread>
#include <stdexcept>

namespace dreamscapes::failure {
NativeFailureDetailsBridge::NativeFailureDetailsBridge(GenerationController *controller,
    std::shared_ptr<FailureDetailsModel> model, QObject *parent)
    : QObject(parent), controller_(controller), model_(model) {
    if (!controller || !model) throw std::invalid_argument("Failure details require a controller and model");
    if (controller->thread() != QThread::currentThread() || thread() != QThread::currentThread())
        throw std::logic_error("Failure details bridge requires the controller's owner thread");
    connect(controller, &GenerationController::failureExplained, this,
        [this](const QString &jobId, const QVariantMap &report) {
            if (jobId != activeJob_) return;
            const auto model = model_.lock();
            if (!model) return;
            try {
                const auto updated = model->setReport(nativeValue(report));
                lastError_ = updated ? std::string{} : updated.error().message;
                if (!updated) model->setReportFailure(lastError_);
            } catch (const std::exception &error) {
                lastError_ = error.what();
                try { model->setReportFailure(lastError_); } catch (...) {}
            } catch (...) {
                lastError_ = "Failure report update failed";
                try { model->setReportFailure(lastError_); } catch (...) {}
            }
        });
}
ii::core::Result<void> NativeFailureDetailsBridge::openReport(const QString &jobId) {
    if (thread() != QThread::currentThread())
        return ii::core::fail(ii::core::ErrorCode::wrong_executor, "Failure details require the controller's owner thread");
    if (!controller_) return ii::core::fail(ii::core::ErrorCode::closed, "Generation controller is unavailable");
    const auto model = model_.lock();
    if (!model) return ii::core::fail(ii::core::ErrorCode::closed, "Failure details view is unavailable");
    if (jobId.isEmpty()) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Failure report ID is empty");
    try {
        const auto updated = model->setReport(nativeValue(controller_->failureAnalysis(jobId)));
        if (!updated) { lastError_ = updated.error().message; model->setReportFailure(lastError_); return updated; }
        lastError_.clear(); activeJob_ = jobId; model->show(); return {};
    } catch (const std::exception &error) {
        lastError_ = error.what();
        return ii::core::fail(ii::core::ErrorCode::failed, lastError_, std::current_exception());
    }
}
} // namespace dreamscapes::failure
