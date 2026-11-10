#pragma once
#include "App/Views/Failure/NativeFailureDetails.h"
#include <QObject>
#include <QPointer>
#include <QString>
#include <memory>
#include <string>

class GenerationController;
namespace dreamscapes::failure {
// Transitional backend adapter. The LVUI view and its model do not include Qt.
class NativeFailureDetailsBridge final : public QObject {
  public:
    NativeFailureDetailsBridge(GenerationController *, std::shared_ptr<FailureDetailsModel>, QObject *parent = nullptr);
    [[nodiscard]] ii::core::Result<void> openReport(const QString &jobId);
    [[nodiscard]] const std::string &lastError() const noexcept { return lastError_; }
  private:
    QPointer<GenerationController> controller_;
    std::weak_ptr<FailureDetailsModel> model_;
    QString activeJob_;
    std::string lastError_;
};
}
