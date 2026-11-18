#include "NativeHomeComposerPanel.h"
#include <LVUI>
#include <iiGUI>
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
constexpr std::size_t commonQuickLabels = 5;
}
HomeComposerPanelModel::HomeComposerPanelModel(std::shared_ptr<HomeComposerModel> composer,
        std::shared_ptr<HomeComposerPromptModel> prompt, HomeComposerPanelSources views,
        HomeComposerLayoutSpec spec, HomeComposerLayoutMetrics values)
    : sources(std::move(views)), composer_(std::move(composer)), prompt_(std::move(prompt)), spec_(spec), metrics_(values) {
    if (!composer_ || !composer_->active() || !prompt_ || !prompt_->isFor(composer_) || !prompt_->supportsSubmission() ||
        !sources.attachments || std::ranges::any_of(sources.top, [](const auto &view) { return !view; }) ||
        std::ranges::any_of(sources.settings, [](const auto &view) { return !view; }) ||
        (spec_.compactControls && (!sources.inlineNegative || !sources.inlineNegativeHeight)))
        throw std::invalid_argument("Home composer requires every native control and an active generation draft");
}
std::shared_ptr<HomeComposerPanelModel> HomeComposerPanelModel::create(std::shared_ptr<HomeComposerModel> composer,
        std::shared_ptr<HomeComposerPromptModel> prompt, HomeComposerPanelSources views,
        HomeComposerLayoutSpec spec, HomeComposerLayoutMetrics metrics) {
    auto result = std::shared_ptr<HomeComposerPanelModel>(new HomeComposerPanelModel(std::move(composer),
        std::move(prompt), std::move(views), spec, metrics));
    result->start(); return result;
}
HomeComposerPanelModel::~HomeComposerPanelModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
void HomeComposerPanelModel::start() {
    const std::weak_ptr<HomeComposerPanelModel> weak = shared_from_this();
    snapshotObservation_ = composer_->snapshot.observe([weak](const auto &snapshot, std::uint64_t) {
        const bool hasAttachments = !snapshot.attachments.empty();
        if (auto model = weak.lock(); model && model->active_ &&
            (model->layoutVideo_ != snapshot.video || model->hasAttachments.get() != hasAttachments ||
             model->error.get() != snapshot.errorText))
            (void)model->synchronize();
    });
    const auto promptSizeChanged = [weak](double, std::uint64_t) {
        if (auto model = weak.lock(); model && model->active_) (void)model->synchronize();
    };
    promptObservation_ = prompt_->requestedHeight.observe(promptSizeChanged);
    if (spec_.compactControls) negativePromptObservation_ = sources.inlineNegativeHeight->observe(promptSizeChanged);
    if (spec_.compactControls) for (std::size_t i=0;i<sources.quickLabels.size();++i) {
        if (sources.quickLabels[i]) quickLabelObservations_[i]=sources.quickLabels[i]->observe([weak](const auto &,std::uint64_t) {
            if (auto model=weak.lock();model && model->active_) (void)model->synchronize();
        });
    }
}
ii::core::Result<void> HomeComposerPanelModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Composer panel requires its owner thread");
    if (!active_ || !composer_ || !composer_->active() || !prompt_ || !prompt_->active())
        return ii::core::fail(ErrorCode::closed, "Composer panel is closed");
    return {};
}
ii::core::Result<void> HomeComposerPanelModel::setViewport(double width, double height) {
    return setViewport(width, height, metrics_);
}
ii::core::Result<void> HomeComposerPanelModel::setViewport(double width, double height, HomeComposerLayoutMetrics metrics) {
    if (auto result = writable(); !result) return result;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height < 0 || width > 8192 || height > 8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid composer panel viewport");
    if (width_ == width && metrics_ == metrics) return {};
    const auto previousWidth = width_; const auto previousMetrics = metrics_;
    width_ = width; metrics_ = metrics;
    auto result = synchronize();
    if (!result && active_) { width_ = previousWidth; metrics_ = previousMetrics; }
    return result;
}
ii::core::Result<void> HomeComposerPanelModel::setErrorHeight(double height) {
    if (auto result = writable(); !result) return result;
    if (!std::isfinite(height) || height < 0 || height > 8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Invalid composer error height");
    if (measuredError_ == height) return {};
    const auto previous = measuredError_; measuredError_ = height;
    auto result = synchronize();
    if (!result && active_) measuredError_ = previous;
    return result;
}
ii::core::Result<void> HomeComposerPanelModel::setFontFamily(std::string family) {
    if (auto result=writable();!result) return result;
    if (fontFamily_==family) return {};
    fontFamily_=std::move(family);
    measuredWidths_.fill(0);
    return synchronize();
}
ii::core::Result<void> HomeComposerPanelModel::synchronize() {
    if (publishing_) { pending_ = true; return {}; }
    if (auto result = writable(); !result) return result;
    if (width_ <= 0) return {};
    const auto keepAlive = shared_from_this();
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{publishing_}; publishing_ = true;
    try {
        unsigned rounds = 0;
        do {
            if (++rounds > 64) return ii::core::fail(ii::core::ErrorCode::busy, "Composer layout did not settle");
            pending_ = false;
            if (!active_ || !composer_ || !prompt_) return ii::core::fail(ii::core::ErrorCode::closed, "Composer panel retired during layout");
            const auto &snapshot = composer_->snapshot.get();
            const bool attached = !snapshot.attachments.empty(), errorPresent = !snapshot.errorText.empty(), video = snapshot.video;
            auto message = snapshot.errorText;
            const auto visibleErrorHeight = errorPresent ? std::max(metrics_.controlHeight, measuredError_) : 0;
            auto measuredSpec=spec_;
            if (spec_.compactControls) for (std::size_t i=0;i<sources.quickLabels.size();++i) {
                if (!sources.quickLabels[i] || (!video && i >= commonQuickLabels)) continue;
                const auto &label=sources.quickLabels[i]->get();
                if (measuredWidths_[i]<=0 || measuredLabels_[i]!=label) {
                    const lvui::DropdownConfiguration style;
                    auto text=ii::gui::measure_text_layout({.text=label,.font_size=float(style.font_size),
                        .font_weight=style.font_weight,.font_family=fontFamily_,.line_height=float(style.line_height),.elide_right=true});
                    if (!text) return std::unexpected(text.error());
                    measuredLabels_[i]=label;
                    measuredWidths_[i]=std::min(8192.0,std::ceil(text->width+style.left_padding+style.right_padding+
                                                                   style.indicator_size+style.spacing));
                }
                if (i < commonQuickLabels) measuredSpec.settingsPreferred[i] = measuredWidths_[i];
                else measuredSpec.videoPreferred[i - commonQuickLabels] = measuredWidths_[i];
            }
            const auto negativeHeight = spec_.compactControls ? sources.inlineNegativeHeight->get() : 0;
            auto next = calculateHomeComposerLayout(width_, prompt_->requestedHeight.get(), video, attached,
                visibleErrorHeight, measuredSpec, metrics_, negativeHeight);
            if (!next) return std::unexpected(next.error());
            (void)viewportWidth.set(width_); if (!active_) break; if (pending_) continue;
            (void)contentWidth.set(next->contentWidth); if (!active_) break; if (pending_) continue;
            (void)topHeight.set(next->topHeight); if (!active_) break; if (pending_) continue;
            (void)settingsHeight.set(next->settingsHeight); if (!active_) break; if (pending_) continue;
            (void)errorHeight.set(visibleErrorHeight); if (!active_) break; if (pending_) continue;
            (void)hasAttachments.set(attached); if (!active_) break; if (pending_) continue;
            (void)error.set(std::move(message)); if (!active_) break; if (pending_) continue;
            (void)hasError.set(errorPresent); if (!active_) break; if (pending_) continue;
            (void)requestedHeight.set(next->requestedHeight); if (!active_) break; if (pending_) continue;
            layoutVideo_ = video;
            (void)layout.set(std::move(*next));
        } while (pending_);
        return active_ ? ii::core::Result<void>{} : ii::core::fail(ii::core::ErrorCode::closed, "Composer panel retired during layout");
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot publish composer layout", std::current_exception()); }
}
void HomeComposerPanelModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Composer panel requires its owner thread");
    if (!active_) return;
    active_ = false; snapshotObservation_.reset(); promptObservation_.reset(); negativePromptObservation_.reset();
    for (auto &observation:quickLabelObservations_) observation.reset();
    composer_.reset(); prompt_.reset();
}
} // namespace dreamscapes::home
