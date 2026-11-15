#include "NativeHomeComposerPrompt.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace { bool valid(double value) { return std::isfinite(value) && value >= 0 && value <= 8192; } }
HomeComposerPromptModel::HomeComposerPromptModel(std::shared_ptr<HomeComposerModel> composer, HomeComposerTextRole role,
        std::string hint, std::string name, std::string clearName, bool supportsClear)
    : placeholder(std::move(hint)), accessible(std::move(name)), clearAccessible(std::move(clearName)),
      composer_(std::move(composer)), role_(role), supportsClear_(supportsClear) {
    if (!composer_ || !composer_->active()) throw std::invalid_argument("Composer prompt requires an active generation model");
    if ((role_ != HomeComposerTextRole::prompt && role_ != HomeComposerTextRole::negativePrompt) ||
        accessible.empty() || (supportsClear_ && clearAccessible.empty()))
        throw std::invalid_argument("Composer prompt requires a valid role and accessible labels");
}
std::shared_ptr<HomeComposerPromptModel> HomeComposerPromptModel::create(std::shared_ptr<HomeComposerModel> composer,
        HomeComposerTextRole role, std::string hint, std::string name, std::string clearName, bool supportsClear) {
    auto model = std::shared_ptr<HomeComposerPromptModel>(new HomeComposerPromptModel(std::move(composer), role,
        std::move(hint), std::move(name), std::move(clearName), supportsClear));
    model->start(); return model;
}
HomeComposerPromptModel::~HomeComposerPromptModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
void HomeComposerPromptModel::start() {
    const std::weak_ptr<HomeComposerPromptModel> weak = shared_from_this();
    observation_ = composer_->snapshot.observe([weak](const auto &value, std::uint64_t) {
        if (auto model = weak.lock(); model && model->active_) model->synchronize(value);
    });
    commandsObservation_ = composer_->commandsAvailable.observe([weak](bool available, std::uint64_t) {
        if (auto model = weak.lock(); model && model->active_) (void)model->enabled.set(available);
    });
    (void)enabled.set(composer_->commandsAvailable.get());
    synchronize(composer_->snapshot.get());
}
ii::core::Result<void> HomeComposerPromptModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Composer prompt requires its owner thread");
    if (!active_ || !composer_ || !composer_->active()) return ii::core::fail(ErrorCode::closed, "Composer prompt is closed");
    return {};
}
void HomeComposerPromptModel::synchronize(const HomeComposerSnapshot &value) {
    const auto &next = role_ == HomeComposerTextRole::prompt ? value.prompt : value.negativePrompt;
    (void)text.set(next); if (!active_) return;
    (void)clearVisible.set(supportsClear_ && !next.empty()); if (active_) updateWidth();
}
void HomeComposerPromptModel::updateWidth() {
    (void)editorWidth.set(std::max(1.0, viewportWidth.get() - (clearVisible.get() ? gap_ + buttonWidth_ : 0)));
}
ii::core::Result<void> HomeComposerPromptModel::setViewport(double width, double height, double gap, double buttonWidth) {
    if (auto result = writable(); !result) return result;
    if (!valid(width) || !valid(height) || !valid(gap) || !valid(buttonWidth))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid composer prompt geometry");
    const auto keepAlive = shared_from_this();
    try {
        gap_ = gap; buttonWidth_ = buttonWidth;
        (void)viewportWidth.set(width);
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed, "Composer prompt retired during layout");
        (void)viewportHeight.set(std::max(1.0, height));
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed, "Composer prompt retired during layout");
        updateWidth(); return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update prompt geometry", std::current_exception()); }
}
ii::core::Result<void> HomeComposerPromptModel::setMeasuredHeight(double content, double padding, double minimum) {
    if (auto result = writable(); !result) return result;
    if (!valid(content) || !valid(padding) || !valid(minimum) || minimum > 160)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid composer prompt measurement");
    const auto keepAlive = shared_from_this();
    try { (void)requestedHeight.set(std::clamp(std::ceil(content) + 2 * padding, minimum, 160.0)); return {}; }
    catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update prompt measurement", std::current_exception()); }
}
ii::core::Result<void> HomeComposerPromptModel::edit(std::string value) {
    if (auto result = writable(); !result) return result;
    const auto keepAlive = shared_from_this();
    return composer_->dispatch(role_ == HomeComposerTextRole::prompt ? HomeComposerCommandKind::editPrompt :
        HomeComposerCommandKind::editNegativePrompt, std::move(value));
}
ii::core::Result<void> HomeComposerPromptModel::clear() {
    if (auto result = writable(); !result) return result;
    if (!supportsClear_) return ii::core::fail(ii::core::ErrorCode::unsupported, "This prompt has no clear action");
    return edit({});
}
ii::core::Result<void> HomeComposerPromptModel::submit() {
    if (auto result = writable(); !result) return result;
    if (!supportsSubmission()) return ii::core::fail(ii::core::ErrorCode::unsupported, "Negative prompt does not submit generation");
    const auto keepAlive = shared_from_this();
    return composer_->dispatch(HomeComposerCommandKind::submit);
}
void HomeComposerPromptModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Composer prompt requires its owner thread");
    if (!active_) return;
    active_ = false; observation_.reset(); commandsObservation_.reset(); composer_.reset(); (void)enabled.set(false);
}
} // namespace dreamscapes::home
