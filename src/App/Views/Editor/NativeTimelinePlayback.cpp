#include "NativeTimelinePlayback.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace dreamscapes::editor {
using ii::core::ErrorCode;
TimelinePlayback::TimelinePlayback(std::shared_ptr<native::DocumentSession> doc,ii::runtime::EventLoop::Sender sender,Now now)
    : document_(std::move(doc)),sender_(std::move(sender)),now_(std::move(now)) {
    if (!document_ || !now_) throw std::invalid_argument("Timeline playback requires a document and clock");
}
std::shared_ptr<TimelinePlayback> TimelinePlayback::create(std::shared_ptr<native::DocumentSession> doc,
    ii::runtime::EventLoop &loop,Now now) {
    auto value=std::shared_ptr<TimelinePlayback>(new TimelinePlayback(std::move(doc),loop.sender(),std::move(now)));
    value->start(); return value;
}
void TimelinePlayback::start() {
    const std::weak_ptr<TimelinePlayback> weak=shared_from_this();
    generationObserver_=document_->documentGeneration.observe([weak](const auto &,std::uint64_t) {
        if (auto self=weak.lock()) (void)self->pause();
    });
    frameObserver_=document_->frame.observe([weak](const auto &,std::uint64_t) {
        if (auto self=weak.lock(); self && !self->internalSeek_ && self->playing.get()) (void)self->pause();
    });
}
TimelinePlayback::~TimelinePlayback() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
ii::core::Result<void> TimelinePlayback::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Timeline playback requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Timeline playback is closed");
    return {};
}
ii::core::Result<void> TimelinePlayback::play() {
    if (auto valid=writable(); !valid) return valid;
    if (playing.get()) return {};
    const auto *doc=document_->document();
    if (!doc || document_->frameCount.get()<2 || !doc->timeline.frameRate.numerator || !doc->timeline.frameRate.denominator)
        return ii::core::fail(ErrorCode::unsupported,"This canvas has no playable timeline");
    const auto generation=document_->documentGeneration.get();
    const auto rate=doc->timeline.frameRate;
    if (document_->frame.get()>=document_->frameCount.get()-1) if (auto seek=document_->seek(0); !seek) return seek;
    if (generation!=document_->documentGeneration.get())
        return ii::core::fail(ErrorCode::cancelled,"Canvas changed while starting playback");
    first_=document_->frame.get(); generation_=generation;
    numerator_=rate.numerator; denominator_=rate.denominator;
    origin_=now_();
    const auto period=std::max(std::chrono::ceil<ii::runtime::Clock::duration>(
        std::chrono::duration<long double>(static_cast<long double>(denominator_)/numerator_)),
        std::chrono::duration_cast<ii::runtime::Clock::duration>(std::chrono::milliseconds(8)));
    const std::weak_ptr<TimelinePlayback> weak=shared_from_this();
    auto scheduled=sender_.every(period,[weak] {if (auto self=weak.lock()) (void)self->advance();});
    if (!scheduled) return ii::core::fail(ErrorCode::busy,"Timeline timer is unavailable");
    timer_=scheduled.token; (void)playing.set(true); return {};
}
ii::core::Result<void> TimelinePlayback::pause() {
    if (auto valid=writable(); !valid) return valid;
    if (playing.get()) { (void)sender_.cancel(timer_); (void)playing.set(false); }
    return {};
}
ii::core::Result<void> TimelinePlayback::advance() {
    if (auto valid=writable(); !valid) return valid;
    if (!playing.get()) return {};
    if (document_->documentGeneration.get()!=generation_ || !document_->document()) return pause();
    const auto elapsed=std::max(0.0L,std::chrono::duration<long double>(now_()-origin_).count());
    const long double offset=std::floor(elapsed*static_cast<long double>(numerator_)/denominator_);
    const auto count=document_->frameCount.get();
    const auto remaining=count-first_;
    const auto target=offset>=remaining ? count-1 : first_+static_cast<std::int64_t>(offset);
    if (target!=document_->frame.get()) {
        internalSeek_=true;
        ii::core::Result<void> result;
        try {result=document_->seek(target);}
        catch (...) {internalSeek_=false; (void)pause();
            return ii::core::fail(ErrorCode::failed,"Timeline frame could not be displayed",std::current_exception());}
        internalSeek_=false;
        if (!result) {(void)pause(); return result;}
        if (document_->documentGeneration.get()!=generation_ || document_->frame.get()!=target) return pause();
    }
    if (offset>=remaining) return pause();
    return {};
}
void TimelinePlayback::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Timeline playback requires its owner thread");
    if (!active_) return;
    (void)pause(); active_=false; generationObserver_={}; frameObserver_={}; now_={};
}
}
