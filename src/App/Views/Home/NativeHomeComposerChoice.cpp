#include "NativeHomeComposerChoice.h"
#include <iiGUI>
#include <LVUI>
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
bool validDimension(double value) { return std::isfinite(value) && value >= 0 && value <= 8192; }
}
HomeComposerChoiceModel::Projection HomeComposerChoiceModel::project(const HomeComposerSnapshot &s) const {
    using Role = HomeComposerChoiceRole; using Command = HomeComposerCommandKind;
    switch (role_) {
    case Role::media: return {s.choices.media, Command::chooseMedia, s.mediaType, {}};
    case Role::model: return {s.choices.models, Command::chooseModel, s.modelId, s.modelLabel};
    case Role::ratio: return {s.choices.ratios, Command::chooseRatio, s.aspectRatio, s.aspectRatio};
    case Role::resolution: return {s.choices.resolutions, Command::chooseResolution, std::to_string(s.shortSide), s.resolutionLabel, !s.video};
    case Role::count: return {s.choices.counts, Command::chooseCount, std::to_string(s.outputCount), {}};
    case Role::duration: return {s.choices.durations, Command::chooseDuration, std::to_string(s.videoDuration), {}};
    case Role::frameRate: return {s.choices.frameRates, Command::chooseFrameRate, std::to_string(s.videoFps), {}};
    case Role::preset: return {s.choices.presets, Command::loadPreset, s.presetName, s.presetName};
    }
    throw std::invalid_argument("Unknown Home composer choice role");
}
HomeComposerChoiceModel::HomeComposerChoiceModel(std::shared_ptr<HomeComposerModel> composer, HomeComposerChoiceRole role,
        std::string title, std::string hint, bool showTitle)
    : heading(std::move(title)), placeholder(std::move(hint)), showHeading(showTitle), composer_(std::move(composer)), role_(role) {
    if (!composer_ || !composer_->active() || heading.empty())
        throw std::invalid_argument("Composer choice requires an active model and accessible heading");
    (void)project(composer_->snapshot.get());
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
        if (auto owner = weak.lock(); owner && owner->active_) (void)owner->synchronize();
    });
    commandsObservation_ = composer_->commandsAvailable.observe([weak](bool, std::uint64_t) {
        if (auto owner = weak.lock(); owner && owner->active_ && owner->composer_) (void)owner->synchronize();
    });
    if (auto result = synchronize(); !result) throw std::runtime_error(result.error().message);
}
ii::core::Result<void> HomeComposerChoiceModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Composer choice requires its owner thread");
    if (!active_ || !composer_ || !composer_->active()) return ii::core::fail(ErrorCode::closed, "Composer choice is closed");
    return {};
}
ii::core::Result<void> HomeComposerChoiceModel::synchronize() {
    if (publishing_) { pending_ = true; return {}; }
    if (auto result = writable(); !result) return result;
    struct Reset final { bool &flag; ~Reset() { flag = false; } } reset{publishing_}; publishing_ = true;
    try {
        unsigned rounds = 0;
        do {
            if (++rounds > 64) return ii::core::fail(ii::core::ErrorCode::busy, "Composer choice projection did not settle");
            pending_ = false;
            if (!active_ || !composer_) return ii::core::fail(ii::core::ErrorCode::closed, "Composer choice retired during projection");
            const auto &snapshot = composer_->snapshot.get();
            auto projection = project(snapshot);
            projection.available = projection.available && composer_->commandsAvailable.get();
            if (measured_ && published_ == projection) return {};
            published_.reset();
            auto choiceLabel = projection.label;
            std::vector<HomeComposerChoiceRow> rows; rows.reserve(projection.choices.size());
            bool any = false;
            for (const auto &choice : projection.choices) {
                any = any || choice.enabled;
                const auto state = !choice.enabled ? HomeComposerMenuState::inactive :
                                   choice.key == projection.key ? HomeComposerMenuState::selected : HomeComposerMenuState::normal;
                rows.push_back({choice.key, choice.label, choice.enabled, state});
                if (choiceLabel.empty() && choice.key == projection.key) choiceLabel = choice.label;
            }
            const bool labelsChanged = !measured_ || rows.size() != measuredLabels_.size() ||
                !std::equal(rows.begin(), rows.end(), measuredLabels_.begin(),
                            [](const auto &row, const auto &label) { return row.label == label; });
            if (labelsChanged) {
                const lvui::MenuItemConfiguration itemStyle;
                const lvui::MenuConfiguration menuStyle;
                double widest = rows.empty() ? menuStyle.item_width : 1;
                for (const auto &row : rows) {
                    auto measured = ii::gui::measure_text_layout({.text=row.label,.font_size=float(itemStyle.font_size),
                        .font_weight=itemStyle.font_weight,.font_family=fontFamily_,.line_height=float(itemStyle.line_height),.elide_right=true});
                    if (!measured) return std::unexpected(measured.error());
                    widest = std::max(widest, std::ceil(measured->width + 2 * itemStyle.horizontal_padding));
                }
                widest = std::min(widest, 8192.0 - menuStyle.left_padding - menuStyle.right_padding);
                std::vector<std::string> labels; labels.reserve(rows.size());
                for (const auto &row : rows) labels.push_back(row.label);
                measuredLabels_ = std::move(labels); measured_ = true;
                (void)menuItemWidth.set(widest); if (!active_) break; if (pending_) continue;
            }
            const bool available = projection.available && (any || role_ == HomeComposerChoiceRole::preset);
            (void)choices.set(std::move(rows)); if (!active_) break; if (pending_) continue;
            (void)value.set(choiceLabel.empty() ? placeholder : std::move(choiceLabel)); if (!active_) break; if (pending_) continue;
            (void)enabled.set(available);
            published_ = std::move(projection);
        } while (pending_);
        return active_ ? ii::core::Result<void>{} : ii::core::fail(ii::core::ErrorCode::closed, "Composer choice retired during projection");
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot publish composer choice", std::current_exception()); }
}
ii::core::Result<void> HomeComposerChoiceModel::setFontFamily(std::string family) {
    if (auto result = writable(); !result) return result;
    if (fontFamily_ == family) return {};
    fontFamily_ = std::move(family); measured_ = false;
    return synchronize();
}
ii::core::Result<void> HomeComposerChoiceModel::choose(std::string key) {
    if (auto result = writable(); !result) return result;
    if (!enabled.get()) return ii::core::fail(ii::core::ErrorCode::unsupported, "Composer choice is unavailable");
    const auto keepAlive = shared_from_this();
    return composer_->dispatch(project(composer_->snapshot.get()).command, std::move(key));
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
