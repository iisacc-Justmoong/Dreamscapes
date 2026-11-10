#import <AppKit/AppKit.h>
#include "App/Views/Failure/NativeFailureDetails.h"
#include <iiGUI/DesktopCommon.h>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void exercise() {
    @autoreleasepool {
        NSPasteboard *board = [NSPasteboard pasteboardWithUniqueName];
        struct Release { NSPasteboard *board; ~Release() { [board releaseGlobally]; } } release{board};
        dreamscapes::failure::FailureDetailsModel model;
        std::string report(2 * 1024 * 1024, 'r'); report += "\n한글 😀";
        const dreamscapes::vm::Value evidence(dreamscapes::vm::Object{{"details", dreamscapes::vm::Value(report)}});
        check(bool(model.setReport(evidence)), "The report model rejected a large report");
        model.setClipboardWriter([board](std::string_view text) {
            return ii::gui::detail::write_pasteboard((__bridge void *)board, {.text = std::string(text)});
        });
        model.copyReport();
        check(!model.copyFailed.get(), "The native report copy failed");
        const auto read = ii::gui::detail::read_pasteboard((__bridge void *)board);
        check(read && read->text == report, "The native report copy was truncated");
    }
}
}
int main() {
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
