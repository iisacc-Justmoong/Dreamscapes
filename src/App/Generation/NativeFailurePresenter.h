#pragma once

#include "NativeFailureDetailsBridge.h"

namespace dreamscapes::failure {
// The owning LVUI shell mounts the modal; this presenter only supplies data.
class NativeFailurePresenter final {
  public:
    explicit NativeFailurePresenter(std::shared_ptr<FailureDetailsModel> model):bridge_(std::move(model)) {}
    [[nodiscard]] ii::core::Result<void> open(const generation::QueueJob &job) {return bridge_.openReport(job);}
    [[nodiscard]] const std::string &lastError() const noexcept {return bridge_.lastError();}
  private:
    NativeFailureDetailsBridge bridge_;
};
}
