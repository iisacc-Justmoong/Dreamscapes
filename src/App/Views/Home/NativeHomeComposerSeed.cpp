#include "NativeHomeComposerSeed.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
HomeComposerSeedModel::HomeComposerSeedModel(std::shared_ptr<HomeComposerModel> composer, std::string title,
        std::string random, std::string apply)
    : heading(std::move(title)), randomLabel(std::move(random)), applyLabel(std::move(apply)), composer_(std::move(composer)) {
    if (!composer_ || !composer_->active() || heading.empty() || randomLabel.empty() || applyLabel.empty())
        throw std::invalid_argument("Seed editor requires an active model and accessible labels");
}
std::shared_ptr<HomeComposerSeedModel> HomeComposerSeedModel::create(std::shared_ptr<HomeComposerModel> composer,
        std::string heading, std::string random, std::string apply) {
    auto result = std::shared_ptr<HomeComposerSeedModel>(new HomeComposerSeedModel(std::move(composer),
        std::move(heading), std::move(random), std::move(apply)));
    result->start(); return result;
}
HomeComposerSeedModel::~HomeComposerSeedModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
void HomeComposerSeedModel::start() {
    const std::weak_ptr<HomeComposerSeedModel> weak = shared_from_this();
    snapshotObservation_ = composer_->snapshot.observe([weak](const auto &snapshot, std::uint64_t) {
        if (auto model = weak.lock(); model && model->active_)
            (void)model->value.set(snapshot.seedLabel.empty() && snapshot.seed < 0 ? model->randomLabel : snapshot.seedLabel);
    });
    commandsObservation_ = composer_->commandsAvailable.observe([weak](bool available, std::uint64_t) {
        if (auto model = weak.lock(); model && model->active_) (void)model->enabled.set(available);
    });
    const auto &snapshot = composer_->snapshot.get();
    (void)value.set(snapshot.seedLabel.empty() && snapshot.seed < 0 ? randomLabel : snapshot.seedLabel);
    (void)enabled.set(composer_->commandsAvailable.get());
}
ii::core::Result<void> HomeComposerSeedModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Seed editor requires its owner thread");
    if (!active_ || !composer_ || !composer_->active()) return ii::core::fail(ErrorCode::closed, "Seed editor is closed");
    if (!composer_->commandsAvailable.get()) return ii::core::fail(ErrorCode::unsupported, "Seed editor has no command handler");
    return {};
}
ii::core::Result<void> HomeComposerSeedModel::begin() {
    if (auto result = writable(); !result) return result;
    const auto keepAlive = shared_from_this();
    try {
        const auto &snapshot = composer_->snapshot.get();
        (void)draft.set(snapshot.seed < 0 ? std::string{} : snapshot.seedLabel);
        return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot open the seed editor", std::current_exception()); }
}
ii::core::Result<void> HomeComposerSeedModel::edit(std::string text) {
    if (auto result = writable(); !result) return result;
    const auto keepAlive = shared_from_this();
    try { (void)draft.set(std::move(text)); return {}; }
    catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot edit the seed", std::current_exception()); }
}
ii::core::Result<void> HomeComposerSeedModel::apply() {
    if (auto result = writable(); !result) return result;
    const auto keepAlive = shared_from_this();
    return composer_->dispatch(HomeComposerCommandKind::applySeed, draft.get());
}
ii::core::Result<void> HomeComposerSeedModel::randomize() {
    if (auto result = writable(); !result) return result;
    const auto keepAlive = shared_from_this();
    return composer_->dispatch(HomeComposerCommandKind::randomSeed);
}
ii::core::Result<void> HomeComposerSeedModel::setViewport(double width, double height) {
    if (auto result = writable(); !result) return result;
    if (!std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 || width > 8192 || height > 8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid seed editor geometry");
    const auto keepAlive = shared_from_this();
    try {
        (void)viewportWidth.set(std::max(1.0, width));
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed, "Seed editor retired during layout");
        (void)viewportHeight.set(std::max(1.0, height)); return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update seed editor geometry", std::current_exception()); }
}
void HomeComposerSeedModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Seed editor requires its owner thread");
    if (!active_) return;
    active_ = false; snapshotObservation_.reset(); commandsObservation_.reset(); composer_.reset(); (void)enabled.set(false);
}
} // namespace dreamscapes::home
