#include "NativeHomeSelectedFileSession.h"
#include "CanvasPreviewAddress.h"
#include "App/ViewModels/ViewData.h"
#include <QPointer>
#include <QThread>
#include <utility>

namespace dreamscapes::home {
NativeHomeSelectedFileSession::NativeHomeSelectedFileSession(HomeSelectedFileStrings strings, Open open, QObject *parent)
    : QObject(parent), model_(HomeSelectedFileModel::create(std::move(strings))), open_(std::move(open)) {
    const QPointer<NativeHomeSelectedFileSession> guard(this);
    if (open_) model_->setOpenHandler([guard] {
        if (!guard || !guard->open_) return;
        const auto action = guard->open_;
        const auto file = guard->file_;
        action(file);
    });
    connect(&details_, &HomeFileDetails::metadataChanged, this, [this] { metadataChanged(); });
}
NativeHomeSelectedFileSession::~NativeHomeSelectedFileSession() {
    open_ = {};
    if (model_ && model_->active()) try { model_->setOpenHandler({}); } catch (...) {}
}
ii::core::Result<void> NativeHomeSelectedFileSession::setFile(QVariantMap file) {
    using ii::core::ErrorCode;
    if (thread() != QThread::currentThread())
        return ii::core::fail(ErrorCode::wrong_executor, "Selected file session requires its owner thread");
    const auto updating = updating_;
    if (*updating) return ii::core::fail(ErrorCode::busy, "Selected file session is updating");
    struct Reset final { std::shared_ptr<bool> value; ~Reset() { *value = false; } } reset{updating}; *updating = true;
    const QPointer<NativeHomeSelectedFileSession> guard(this);
    auto model = model_;
    auto preview = canvasPreviewSource(file).toEncoded(QUrl::FullyEncoded).toStdString();
    file_ = std::move(file);
    const auto revision = model->setFile(nativeValue(file_), std::move(preview));
    if (!revision) return std::unexpected(revision.error());
    if (!guard) return ii::core::fail(ErrorCode::closed, "Selected file session was retired");
    generation_ = *revision;
    details_.setFile(file_);
    return {};
}
void NativeHomeSelectedFileSession::metadataChanged() {
    if (*updating_ || !model_ || !model_->active()) return;
    (void)model_->setMetadata(generation_, nativeValue(details_.metadata()));
}
} // namespace dreamscapes::home
