#include "NativeFailureDetails.h"
#include <LVUI/NativeModal.h>
#include <iiGUI/NativeTextLayout.h>
#include <iiGUI/ModalModel.h>
#include <iiGUI/TextInputModel.h>
#include <iiGUI/Desktop.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::failure {
namespace {
using Modal = ii::gui::BasicModal<lvui::native::ModalHostModelPolicy>;
std::shared_ptr<lvui::CodeMemoryDocument> makeDocument() {
    auto created = lvui::CodeMemoryDocument::create({}, {.maximum_bytes = 64 * 1024 * 1024,
        .maximum_edit_bytes = 64 * 1024 * 1024});
    if (!created) throw std::runtime_error(created.error().message);
    return std::move(*created);
}
}
FailureDetailsModel::FailureDetailsModel(FailureStrings strings) : document(makeDocument()), strings_(std::move(strings)) {
    title.set(strings_.title); closeLabel.set(strings_.close); copyLabel.set(strings_.copy);
    accessibilityName.set(strings_.accessibilityName);
    clipboard_ = [](std::string_view text) { return ii::gui::write_clipboard({.text = std::string(text)}); };
    const auto configuration = Modal{}.configuration();
    editorWidth.set(configuration.preferred_width - 2 * configuration.content_padding);
    editorHeight.set(540);
}

ii::core::Result<void> FailureDetailsModel::setReport(const vm::Value &report) {
    if (owner_ != std::this_thread::get_id())
        return ii::core::fail(ii::core::ErrorCode::wrong_executor, "Failure details require the owner thread");
    if (updating_) return ii::core::fail(ii::core::ErrorCode::busy, "Failure report update is already in progress");
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{updating_};
    updating_ = true;
    using vm::get;
    using vm::text;
    std::string next;
    if (text(get(report, "state")) == "pending") next = strings_.pending + "\n\n";
    auto body = text(get(report, "details"));
    if (body.empty()) body = text(get(report, "rawError"));
    next += body;
    const auto append = [&](std::string_view key, std::string_view prefix) {
        auto value = text(get(report, key));
        if (!value.empty()) { next += "\n\n"; next += prefix; next += value; }
    };
    append("modelUnavailableReason", {});
    append("agentError", strings_.agentStatus);
    append("persistenceError", strings_.persistenceError);
    append("reportPath", strings_.reportFile);
    if (next == text_) { reportError.set({}); reportFailed.set(false); return {}; }
    const auto current = document->snapshot();
    if (!current) return ii::core::fail(ii::core::ErrorCode::failed, current.error().message);
    if ((*current)->revision() >= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return ii::core::fail(ii::core::ErrorCode::out_of_range, "Failure report revision exhausted");
    const auto changed = document->replace({.revision = (*current)->revision(),
        .range = {0, (*current)->length()}, .text = next});
    if (!changed) return ii::core::fail(ii::core::ErrorCode::invalid_argument, changed.error().message);
    text_ = std::move(next);
    // A byte bound also bounds the UTF-16 units and visual rows, even for a
    // one-pixel viewport. Larger reports retain the document-backed viewer.
    const bool canWrap = text_.size() < ii::gui::NativeTextLayout::maximum_lines &&
        text_.size() <= ii::gui::TextInputModel::maximum_bytes;
    if (canWrap) { wrappedText.set(text_); usesWrappedText.set(true); }
    else { usesWrappedText.set(false); wrappedText.set({}); }
    canCopy.set(!text_.empty());
    copyError.set({}); copyFailed.set(false);
    reportError.set({}); reportFailed.set(false);
    revision.set(static_cast<std::int64_t>(changed->revision));
    return {};
}

ii::core::Result<void> FailureDetailsModel::setViewport(double width, double height) {
    if (owner_ != std::this_thread::get_id())
        return ii::core::fail(ii::core::ErrorCode::wrong_executor, "Failure details require the owner thread");
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 || width > 8192 || height > 8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid failure dialog viewport");
    Modal modal;
    auto configuration = modal.configuration();
    configuration.minimum_width = 0;
    configuration.maximum_width = 860; // The current failure dialog's maximum width.
    if (!modal.set_configuration(configuration))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid failure dialog layout");
    const auto frame = modal.frame(width, height, 0);
    if (!frame) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid failure dialog frame");
    editorWidth.set(std::max(1., frame->width - 2 * configuration.content_padding));
    editorHeight.set(std::max(1., std::min(540., height - 200.)));
    return {};
}
void FailureDetailsModel::requireOwner() const {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Failure details require the owner thread");
}
void FailureDetailsModel::setClipboardWriter(ClipboardWriter writer) { requireOwner(); clipboard_ = std::move(writer); }
void FailureDetailsModel::show() { requireOwner(); open.set(true); }
void FailureDetailsModel::dismiss() { requireOwner(); open.set(false); }
void FailureDetailsModel::setReportFailure(std::string error) {
    requireOwner(); reportError.set(std::move(error)); reportFailed.set(!reportError.get().empty());
}
void FailureDetailsModel::copyReport() {
    requireOwner();
    if (text_.empty()) return;
    const auto snapshot = text_;
    const auto writer = clipboard_;
    ii::core::Result<void> result;
    try {
        result = writer ? writer(snapshot)
            : ii::core::Result<void>(ii::core::fail(ii::core::ErrorCode::unsupported, "Clipboard unavailable"));
    } catch (const std::exception &error) {
        result = ii::core::fail(ii::core::ErrorCode::failed, error.what(), std::current_exception());
    } catch (...) {
        result = ii::core::fail(ii::core::ErrorCode::failed, "Clipboard write failed", std::current_exception());
    }
    copyError.set(result ? std::string{} : result.error().message);
    copyFailed.set(!result);
}
} // namespace dreamscapes::failure

NativeFailureDetailsBackend::NativeFailureDetailsBackend()
    : NativeFailureDetailsBackend(std::make_shared<dreamscapes::failure::FailureDetailsModel>()) {}
NativeFailureDetailsBackend::NativeFailureDetailsBackend(std::shared_ptr<dreamscapes::failure::FailureDetailsModel> source)
    : model(std::move(source)), document(model->document), revision(model->revision), open(model->open),
      canCopy(model->canCopy), motionEnabled(model->motionEnabled), copyFailed(model->copyFailed), reportFailed(model->reportFailed),
      usesWrappedText(model->usesWrappedText),
      editorWidth(model->editorWidth), editorHeight(model->editorHeight), title(model->title),
      closeLabel(model->closeLabel), copyLabel(model->copyLabel), accessibilityName(model->accessibilityName),
      copyError(model->copyError), reportError(model->reportError), wrappedText(model->wrappedText) {}
