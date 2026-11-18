#include "App/Views/Failure/NativeFailureDetails.h"
#include "App/Views/Failure/NativeFailureDetailsView.h"
#include "NativeFailureTestHelpers.h"
#include <iiGUI>
#include <iiGUI/WidgetState.h>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template <class T> T take(ii::core::Result<T> result) {
    if (!result) {
        if (result.error().cause) std::rethrow_exception(result.error().cause);
        throw std::runtime_error(result.error().message);
    }
    return std::move(*result);
}
void good(ii::core::Result<void> result) {
    if (!result) {
        if (result.error().cause) std::rethrow_exception(result.error().cause);
        throw std::runtime_error(result.error().message);
    }
}
void exercise() {
    using namespace dreamscapes;
    using namespace ii::gui;
    failure::FailureStrings translated;
    translated.title = "Localized title"; translated.pending = "Localized pending";
    failure::FailureDetailsModel localized(std::move(translated));
    good(localized.setReport(vm::Value(vm::Object{{"state", vm::Value("pending")}})));
    check(localized.title.get() == "Localized title" && localized.reportText() == "Localized pending\n\n",
          "The native view discarded host-supplied localized strings");
    failure::FailureDetailsModel threshold;
    const auto limit = ii::gui::NativeTextLayout::maximum_lines;
    good(threshold.setReport(vm::Value(vm::Object{{"details", vm::Value(std::string(limit - 1, 'x'))}})));
    check(threshold.usesWrappedText.get(), "Text below the layout limit should use wrapping");
    good(threshold.setReport(vm::Value(vm::Object{{"details", vm::Value(std::string(limit, 'x'))}})));
    check(threshold.usesWrappedText.get(), "A long line fitting the text byte budget should try native wrapping");
    std::string korean;
    for (std::size_t i = 0; i < limit - 1; ++i) korean += "한";
    good(threshold.setReport(vm::Value(vm::Object{{"details", vm::Value(korean)}})));
    check(threshold.usesWrappedText.get(), "Safe Korean text was rejected by its UTF-8 byte length");
    korean += "한";
    good(threshold.setReport(vm::Value(vm::Object{{"details", vm::Value(korean)}})));
    check(threshold.usesWrappedText.get(), "Korean text was rejected before actual layout");
    std::string emoji;
    for (std::size_t i = 0; i < limit / 2 - 1; ++i) emoji += "😀";
    good(threshold.setReport(vm::Value(vm::Object{{"details", vm::Value(emoji)}})));
    check(threshold.usesWrappedText.get(), "Safe supplementary text was rejected by its UTF-8 byte length");
    emoji += "😀";
    good(threshold.setReport(vm::Value(vm::Object{{"details", vm::Value(emoji)}})));
    check(threshold.usesWrappedText.get(), "Supplementary text was rejected before actual layout");
    good(threshold.setReport(vm::Value(vm::Object{{"details", vm::Value(std::string(limit - 1, '\n'))}})));
    check(threshold.usesWrappedText.get(), "A trailing empty line inside the layout budget was rejected");
    const auto narrow = NativeTextLayout::create(std::string(limit - 1, '\n'), {}, false, 1.0);
    check(narrow && narrow->lines().size() == limit,
          "The conservative bound did not preserve the final empty visual line");
    good(threshold.setReport(vm::Value(vm::Object{{"details", vm::Value(std::string(limit, '\n'))}})));
    check(!threshold.usesWrappedText.get(), "Too many actual paragraphs reached the bounded text editor");
    good(threshold.setReport(vm::Value(vm::Object{{"details", vm::Value(std::string(NativeTextLayout::maximum_bytes+1, 'x'))}})));
    check(!threshold.usesWrappedText.get(), "Text beyond the input byte limit reached the text editor");
    failure::FailureDetailsModel reentrant;
    bool rejectedReentry = false;
    auto subscription = reentrant.canCopy.observe([&](bool value, std::uint64_t) {
        if (!value) return;
        const auto result = reentrant.setReport(vm::Value(vm::Object{{"details", vm::Value("reentered")}}));
        rejectedReentry = !result && result.error().code == ii::core::ErrorCode::busy;
    });
    good(reentrant.setReport(vm::Value(vm::Object{{"details", vm::Value("original")}})));
    check(rejectedReentry && reentrant.reportText() == "original", "A reentrant update corrupted the report");
    auto model = std::make_shared<failure::FailureDetailsModel>();
    model->motionEnabled.set(false);
    good(model->setViewport(900, 700));
    std::string copied;
    model->setClipboardWriter([&](std::string_view text) -> ii::core::Result<void> { copied = text; return {}; });
    const vm::Value pending(vm::Object{{"state", vm::Value("pending")}, {"rawError", vm::Value("오류 😀")},
        {"modelUnavailableReason", vm::Value("model unavailable")}, {"agentError", vm::Value("agent timeout")},
        {"persistenceError", vm::Value("permission denied")}, {"reportPath", vm::Value("/reports/one.json")}});
    good(model->setReport(pending));
    const std::string expected = "Agent analysis is running. Captured evidence is available below.\n\n오류 😀"
        "\n\nmodel unavailable\n\nAgent status: agent timeout\n\nReport could not be saved: permission denied"
        "\n\nReport file: /reports/one.json";
    check(model->reportText() == expected && model->canCopy.get(), "Failure report composition changed");
    bool rejectedWorker = false;
    std::thread worker([&] {
        const auto result = model->setReport(vm::Value(vm::Object{{"details", vm::Value("wrong thread")}}));
        rejectedWorker = !result && result.error().code == ii::core::ErrorCode::wrong_executor;
    });
    worker.join();
    check(rejectedWorker && model->reportText() == expected, "Cross-thread report delivery mutated the view");
    auto tree = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(tree->content_host({}, "root", {900, 700}));
    good(root.set_content([&](WidgetBuilder &builder) { return failure::buildFailureDetails(builder, {}, model); }));
    auto modal = take(root.content_item("failureDetails"));
    model->show();
    (void)take(tree->drawing_snapshot({900, 700}));
    auto branch = take(failureReportBranch(modal,"failureExplanationText"));
    auto editor = take(branch.content_item("failureExplanationText"));
    auto snapshot = take(editor.snapshot());
    check(snapshot.read_only && take(editor.text_content({0, snapshot.text_length})).text == expected,
          "Native failure text was not readable or retained");
    const auto bounds = snapshot.bounds;
    good(tree->input({.kind=WidgetInputKind::pointer_down, .x=bounds.x+10, .y=bounds.y+10, .buttons=1}));
    good(tree->input({.kind=WidgetInputKind::pointer_move, .x=bounds.x+90, .y=bounds.y+10, .buttons=1}));
    good(tree->input({.kind=WidgetInputKind::pointer_up, .x=bounds.x+90, .y=bounds.y+10}));
    const auto selection = take(editor.snapshot()).text_selection;
    check(selection.length > 0, "Native failure text could not be selected by dragging");
    good(take(modal.popup_content("failureDetails.action-1")).activate());
    check(copied == expected && take(editor.snapshot()).text_selection == selection,
          "Copy report changed the selection or failed to copy the whole report");
    model->setClipboardWriter([](std::string_view) -> ii::core::Result<void> {
        return ii::core::fail(ii::core::ErrorCode::failed, "Clipboard unavailable");
    });
    good(take(modal.popup_content("failureDetails.action-1")).activate());
    check(model->copyFailed.get() && model->copyError.get() == "Clipboard unavailable" &&
              take(editor.snapshot()).text_selection == selection &&
              take(take(modal.popup_content("failureCopyError")).snapshot()).visible,
          "Clipboard failure was lost or changed the selected text");
    const auto beforeRevision = model->revision.get();
    const auto bad = vm::Value(vm::Object{{"details", vm::Value(std::string("a\0b", 3))}});
    check(!model->setReport(bad) && model->reportText() == expected && model->revision.get() == beforeRevision,
          "Invalid report text damaged the displayed evidence");
    model->setReportFailure("Report update failed");
    check(take(take(modal.popup_content("failureReportError")).snapshot()).visible,
          "The report update error was not displayed");
    good(model->setReport(pending));
    check(!model->reportFailed.get(), "A valid unchanged report did not clear the update error");
    const auto previousWidth = model->editorWidth.get();
    good(model->setViewport(390, 700));
    check(model->editorWidth.get() < previousWidth && model->editorWidth.get() > 0,
          "Native failure view did not adapt to a narrow viewport");
    check(!model->setViewport(-1, 700), "Invalid viewport was accepted");
    model->setClipboardWriter([&](std::string_view text) -> ii::core::Result<void> { copied = text; return {}; });
    good(model->setReport(vm::Value(vm::Object{{"details", vm::Value("Final evidence")}, {"rawError", vm::Value("old")}})));
    snapshot = take(editor.snapshot());
    check(take(editor.text_content({0, snapshot.text_length})).text == "Final evidence" && !model->copyFailed.get(),
          "Active view did not update or clear the stale copy error");
    good(take(modal.popup_content("failureDetails.action-0")).activate());
    check(!model->open.get() && !take(modal.snapshot()).opened, "Close did not dismiss the native dialog");
    const std::string wrappedReport = std::string(5000, 'x') + " 한글 😀 end";
    good(model->setReport(vm::Value(vm::Object{{"details", vm::Value(wrappedReport)}})));
    check(model->usesWrappedText.get(), "A normal report did not use the wrapped viewer");
    model->show();
    (void)take(tree->drawing_snapshot({900, 700}));
    branch = take(failureReportBranch(modal,"failureExplanationText"));
    auto wrapped = take(branch.content_item("failureExplanationText"));
    const auto wrappedBounds = take(wrapped.snapshot()).bounds;
    const auto topHit = take(wrapped.text_position(wrappedBounds.x + 12, wrappedBounds.y + 12));
    const auto lowerHit = take(wrapped.text_position(wrappedBounds.x + 12, wrappedBounds.y + 35));
    check(lowerHit > topHit, "A long report line did not wrap into selectable visual rows");
    good(tree->input({.kind=WidgetInputKind::scroll, .x=wrappedBounds.x+12, .y=wrappedBounds.y+12, .scroll_y=100}));
    check(take(wrapped.text_position(wrappedBounds.x+12,wrappedBounds.y+12)) > topHit, "The wrapped report did not scroll");
    model->copyReport();
    check(copied == wrappedReport, "Visual wrapping changed the copied report");
    good(take(modal.popup_content("failureDetails.action-0")).activate());
    std::string koreanReport;
    for (int i = 0; i < 1800; ++i) koreanReport += "한";
    good(model->setReport(vm::Value(vm::Object{{"details", vm::Value(koreanReport)}})));
    check(model->usesWrappedText.get(), "A byte-heavy Korean report did not use the native viewer");
    model->show();
    (void)take(tree->drawing_snapshot({900, 700}));
    branch = take(failureReportBranch(modal,"failureExplanationText"));
    auto koreanEditor = take(branch.content_item("failureExplanationText"));
    auto koreanSnapshot = take(koreanEditor.snapshot());
    check(take(koreanEditor.text_content({0, koreanSnapshot.text_length})).text == koreanReport,
          "The wrapped native viewer lost Korean report text");
    const auto koreanBounds = koreanSnapshot.bounds;
    const auto koreanTop = take(koreanEditor.text_position(koreanBounds.x + 12, koreanBounds.y + 12));
    const auto koreanNext = take(koreanEditor.text_position(koreanBounds.x + 12, koreanBounds.y + 35));
    check(koreanNext > koreanTop, "The Korean report did not wrap into selectable visual lines");
    good(tree->input({.kind=WidgetInputKind::pointer_down, .x=koreanBounds.x+10, .y=koreanBounds.y+10, .buttons=1}));
    good(tree->input({.kind=WidgetInputKind::pointer_move, .x=koreanBounds.x+90, .y=koreanBounds.y+10, .buttons=1}));
    good(tree->input({.kind=WidgetInputKind::pointer_up, .x=koreanBounds.x+90, .y=koreanBounds.y+10}));
    const auto koreanSelection = take(koreanEditor.snapshot()).text_selection;
    check(koreanSelection.length > 0, "The Korean report could not be selected by dragging");
    model->copyReport();
    check(copied == koreanReport && take(koreanEditor.snapshot()).text_selection == koreanSelection,
          "The native viewer changed the Korean report or its selection during copy");
    good(take(modal.popup_content("failureDetails.action-0")).activate());
    good(model->setReport(vm::Value(vm::Object{{"details", vm::Value(wrappedReport)}})));
    model->show();
    (void)take(tree->drawing_snapshot({900,700}));
    check(model->usesWrappedText.get(), "The wide viewport unnecessarily left the native wrapped viewer");
    good(model->setViewport(17,700));
    good(root.set_extent({17,700}));
    (void)take(tree->drawing_snapshot({17,700}));
    if (auto narrowBranch=failureReportBranch(modal,"failureExplanationText")) {
        if (auto field=narrowBranch->content_item("failureExplanationText")) {
            if (auto geometry=field->snapshot())
                (void)field->text_position(geometry->bounds.x+1,geometry->bounds.y+1);
        }
    }
    check(!model->usesWrappedText.get() && model->reportText()==wrappedReport,
          "An actual layout fallback was not reported or damaged the text");
    good(take(modal.popup_content("failureDetails.action-0")).activate());
    good(root.set_extent({900,700}));
    good(model->setViewport(900,700));
    const std::string longReport = std::string(120000, 'x') + "\n완료";
    good(model->setReport(vm::Value(vm::Object{{"details", vm::Value(longReport)}})));
    model->copyReport();
    check(!model->usesWrappedText.get() && model->reportText() == longReport && copied == longReport,
          "Long failure report was truncated or exceeded the wrapped editor budget");
    model->show();
    (void)take(tree->drawing_snapshot({900, 700}));
    branch = take(failureReportBranch(modal,"failureExplanationLongText"));
    auto longEditor = take(branch.content_item("failureExplanationLongText"));
    auto longSnapshot = take(longEditor.snapshot());
    std::string longExposed;
    for (std::size_t offset = 0; offset < longSnapshot.text_length;) {
        auto part = take(longEditor.text_content({offset,longSnapshot.text_length-offset}));
        check(part.range.length > 0, "The bounded text reader stopped before the end of the report");
        offset += part.range.length;
        longExposed += part.text;
    }
    check(longExposed == longReport,
          "The document-backed fallback did not expose the complete report");
    good(take(modal.popup_content("failureDetails.action-0")).activate());
    model->setClipboardWriter([&](std::string_view text) -> ii::core::Result<void> {
        model->setClipboardWriter({});
        good(model->setReport(vm::Value(vm::Object{{"details", vm::Value("replacement")}})));
        copied = text; return {};
    });
    model->copyReport();
    check(copied == longReport && model->reportText() == "replacement",
          "A reentrant report change corrupted the clipboard snapshot");
    model->setClipboardWriter([](std::string_view) -> ii::core::Result<void> { throw std::runtime_error("clipboard exception"); });
    model->copyReport();
    check(model->copyFailed.get() && model->copyError.get() == "clipboard exception", "Clipboard exception escaped the view");
    good(model->setReport(vm::Value(vm::Object{})));
    check(!model->canCopy.get(), "Empty reports must not enable copy");
    good(root.clear_content()); tree->close();
}
}
int main() {
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
