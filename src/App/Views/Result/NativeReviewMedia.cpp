#include "NativeReviewMedia.h"
#include <iiSDKCore/Core/Text/Utf8.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
namespace {
bool valid(std::string_view value) { return value.find('\0')==value.npos && ii::core::valid_utf8(value); }
}
ReviewMediaModel::ReviewMediaModel(ReviewMediaStrings value) : strings(std::move(value)) {
    if (!strings.time) throw std::invalid_argument("Review media requires a time formatter");
    (void)playLabel.set(strings.play); (void)timeText.set(strings.time(0,0));
}
std::shared_ptr<ReviewMediaModel> ReviewMediaModel::create(ReviewMediaStrings strings) {
    return std::shared_ptr<ReviewMediaModel>(new ReviewMediaModel(std::move(strings)));
}
ReviewMediaModel::~ReviewMediaModel() { if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {} }
ii::core::Result<void> ReviewMediaModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Review media requires its owner thread");
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Review media is closed");
    return {};
}
void ReviewMediaModel::bindEvents() {
    if (!playback_) return;
    const std::weak_ptr<ReviewMediaModel> weak=shared_from_this();
    playback_->setEvents({
        .state=[weak](auto generation,auto state,auto position,auto duration) {
            if (auto self=weak.lock()) try {self->acceptState(generation,state,position,duration);} catch (...) {}
        },
        .frame=[weak](auto generation,auto image) {
            if (auto self=weak.lock()) try {self->acceptFrame(generation,std::move(image));} catch (...) {}
        },
        .error=[weak](auto generation,auto message) {
            if (auto self=weak.lock()) try {self->acceptError(generation,std::move(message));} catch (...) {}
        }});
}
ii::core::Result<void> ReviewMediaModel::setPlayback(std::shared_ptr<ReviewPlaybackPort> next) {
    if (auto result=writable(); !result) return result;
    if (playback_==next) return {};
    if (generation_==std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ii::core::ErrorCode::out_of_range,"Review media generation exhausted");
    const auto keepAlive=shared_from_this(); const auto generation=++generation_;
    if (playback_) { playback_->setEvents({}); (void)playback_->stop(); }
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Review media retired during backend replacement");
    if (generation_!=generation) return ii::core::fail(ii::core::ErrorCode::cancelled,"Review media changed during backend replacement");
    playback_=std::move(next); bindEvents(); updateVisibility();
    if (!active_ || generation_!=generation) return ii::core::fail(ii::core::ErrorCode::cancelled,"Review media changed during backend replacement");
    if (playback_)
        if (auto result=playback_->setViewport(viewportWidth.get(),previewHeight.get()); !result) return result;
    if (!active_ || generation_!=generation) return ii::core::fail(ii::core::ErrorCode::cancelled,"Review media changed during backend replacement");
    return replaceSource();
}
ii::core::Result<void> ReviewMediaModel::setResult(bool video,std::string preview,std::string media) {
    if (auto result=writable(); !result) return result;
    if (!valid(preview) || !valid(media)) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid review media source");
    const bool changed=video_!=video || media_!=media;
    if (changed && generation_==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ii::core::ErrorCode::out_of_range,"Review media generation exhausted");
    const auto keepAlive=shared_from_this();
    if (changed) ++generation_;
    const auto generation=generation_;
    video_=video; media_=std::move(media);
    (void)previewSource.set(std::move(preview));
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Review media retired during result change");
    if (generation_!=generation) return ii::core::fail(ii::core::ErrorCode::cancelled,"Review media changed during result notification");
    (void)noPreview.set(previewSource.get().empty()); updateVisibility();
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Review media retired during result change");
    if (generation_!=generation) return ii::core::fail(ii::core::ErrorCode::cancelled,"Review media changed during result notification");
    return changed ? replaceSource() : ii::core::Result<void>{};
}
ii::core::Result<void> ReviewMediaModel::setVisible(bool value) {
    if (auto result=writable(); !result) return result;
    if (visible_==value) return {};
    if (generation_==std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ii::core::ErrorCode::out_of_range,"Review media generation exhausted");
    const auto keepAlive=shared_from_this(); const auto generation=++generation_; visible_=value; updateVisibility();
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Review media retired during visibility change");
    if (generation_!=generation) return ii::core::fail(ii::core::ErrorCode::cancelled,"Review media changed during visibility notification");
    return replaceSource();
}
void ReviewMediaModel::updateVisibility() {
    const auto generation=generation_;
    const bool controls=visible_ && video_ && !media_.empty();
    (void)controlsVisible.set(controls);
    if (!active_ || generation_!=generation) return;
    (void)canControl.set(controls && bool(playback_));
    if (!active_ || generation_!=generation) return;
    updateGeometry();
}
ii::core::Result<void> ReviewMediaModel::setViewport(double width,double height,double controlsHeight,double gap) {
    if (auto result=writable(); !result) return result;
    const auto validSize=[](double value){return std::isfinite(value) && value>=0 && value<=8192;};
    if (!validSize(width) || !validSize(height) || !validSize(controlsHeight) || !validSize(gap))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid review media viewport");
    const auto keepAlive=shared_from_this(); controlsHeight_=controlsHeight; gap_=gap;
    (void)viewportWidth.set(std::max(1.0,width));
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Review media retired during layout");
    (void)viewportHeight.set(std::max(1.0,height));
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Review media retired during layout");
    updateGeometry();
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Review media retired during layout");
    if (playback_) return playback_->setViewport(viewportWidth.get(),previewHeight.get());
    return {};
}
void ReviewMediaModel::updateGeometry() {
    (void)previewHeight.set(std::max(1.0,viewportHeight.get()-(controlsVisible.get()?controlsHeight_+gap_:0)));
}
ii::core::Result<void> ReviewMediaModel::replaceSource() {
    const auto generation=generation_;
    const auto current=[this,generation]{return active_ && generation_==generation;};
    const auto retired=[] {return ii::core::fail(ii::core::ErrorCode::cancelled,"Review media source changed during notification");};
    state_=ReviewPlaybackState::stopped;
    (void)playing.set(false); if (!current()) return retired();
    (void)playLabel.set(strings.play); if (!current()) return retired();
    (void)showFrame.set(false); if (!current()) return retired();
    (void)frame.set({}); if (!current()) return retired();
    (void)timeText.set(strings.time(0,0)); if (!current()) return retired();
    (void)error.set({}); if (!current()) return retired();
    (void)hasError.set(false); if (!current()) return retired();
    if (!playback_) return {};
    auto playback=playback_;
    if (auto stopped=playback->stop(); !stopped) return stopped;
    if (!current() || playback_!=playback) return retired();
    if (auto resized=playback->setViewport(viewportWidth.get(),previewHeight.get()); !resized) return resized;
    if (!current() || playback_!=playback) return retired();
    if (visible_ && video_ && !media_.empty()) return playback->load(media_,generation_);
    return {};
}
ii::core::Result<void> ReviewMediaModel::toggle() {
    if (auto result=writable(); !result) return result;
    if (!controlsVisible.get() || !playback_) return ii::core::fail(ii::core::ErrorCode::unsupported,"Review playback is unavailable");
    const auto keepAlive=shared_from_this(); auto playback=playback_; const auto generation=generation_;
    auto result=state_==ReviewPlaybackState::playing?playback->pause():playback->play();
    if (!result && active_) acceptError(generation,result.error().message);
    return result;
}
ii::core::Result<void> ReviewMediaModel::replay() {
    if (auto result=writable(); !result) return result;
    if (!controlsVisible.get() || !playback_) return ii::core::fail(ii::core::ErrorCode::unsupported,"Review playback is unavailable");
    const auto keepAlive=shared_from_this(); auto playback=playback_; const auto generation=generation_;
    auto result=playback->replay();
    if (!result && active_) acceptError(generation,result.error().message);
    return result;
}
void ReviewMediaModel::setFailureHandler(std::function<void(std::string)> handler) {
    if (!writable()) throw std::logic_error("Review media is unavailable on this thread");
    failure_=std::move(handler);
}
void ReviewMediaModel::acceptState(std::uint64_t generation,ReviewPlaybackState state,std::int64_t position,std::int64_t duration) {
    if (!writable() || generation!=generation_ || !visible_ || !video_) return;
    if (state!=ReviewPlaybackState::stopped && state!=ReviewPlaybackState::playing && state!=ReviewPlaybackState::paused) return;
    state_=state; (void)playing.set(state==ReviewPlaybackState::playing);
    if (!active_ || generation!=generation_) return;
    (void)playLabel.set(playing.get()?strings.pause:strings.play);
    if (!active_ || generation!=generation_) return;
    (void)timeText.set(strings.time(std::max<std::int64_t>(0,position),std::max<std::int64_t>(0,duration)));
    if (!active_ || generation!=generation_) return;
    (void)showFrame.set(state_!=ReviewPlaybackState::stopped && bool(frame.get()));
}
void ReviewMediaModel::acceptFrame(std::uint64_t generation,std::shared_ptr<const ii::gui::RasterImage> value) {
    if (!writable() || generation!=generation_ || !visible_ || !video_) return;
    (void)frame.set(std::move(value));
    if (!active_ || generation!=generation_) return;
    (void)showFrame.set(state_!=ReviewPlaybackState::stopped && bool(frame.get()));
}
void ReviewMediaModel::acceptError(std::uint64_t generation,std::string value) {
    if (!writable() || generation!=generation_ || !visible_ || !video_ || value.empty()) return;
    (void)error.set(value); if (!active_ || generation!=generation_) return;
    (void)hasError.set(true); if (!active_ || generation!=generation_) return;
    auto handler=failure_; if (handler) handler(std::move(value));
}
void ReviewMediaModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Review media requires its owner thread");
    if (!active_) return;
    active_=false; ++generation_; failure_={};
    if (auto playback=std::move(playback_)) { playback->setEvents({}); (void)playback->stop(); }
    (void)controlsVisible.set(false); (void)canControl.set(false); (void)showFrame.set(false); (void)frame.set({});
}
} // namespace dreamscapes::review
