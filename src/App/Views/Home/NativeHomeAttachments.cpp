#include "NativeHomeAttachments.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
HomeAttachmentsModel::HomeAttachmentsModel(std::shared_ptr<HomeComposerModel> composer, RemoveLabel label)
    : composer_(std::move(composer)), removeLabel_(std::move(label)) {
    if (!composer_ || !composer_->active() || !removeLabel_)
        throw std::invalid_argument("Attachments require an active composer and accessible remove labels");
}
std::shared_ptr<HomeAttachmentsModel> HomeAttachmentsModel::create(std::shared_ptr<HomeComposerModel> composer, RemoveLabel label) {
    auto result = std::shared_ptr<HomeAttachmentsModel>(new HomeAttachmentsModel(std::move(composer), std::move(label)));
    result->start(); return result;
}
HomeAttachmentsModel::~HomeAttachmentsModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
void HomeAttachmentsModel::start() {
    const std::weak_ptr<HomeAttachmentsModel> weak = shared_from_this();
    snapshotObservation_ = composer_->snapshot.observe([weak](const auto &, std::uint64_t) {
        if (auto model = weak.lock(); model && model->active_) model->synchronize();
    });
    commandsObservation_ = composer_->commandsAvailable.observe([weak](bool, std::uint64_t) {
        if (auto model = weak.lock(); model && model->active_) model->synchronize();
    });
    synchronize();
}
void HomeAttachmentsModel::synchronize() {
    if (publishing_) { pending_ = true; return; }
    struct Reset final { bool &flag; ~Reset() { flag = false; } } reset{publishing_}; publishing_ = true;
    unsigned rounds = 0;
    do {
        if (++rounds > 64) throw std::logic_error("Attachment projection did not settle");
        pending_ = false;
        if (!active_ || !composer_) return;
        const auto attachments = composer_->snapshot.get().attachments;
        auto labeler = removeLabel_;
        std::vector<HomeAttachmentRow> next; next.reserve(attachments.size());
        for (const auto &item : attachments) {
            auto label = labeler(item.name);
            if (!active_) return;
            if (label.empty()) throw std::invalid_argument("Attachment remove action needs an accessible label");
            next.push_back({item.key, item.name, item.preview, item.detail, std::move(label)});
        }
        if (pending_) continue;
        (void)rows.set(std::move(next)); if (!active_) return;
        if (pending_) continue;
        (void)visible.set(!attachments.empty()); if (!active_) return;
        if (pending_) continue;
        (void)enabled.set(composer_->commandsAvailable.get());
    } while (pending_);
}
ii::core::Result<void> HomeAttachmentsModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Attachments require their owner thread");
    if (!active_ || !composer_ || !composer_->active()) return ii::core::fail(ErrorCode::closed, "Attachments are closed");
    return {};
}
ii::core::Result<void> HomeAttachmentsModel::remove(std::string key) {
    if (auto result = writable(); !result) return result;
    const auto keepAlive = shared_from_this();
    return composer_->dispatch(HomeComposerCommandKind::removeAttachment, std::move(key));
}
ii::core::Result<void> HomeAttachmentsModel::setViewport(double width, double height) {
    if (auto result = writable(); !result) return result;
    if (!std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 || width > 8192 || height > 8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid attachment viewport");
    const auto keepAlive = shared_from_this();
    try {
        (void)viewportWidth.set(std::max(1.0, width));
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed, "Attachments retired during layout");
        (void)viewportHeight.set(std::max(1.0, height)); return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update attachment viewport", std::current_exception()); }
}
void HomeAttachmentsModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Attachments require their owner thread");
    if (!active_) return;
    active_ = false; snapshotObservation_.reset(); commandsObservation_.reset(); composer_.reset(); removeLabel_ = {};
    (void)enabled.set(false);
}
} // namespace dreamscapes::home
