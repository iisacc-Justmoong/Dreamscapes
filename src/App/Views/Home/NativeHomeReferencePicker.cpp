#include "NativeHomeReferencePicker.h"
#include "App/Generation/NativeHomeReferenceInputs.h"
#include <iiSDKCore>
#include <QThread>
#include <QUrl>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
ii::gui::FileDialogOptions homeReferenceDialogOptions(std::string title, std::string filter) {
    return {.mode = ii::gui::FileDialogMode::open, .title = std::move(title),
            .filters = {{std::move(filter), {"iisc","png","jpg","jpeg","webp","bmp","tif","tiff","heic"}}}, .multiple = true};
}
NativeHomeReferencePicker::NativeHomeReferencePicker(NativeHomeReferenceInputs *inputs, Chooser chooser,
        ii::gui::FileDialogOptions options, Failure failure, QObject *parent)
    : QObject(parent), inputs_(inputs), chooser_(std::move(chooser)), options_(std::move(options)), failure_(std::move(failure)) {
    if (!inputs || !chooser_ || inputs->thread() != QThread::currentThread() || thread() != QThread::currentThread() ||
        options_.mode != ii::gui::FileDialogMode::open || !options_.multiple)
        throw std::invalid_argument("Home reference picker requires an input provider and a multi-file chooser on their owner thread");
}
NativeHomeReferencePicker::~NativeHomeReferencePicker() { state_->pending = false; ++state_->ticket; }
bool NativeHomeReferencePicker::pending() const { return state_->pending || state_->processing; }
ii::core::Result<void> NativeHomeReferencePicker::choose() {
    using ii::core::ErrorCode;
    if (thread() != QThread::currentThread()) return ii::core::fail(ErrorCode::wrong_executor, "Home reference picker requires its owner thread");
    if (!inputs_) return ii::core::fail(ErrorCode::closed, "Home reference inputs are unavailable");
    const auto state = state_;
    if (state->pending || state->processing) return ii::core::fail(ErrorCode::busy, "A reference picker is already open");
    if (state->ticket == std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range, "Reference picker requests exhausted");
    const auto ticket = ++state->ticket; state->pending = true;
    const QPointer<NativeHomeReferencePicker> guard(this);
    try {
        auto chooser = chooser_; auto options = options_;
        auto result = chooser(std::move(options), [guard, ticket](auto selection) {
            if (guard) guard->complete(ticket, std::move(selection));
        });
        if (!result && state->ticket == ticket) state->pending = false;
        return result;
    } catch (...) {
        if (state->ticket == ticket) state->pending = false;
        return ii::core::fail(ErrorCode::failed, "Cannot open the reference picker", std::current_exception());
    }
}
void NativeHomeReferencePicker::complete(std::uint64_t ticket, ii::core::Result<ii::gui::FileDialogSelection> selection) {
    const auto state = state_;
    if (thread() != QThread::currentThread() || !state->pending || state->ticket != ticket) return;
    state->pending = false; state->processing = true;
    struct Reset final { std::shared_ptr<State> value; ~Reset() { value->processing = false; } } reset{state};
    const QPointer<NativeHomeReferencePicker> guard(this);
    const auto report = [guard](ii::core::Error error) {
        if (!guard || !guard->failure_) return;
        auto callback = guard->failure_;
        try { callback(error); } catch (...) {}
    };
    if (!selection) { report(selection.error()); return; }
    if (selection->canceled) return;
    for (const auto &path : selection->paths) {
        if (!guard || !inputs_) return;
        if (path.empty() || path.find('\0') != path.npos || !ii::core::valid_utf8(path)) {
            report({ii::core::ErrorCode::invalid_argument, "The selected file path is invalid"});
            continue;
        }
        try {
            auto input = inputs_;
            const bool added = input->addAttachment(QUrl::fromLocalFile(QString::fromUtf8(path.data(), static_cast<qsizetype>(path.size()))));
            if (!guard || !input) return;
            if (!added) report({ii::core::ErrorCode::failed, input->inputError().toStdString()});
        } catch (...) { report({ii::core::ErrorCode::failed, "Cannot add the selected reference", std::current_exception()}); }
    }
}
} // namespace dreamscapes::home
