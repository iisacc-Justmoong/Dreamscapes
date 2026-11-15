#include "NativeHomeNegativePrompt.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
HomeNegativePromptModel::HomeNegativePromptModel(std::shared_ptr<HomeComposerPromptModel> prompt, std::string title, std::string done)
    : heading(std::move(title)), doneLabel(std::move(done)), prompt_(std::move(prompt)) {
    if (!prompt_ || !prompt_->active() || prompt_->supportsSubmission() || heading.empty() || doneLabel.empty())
        throw std::invalid_argument("Negative prompt requires its own active draft and accessible labels");
}
std::shared_ptr<HomeNegativePromptModel> HomeNegativePromptModel::create(std::shared_ptr<HomeComposerPromptModel> prompt,
        std::string heading, std::string done) {
    auto result = std::shared_ptr<HomeNegativePromptModel>(new HomeNegativePromptModel(std::move(prompt), std::move(heading), std::move(done)));
    result->start(); return result;
}
HomeNegativePromptModel::~HomeNegativePromptModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
void HomeNegativePromptModel::start() {
    const std::weak_ptr<HomeNegativePromptModel> weak = shared_from_this();
    enabledObservation_ = prompt_->enabled.observe([weak](bool available, std::uint64_t) {
        if (auto model = weak.lock(); model && model->active_) (void)model->enabled.set(available);
    });
    (void)enabled.set(prompt_->enabled.get());
}
ii::core::Result<void> HomeNegativePromptModel::setViewport(double width, double height) {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Negative prompt requires its owner thread");
    if (!active_ || !prompt_ || !prompt_->active()) return ii::core::fail(ErrorCode::closed, "Negative prompt is closed");
    if (!std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 || width > 8192 || height > 8192)
        return ii::core::fail(ErrorCode::invalid_argument, "Invalid negative prompt geometry");
    const auto keepAlive = shared_from_this();
    try {
        (void)viewportWidth.set(std::max(1.0, width));
        if (!active_) return ii::core::fail(ErrorCode::closed, "Negative prompt retired during layout");
        (void)viewportHeight.set(std::max(1.0, height)); return {};
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot update negative prompt geometry", std::current_exception()); }
}
void HomeNegativePromptModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Negative prompt requires its owner thread");
    if (!active_) return;
    active_ = false; enabledObservation_.reset(); prompt_.reset(); (void)enabled.set(false);
}
} // namespace dreamscapes::home
