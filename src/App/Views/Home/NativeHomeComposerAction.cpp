#include "NativeHomeComposerAction.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
HomeComposerCommandKind command(HomeComposerActionRole role) {
    switch (role) {
    case HomeComposerActionRole::attach: return HomeComposerCommandKind::chooseAttachments;
    case HomeComposerActionRole::paint: return HomeComposerCommandKind::paint;
    case HomeComposerActionRole::advanced: return HomeComposerCommandKind::advanced;
    case HomeComposerActionRole::generate: return HomeComposerCommandKind::submit;
    }
    throw std::invalid_argument("Unknown Home composer action");
}
}
HomeComposerActionModel::HomeComposerActionModel(std::shared_ptr<HomeComposerModel> composer, HomeComposerActionRole role,
        HomeComposerActionLabels labels)
    : icon(role == HomeComposerActionRole::attach), primary(role == HomeComposerActionRole::generate),
      showHeading(!labels.heading.empty()), heading(labels.heading), accessible(labels.accessible),
      composer_(std::move(composer)), role_(role), labels_(std::move(labels)) {
    (void)command(role_);
    if (!composer_ || !composer_->active() || accessible.empty() ||
        (primary ? !labels_.generation : !icon && labels_.label.empty()))
        throw std::invalid_argument("Composer action requires an active model and accessible labels");
}
std::shared_ptr<HomeComposerActionModel> HomeComposerActionModel::create(std::shared_ptr<HomeComposerModel> composer,
        HomeComposerActionRole role, HomeComposerActionLabels labels) {
    auto result = std::shared_ptr<HomeComposerActionModel>(new HomeComposerActionModel(std::move(composer), role, std::move(labels)));
    result->start(); return result;
}
HomeComposerActionModel::~HomeComposerActionModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
void HomeComposerActionModel::start() {
    const std::weak_ptr<HomeComposerActionModel> weak = shared_from_this();
    auto changed = [weak](const auto &, std::uint64_t) { if (auto model = weak.lock(); model && model->active_) model->synchronize(); };
    snapshotObservation_ = composer_->snapshot.observe(changed);
    commandsObservation_ = composer_->commandsAvailable.observe(changed);
    canSubmitObservation_ = composer_->canSubmit.observe(changed);
    synchronize();
}
void HomeComposerActionModel::synchronize() {
    if (publishing_) { pending_ = true; return; }
    struct Reset final { bool &flag; ~Reset() { flag = false; } } reset{publishing_}; publishing_ = true;
    unsigned rounds = 0;
    do {
        if (++rounds > 64) throw std::logic_error("Composer action projection did not settle");
        pending_ = false;
        if (!active_ || !composer_) return;
        const auto snapshot = composer_->snapshot.get();
        auto makeLabel = labels_.generation;
        auto next = primary ? makeLabel(snapshot.outputCount, snapshot.video) : labels_.label;
        if (!active_) return;
        if (pending_) continue;
        if (primary && next.empty()) throw std::invalid_argument("Generate action needs a visible label");
        (void)label.set(std::move(next)); if (!active_) return;
        if (pending_) continue;
        const bool available = role_ == HomeComposerActionRole::attach ? snapshot.attachmentsAvailable :
            role_ == HomeComposerActionRole::paint ? snapshot.paintAvailable :
            role_ == HomeComposerActionRole::advanced ? snapshot.advancedAvailable : true;
        (void)enabled.set(primary ? composer_->canSubmit.get() : composer_->commandsAvailable.get() && available);
    } while (pending_);
}
ii::core::Result<void> HomeComposerActionModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Composer action requires its owner thread");
    if (!active_ || !composer_ || !composer_->active()) return ii::core::fail(ErrorCode::closed, "Composer action is closed");
    return {};
}
ii::core::Result<void> HomeComposerActionModel::invoke() {
    if (auto result = writable(); !result) return result;
    if (!enabled.get()) return ii::core::fail(ii::core::ErrorCode::unsupported, "Composer action is unavailable");
    const auto keepAlive = shared_from_this();
    return composer_->dispatch(command(role_));
}
ii::core::Result<void> HomeComposerActionModel::setViewport(double width, double height) {
    if (auto result = writable(); !result) return result;
    if (!std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 || width > 8192 || height > 8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid composer action geometry");
    const auto keepAlive = shared_from_this();
    try {
        (void)viewportWidth.set(std::max(1.0, width));
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed, "Composer action retired during layout");
        (void)viewportHeight.set(std::max(1.0, height)); return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update composer action geometry", std::current_exception()); }
}
void HomeComposerActionModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Composer action requires its owner thread");
    if (!active_) return;
    active_ = false; snapshotObservation_.reset(); commandsObservation_.reset(); canSubmitObservation_.reset(); composer_.reset();
    labels_.generation = {}; (void)enabled.set(false);
}
} // namespace dreamscapes::home
