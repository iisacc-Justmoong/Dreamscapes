#pragma once

#include "App/ViewModels/Native/ViewValue.h"
#include <LVUI/CodeDocument.h>
#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace dreamscapes::failure {
// The host supplies translated strings and the clipboard boundary. No source
// report or file is edited by this presentation model.
struct FailureStrings final {
    std::string title = "Failure explanation";
    std::string close = "Close";
    std::string copy = "Copy report";
    std::string accessibilityName = "Failure explanation text";
    std::string pending = "Agent analysis is running. Captured evidence is available below.";
    std::string agentStatus = "Agent status: ";
    std::string persistenceError = "Report could not be saved: ";
    std::string reportFile = "Report file: ";
};

class FailureDetailsModel final {
  public:
    using ClipboardWriter = std::function<ii::core::Result<void>(std::string_view)>;
    explicit FailureDetailsModel(FailureStrings strings = {});
    [[nodiscard]] ii::core::Result<void> setReport(const vm::Value &report);
    [[nodiscard]] ii::core::Result<void> setViewport(double width, double height);
    void setClipboardWriter(ClipboardWriter writer);
    void show();
    void dismiss();
    void copyReport();
    void setReportFailure(std::string error);
    [[nodiscard]] const std::string &reportText() const noexcept { return text_; }

    const std::shared_ptr<lvui::CodeMemoryDocument> document;
    ii::core::Observable<std::int64_t> revision{0};
    ii::core::Observable<bool> open{false}, canCopy{false}, motionEnabled{true}, copyFailed{false}, reportFailed{false},
        usesWrappedText{true};
    ii::core::Observable<double> editorWidth{1}, editorHeight{1};
    ii::core::Observable<std::string> title, closeLabel, copyLabel, accessibilityName, copyError, reportError, wrappedText;

  private:
    void requireOwner() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    FailureStrings strings_;
    std::string text_;
    ClipboardWriter clipboard_;
    bool updating_ = false;
};
} // namespace dreamscapes::failure

// Generated LVUI code creates a backend scope; the embedding application can
// inject the same model that receives reports from its generation service.
class NativeFailureDetailsBackend final {
  public:
    explicit NativeFailureDetailsBackend(std::shared_ptr<dreamscapes::failure::FailureDetailsModel> source);
    NativeFailureDetailsBackend();
    std::shared_ptr<dreamscapes::failure::FailureDetailsModel> model;
    std::shared_ptr<lvui::CodeMemoryDocument> document;
    ii::core::Observable<std::int64_t> &revision;
    ii::core::Observable<bool> &open, &canCopy, &motionEnabled, &copyFailed, &reportFailed, &usesWrappedText;
    ii::core::Observable<double> &editorWidth, &editorHeight;
    ii::core::Observable<std::string> &title, &closeLabel, &copyLabel, &accessibilityName, &copyError, &reportError, &wrappedText;
    void dismiss() { model->dismiss(); }
    void copyReport() { model->copyReport(); }
};
