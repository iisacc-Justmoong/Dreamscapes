#include "NativeHomeComposerChoice.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
struct Projection final {
    const std::vector<HomeComposerChoice> *choices;
    HomeComposerCommandKind command;
    std::string key, label;
    bool available = true;
};
Projection project(HomeComposerChoiceRole role, const HomeComposerSnapshot &s) {
    using Role = HomeComposerChoiceRole; using Command = HomeComposerCommandKind;
    switch (role) {
    case Role::media: return {&s.choices.media, Command::chooseMedia, s.mediaType, {}};
    case Role::model: return {&s.choices.models, Command::chooseModel, s.modelId, s.modelLabel};
    case Role::ratio: return {&s.choices.ratios, Command::chooseRatio, s.aspectRatio, s.aspectRatio};
    case Role::resolution: return {&s.choices.resolutions, Command::chooseResolution, std::to_string(s.shortSide), s.resolutionLabel, !s.video};
    case Role::count: return {&s.choices.counts, Command::chooseCount, std::to_string(s.outputCount), {}};
    case Role::duration: return {&s.choices.durations, Command::chooseDuration, std::to_string(s.videoDuration), {}};
    case Role::frameRate: return {&s.choices.frameRates, Command::chooseFrameRate, std::to_string(s.videoFps), {}};
    case Role::preset: return {&s.choices.presets, Command::loadPreset, s.presetName, s.presetName};
    }
    throw std::invalid_argument("Unknown Home composer choice role");
}
bool validDimension(double value) { return std::isfinite(value) && value >= 0 && value <= 8192; }
}
HomeComposerChoiceModel::HomeComposerChoiceModel(std::shared_ptr<HomeComposerModel> composer, HomeComposerChoiceRole role,
        std::string title, std::string hint, bool showTitle)
    : heading(std::move(title)), placeholder(std::move(hint)), showHeading(showTitle), composer_(std::move(composer)), role_(role) {
    if (!composer_ || !composer_->active() || heading.empty())
        throw std::invalid_argument("Composer choice requires an active model and accessible heading");
    (void)project(role_, composer_->snapshot.get());
}
std::shared_ptr<HomeComposerChoiceModel> HomeComposerChoiceModel::create(std::shared_ptr<HomeComposerModel> composer,
        HomeComposerChoiceRole role, std::string title, std::string hint, bool showTitle) {
    auto result = std::shared_ptr<HomeComposerChoiceModel>(new HomeComposerChoiceModel(std::move(composer), role,
        std::move(title), std::move(hint), showTitle));
    result->start(); return result;
}
HomeComposerChoiceModel::~HomeComposerChoiceModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
void HomeComposerChoiceModel::start() {
    const std::weak_ptr<HomeComposerChoiceModel> weak = shared_from_this();
    snapshotObservation_ = composer_->snapshot.observe([weak](const auto &, std::uint64_t) {
        if (auto owner = weak.lock(); owner && owner->active_) owner->synchronize();
    });
    commandsObservation_ = composer_->commandsAvailable.observe([weak](bool, std::uint64_t) {
        if (auto owner = weak.lock(); owner && owner->active_ && owner->composer_) owner->synchronize();
    });
    synchronize();
}
ii::core::Result<void> HomeComposerChoiceModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Composer choice requires its owner thread");
    if (!active_ || !composer_ || !composer_->active()) return ii::core::fail(ErrorCode::closed, "Composer choice is closed");
    return {};
}
void HomeComposerChoiceModel::synchronize() {
    if (publishing_) { pending_ = true; return; }
    struct Reset final { bool &flag; ~Reset() { flag = false; } } reset{publishing_}; publishing_ = true;
    unsigned rounds = 0;
    do {
        if (++rounds > 64) throw std::logic_error("Composer choice projection did not settle");
        pending_ = false;
        if (!active_ || !composer_) return;
        const auto &snapshot = composer_->snapshot.get();
        auto projection = project(role_, snapshot);
        std::vector<HomeComposerChoiceRow> rows; rows.reserve(projection.choices->size());
        bool any = false;
        for (const auto &choice : *projection.choices) {
            any = any || choice.enabled;
            const auto state = !choice.enabled ? HomeComposerMenuState::inactive :
                               choice.key == projection.key ? HomeComposerMenuState::selected : HomeComposerMenuState::normal;
            rows.push_back({choice.key, choice.label, choice.enabled, state});
            if (projection.label.empty() && choice.key == projection.key) projection.label = choice.label;
        }
        const bool available = composer_->commandsAvailable.get() && projection.available && (any || role_ == HomeComposerChoiceRole::preset);
        (void)choices.set(std::move(rows)); if (!active_) return;
        (void)value.set(projection.label.empty() ? placeholder : projection.label); if (!active_) return;
        (void)enabled.set(available);
    } while (pending_);
}
ii::core::Result<void> HomeComposerChoiceModel::choose(std::string key) {
    if (auto result = writable(); !result) return result;
    if (!enabled.get()) return ii::core::fail(ii::core::ErrorCode::unsupported, "Composer choice is unavailable");
    const auto keepAlive = shared_from_this();
    return composer_->dispatch(project(role_, composer_->snapshot.get()).command, std::move(key));
}
ii::core::Result<void> HomeComposerChoiceModel::setViewport(double width, double height) {
    if (auto result = writable(); !result) return result;
    if (!validDimension(width) || !validDimension(height)) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid composer choice geometry");
    const auto keepAlive = shared_from_this();
    try {
        (void)viewportWidth.set(std::max(1.0, width));
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed, "Composer choice retired during layout");
        (void)viewportHeight.set(std::max(1.0, height)); return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update composer choice geometry", std::current_exception()); }
}
void HomeComposerChoiceModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Composer choice requires its owner thread");
    if (!active_) return;
    active_ = false; snapshotObservation_.reset(); commandsObservation_.reset(); composer_.reset(); (void)enabled.set(false);
}
} // namespace dreamscapes::home
